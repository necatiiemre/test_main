/**
 * @file latency_results_shm.h
 * @brief Shared Memory interface for Latency Test Results
 *
 * Bu header hem latency_test hem de DPDK tarafından kullanılır.
 * Latency test sonuçları shared memory üzerinden paylaşılır.
 *
 * Kullanım:
 *   latency_test: latency_shm_write_results() ile sonuçları yazar
 *   DPDK:         latency_shm_read_results() ile sonuçları okur
 */

#ifndef LATENCY_RESULTS_SHM_H
#define LATENCY_RESULTS_SHM_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================
// CONFIGURATION
// ============================================
#define LATENCY_SHM_NAME        "/latency_test_results"
#define LATENCY_SHM_MAX_RESULTS 64      // Maximum number of VLAN results
#define LATENCY_SHM_MAGIC       0x4C415459  // "LATY" magic number

// ============================================
// RESULT STRUCTURE (matches latency_test's struct)
// ============================================
struct shm_latency_result {
    uint16_t tx_port;           // TX port ID
    uint16_t rx_port;           // RX port ID
    uint16_t vlan_id;           // VLAN tag
    uint16_t vl_id;             // VL-ID (MAC/IP suffix)

    uint32_t tx_count;          // Packets sent
    uint32_t rx_count;          // Packets received

    uint64_t min_latency_ns;    // Minimum latency (nanoseconds)
    uint64_t max_latency_ns;    // Maximum latency (nanoseconds)
    uint64_t total_latency_ns;  // Total latency (for average calculation)

    bool     valid;             // Valid result?
    bool     passed;            // Latency threshold check: true = PASS, false = FAIL
    char     error_msg[64];     // Error message (if any)
};

// ============================================
// SHARED MEMORY HEADER
// ============================================
struct latency_shm_header {
    uint32_t magic;             // Magic number for validation (LATENCY_SHM_MAGIC)
    uint32_t version;           // Version number (for compatibility)
    uint32_t result_count;      // Number of valid results
    uint32_t test_complete;     // 1 = test completed, 0 = in progress

    // Test configuration
    int32_t  packet_count;      // Packets per VLAN
    int32_t  packet_size;       // Packet size
    uint64_t max_latency_ns;    // Threshold used

    // Timing
    uint64_t test_start_time;   // Test start timestamp (epoch ns)
    uint64_t test_end_time;     // Test end timestamp (epoch ns)

    // Summary statistics
    uint64_t overall_min_ns;    // Minimum across all VLANs
    uint64_t overall_max_ns;    // Maximum across all VLANs
    uint64_t overall_avg_ns;    // Average across all VLANs
    uint32_t total_passed;      // Number of passed tests
    uint32_t total_failed;      // Number of failed tests

    // Padding for future expansion
    uint8_t  reserved[64];

    // Results array
    struct shm_latency_result results[LATENCY_SHM_MAX_RESULTS];
};

// ============================================
// WRITER API (for latency_test)
// ============================================

/**
 * Initialize shared memory for writing
 * Creates or opens the shared memory segment
 *
 * @return Pointer to header, or NULL on error
 */
struct latency_shm_header* latency_shm_create(void);

/**
 * Write a single result to shared memory
 *
 * @param shm       Shared memory header pointer
 * @param result    Result to write
 * @param index     Index in results array
 * @return          0 on success, -1 on error
 */
int latency_shm_write_result(struct latency_shm_header *shm,
                             const struct shm_latency_result *result,
                             int index);

/**
 * Mark test as complete and calculate summary statistics
 *
 * @param shm           Shared memory header pointer
 * @param result_count  Total number of results
 */
void latency_shm_finalize(struct latency_shm_header *shm, int result_count);

/**
 * Close writer's shared memory mapping
 * Note: Does NOT unlink the shared memory (reader may still need it)
 *
 * @param shm   Shared memory header pointer
 */
void latency_shm_close_writer(struct latency_shm_header *shm);

/**
 * Unlink (delete) the shared memory segment
 * Call this when results are no longer needed
 */
void latency_shm_unlink(void);

// ============================================
// READER API (for DPDK)
// ============================================

/**
 * Open shared memory for reading
 * Waits for the segment to be created if it doesn't exist
 *
 * @param timeout_ms    Timeout in milliseconds (0 = no wait)
 * @return              Pointer to header, or NULL on error/timeout
 */
struct latency_shm_header* latency_shm_open(int timeout_ms);

/**
 * Check if test is complete
 *
 * @param shm   Shared memory header pointer
 * @return      true if test is complete
 */
bool latency_shm_is_complete(const struct latency_shm_header *shm);

/**
 * Get result by index
 *
 * @param shm   Shared memory header pointer
 * @param index Result index
 * @return      Pointer to result, or NULL if invalid
 */
const struct shm_latency_result* latency_shm_get_result(
    const struct latency_shm_header *shm, int index);

/**
 * Get result by VLAN ID
 *
 * @param shm       Shared memory header pointer
 * @param vlan_id   VLAN ID to search for
 * @return          Pointer to result, or NULL if not found
 */
const struct shm_latency_result* latency_shm_get_result_by_vlan(
    const struct latency_shm_header *shm, uint16_t vlan_id);

/**
 * Get result by port pair
 *
 * @param shm       Shared memory header pointer
 * @param tx_port   TX port ID
 * @param rx_port   RX port ID
 * @param vlan_id   VLAN ID
 * @return          Pointer to result, or NULL if not found
 */
const struct shm_latency_result* latency_shm_get_result_by_port(
    const struct latency_shm_header *shm,
    uint16_t tx_port, uint16_t rx_port, uint16_t vlan_id);

/**
 * Print all results to stdout (for debugging)
 *
 * @param shm   Shared memory header pointer
 */
void latency_shm_print_results(const struct latency_shm_header *shm);

/**
 * Close reader's shared memory mapping
 *
 * @param shm   Shared memory header pointer
 */
void latency_shm_close_reader(struct latency_shm_header *shm);

// ============================================
// UTILITY FUNCTIONS
// ============================================

/**
 * Get current time in nanoseconds (epoch time)
 */
static inline uint64_t latency_shm_get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/**
 * Convert nanoseconds to microseconds
 */
static inline double latency_shm_ns_to_us(uint64_t ns) {
    return (double)ns / 1000.0;
}

#ifdef __cplusplus
}
#endif

#endif // LATENCY_RESULTS_SHM_H
