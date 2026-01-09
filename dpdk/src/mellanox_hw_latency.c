/**
 * @file mellanox_hw_latency.c
 * @brief Mellanox HW Timestamp Latency Test - DPDK Integration Implementation
 *
 * Bu dosya mellanox_latency testini DPDK uygulamasına entegre eder.
 * mellanox_latency kaynak kodlarını çağırır ve sonuçları global struct'a yazar.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "mellanox_hw_latency.h"

// Include mellanox_latency headers using relative path to avoid conflict with dpdk/include/config.h
// Path from dpdk/src/ -> dpdk/ -> test_main/ -> mellanox_latency/include/
#include "../../mellanox_latency/include/common.h"
#include "../../mellanox_latency/include/config.h"
#include "../../mellanox_latency/include/hw_timestamp.h"
#include "../../mellanox_latency/include/latency_test.h"

// ============================================
// GLOBAL VARIABLES
// ============================================

// Global latency summary - accessible from all DPDK code
struct mlx_latency_summary g_mellanox_latency_summary = {0};

// Debug level for mellanox_latency (extern from common.h)
// Already defined in mellanox_latency/src/main.c, but we need it here too
// We'll set it before running the test
int g_debug_level = 0;

// Interrupt flag (used by mellanox_latency)
volatile int g_interrupted = 0;

// ============================================
// SIGNAL HANDLER (for Ctrl+C during test)
// ============================================

static void mlx_signal_handler(int sig) {
    (void)sig;
    g_interrupted = 1;
}

// ============================================
// HELPER FUNCTIONS
// ============================================

/**
 * Convert mellanox_latency result to our summary format
 */
static void convert_result(const struct latency_result *src, struct mlx_latency_result *dst) {
    dst->tx_port = src->tx_port;
    dst->rx_port = src->rx_port;
    dst->vlan_id = src->vlan_id;
    dst->vl_id = src->vl_id;
    dst->tx_count = src->tx_count;
    dst->rx_count = src->rx_count;
    dst->valid = src->valid;
    dst->passed = src->passed;

    if (src->rx_count > 0) {
        dst->min_latency_us = ns_to_us(src->min_latency_ns);
        dst->avg_latency_us = ns_to_us(src->total_latency_ns / src->rx_count);
        dst->max_latency_us = ns_to_us(src->max_latency_ns);
    } else {
        dst->min_latency_us = 0.0;
        dst->avg_latency_us = 0.0;
        dst->max_latency_us = 0.0;
    }
}

/**
 * Calculate summary statistics from all results
 */
static void calculate_summary(struct latency_result *results, int result_count) {
    struct mlx_latency_summary *summary = &g_mellanox_latency_summary;

    // Reset summary
    memset(summary, 0, sizeof(*summary));

    summary->result_count = (uint16_t)result_count;

    // Track per-port data
    double port_latency_sum[MLX_MAX_PORT_PAIRS] = {0};
    int port_latency_count[MLX_MAX_PORT_PAIRS] = {0};

    double global_sum = 0.0;
    int global_count = 0;
    double global_min = 1e9;
    double global_max = 0.0;

    // Process each result
    for (int i = 0; i < result_count && i < MLX_MAX_RESULTS; i++) {
        struct latency_result *src = &results[i];
        struct mlx_latency_result *dst = &summary->all_results[i];

        // Convert to our format
        convert_result(src, dst);

        // Update counts
        summary->total_tx_packets += src->tx_count;
        summary->total_rx_packets += src->rx_count;
        summary->total_vlan_count++;

        if (src->passed) {
            summary->passed_vlan_count++;
        } else {
            summary->failed_vlan_count++;
        }

        // Update latency stats if valid
        if (src->rx_count > 0) {
            double avg_us = dst->avg_latency_us;
            double min_us = dst->min_latency_us;
            double max_us = dst->max_latency_us;

            // Global stats
            global_sum += avg_us;
            global_count++;
            if (min_us < global_min) global_min = min_us;
            if (max_us > global_max) global_max = max_us;

            // Per-port stats
            uint16_t tx_port = src->tx_port;
            if (tx_port < MLX_MAX_PORT_PAIRS) {
                port_latency_sum[tx_port] += avg_us;
                port_latency_count[tx_port]++;

                // Update port structure
                struct mlx_port_latency *port = &summary->ports[tx_port];
                port->port_id = tx_port;
                port->total_tx += src->tx_count;
                port->total_rx += src->rx_count;

                if (port->vlan_count < MLX_MAX_VLANS_PER_PAIR) {
                    port->vlan_results[port->vlan_count] = *dst;
                    port->vlan_count++;
                }

                if (src->passed) {
                    port->passed_count++;
                }

                // Update port min/max
                if (port->min_latency_us == 0.0 || min_us < port->min_latency_us) {
                    port->min_latency_us = min_us;
                }
                if (max_us > port->max_latency_us) {
                    port->max_latency_us = max_us;
                }
            }
        }
    }

    // Calculate global averages
    if (global_count > 0) {
        summary->global_avg_us = global_sum / global_count;
        summary->global_min_us = global_min;
        summary->global_max_us = global_max;
    }

    // Calculate per-port averages and count ports
    for (int p = 0; p < MLX_MAX_PORT_PAIRS; p++) {
        if (port_latency_count[p] > 0) {
            summary->ports[p].avg_latency_us = port_latency_sum[p] / port_latency_count[p];
            summary->port_count++;
        }
    }

    // Set test status
    summary->test_completed = true;
    summary->test_passed = (summary->failed_vlan_count == 0);
}

