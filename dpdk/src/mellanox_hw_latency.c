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

// ============================================
// DUAL-TEST SYSTEM: LOOPBACK + UNIT TEST
// ============================================

// Global variables for dual-test system
struct mlx_loopback_result g_loopback_result = {0};
struct mlx_unit_result g_unit_result = {0};
struct mlx_combined_result g_combined_result = {0};

/**
 * Helper: Copy results from g_mellanox_latency_summary to port test result
 */
static void copy_summary_to_port_result(struct mlx_port_test_result *dst) {
    struct mlx_latency_summary *src = &g_mellanox_latency_summary;

    for (int p = 0; p < MLX_MAX_PORT_PAIRS; p++) {
        struct mlx_port_latency *port = &src->ports[p];
        if (port->vlan_count > 0) {
            dst[p].port_id = (uint16_t)p;
            dst[p].tested = true;
            dst[p].passed = (port->passed_count == port->vlan_count);
            dst[p].min_latency_us = port->min_latency_us;
            dst[p].avg_latency_us = port->avg_latency_us;
            dst[p].max_latency_us = port->max_latency_us;
            dst[p].tx_count = port->total_tx;
            dst[p].rx_count = port->total_rx;
            dst[p].vlan_count = port->vlan_count;
            dst[p].passed_count = port->passed_count;
        }
    }
}

// ============================================
// LOOPBACK TEST
// ============================================

int run_loopback_test(int packet_count, int verbose) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║              LOOPBACK TEST (NIC Latency Measurement)             ║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    // Reset loopback result
    memset(&g_loopback_result, 0, sizeof(g_loopback_result));

    // Run the standard latency test (loopback mode - same port TX/RX)
    int ret = run_mellanox_hw_latency_test(packet_count, verbose);

    if (ret < 0) {
        printf("Loopback test failed with error: %d\n", ret);
        return ret;
    }

    // Copy results to loopback result struct
    g_loopback_result.test_completed = g_mellanox_latency_summary.test_completed;
    g_loopback_result.test_passed = g_mellanox_latency_summary.test_passed;
    g_loopback_result.used_default = false;
    g_loopback_result.global_avg_us = g_mellanox_latency_summary.global_avg_us;
    g_loopback_result.port_count = g_mellanox_latency_summary.port_count;

    // Copy per-port results
    copy_summary_to_port_result(g_loopback_result.ports);

    printf("\n=== LOOPBACK TEST COMPLETED ===\n");
    return ret;
}

void skip_loopback_test_use_default(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║     LOOPBACK TEST SKIPPED - Using Default %.1f us              ║\n",
           MLX_DEFAULT_LOOPBACK_LATENCY_US);
    printf("╚══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    // Reset and set default values
    memset(&g_loopback_result, 0, sizeof(g_loopback_result));

    g_loopback_result.test_completed = true;
    g_loopback_result.test_passed = true;
    g_loopback_result.used_default = true;
    g_loopback_result.global_avg_us = MLX_DEFAULT_LOOPBACK_LATENCY_US;

    // Set default for all ports
    for (int p = 0; p < MLX_MAX_PORT_PAIRS; p++) {
        g_loopback_result.ports[p].port_id = (uint16_t)p;
        g_loopback_result.ports[p].tested = true;
        g_loopback_result.ports[p].passed = true;
        g_loopback_result.ports[p].avg_latency_us = MLX_DEFAULT_LOOPBACK_LATENCY_US;
        g_loopback_result.ports[p].min_latency_us = MLX_DEFAULT_LOOPBACK_LATENCY_US;
        g_loopback_result.ports[p].max_latency_us = MLX_DEFAULT_LOOPBACK_LATENCY_US;
    }

    g_loopback_result.port_count = MLX_MAX_PORT_PAIRS;

    printf("All ports set to default loopback latency: %.1f us\n", MLX_DEFAULT_LOOPBACK_LATENCY_US);
}

// ============================================
// UNIT TEST
// ============================================

