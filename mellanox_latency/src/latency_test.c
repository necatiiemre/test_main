/**
 * @file latency_test.c
 * @brief Mellanox HW Timestamp Latency Test - Test Logic Implementation
 *
 * Ana test mantığı:
 * - Her port çifti için soket aç
 * - Her VLAN için N paket gönder/al
 * - VLAN testleri arasında 32µs bekle
 * - Latency hesapla
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "latency_test.h"
#include "hw_timestamp.h"
#include "packet.h"
#include "common.h"
#include "config.h"

// Interrupt flag (defined in main.c)
extern volatile int g_interrupted;

// ============================================
// INTERNAL STRUCTURES
// ============================================

// TX timestamp kaydı (paket gönderildiğinde)
struct tx_record {
    uint64_t seq_num;
    uint64_t tx_timestamp;
    bool     used;
};

// ============================================
// CHECK INTERFACES
// ============================================

int check_all_interfaces(void) {
    LOG_INFO("Checking HW timestamp support for all interfaces...");

    int failed = 0;

    for (int i = 0; i < NUM_PORT_PAIRS; i++) {
        const struct port_pair *pp = &g_port_pairs[i];

        // Check TX interface
        if (!check_hw_timestamp_support(pp->tx_iface)) {
            LOG_ERROR("TX interface %s (Port %d) does not support HW timestamp",
                     pp->tx_iface, pp->tx_port);
            failed++;
        }

        // Check RX interface
        if (!check_hw_timestamp_support(pp->rx_iface)) {
            LOG_ERROR("RX interface %s (Port %d) does not support HW timestamp",
                     pp->rx_iface, pp->rx_port);
            failed++;
        }
    }

    if (failed > 0) {
        LOG_ERROR("%d interfaces failed HW timestamp check", failed);
        return -1;
    }

    LOG_INFO("All interfaces support HW timestamp");
    return 0;
}

// ============================================
// SINGLE VLAN TEST
// ============================================

int run_vlan_test(const struct port_pair *pair,
                  int vlan_idx,
                  const struct test_config *config,
                  struct latency_result *result) {

    uint16_t vlan_id = pair->vlans[vlan_idx];
    uint16_t vl_id = pair->vl_ids[vlan_idx];

    // Initialize result
    memset(result, 0, sizeof(*result));
    result->tx_port = pair->tx_port;
    result->rx_port = pair->rx_port;
    result->vlan_id = vlan_id;
    result->vl_id = vl_id;
    result->min_latency_ns = UINT64_MAX;
    result->valid = false;

    LOG_DEBUG("Testing VLAN %u (VL-ID %u): Port %d (%s) -> Port %d (%s)",
             vlan_id, vl_id,
             pair->tx_port, pair->tx_iface,
             pair->rx_port, pair->rx_iface);

    // Create sockets
    struct hw_socket tx_sock, rx_sock;

    int ret = create_hw_timestamp_socket(pair->tx_iface, SOCK_TYPE_TX, &tx_sock);
    if (ret < 0) {
        snprintf(result->error_msg, sizeof(result->error_msg),
                "TX socket failed: %d", ret);
        LOG_ERROR("Failed to create TX socket for %s: %d", pair->tx_iface, ret);
        return -1;
    }

    ret = create_hw_timestamp_socket(pair->rx_iface, SOCK_TYPE_RX, &rx_sock);
    if (ret < 0) {
        snprintf(result->error_msg, sizeof(result->error_msg),
                "RX socket failed: %d", ret);
        LOG_ERROR("Failed to create RX socket for %s: %d", pair->rx_iface, ret);
        close_hw_timestamp_socket(&tx_sock);
        return -2;
    }

    // Allocate TX records
    struct tx_record *tx_records = calloc(config->packet_count, sizeof(struct tx_record));
    if (!tx_records) {
        LOG_ERROR("Failed to allocate TX records");
        close_hw_timestamp_socket(&tx_sock);
        close_hw_timestamp_socket(&rx_sock);
        return -3;
    }

    // Packet buffer
    uint8_t *pkt_buf = malloc(config->packet_size);
    if (!pkt_buf) {
        LOG_ERROR("Failed to allocate packet buffer");
        free(tx_records);
        close_hw_timestamp_socket(&tx_sock);
        close_hw_timestamp_socket(&rx_sock);
        return -4;
    }

    // =========================================
    // PHASE 1: Send all packets for this VLAN
    // =========================================
    LOG_DEBUG("Sending %d packets for VLAN %u...", config->packet_count, vlan_id);

    for (int pkt = 0; pkt < config->packet_count && !g_interrupted; pkt++) {
        uint64_t seq_num = (uint64_t)vlan_id << 32 | (uint64_t)pkt;

        // Build packet
        int pkt_len = build_test_packet(pkt_buf, config->packet_size,
                                        vlan_id, vl_id, seq_num);
        if (pkt_len < 0) {
            LOG_ERROR("Failed to build packet %d for VLAN %u", pkt, vlan_id);
            continue;
        }

        // Send and get TX timestamp
        uint64_t tx_ts = 0;
        ret = send_packet_get_tx_timestamp(&tx_sock, pkt_buf, pkt_len, &tx_ts);

        if (ret == 0 && tx_ts > 0) {
            tx_records[pkt].seq_num = seq_num;
            tx_records[pkt].tx_timestamp = tx_ts;
            tx_records[pkt].used = true;
            result->tx_count++;

            LOG_TRACE("TX[%d]: seq=%lu, ts=%lu ns", pkt, seq_num, tx_ts);
        } else {
            LOG_WARN("TX[%d]: Failed to get timestamp (ret=%d, ts=%lu)",
                    pkt, ret, tx_ts);
            result->tx_count++;  // Paket gönderildi ama timestamp alınamadı
        }

        // NOT: Paketler arası bekleme yok, sadece VLAN testleri arasında bekleme var
    }

    LOG_DEBUG("Sent %u packets, waiting for responses...", result->tx_count);

    // =========================================
    // PHASE 2: Receive packets and match
    // =========================================
    uint8_t *rx_buf = malloc(config->packet_size + 64);
    if (!rx_buf) {
        LOG_ERROR("Failed to allocate RX buffer");
        free(pkt_buf);
        free(tx_records);
        close_hw_timestamp_socket(&tx_sock);
        close_hw_timestamp_socket(&rx_sock);
        return -5;
    }

    int remaining_timeout = config->timeout_ms;
    uint64_t start_time = get_time_ns();

    while (remaining_timeout > 0 && result->rx_count < result->tx_count && !g_interrupted) {
        size_t rx_len = config->packet_size + 64;
        uint64_t rx_ts = 0;

        ret = recv_packet_get_rx_timestamp(&rx_sock, rx_buf, &rx_len, &rx_ts, 100);

        if (ret == -1) {
            // Timeout, update remaining
            uint64_t elapsed_ms = (get_time_ns() - start_time) / 1000000;
            remaining_timeout = config->timeout_ms - (int)elapsed_ms;
            continue;
        }

        if (ret == -10) {
            // Interrupted by signal, exit gracefully
            break;
        }

        if (ret < 0) {
            LOG_WARN("RX error: %d", ret);
            continue;
        }

        // Check if this is our packet
        if (!is_our_test_packet(rx_buf, rx_len, vlan_id, vl_id)) {
            LOG_TRACE("Received non-matching packet, skipping");
            continue;
        }

        // Extract sequence number
        uint64_t rx_seq = extract_seq_num(rx_buf, rx_len);

        LOG_TRACE("RX: seq=%lu, ts=%lu ns, len=%zu", rx_seq, rx_ts, rx_len);

        // Find matching TX record
        bool matched = false;
        for (int i = 0; i < config->packet_count; i++) {
            if (tx_records[i].used && tx_records[i].seq_num == rx_seq) {
                // Calculate latency
                if (rx_ts > 0 && tx_records[i].tx_timestamp > 0) {
                    uint64_t latency = rx_ts - tx_records[i].tx_timestamp;

                    result->total_latency_ns += latency;
                    if (latency < result->min_latency_ns) {
                        result->min_latency_ns = latency;
                    }
                    if (latency > result->max_latency_ns) {
                        result->max_latency_ns = latency;
                    }

                    LOG_DEBUG("Latency[%d]: %lu ns (%.2f us)",
                             i, latency, ns_to_us(latency));
                }

                tx_records[i].used = false;  // Mark as matched
                result->rx_count++;
                matched = true;
                break;
            }
        }

        if (!matched) {
            LOG_WARN("RX packet with unknown seq=%lu", rx_seq);
        }
    }

    // =========================================
    // Finalize result
    // =========================================
    if (result->rx_count > 0) {
        result->valid = true;
        if (result->min_latency_ns == UINT64_MAX) {
            result->min_latency_ns = 0;
        }
    } else {
        snprintf(result->error_msg, sizeof(result->error_msg),
                "No packets received");
    }

    LOG_INFO("VLAN %u: TX=%u, RX=%u, Min=%.2f us, Avg=%.2f us, Max=%.2f us",
            vlan_id, result->tx_count, result->rx_count,
            ns_to_us(result->min_latency_ns),
            result->rx_count > 0 ? ns_to_us(result->total_latency_ns / result->rx_count) : 0.0,
            ns_to_us(result->max_latency_ns));

    // Cleanup
    free(rx_buf);
    free(pkt_buf);
    free(tx_records);
    close_hw_timestamp_socket(&tx_sock);
    close_hw_timestamp_socket(&rx_sock);

    return 0;
}

// ============================================
// PORT PAIR TEST
// ============================================

int run_port_pair_test(const struct port_pair *pair,
                       const struct test_config *config,
                       struct latency_result *results) {

    LOG_INFO("Testing port pair: Port %d (%s) -> Port %d (%s)",
            pair->tx_port, pair->tx_iface,
            pair->rx_port, pair->rx_iface);

    for (int v = 0; v < pair->vlan_count && !g_interrupted; v++) {
        run_vlan_test(pair, v, config, &results[v]);

        // 32µs bekleme (son VLAN hariç)
        if (v < pair->vlan_count - 1 && !g_interrupted) {
            LOG_TRACE("Waiting %d us before next VLAN test...", config->delay_us);

            if (config->use_busy_wait) {
                precise_delay_us_busy(config->delay_us);
            } else {
                precise_delay_us(config->delay_us);
            }
        }
    }

    return 0;
}

// ============================================
// MAIN TEST FUNCTION
// ============================================

int run_latency_test(const struct test_config *config,
                     struct latency_result *results,
                     int *result_count) {

    LOG_INFO("Starting latency test...");
    LOG_INFO("  Packet count per VLAN: %d", config->packet_count);
    LOG_INFO("  Packet size: %d bytes", config->packet_size);
    LOG_INFO("  Inter-VLAN delay: %d us", config->delay_us);
    LOG_INFO("  RX timeout: %d ms", config->timeout_ms);
    LOG_INFO("  Port filter: %s",
            config->port_filter < 0 ? "all" :
            (char[16]){0} + sprintf((char[16]){0}, "%d", config->port_filter));

    *result_count = 0;

    for (int p = 0; p < NUM_PORT_PAIRS && !g_interrupted; p++) {
        const struct port_pair *pair = &g_port_pairs[p];

        // Port filter
        if (config->port_filter >= 0 && pair->tx_port != config->port_filter) {
            LOG_DEBUG("Skipping port pair %d (filter=%d)", pair->tx_port, config->port_filter);
            continue;
        }

        run_port_pair_test(pair, config, &results[*result_count]);
        *result_count += pair->vlan_count;

        // Port çiftleri arasında da 32µs bekle
        if (p < NUM_PORT_PAIRS - 1 && !g_interrupted) {
            LOG_TRACE("Waiting %d us before next port pair...", config->delay_us);

            if (config->use_busy_wait) {
                precise_delay_us_busy(config->delay_us);
            } else {
                precise_delay_us(config->delay_us);
            }
        }
    }

    LOG_INFO("Latency test completed. Total results: %d", *result_count);

    return 0;
}
