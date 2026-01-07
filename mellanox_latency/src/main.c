/**
 * @file main.c
 * @brief Mellanox HW Timestamp Latency Test - Main Entry Point
 *
 * Komut satırı argümanlarını parse eder ve testi başlatır.
 *
 * Kullanım:
 *   ./mellanox_latency [options]
 *
 * Seçenekler:
 *   -n, --count <N>     Her VLAN için paket sayısı (default: 1)
 *   -s, --size <bytes>  Paket boyutu (default: 1518)
 *   -d, --delay <us>    VLAN testleri arası bekleme (default: 32)
 *   -T, --timeout <ms>  RX timeout (default: 5000)
 *   -p, --port <id>     Sadece bu TX port'u test et (default: hepsi)
 *   -v, --verbose       Verbose çıktı (birden fazla -v daha detaylı)
 *   -c, --csv           CSV formatında çıktı
 *   -b, --busy-wait     Hassas bekleme için busy-wait kullan
 *   -C, --check         Sadece interface kontrolü yap
 *   -I, --info          Interface HW timestamp bilgilerini göster
 *   -h, --help          Yardım
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <signal.h>

#include "common.h"
#include "config.h"
#include "hw_timestamp.h"
#include "latency_test.h"

// ============================================
// GLOBAL VARIABLES
// ============================================

int g_debug_level = DEBUG_LEVEL_NONE;
static volatile int g_interrupted = 0;

// ============================================
// SIGNAL HANDLER
// ============================================

static void signal_handler(int sig) {
    (void)sig;
    g_interrupted = 1;
    printf("\nInterrupted, exiting...\n");
}

// ============================================
// USAGE
// ============================================

static void print_usage(const char *prog) {
    printf("Mellanox HW Timestamp Latency Test\n");
    printf("==================================\n\n");
    printf("Kullanim: %s [options]\n\n", prog);
    printf("Secenekler:\n");
    printf("  -n, --count <N>     Her VLAN icin paket sayisi (default: %d)\n", DEFAULT_PACKET_COUNT);
    printf("  -s, --size <bytes>  Paket boyutu (default: %d)\n", DEFAULT_PACKET_SIZE);
    printf("  -d, --delay <us>    VLAN testleri arasi bekleme, mikrosaniye (default: %d)\n", DEFAULT_PACKET_INTERVAL_US);
    printf("  -T, --timeout <ms>  RX timeout, milisaniye (default: %d)\n", DEFAULT_TIMEOUT_MS);
    printf("  -p, --port <id>     Sadece bu TX port'u test et (0-7, default: hepsi)\n");
    printf("  -v, --verbose       Verbose cikti (tekrarla: -vv, -vvv)\n");
    printf("  -c, --csv           CSV formatinda cikti\n");
    printf("  -b, --busy-wait     Hassas bekleme icin busy-wait kullan\n");
    printf("  -C, --check         Sadece interface kontrolu yap\n");
    printf("  -I, --info          Interface HW timestamp bilgilerini goster\n");
    printf("  -h, --help          Bu yardim mesaji\n");
    printf("\n");
    printf("Ornekler:\n");
    printf("  %s                    Varsayilan ayarlarla test\n", prog);
    printf("  %s -n 10              Her VLAN icin 10 paket\n", prog);
    printf("  %s -n 10 -v           Verbose cikti ile test\n", prog);
    printf("  %s -p 2 -n 5          Sadece Port 2 testi, 5 paket\n", prog);
    printf("  %s -c > results.csv   CSV olarak kaydet\n", prog);
    printf("  %s -I                 Interface bilgilerini goster\n", prog);
    printf("\n");
    printf("Port Eslestirmesi:\n");
    printf("  TX Port -> RX Port | Interface'ler        | VLAN'lar\n");
    printf("  ---------|---------|----------------------|----------\n");
    for (int i = 0; i < NUM_PORT_PAIRS; i++) {
        const struct port_pair *pp = &g_port_pairs[i];
        printf("  Port %d   -> Port %d | %-10s -> %-10s | %d-%d\n",
               pp->tx_port, pp->rx_port,
               pp->tx_iface, pp->rx_iface,
               pp->vlans[0], pp->vlans[pp->vlan_count - 1]);
    }
    printf("\n");
}

// ============================================
// SHOW INTERFACE INFO
// ============================================

static void show_interface_info(void) {
    printf("Interface HW Timestamp Bilgileri:\n");
    printf("=================================\n\n");

    for (int i = 0; i < NUM_PORT_PAIRS; i++) {
        const struct port_pair *pp = &g_port_pairs[i];

        printf("Port %d (%s):\n", pp->tx_port, pp->tx_iface);
        print_hw_timestamp_caps(pp->tx_iface);
    }
}

// ============================================
// MAIN
// ============================================

int main(int argc, char *argv[]) {
    // Default config
    struct test_config config = {
        .packet_count = DEFAULT_PACKET_COUNT,
        .packet_size = DEFAULT_PACKET_SIZE,
        .delay_us = DEFAULT_PACKET_INTERVAL_US,
        .timeout_ms = DEFAULT_TIMEOUT_MS,
        .port_filter = -1,
        .use_busy_wait = false
    };

    bool csv_output = false;
    bool check_only = false;
    bool show_info = false;

    // Long options
    static struct option long_options[] = {
        {"count",     required_argument, 0, 'n'},
        {"size",      required_argument, 0, 's'},
        {"delay",     required_argument, 0, 'd'},
        {"timeout",   required_argument, 0, 'T'},
        {"port",      required_argument, 0, 'p'},
        {"verbose",   no_argument,       0, 'v'},
        {"csv",       no_argument,       0, 'c'},
        {"busy-wait", no_argument,       0, 'b'},
        {"check",     no_argument,       0, 'C'},
        {"info",      no_argument,       0, 'I'},
        {"help",      no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    // Parse arguments
    int opt;
    while ((opt = getopt_long(argc, argv, "n:s:d:T:p:vcbCIh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'n':
                config.packet_count = atoi(optarg);
                if (config.packet_count < 1) {
                    fprintf(stderr, "Hata: Paket sayisi en az 1 olmali\n");
                    return 1;
                }
                break;

            case 's':
                config.packet_size = atoi(optarg);
                if (config.packet_size < MIN_PACKET_SIZE) {
                    fprintf(stderr, "Hata: Paket boyutu en az %d byte olmali\n", MIN_PACKET_SIZE);
                    return 1;
                }
                if (config.packet_size > MAX_PACKET_SIZE) {
                    fprintf(stderr, "Hata: Paket boyutu en fazla %d byte olmali\n", MAX_PACKET_SIZE);
                    return 1;
                }
                break;

            case 'd':
                config.delay_us = atoi(optarg);
                if (config.delay_us < 0) {
                    fprintf(stderr, "Hata: Bekleme suresi negatif olamaz\n");
                    return 1;
                }
                break;

            case 'T':
                config.timeout_ms = atoi(optarg);
                if (config.timeout_ms < 100) {
                    fprintf(stderr, "Hata: Timeout en az 100ms olmali\n");
                    return 1;
                }
                break;

            case 'p':
                config.port_filter = atoi(optarg);
                if (config.port_filter < 0 || config.port_filter > 7) {
                    fprintf(stderr, "Hata: Port ID 0-7 arasinda olmali\n");
                    return 1;
                }
                break;

            case 'v':
                g_debug_level++;
                if (g_debug_level > DEBUG_LEVEL_TRACE) {
                    g_debug_level = DEBUG_LEVEL_TRACE;
                }
                break;

            case 'c':
                csv_output = true;
                break;

            case 'b':
                config.use_busy_wait = true;
                break;

            case 'C':
                check_only = true;
                break;

            case 'I':
                show_info = true;
                break;

            case 'h':
                print_usage(argv[0]);
                return 0;

            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    // Check root
    if (geteuid() != 0) {
        fprintf(stderr, "Hata: Bu program root yetkisi gerektirir.\n");
        fprintf(stderr, "       sudo %s ...\n", argv[0]);
        return 1;
    }

    // Signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Show interface info
    if (show_info) {
        show_interface_info();
        return 0;
    }

    // Check interfaces
    LOG_INFO("Interface kontrolu yapiliyor...");
    int check_ret = check_all_interfaces();

    if (check_only) {
        if (check_ret == 0) {
            printf("Tum interface'ler HW timestamp destekliyor.\n");
            return 0;
        } else {
            printf("Bazi interface'ler HW timestamp desteklemiyor!\n");
            return 1;
        }
    }

    if (check_ret < 0) {
        LOG_WARN("Bazi interface'ler HW timestamp desteklemiyor, devam ediliyor...");
    }

    // Print config
    if (!csv_output) {
        printf("\n");
        printf("Mellanox HW Timestamp Latency Test\n");
        printf("==================================\n");
        printf("Paket sayisi (VLAN basina): %d\n", config.packet_count);
        printf("Paket boyutu: %d bytes\n", config.packet_size);
        printf("VLAN arasi bekleme: %d us\n", config.delay_us);
        printf("RX timeout: %d ms\n", config.timeout_ms);
        printf("Port filtresi: %s\n", config.port_filter < 0 ? "hepsi" : "belirtilen");
        printf("Bekleme modu: %s\n", config.use_busy_wait ? "busy-wait" : "sleep");
        printf("Debug seviyesi: %d\n", g_debug_level);
        printf("\n");
    }

    // Allocate results
    struct latency_result *results = calloc(MAX_RESULTS, sizeof(struct latency_result));
    if (!results) {
        LOG_ERROR("Sonuc dizisi icin bellek ayrilamadi");
        return 1;
    }

    int result_count = 0;

    // Run test
    LOG_INFO("Test baslatiliyor...");
    int ret = run_latency_test(&config, results, &result_count);

    if (g_interrupted) {
        LOG_WARN("Test kesildi");
    }

    if (ret < 0) {
        LOG_ERROR("Test basarisiz: %d", ret);
        free(results);
        return 1;
    }

    // Print results
    if (csv_output) {
        // Declare function from results.c
        extern void print_results_csv(const struct latency_result *results, int result_count);
        print_results_csv(results, result_count);
    } else {
        // Declare function from results.c
        extern void print_results_table(const struct latency_result *results, int result_count, int packet_count);
        print_results_table(results, result_count, config.packet_count);
    }

    // Cleanup
    free(results);

    LOG_INFO("Test tamamlandi");

    return 0;
}
