/**
 * @file latency_results_shm.c
 * @brief Shared Memory implementation for Latency Test Results
 *
 * POSIX shared memory kullanarak latency_test ve DPDK arasında
 * sonuç paylaşımı sağlar.
 */

#include "latency_results_shm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>

// ============================================
// INTERNAL HELPERS
// ============================================

static size_t get_shm_size(void) {
    return sizeof(struct latency_shm_header);
}

// ============================================
// WRITER API IMPLEMENTATION
// ============================================

struct latency_shm_header* latency_shm_create(void) {
    // Remove existing shared memory if exists
    shm_unlink(LATENCY_SHM_NAME);

    // Create shared memory
    int fd = shm_open(LATENCY_SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd < 0) {
        fprintf(stderr, "latency_shm: Failed to create shared memory: %s\n",
                strerror(errno));
        return NULL;
    }

    // Set size
    size_t shm_size = get_shm_size();
    if (ftruncate(fd, shm_size) < 0) {
        fprintf(stderr, "latency_shm: Failed to set shared memory size: %s\n",
                strerror(errno));
        close(fd);
        shm_unlink(LATENCY_SHM_NAME);
        return NULL;
    }

    // Map memory
    struct latency_shm_header *shm = mmap(NULL, shm_size,
                                          PROT_READ | PROT_WRITE,
                                          MAP_SHARED, fd, 0);
    close(fd);  // fd not needed after mmap

    if (shm == MAP_FAILED) {
        fprintf(stderr, "latency_shm: Failed to map shared memory: %s\n",
                strerror(errno));
        shm_unlink(LATENCY_SHM_NAME);
        return NULL;
    }

    // Initialize header
    memset(shm, 0, shm_size);
    shm->magic = LATENCY_SHM_MAGIC;
    shm->version = 1;
    shm->test_complete = 0;
    shm->test_start_time = latency_shm_get_time_ns();

    printf("latency_shm: Created shared memory '%s' (%zu bytes)\n",
           LATENCY_SHM_NAME, shm_size);

    return shm;
}

int latency_shm_write_result(struct latency_shm_header *shm,
                             const struct shm_latency_result *result,
                             int index) {
    if (!shm || !result) return -1;
    if (index < 0 || index >= LATENCY_SHM_MAX_RESULTS) return -1;

    // Copy result
    memcpy(&shm->results[index], result, sizeof(*result));

    // Update count if needed
    if ((uint32_t)(index + 1) > shm->result_count) {
        shm->result_count = index + 1;
    }

    return 0;
}

void latency_shm_finalize(struct latency_shm_header *shm, int result_count) {
    if (!shm) return;

    shm->result_count = result_count;
    shm->test_end_time = latency_shm_get_time_ns();

    // Calculate summary statistics
    uint64_t min_ns = UINT64_MAX;
    uint64_t max_ns = 0;
    uint64_t total_ns = 0;
    uint32_t valid_count = 0;
    uint32_t passed = 0;
    uint32_t failed = 0;

    for (int i = 0; i < result_count; i++) {
        const struct shm_latency_result *r = &shm->results[i];

        if (r->valid && r->rx_count > 0) {
            valid_count++;

            if (r->min_latency_ns < min_ns) {
                min_ns = r->min_latency_ns;
            }
            if (r->max_latency_ns > max_ns) {
                max_ns = r->max_latency_ns;
            }
            total_ns += r->total_latency_ns / r->rx_count;  // Average for this VLAN
        }

        if (r->passed) {
            passed++;
        } else {
            failed++;
        }
    }

    shm->overall_min_ns = (min_ns == UINT64_MAX) ? 0 : min_ns;
    shm->overall_max_ns = max_ns;
    shm->overall_avg_ns = (valid_count > 0) ? (total_ns / valid_count) : 0;
    shm->total_passed = passed;
    shm->total_failed = failed;

    // Memory barrier to ensure all writes are visible
    __sync_synchronize();

    // Mark as complete (atomic)
    __sync_fetch_and_or(&shm->test_complete, 1);

    printf("latency_shm: Finalized results (count=%d, passed=%u, failed=%u)\n",
           result_count, passed, failed);
}

void latency_shm_close_writer(struct latency_shm_header *shm) {
    if (shm && shm != MAP_FAILED) {
        munmap(shm, get_shm_size());
        printf("latency_shm: Writer closed\n");
    }
}

void latency_shm_unlink(void) {
    if (shm_unlink(LATENCY_SHM_NAME) == 0) {
        printf("latency_shm: Shared memory unlinked\n");
    }
}

// ============================================
// READER API IMPLEMENTATION
// ============================================

struct latency_shm_header* latency_shm_open(int timeout_ms) {
    int elapsed_ms = 0;
    int fd = -1;