// ============================================
// PUBLIC API FUNCTIONS
// ============================================

int run_mellanox_hw_latency_test(int packet_count, int verbose) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║       MELLANOX HW TIMESTAMP LATENCY TEST (DPDK INTEGRATED)       ║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    // Check root
    if (geteuid() != 0) {
        fprintf(stderr, "Error: Mellanox latency test requires root privileges.\n");
        return -1;
    }

    // Reset global state
    memset(&g_mellanox_latency_summary, 0, sizeof(g_mellanox_latency_summary));
    g_interrupted = 0;

    // Set debug level
    g_debug_level = verbose;

    // Setup signal handler
    struct sigaction sa_old_int, sa_old_term;
    struct sigaction sa_new;
    memset(&sa_new, 0, sizeof(sa_new));
    sa_new.sa_handler = mlx_signal_handler;

    sigaction(SIGINT, &sa_new, &sa_old_int);
    sigaction(SIGTERM, &sa_new, &sa_old_term);

    // Setup test config
    struct test_config config = {
        .packet_count = (packet_count > 0) ? packet_count : DEFAULT_PACKET_COUNT,
        .packet_size = DEFAULT_PACKET_SIZE,
        .delay_us = DEFAULT_PACKET_INTERVAL_US,
        .timeout_ms = DEFAULT_TIMEOUT_MS,
        .port_filter = -1,  // All ports
        .use_busy_wait = false,
        .max_latency_ns = DEFAULT_MAX_LATENCY_NS,
        .retry_count = DEFAULT_RETRY_COUNT
    };

    printf("Test Configuration:\n");
    printf("  Packet count per VLAN: %d\n", config.packet_count);
    printf("  Packet size: %d bytes\n", config.packet_size);
    printf("  RX timeout: %d ms\n", config.timeout_ms);
    printf("  Max latency threshold: %.1f us\n", (double)config.max_latency_ns / 1000.0);
    printf("  Retry count: %d\n", config.retry_count);
    printf("\n");

    // Check interfaces first
    printf("Checking HW timestamp support on interfaces...\n");
    int check_ret = check_all_interfaces();
    if (check_ret < 0) {
        printf("Warning: Some interfaces may not support HW timestamp\n");
    }

    // Allocate results
    struct latency_result *results = calloc(MAX_RESULTS, sizeof(struct latency_result));
    if (!results) {
        fprintf(stderr, "Error: Failed to allocate memory for results\n");
        sigaction(SIGINT, &sa_old_int, NULL);
        sigaction(SIGTERM, &sa_old_term, NULL);
        return -2;
    }

    // Run test with retry
    int result_count = 0;
    int attempt = 0;

    printf("\nStarting latency test...\n\n");
    int ret = run_latency_test_with_retry(&config, results, &result_count, &attempt);

    // Store attempt count
    g_mellanox_latency_summary.attempt_count = attempt;

    // Calculate and store summary
    if (result_count > 0) {
        calculate_summary(results, result_count);
    }

    // Cleanup
    free(results);

    // Restore signal handlers
    sigaction(SIGINT, &sa_old_int, NULL);
    sigaction(SIGTERM, &sa_old_term, NULL);

    // Print summary
    if (!g_interrupted) {
        printf("\n");
        print_mellanox_latency_summary();
    }

    if (g_interrupted) {
        printf("\nTest interrupted by user.\n");
        return -10;
    }

    if (ret < 0) {
        printf("\nTest failed with error: %d\n", ret);
        return ret;
    }

    printf("\n");
    if (g_mellanox_latency_summary.test_passed) {
        printf("=== ALL TESTS PASSED ===\n");
    } else {
        printf("=== %d TESTS FAILED ===\n", g_mellanox_latency_summary.failed_vlan_count);
    }
    printf("\n");

    return g_mellanox_latency_summary.failed_vlan_count;
}

