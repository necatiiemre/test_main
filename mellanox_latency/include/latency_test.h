/**
 * @file latency_test.h
 * @brief Mellanox HW Timestamp Latency Test - Test Logic API
 */

#ifndef LATENCY_TEST_H
#define LATENCY_TEST_H

#include "common.h"
#include "config.h"

// ============================================
// TOTAL RESULTS COUNT
// ============================================
#define MAX_RESULTS     (NUM_PORT_PAIRS * MAX_VLANS_PER_PAIR)  // 8 * 4 = 32

// ============================================
// FUNCTION DECLARATIONS
// ============================================

/**
 * Tüm interface'lerin HW timestamp desteğini kontrol et
 *
 * @return  0 = hepsi destekliyor, <0 = bazıları desteklemiyor
 */
int check_all_interfaces(void);

/**
 * Latency testini çalıştır
 *
 * @param config    Test konfigürasyonu
 * @param results   Sonuç dizisi (en az MAX_RESULTS eleman)
 * @param result_count  Çıktı: geçerli sonuç sayısı
 * @return          0 = başarılı, <0 = hata
 */
int run_latency_test(const struct test_config *config,
                     struct latency_result *results,
                     int *result_count);

/**
 * Tek bir port çifti için test çalıştır
 *
 * @param pair      Port çifti
 * @param config    Test konfigürasyonu
 * @param results   Sonuç dizisi (en az pair->vlan_count eleman)
 * @return          0 = başarılı, <0 = hata
 */
int run_port_pair_test(const struct port_pair *pair,
                       const struct test_config *config,
                       struct latency_result *results);

/**
 * Tek bir VLAN için test çalıştır
 *
 * @param pair      Port çifti
 * @param vlan_idx  VLAN indexi (0-3)
 * @param config    Test konfigürasyonu
 * @param result    Sonuç
 * @return          0 = başarılı, <0 = hata
 */
int run_vlan_test(const struct port_pair *pair,
                  int vlan_idx,
                  const struct test_config *config,
                  struct latency_result *result);

/**
 * Retry mekanizmalı latency testi
 * Eğer FAIL olan testler varsa, belirtilen sayıda tekrar dener
 *
 * @param config        Test konfigürasyonu
 * @param results       Sonuç dizisi (en az MAX_RESULTS eleman)
 * @param result_count  Çıktı: geçerli sonuç sayısı
 * @param attempt_out   Çıktı: kaçıncı denemede tamamlandı (1 = ilk deneme)
 * @return              0 = tüm testler PASS, >0 = FAIL sayısı, <0 = hata
 */
int run_latency_test_with_retry(const struct test_config *config,
                                struct latency_result *results,
                                int *result_count,
                                int *attempt_out);

/**
 * Sonuçlardaki FAIL sayısını hesapla
 *
 * @param results       Sonuç dizisi
 * @param result_count  Sonuç sayısı
 * @return              FAIL olan test sayısı
 */
int count_failed_results(const struct latency_result *results, int result_count);

#endif // LATENCY_TEST_H
