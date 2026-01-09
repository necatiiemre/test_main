#pragma once
#include <signal.h>
#include <getopt.h>
#include "common.h"
#include "port.h"

/**
 * Signal handler for graceful shutdown
 * Catches SIGINT (Ctrl+C) and SIGTERM
 */
static void signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\n\nSignal %d received, preparing to exit...\n", signum);
        force_quit = true;
    }
}

// Warm-up sırasında HW istatistikleri ve yerel sayaçları sıfırlar
void helper_reset_stats(const struct ports_config *ports_config,
                        uint64_t prev_tx_bytes[], uint64_t prev_rx_bytes[]);

// Her saniye çağır: büyük tablo + kuyruk dağılımları yazdırır
void helper_print_stats(const struct ports_config *ports_config,
                        const uint64_t prev_tx_bytes[], const uint64_t prev_rx_bytes[],
                        bool warmup_complete, unsigned loop_count, unsigned test_time);
