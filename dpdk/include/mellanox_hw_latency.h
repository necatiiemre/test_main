/**
 * @file mellanox_hw_latency.h
 * @brief Mellanox HW Timestamp Latency Test - DPDK Integration API
 *
 * Bu header, mellanox_latency testini DPDK uygulamasına entegre eder.
 * Test sonuçları global struct üzerinden tüm DPDK koduna erişilebilir.
 *
 * İKİ TEST MODU:
 *   1. LOOPBACK TEST: Direkt kablo ile NIC latency ölçümü
 *   2. UNIT TEST: Switch üzerinden uçtan uca latency ölçümü
 *
 * NET LATENCY = UNIT LATENCY - LOOPBACK LATENCY
 * (Pure unit/switch latency, NIC overhead çıkarılmış)
 *
 * Kullanım:
 *   1. DPDK EAL init'ten ÖNCE loopback/unit test çağır
 *   2. Sonuçlara global struct'lar üzerinden eriş:
 *      - g_loopback_test_result (loopback test sonuçları)
 *      - g_unit_test_result (unit test sonuçları)
 *      - g_combined_latency_result (net latency dahil)
 *
 * Not: Testler raw socket kullandığı için DPDK port'ları
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

// Default loopback latency (microseconds) - used when loopback test skipped
#define MLX_DEFAULT_LOOPBACK_LATENCY_US  14.0

// ============================================
// TEST TYPES
// ============================================
typedef enum {
    MLX_TEST_LOOPBACK,      // Direct cable loopback test (NIC latency)
    MLX_TEST_UNIT           // Unit test through switch (end-to-end latency)
} mlx_test_type_t;

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

// ============================================
// DUAL-TEST SYSTEM: LOOPBACK + UNIT TEST
// ============================================

/**
 * Unit Test Port Mapping (cross-port through switch)
 * TX Port -> RX Port
 */
#define MLX_UNIT_TEST_RX_PORT(tx_port) ((tx_port) ^ 1)
// Port 0 -> 1, Port 1 -> 0
// Port 2 -> 3, Port 3 -> 2
// Port 4 -> 5, Port 5 -> 4
// Port 6 -> 7, Port 7 -> 6

/**
 * Per-port latency result for Loopback/Unit tests
 */
struct mlx_port_test_result {
    uint16_t port_id;           // Port ID (0-7)
    bool     tested;            // Was this port tested?
    bool     passed;            // Did all VLANs pass?

    double   min_latency_us;    // Minimum latency (microseconds)
    double   avg_latency_us;    // Average latency (microseconds)
    double   max_latency_us;    // Maximum latency (microseconds)

    uint32_t tx_count;          // Packets sent
    uint32_t rx_count;          // Packets received
    uint16_t vlan_count;        // VLANs tested
    uint16_t passed_count;      // VLANs passed
};

/**
 * Loopback Test Result
 * Stores per-port NIC latency (direct cable loopback)
 */
struct mlx_loopback_result {
    bool     test_completed;    // Test completed?
    bool     test_passed;       // All ports passed?
    bool     used_default;      // Used default 14us (test skipped)?

    double   global_avg_us;     // Global average latency

    // Per-port results
    struct mlx_port_test_result ports[MLX_MAX_PORT_PAIRS];
    uint16_t port_count;        // Number of ports tested
};

/**
 * Unit Test Result
 * Stores per-port end-to-end latency (through switch)
 */
struct mlx_unit_result {
    bool     test_completed;    // Test completed?
    bool     test_passed;       // All ports passed?

    double   global_avg_us;     // Global average latency

    // Per-port results (includes switch latency)
    struct mlx_port_test_result ports[MLX_MAX_PORT_PAIRS];
    uint16_t port_count;        // Number of ports tested
};

/**
 * Combined Latency Result
 * Contains raw unit latency, loopback latency, and net latency
 * NET = UNIT - LOOPBACK (pure switch/unit latency)
 */
struct mlx_combined_result {
    bool     loopback_completed;    // Loopback test done?
    bool     unit_completed;        // Unit test done?
    bool     loopback_used_default; // Used default 14us?

    // Global statistics
    double   global_loopback_us;    // Global loopback avg
    double   global_unit_us;        // Global unit avg
    double   global_net_us;         // Global net avg (unit - loopback)

    // Per-port combined results
    struct {
        uint16_t port_id;
        bool     valid;             // Has valid data?

        double   loopback_us;       // Loopback latency (NIC only)
        double   unit_us;           // Unit latency (NIC + switch)
        double   net_us;            // Net latency (switch only)
    } ports[MLX_MAX_PORT_PAIRS];

    uint16_t port_count;
};

// ============================================
// GLOBAL VARIABLES FOR DUAL-TEST SYSTEM
// ============================================

extern struct mlx_loopback_result g_loopback_result;
extern struct mlx_unit_result g_unit_result;
extern struct mlx_combined_result g_combined_result;

// ============================================
// DUAL-TEST FUNCTION DECLARATIONS
// ============================================

/**
 * Run Loopback Test (optional)
 * Measures NIC latency with direct cable loopback
 *
 * @param packet_count  Packets per VLAN
 * @param verbose       Verbose level
 * @return              0 = success, <0 = error
 */
int run_loopback_test(int packet_count, int verbose);

/**
 * Skip Loopback Test and use default latency
 * Sets all ports to MLX_DEFAULT_LOOPBACK_LATENCY_US (14.0 us)
 */
void skip_loopback_test_use_default(void);

/**
 * Run Unit Test (always runs)
 * Measures end-to-end latency through switch
 * Uses cross-port mapping: 0↔1, 2↔3, 4↔5, 6↔7
 *
 * @param packet_count  Packets per VLAN
 * @param verbose       Verbose level
 * @return              0 = success, <0 = error
 */
int run_unit_test(int packet_count, int verbose);

/**
 * Calculate combined/net latency results
 * Must be called after loopback (or skip) and unit tests
 * Populates g_combined_result with:
 *   - loopback_us: from loopback test or default 14us
 *   - unit_us: from unit test
 *   - net_us: unit_us - loopback_us
 */
void calculate_combined_latency(void);

/**
 * Interactive loopback test flow
 * Asks user if they want loopback test, handles cable check
 *
 * @param packet_count  Packets per VLAN
 * @param verbose       Verbose level
 * @return              true if loopback test was run, false if skipped
 */
bool interactive_loopback_test(int packet_count, int verbose);

/**
 * Run complete latency test sequence
 * 1. Interactive loopback test (optional)
 * 2. Unit test (always)
 * 3. Calculate combined results
 *
 * @param packet_count  Packets per VLAN
 * @param verbose       Verbose level
 * @return              0 = success, <0 = error
 */
int run_complete_latency_test(int packet_count, int verbose);

/**
 * Print combined latency results
 * Shows loopback, unit, and net latency per port
 */
void print_combined_latency_summary(void);

/**
 * Get net latency for a specific port
 *
 * @param port_id   Port ID (0-7)
 * @return          Net latency in microseconds, -1.0 if not available
 */
double get_port_net_latency_us(uint16_t port_id);

/**
 * Get global net latency average
 *
 * @return          Global net latency in microseconds, -1.0 if not available
 */
double get_global_net_latency_us(void);

#endif // MELLANOX_HW_LATENCY_H