int run_mellanox_hw_latency_test_default(void) {
    return run_mellanox_hw_latency_test(1, 1);
}

void print_mellanox_latency_summary(void) {
    struct mlx_latency_summary *s = &g_mellanox_latency_summary;

    if (!s->test_completed) {
        printf("Mellanox latency test not completed.\n");
        return;
    }

    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║               MELLANOX LATENCY SUMMARY                           ║\n");
    printf("╠══════════════════════════════════════════════════════════════════╣\n");
    printf("║  Status: %s (Attempts: %d)                              ║\n",
           s->test_passed ? "ALL PASS" : "FAILED  ",
           s->attempt_count);
    printf("╠══════════════════════════════════════════════════════════════════╣\n");
    printf("║  Global Latency:                                                 ║\n");
    printf("║    Min: %8.2f us | Avg: %8.2f us | Max: %8.2f us        ║\n",
           s->global_min_us, s->global_avg_us, s->global_max_us);
    printf("╠══════════════════════════════════════════════════════════════════╣\n");
    printf("║  VLANs: %3u tested | %3u passed | %3u failed                     ║\n",
           s->total_vlan_count, s->passed_vlan_count, s->failed_vlan_count);
    printf("║  Packets: %6u TX | %6u RX                                    ║\n",
           s->total_tx_packets, s->total_rx_packets);
    printf("╠══════════════════════════════════════════════════════════════════╣\n");
    printf("║  Per-Port Summary:                                               ║\n");
    printf("║  Port │  Min (us) │  Avg (us) │  Max (us) │ VLANs │ Status       ║\n");
    printf("║  ─────┼───────────┼───────────┼───────────┼───────┼──────────    ║\n");

    for (int p = 0; p < MLX_MAX_PORT_PAIRS; p++) {
        struct mlx_port_latency *port = &s->ports[p];
        if (port->vlan_count > 0) {
            const char *status = (port->passed_count == port->vlan_count) ? "PASS" : "FAIL";
            printf("║   %2d  │ %9.2f │ %9.2f │ %9.2f │ %2d/%2d │ %-4s         ║\n",
                   port->port_id,
                   port->min_latency_us,
                   port->avg_latency_us,
                   port->max_latency_us,
                   port->passed_count,
                   port->vlan_count,
                   status);
        }
    }

    printf("╚══════════════════════════════════════════════════════════════════╝\n");
}

double get_port_avg_latency_us(uint16_t port_id) {
    if (port_id >= MLX_MAX_PORT_PAIRS) {
        return -1.0;
    }

    struct mlx_latency_summary *s = &g_mellanox_latency_summary;
    if (!s->test_completed) {
        return -1.0;
    }

    struct mlx_port_latency *port = &s->ports[port_id];
    if (port->vlan_count == 0) {
        return -1.0;
    }

    return port->avg_latency_us;
}

double get_global_avg_latency_us(void) {
    struct mlx_latency_summary *s = &g_mellanox_latency_summary;
    if (!s->test_completed) {
        return -1.0;
    }
    return s->global_avg_us;
}

bool is_latency_test_passed(void) {
    struct mlx_latency_summary *s = &g_mellanox_latency_summary;
    return s->test_completed && s->test_passed;
}