    // Try to open, with optional timeout
    while (1) {
        fd = shm_open(LATENCY_SHM_NAME, O_RDONLY, 0);
        if (fd >= 0) break;

        if (timeout_ms <= 0) {
            // No wait requested
            return NULL;
        }

        if (elapsed_ms >= timeout_ms) {
            fprintf(stderr, "latency_shm: Timeout waiting for shared memory\n");
            return NULL;
        }

        // Wait and retry
        usleep(100000);  // 100ms
        elapsed_ms += 100;
    }

    // Map memory (read-only)
    size_t shm_size = get_shm_size();
    struct latency_shm_header *shm = mmap(NULL, shm_size,
                                          PROT_READ,
                                          MAP_SHARED, fd, 0);
    close(fd);

    if (shm == MAP_FAILED) {
        fprintf(stderr, "latency_shm: Failed to map shared memory: %s\n",
                strerror(errno));
        return NULL;
    }

    // Validate magic number
    if (shm->magic != LATENCY_SHM_MAGIC) {
        fprintf(stderr, "latency_shm: Invalid magic number (got 0x%08X, expected 0x%08X)\n",
                shm->magic, LATENCY_SHM_MAGIC);
        munmap((void*)shm, shm_size);
        return NULL;
    }

    printf("latency_shm: Opened shared memory (version=%u, results=%u)\n",
           shm->version, shm->result_count);

    return shm;
}

bool latency_shm_is_complete(const struct latency_shm_header *shm) {
    if (!shm) return false;
    // Atomic read
    return __sync_fetch_and_add((uint32_t*)&shm->test_complete, 0) != 0;
}

const struct shm_latency_result* latency_shm_get_result(
    const struct latency_shm_header *shm, int index) {

    if (!shm) return NULL;
    if (index < 0 || (uint32_t)index >= shm->result_count) return NULL;

    return &shm->results[index];
}

const struct shm_latency_result* latency_shm_get_result_by_vlan(
    const struct latency_shm_header *shm, uint16_t vlan_id) {

    if (!shm) return NULL;

    for (uint32_t i = 0; i < shm->result_count; i++) {
        if (shm->results[i].vlan_id == vlan_id) {
            return &shm->results[i];
        }
    }
    return NULL;
}

const struct shm_latency_result* latency_shm_get_result_by_port(
    const struct latency_shm_header *shm,
    uint16_t tx_port, uint16_t rx_port, uint16_t vlan_id) {

    if (!shm) return NULL;

    for (uint32_t i = 0; i < shm->result_count; i++) {
        const struct shm_latency_result *r = &shm->results[i];
        if (r->tx_port == tx_port && r->rx_port == rx_port && r->vlan_id == vlan_id) {
            return r;
        }
    }
    return NULL;
}

void latency_shm_print_results(const struct latency_shm_header *shm) {
    if (!shm) {
        printf("latency_shm: No shared memory\n");
        return;
    }

    printf("\n");
    printf("========== LATENCY TEST RESULTS (Shared Memory) ==========\n");
    printf("Version: %u | Complete: %s | Results: %u\n",
           shm->version,
           shm->test_complete ? "YES" : "NO",
           shm->result_count);

    if (shm->test_complete) {
        double duration_ms = (shm->test_end_time - shm->test_start_time) / 1000000.0;
        printf("Duration: %.2f ms\n", duration_ms);
        printf("Summary: PASS=%u, FAIL=%u\n", shm->total_passed, shm->total_failed);
        printf("Latency: Min=%.2f us, Avg=%.2f us, Max=%.2f us\n",
               latency_shm_ns_to_us(shm->overall_min_ns),
               latency_shm_ns_to_us(shm->overall_avg_ns),
               latency_shm_ns_to_us(shm->overall_max_ns));
    }

    printf("\n");
    printf("%-8s %-8s %-8s %-8s %-12s %-12s %-12s %-8s %-8s\n",
           "TX Port", "RX Port", "VLAN", "VL-ID",
           "Min (us)", "Avg (us)", "Max (us)", "RX/TX", "Result");
    printf("-------- -------- -------- -------- ------------ ------------ ------------ -------- --------\n");

    for (uint32_t i = 0; i < shm->result_count; i++) {
        const struct shm_latency_result *r = &shm->results[i];

        double min_us = latency_shm_ns_to_us(r->min_latency_ns);
        double avg_us = (r->rx_count > 0) ?
                        latency_shm_ns_to_us(r->total_latency_ns / r->rx_count) : 0;
        double max_us = latency_shm_ns_to_us(r->max_latency_ns);

        printf("%-8u %-8u %-8u %-8u %-12.2f %-12.2f %-12.2f %u/%-5u %s\n",
               r->tx_port, r->rx_port, r->vlan_id, r->vl_id,
               min_us, avg_us, max_us,
               r->rx_count, r->tx_count,
               r->passed ? "PASS" : "FAIL");
    }

    printf("==========================================================\n\n");
}

void latency_shm_close_reader(struct latency_shm_header *shm) {
    if (shm && shm != MAP_FAILED) {
        munmap((void*)shm, get_shm_size());
        printf("latency_shm: Reader closed\n");
    }
}
