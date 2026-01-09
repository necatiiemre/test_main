/**
 * @file external_latency.h
 * @brief External Latency Test Results Reader API
 *
 * latency_test uygulamasından sonuçları okumak için API.
 *
 * Kullanım:
 *   1. latency_test -S çalıştır (sonuçları shared memory'ye yazar)
 *   2. DPDK'da external_latency_load() çağır
 *   3. external_latency_get_by_vlan() ile sonuçları oku
 *   4. external_latency_close() ile kapat
 */

#ifndef EXTERNAL_LATENCY_H
#define EXTERNAL_LATENCY_H

#include <stdint.h>
#include <stdbool.h>
#include "latency_results_shm.h"

/**
 * External latency sonuçlarını shared memory'den yükle
 *
 * @param timeout_ms  Bekleme süresi (ms), 0 = beklemeden dene
 * @return            true = başarılı, false = bulunamadı
 */
bool external_latency_load(int timeout_ms);

/**
 * Sonuçlar yüklü mü?
 */
bool external_latency_is_loaded(void);

/**
 * Test tamamlandı mı?
 */
bool external_latency_is_complete(void);

/**
 * Toplam sonuç sayısı
 */
int external_latency_get_count(void);

/**
 * Index'e göre sonuç al
 *
 * @param index   0-based index
 * @return        Sonuç pointer'ı veya NULL
 */
const struct shm_latency_result* external_latency_get(int index);

/**
 * VLAN ID'ye göre sonuç al
 *
 * @param vlan_id   VLAN ID (örn: 105, 106, ...)
 * @return          Sonuç pointer'ı veya NULL
 */
const struct shm_latency_result* external_latency_get_by_vlan(uint16_t vlan_id);

/**
 * Port ve VLAN'a göre sonuç al
 *
 * @param tx_port   TX port ID
 * @param rx_port   RX port ID
 * @param vlan_id   VLAN ID
 * @return          Sonuç pointer'ı veya NULL
 */
const struct shm_latency_result* external_latency_get_by_port(
    uint16_t tx_port, uint16_t rx_port, uint16_t vlan_id);

/**
 * VLAN için latency değerlerini al (microseconds)
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
                                  double *max_us);

/**
 * VLAN için PASS/FAIL durumu
 *
 * @param vlan_id   VLAN ID
 * @return          true = PASS, false = FAIL veya bulunamadı
 */
bool external_latency_passed(uint16_t vlan_id);

/**
 * Özet istatistikleri al
 */
void external_latency_get_summary(uint32_t *total_passed,
                                   uint32_t *total_failed,
                                   double *overall_min_us,
                                   double *overall_avg_us,
                                   double *overall_max_us);

/**
 * Tüm sonuçları ekrana yazdır
 */
void external_latency_print(void);

/**
 * Kaynakları serbest bırak
 */
void external_latency_close(void);

#endif // EXTERNAL_LATENCY_H
