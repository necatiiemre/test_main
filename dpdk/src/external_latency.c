/**
 * @file external_latency.c
 * @brief External Latency Test Results Reader
 *
 * latency_test uygulamasından shared memory üzerinden
 * sonuçları okur ve DPDK'ya sunar.
 */

#include <stdio.h>
#include <stdbool.h>
#include "latency_results_shm.h"

// Global pointer to shared memory
static struct latency_shm_header *g_ext_latency_shm = NULL;

/**
 * External latency sonuçlarını shared memory'den oku
 * latency_test -S ile çalıştırılmış olmalı
 *
 * @param timeout_ms  Bekleme süresi (ms), 0 = beklemeden dene
 * @return true = başarılı, false = bulunamadı
 */
bool external_latency_load(int timeout_ms) {
    if (g_ext_latency_shm != NULL) {
        printf("[EXT_LATENCY] Already loaded, closing previous...\n");
        latency_shm_close_reader(g_ext_latency_shm);
        g_ext_latency_shm = NULL;
    }

    printf("[EXT_LATENCY] Loading external latency results (timeout=%d ms)...\n", timeout_ms);

    g_ext_latency_shm = latency_shm_open(timeout_ms);
    if (!g_ext_latency_shm) {
        printf("[EXT_LATENCY] Failed to load - run 'latency_test -S' first\n");
        return false;
    }

    if (!latency_shm_is_complete(g_ext_latency_shm)) {
        printf("[EXT_LATENCY] Warning: Test not yet complete\n");
    }

    printf("[EXT_LATENCY] Loaded %u results\n", g_ext_latency_shm->result_count);
    return true;
}

/**
 * Yüklü mü kontrol et
 */
bool external_latency_is_loaded(void) {
    return g_ext_latency_shm != NULL;
}

/**
 * Test tamamlandı mı
 */
bool external_latency_is_complete(void) {
    if (!g_ext_latency_shm) return false;
    return latency_shm_is_complete(g_ext_latency_shm);
}

/**
 * Sonuç sayısını al
 */
int external_latency_get_count(void) {
    if (!g_ext_latency_shm) return 0;
    return (int)g_ext_latency_shm->result_count;
}

/**
 * Index'e göre sonuç al
 */
const struct shm_latency_result* external_latency_get(int index) {
    if (!g_ext_latency_shm) return NULL;
    return latency_shm_get_result(g_ext_latency_shm, index);
}

/**
 * VLAN ID'ye göre sonuç al
 */
const struct shm_latency_result* external_latency_get_by_vlan(uint16_t vlan_id) {
    if (!g_ext_latency_shm) return NULL;
    return latency_shm_get_result_by_vlan(g_ext_latency_shm, vlan_id);
}

/**
 * Port ve VLAN'a göre sonuç al
 */
const struct shm_latency_result* external_latency_get_by_port(
    uint16_t tx_port, uint16_t rx_port, uint16_t vlan_id) {
    if (!g_ext_latency_shm) return NULL;
    return latency_shm_get_result_by_port(g_ext_latency_shm, tx_port, rx_port, vlan_id);
}

/**
 * Belirli bir VLAN için latency değerlerini al (microseconds)
 *
 * @param vlan_id   VLAN ID
 * @param min_us    Output: minimum latency (us)
 * @param avg_us    Output: average latency (us)
 * @param max_us    Output: maximum latency (us)
 * @return          true = bulundu, false = bulunamadı
 */
bool external_latency_get_values(uint16_t vlan_id,
                                  double *min_us,
                                  double *avg_us,
                                  double *max_us) {
    const struct shm_latency_result *r = external_latency_get_by_vlan(vlan_id);
    if (!r || r->rx_count == 0) {
        return false;
    }

    *min_us = latency_shm_ns_to_us(r->min_latency_ns);
    *avg_us = latency_shm_ns_to_us(r->total_latency_ns / r->rx_count);
    *max_us = latency_shm_ns_to_us(r->max_latency_ns);
    return true;
}

/**
 * VLAN için PASS/FAIL durumunu al
 */
bool external_latency_passed(uint16_t vlan_id) {
    const struct shm_latency_result *r = external_latency_get_by_vlan(vlan_id);
    if (!r) return false;
    return r->passed;
}

/**
 * Özet istatistikleri al
 */
void external_latency_get_summary(uint32_t *total_passed,
                                   uint32_t *total_failed,
                                   double *overall_min_us,
                                   double *overall_avg_us,
                                   double *overall_max_us) {
    if (!g_ext_latency_shm) {
        *total_passed = 0;
        *total_failed = 0;
        *overall_min_us = 0;
        *overall_avg_us = 0;
        *overall_max_us = 0;
        return;
    }

    *total_passed = g_ext_latency_shm->total_passed;
    *total_failed = g_ext_latency_shm->total_failed;
    *overall_min_us = latency_shm_ns_to_us(g_ext_latency_shm->overall_min_ns);
    *overall_avg_us = latency_shm_ns_to_us(g_ext_latency_shm->overall_avg_ns);
    *overall_max_us = latency_shm_ns_to_us(g_ext_latency_shm->overall_max_ns);
}

/**
 * Tüm sonuçları ekrana yazdır
 */
void external_latency_print(void) {
    if (!g_ext_latency_shm) {
        printf("[EXT_LATENCY] Not loaded\n");
        return;
    }
    latency_shm_print_results(g_ext_latency_shm);
}

/**
 * Kaynakları serbest bırak
 */
void external_latency_close(void) {
    if (g_ext_latency_shm) {
        latency_shm_close_reader(g_ext_latency_shm);
        g_ext_latency_shm = NULL;
        printf("[EXT_LATENCY] Closed\n");
    }
}