int run_unit_test(int packet_count, int verbose) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║         UNIT TEST (End-to-End Latency Through Switch)            ║\n");
    printf("║         Port Mapping: 0↔1, 2↔3, 4↔5, 6↔7                        ║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    // Reset unit result
    memset(&g_unit_result, 0, sizeof(g_unit_result));

    // Check root
    if (geteuid() != 0) {
        fprintf(stderr, "Error: Unit test requires root privileges.\n");
        return -1;
    }

    // Reset global state
    memset(&g_mellanox_latency_summary, 0, sizeof(g_mellanox_latency_summary));
    g_interrupted = 0;
    g_debug_level = verbose;

    // Setup signal handler
    struct sigaction sa_old_int, sa_old_term;
    struct sigaction sa_new;
    memset(&sa_new, 0, sizeof(sa_new));
    sa_new.sa_handler = mlx_signal_handler;

    sigaction(SIGINT, &sa_new, &sa_old_int);
    sigaction(SIGTERM, &sa_new, &sa_old_term);

    // Setup test config for unit test (cross-port mode)
    struct test_config config = {
        .packet_count = (packet_count > 0) ? packet_count : DEFAULT_PACKET_COUNT,
        .packet_size = DEFAULT_PACKET_SIZE,
        .delay_us = DEFAULT_PACKET_INTERVAL_US,
        .timeout_ms = DEFAULT_TIMEOUT_MS,
        .port_filter = -1,
        .use_busy_wait = false,
        .max_latency_ns = DEFAULT_MAX_LATENCY_NS,
        .retry_count = DEFAULT_RETRY_COUNT
    };

    printf("Unit Test Configuration:\n");
    printf("  Packet count per VLAN: %d\n", config.packet_count);
    printf("  Port mapping: TX->RX cross-port (0↔1, 2↔3, 4↔5, 6↔7)\n");
    printf("\n");

    // Check interfaces
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

    // Run unit test with cross-port mode
    int result_count = 0;
    int attempt = 0;

    printf("\nStarting unit test (cross-port mode)...\n\n");
    int ret = run_latency_test_unit_mode(&config, results, &result_count, &attempt);

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

    if (g_interrupted) {
        printf("\nUnit test interrupted by user.\n");
        return -10;
    }

    if (ret < 0) {
        printf("\nUnit test failed with error: %d\n", ret);
        return ret;
    }

    // Copy results to unit result struct
    g_unit_result.test_completed = g_mellanox_latency_summary.test_completed;
    g_unit_result.test_passed = g_mellanox_latency_summary.test_passed;
    g_unit_result.global_avg_us = g_mellanox_latency_summary.global_avg_us;
    g_unit_result.port_count = g_mellanox_latency_summary.port_count;

    // Copy per-port results
    copy_summary_to_port_result(g_unit_result.ports);

    // Print summary
    printf("\n");
    print_mellanox_latency_summary();

    printf("\n=== UNIT TEST COMPLETED ===\n");
    return g_mellanox_latency_summary.failed_vlan_count;
}

// ============================================
// COMBINED LATENCY CALCULATION
// ============================================

void calculate_combined_latency(void) {
    memset(&g_combined_result, 0, sizeof(g_combined_result));

    g_combined_result.loopback_completed = g_loopback_result.test_completed;
    g_combined_result.unit_completed = g_unit_result.test_completed;
    g_combined_result.loopback_used_default = g_loopback_result.used_default;

    if (!g_unit_result.test_completed) {
        printf("Warning: Unit test not completed, cannot calculate combined latency.\n");
        return;
    }

    g_combined_result.global_loopback_us = g_loopback_result.global_avg_us;
    g_combined_result.global_unit_us = g_unit_result.global_avg_us;
    g_combined_result.global_net_us = g_unit_result.global_avg_us - g_loopback_result.global_avg_us;

    // Calculate per-port net latency
    int port_count = 0;
    for (int p = 0; p < MLX_MAX_PORT_PAIRS; p++) {
        g_combined_result.ports[p].port_id = (uint16_t)p;

        double loopback_us = g_loopback_result.ports[p].avg_latency_us;
        double unit_us = g_unit_result.ports[p].avg_latency_us;

        // Use default if loopback not available for this port
        if (!g_loopback_result.ports[p].tested || loopback_us <= 0) {
            loopback_us = MLX_DEFAULT_LOOPBACK_LATENCY_US;
        }

        if (g_unit_result.ports[p].tested && unit_us > 0) {
            g_combined_result.ports[p].valid = true;
            g_combined_result.ports[p].loopback_us = loopback_us;
            g_combined_result.ports[p].unit_us = unit_us;
            g_combined_result.ports[p].net_us = unit_us - loopback_us;
            port_count++;
        }
    }

    g_combined_result.port_count = (uint16_t)port_count;
}

// ============================================
// INTERACTIVE LOOPBACK TEST
// ============================================

/**
 * Helper: Read yes/no response from user
 */
static bool read_yes_no(const char *prompt) {
    char response[16];

    while (1) {
        printf("%s (yes/no): ", prompt);
        fflush(stdout);

        if (fgets(response, sizeof(response), stdin) == NULL) {
            return false;
        }

        // Remove newline
        response[strcspn(response, "\n")] = 0;

        // Convert to lowercase for comparison
        for (int i = 0; response[i]; i++) {
            if (response[i] >= 'A' && response[i] <= 'Z') {
                response[i] = response[i] + 32;
            }
        }

        if (strcmp(response, "yes") == 0 || strcmp(response, "y") == 0) {
            return true;
        }
        if (strcmp(response, "no") == 0 || strcmp(response, "n") == 0) {
            return false;
        }

        printf("Please enter 'yes' or 'no'.\n");
    }
}

