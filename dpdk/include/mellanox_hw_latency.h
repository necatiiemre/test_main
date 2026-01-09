/**
 * @file mellanox_hw_latency.h
 * @brief Mellanox HW Timestamp Latency Test - DPDK Integration API
 *
 * Bu header, mellanox_latency testini DPDK uygulamasına entegre eder.
 * Test sonuçları global struct üzerinden tüm DPDK koduna erişilebilir.
 *
 * Kullanım:
 *   1. DPDK EAL init'ten ÖNCE run_mellanox_hw_latency_test() çağır
 *   2. Sonuçlara g_mellanox_latency_summary üzerinden eriş
 *
 * Not: Mellanox testi raw socket kullandığı için DPDK port'ları
 * almadan önce çalıştırılmalıdır.
 */

#ifndef MELLANOX_HW_LATENCY_H
#define MELLANOX_HW_LATENCY_H

#include <stdint.h>
#include <stdbool.h>

// ============================================
// CONFIGURATION
// ============================================

// Enable/disable Mellanox HW latency test
#ifndef MELLANOX_HW_LATENCY_ENABLED
#define MELLANOX_HW_LATENCY_ENABLED 1
#endif

// Maximum number of port pairs and VLANs
#define MLX_MAX_PORT_PAIRS      8
#define MLX_MAX_VLANS_PER_PAIR  4
#define MLX_MAX_RESULTS         (MLX_MAX_PORT_PAIRS * MLX_MAX_VLANS_PER_PAIR)  // 32

// ============================================
// PER-VLAN LATENCY RESULT
// ============================================

/**
 * Single VLAN latency test result
 * Her VLAN için ayrı sonuç
 */
struct mlx_latency_result {
    uint16_t tx_port;           // TX port ID (0-7)
    uint16_t rx_port;           // RX port ID (0-7)
    uint16_t vlan_id;           // VLAN tag (802.1Q)
    uint16_t vl_id;             // VL-ID (MAC/IP suffix)

    uint32_t tx_count;          // Packets sent
    uint32_t rx_count;          // Packets received

    double   min_latency_us;    // Minimum latency (microseconds)
    double   avg_latency_us;    // Average latency (microseconds)
    double   max_latency_us;    // Maximum latency (microseconds)

    bool     valid;             // Valid result (at least 1 packet received)
    bool     passed;            // Latency threshold check passed
};

// ============================================
// PER-PORT LATENCY SUMMARY
// ============================================

/**
 * Port latency summary
 * Bir port'un tüm VLAN'larının özeti
 */
struct mlx_port_latency {
    uint16_t port_id;           // Port ID (0-7)
    uint16_t vlan_count;        // Number of VLANs tested

    double   min_latency_us;    // Minimum of all VLANs
    double   avg_latency_us;    // Average of all VLANs
    double   max_latency_us;    // Maximum of all VLANs

    uint32_t total_tx;          // Total packets sent
    uint32_t total_rx;          // Total packets received
    uint16_t passed_count;      // VLANs that passed threshold

    // Per-VLAN results
    struct mlx_latency_result vlan_results[MLX_MAX_VLANS_PER_PAIR];
};

// ============================================
// GLOBAL LATENCY SUMMARY
// ============================================

/**
 * Global latency summary
 * Tüm portların ve VLAN'ların özeti
 * DPDK kodundan erişilebilir global değişken
 */
struct mlx_latency_summary {
    // Test status
    bool     test_completed;    // Test tamamlandı mı?
    bool     test_passed;       // Tüm testler geçti mi?
    int      attempt_count;     // Kaç deneme yapıldı?

    // Global statistics
    double   global_min_us;     // Global minimum latency
    double   global_avg_us;     // Global average latency
    double   global_max_us;     // Global maximum latency

    // Counts
    uint16_t port_count;        // Number of ports tested
    uint16_t total_vlan_count;  // Total VLANs tested
    uint16_t passed_vlan_count; // VLANs that passed threshold
    uint16_t failed_vlan_count; // VLANs that failed threshold

    uint32_t total_tx_packets;  // Total packets sent
    uint32_t total_rx_packets;  // Total packets received

    // Per-port summaries
    struct mlx_port_latency ports[MLX_MAX_PORT_PAIRS];

    // All individual VLAN results (flat array for easy iteration)
    struct mlx_latency_result all_results[MLX_MAX_RESULTS];
    uint16_t result_count;      // Number of valid results
};

// ============================================
// GLOBAL VARIABLE DECLARATION
// ============================================

/**
 * Global latency summary - DPDK kodundan erişilebilir
 *
 * Kullanım örneği:
 *   if (g_mellanox_latency_summary.test_completed) {
 *       printf("Avg latency: %.2f us\n", g_mellanox_latency_summary.global_avg_us);
 *   }
 */
extern struct mlx_latency_summary g_mellanox_latency_summary;

// ============================================
// FUNCTION DECLARATIONS
// ============================================

/**
 * Run Mellanox HW timestamp latency test
 *
 * Bu fonksiyon DPDK EAL init'ten ÖNCE çağrılmalıdır!
 * Raw socket kullandığı için DPDK port'ları almadan önce çalışmalı.
 *
 * @param packet_count  Packet count per VLAN (0 = default: 1)
 * @param verbose       Verbose output level (0-3)
 * @return              0 = success (all passed), >0 = failed VLAN count, <0 = error
 */
int run_mellanox_hw_latency_test(int packet_count, int verbose);

/**
 * Run Mellanox HW latency test with default settings
 *
 * Wrapper for run_mellanox_hw_latency_test(1, 1)
 *
 * @return  0 = success, >0 = failed count, <0 = error
 */
int run_mellanox_hw_latency_test_default(void);

/**
 * Print latency summary to console
 *
 * Formatted table output of all results
 */
void print_mellanox_latency_summary(void);

/**
 * Get average latency for a specific port
 *
 * @param port_id   Port ID (0-7)
 * @return          Average latency in microseconds, -1.0 if not available
 */
double get_port_avg_latency_us(uint16_t port_id);

/**
 * Get global average latency
 *
 * @return  Average latency in microseconds, -1.0 if test not completed
 */
double get_global_avg_latency_us(void);

/**
 * Check if latency test completed successfully
 *
 * @return  true if test completed and all VLANs passed
 */
bool is_latency_test_passed(void);

#endif // MELLANOX_HW_LATENCY_H
