/**
 * @file embedded_latency.h
 * @brief Embedded HW Timestamp Latency Test for DPDK
 *
 * DPDK EAL başlamadan ÖNCE çalıştırılır (raw socket kullanır).
 * Sonuçlar global struct'ta saklanır, DPDK içinden erişilebilir.
 *
 * ÖNEMLI: Bu test DPDK NIC'leri devralMADAN önce çalışmalı!
 */

#ifndef EMBEDDED_LATENCY_H
#define EMBEDDED_LATENCY_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================
// CONFIGURATION
// ============================================
#define EMB_LAT_MAX_RESULTS     64      // Maximum VLAN results
#define EMB_LAT_MAX_PORT_PAIRS  8       // Maximum port pairs

// ============================================
// RESULT STRUCTURE
// ============================================
struct emb_latency_result {
    uint16_t tx_port;           // TX port ID
    uint16_t rx_port;           // RX port ID
    uint16_t vlan_id;           // VLAN tag
    uint16_t vl_id;             // VL-ID

    uint32_t tx_count;          // Packets sent
    uint32_t rx_count;          // Packets received

    uint64_t min_latency_ns;    // Minimum latency (nanoseconds)
    uint64_t max_latency_ns;    // Maximum latency (nanoseconds)
    uint64_t avg_latency_ns;    // Average latency (nanoseconds)

    bool     valid;             // Valid result?
    bool     passed;            // Latency threshold passed?
    char     error_msg[64];     // Error message
};

// ============================================
// GLOBAL STATE
// ============================================
struct emb_latency_state {
    bool     test_completed;            // Test ran?
    bool     test_passed;               // All tests passed?
    uint32_t result_count;              // Number of results
    uint32_t passed_count;              // Passed tests
    uint32_t failed_count;              // Failed tests

    uint64_t overall_min_ns;            // Overall minimum
    uint64_t overall_max_ns;            // Overall maximum
    uint64_t overall_avg_ns;            // Overall average

    uint64_t test_duration_ns;          // Test duration

    struct emb_latency_result results[EMB_LAT_MAX_RESULTS];
};

// Global state - accessible from anywhere in DPDK
extern struct emb_latency_state g_emb_latency;

// ============================================
// API
// ============================================

/**
 * Run embedded latency test
 * MUST be called BEFORE rte_eal_init()!
 *
 * @param packet_count  Packets per VLAN (default: 1)
 * @param timeout_ms    RX timeout in ms (default: 100)
 * @param max_latency_us Maximum acceptable latency in us (default: 30)
 * @return              0 = all passed, >0 = fail count, <0 = error
 */
int emb_latency_run(int packet_count, int timeout_ms, int max_latency_us);

/**
 * Quick run with defaults
 * @return 0 = all passed, >0 = fail count, <0 = error
 */
int emb_latency_run_default(void);

/**
 * Interactive run with user prompts
 * Asks user if they want to run test and if loopback connectors are installed.
 * Same pattern as Dtn.cpp latencyTestSequence()
 *
 * @return 0 = all passed or skipped, >0 = fail count, <0 = error
 */
int emb_latency_run_interactive(void);

/**
 * Check if test completed
 */
bool emb_latency_completed(void);

/**
 * Check if all tests passed
 */
bool emb_latency_all_passed(void);

/**
 * Get result count
 */
int emb_latency_get_count(void);

/**
 * Get result by index
 */
const struct emb_latency_result* emb_latency_get(int index);

/**
 * Get result by VLAN ID
 */
const struct emb_latency_result* emb_latency_get_by_vlan(uint16_t vlan_id);

/**
 * Get latency values for a VLAN (in microseconds)
 */
bool emb_latency_get_us(uint16_t vlan_id, double *min, double *avg, double *max);

/**
 * Print all results
 */
void emb_latency_print(void);

/**
 * Print summary only
 */
void emb_latency_print_summary(void);

#ifdef __cplusplus
}
#endif

#endif // EMBEDDED_LATENCY_H