bool interactive_loopback_test(int packet_count, int verbose) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║                    LOOPBACK TEST CONFIGURATION                   ║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    while (1) {
        // Ask if user wants loopback test
        bool want_loopback = read_yes_no("Do you want to run Loopback Test?");

        if (!want_loopback) {
            // User doesn't want loopback test - use default
            skip_loopback_test_use_default();
            return false;
        }

        // User wants loopback test - check if cables are connected
        printf("\n");
        printf("NOTE: Loopback test requires direct loopback cables connected.\n");
        printf("      Each port should have a cable looping back to itself.\n");
        printf("\n");

        bool cables_connected = read_yes_no("Are loopback cables connected?");

        if (cables_connected) {
            // Cables are connected - run loopback test
            run_loopback_test(packet_count, verbose);
            return true;
        }

        // Cables not connected - ask again
        printf("\n");
        printf("Loopback cables are not connected.\n");
        printf("Please connect cables and try again, or skip loopback test.\n");
        printf("\n");
        // Loop back to ask if they want loopback test again
    }
}

// ============================================
// COMPLETE LATENCY TEST SEQUENCE
// ============================================

int run_complete_latency_test(int packet_count, int verbose) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║              COMPLETE LATENCY TEST SEQUENCE                      ║\n");
    printf("║   1. Loopback Test (Optional) - NIC latency measurement          ║\n");
    printf("║   2. Unit Test (Always) - End-to-end through switch              ║\n");
    printf("║   3. Calculate Net Latency = Unit - Loopback                     ║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    // Step 1: Interactive loopback test (optional)
    interactive_loopback_test(packet_count, verbose);

    // Step 2: Unit test (always runs)
    int ret = run_unit_test(packet_count, verbose);
    if (ret < 0) {
        printf("Unit test failed, cannot continue.\n");
        return ret;
    }

    // Step 3: Calculate combined/net latency
    calculate_combined_latency();

    // Print combined results
    print_combined_latency_summary();

    return 0;
}

// ============================================
// COMBINED LATENCY SUMMARY PRINT
// ============================================

void print_combined_latency_summary(void) {
    struct mlx_combined_result *c = &g_combined_result;

    if (!c->unit_completed) {
        printf("Combined latency results not available (unit test not completed).\n");
        return;
    }

    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════════════════╗\n");
    printf("║                    COMBINED LATENCY RESULTS                              ║\n");
    printf("╠══════════════════════════════════════════════════════════════════════════╣\n");
    printf("║  Loopback Test: %s                                            ║\n",
           c->loopback_used_default ? "SKIPPED (using default 14.0 us)" : "COMPLETED");
    printf("║  Unit Test:     COMPLETED                                                ║\n");
    printf("╠══════════════════════════════════════════════════════════════════════════╣\n");
    printf("║  GLOBAL LATENCY:                                                         ║\n");
    printf("║    Loopback (NIC):     %8.2f us                                       ║\n", c->global_loopback_us);
    printf("║    Unit (Total):       %8.2f us                                       ║\n", c->global_unit_us);
    printf("║    Net (Switch Only):  %8.2f us                                       ║\n", c->global_net_us);
    printf("╠══════════════════════════════════════════════════════════════════════════╣\n");
    printf("║  PER-PORT LATENCY:                                                       ║\n");
    printf("║  Port │ Loopback (us) │  Unit (us)  │   Net (us)  │ Status               ║\n");
    printf("║  ─────┼───────────────┼─────────────┼─────────────┼──────────            ║\n");

    for (int p = 0; p < MLX_MAX_PORT_PAIRS; p++) {
        if (c->ports[p].valid) {
            const char *status = (c->ports[p].net_us >= 0) ? "OK" : "WARN";
            printf("║   %2d  │    %8.2f   │  %8.2f   │  %8.2f   │ %-4s                 ║\n",
                   c->ports[p].port_id,
                   c->ports[p].loopback_us,
                   c->ports[p].unit_us,
                   c->ports[p].net_us,
                   status);
        }
    }

    printf("╚══════════════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("Legend:\n");
    printf("  Loopback: NIC latency only (direct cable or default 14.0 us)\n");
    printf("  Unit:     Total end-to-end latency (NIC + Switch + Cable)\n");
    printf("  Net:      Pure switch/unit latency (Unit - Loopback)\n");
    printf("\n");
}

// ============================================
// NET LATENCY GETTERS
// ============================================

double get_port_net_latency_us(uint16_t port_id) {
    if (port_id >= MLX_MAX_PORT_PAIRS) {
        return -1.0;
    }

    if (!g_combined_result.unit_completed) {
        return -1.0;
    }

    if (!g_combined_result.ports[port_id].valid) {
        return -1.0;
    }

    return g_combined_result.ports[port_id].net_us;
}

double get_global_net_latency_us(void) {
    if (!g_combined_result.unit_completed) {
        return -1.0;
    }
    return g_combined_result.global_net_us;
}
