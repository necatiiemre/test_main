#ifndef RAW_SOCKET_PORT_H
#define RAW_SOCKET_PORT_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <linux/if_packet.h>
#include "config.h"

// ==========================================
// RAW SOCKET PORT - MULTI-TARGET TX/RX
// ==========================================
// DPDK desteklemeyen NIC'ler için raw socket + zero copy implementasyonu.
// Multi-target: Tek port birden fazla hedefe farklı hızlarda gönderebilir.
//
// Port 12 (1G): 5 hedefe gönderim
//   - Port 13: 80 Mbps, VL-ID 4099-4226
//   - Port 5:  220 Mbps, VL-ID 4227-4738
//   - Port 4:  220 Mbps, VL-ID 4739-5250
//   - Port 7:  220 Mbps, VL-ID 5251-5762
//   - Port 6:  220 Mbps, VL-ID 5763-6274
//
// Port 13 (100M): 1 hedefe gönderim
//   - Port 12: 80 Mbps, VL-ID 6275-6306

// PACKET_MMAP ring buffer configuration for zero-copy
// Increased for higher throughput (865 Mbps = ~72K pkt/sec)
#define RAW_SOCKET_RING_BLOCK_SIZE  (4096 * 8)   // 32KB per block
#define RAW_SOCKET_RING_BLOCK_NR    256          // 256 blocks = 8MB total
#define RAW_SOCKET_RING_FRAME_SIZE  2048         // Max frame size
#define RAW_SOCKET_RING_FRAME_NR    ((RAW_SOCKET_RING_BLOCK_SIZE / RAW_SOCKET_RING_FRAME_SIZE) * RAW_SOCKET_RING_BLOCK_NR)

// ==========================================
// MULTI-QUEUE RX CONFIGURATION
// ==========================================
// Port 12 receives ~800 Mbps from DPDK ports 2,3,4,5 -> 4 queues
// Port 13 receives ~90 Mbps from DPDK ports 0,6 -> 2 queues
#define RAW_SOCKET_RX_QUEUE_COUNT   4            // Max RX queues (for array sizing)
#define PORT_12_RX_QUEUE_COUNT      4            // Port 12: 4 queues for 1G
#define PORT_13_RX_QUEUE_COUNT      2            // Port 13: 2 queues for 100M
#define RAW_SOCKET_FANOUT_GROUP_ID  0xCAFE       // Unique fanout group ID

// Packet sizes (no VLAN header)
#define RAW_PKT_ETH_HDR_SIZE   14
#define RAW_PKT_IP_HDR_SIZE    20
#define RAW_PKT_UDP_HDR_SIZE   8
#define RAW_PKT_PAYLOAD_SIZE   1467   // 8 seq + 1459 prbs (max size)
#define RAW_PKT_TOTAL_SIZE     (RAW_PKT_ETH_HDR_SIZE + RAW_PKT_IP_HDR_SIZE + \
                                RAW_PKT_UDP_HDR_SIZE + RAW_PKT_PAYLOAD_SIZE)

// PRBS data size in payload
#define RAW_PKT_SEQ_BYTES      8
#define RAW_PKT_PRBS_BYTES     (RAW_PKT_PAYLOAD_SIZE - RAW_PKT_SEQ_BYTES)

// ==========================================
// RAW SOCKET IMIX SUPPORT
// ==========================================
// Raw socket IMIX: VLAN yok, ETH(14) + IP(20) + UDP(8) = 42 byte header
// Her boyut DPDK boyutundan 4 byte küçük (VLAN header yok)
// Minimum: 75 - 4 = 71 byte raw socket packet
// Maximum: 1518 - 4 = 1514 byte raw socket packet

// x2 katsayılı boyutlar (her biri 2 kez) - DPDK boyutu - 4
#define RAW_IMIX_SIZE_01    71     // 75 - 4
#define RAW_IMIX_SIZE_02    111    // 115 - 4
#define RAW_IMIX_SIZE_03    231    // 235 - 4
#define RAW_IMIX_SIZE_04    791    // 795 - 4
#define RAW_IMIX_SIZE_05    881    // 885 - 4

// x1 katsayılı boyutlar (her biri 1 kez) - DPDK boyutu - 4
#define RAW_IMIX_SIZE_06    1081   // 1085 - 4
#define RAW_IMIX_SIZE_07    1191   // 1195 - 4
#define RAW_IMIX_SIZE_08    1281   // 1285 - 4
#define RAW_IMIX_SIZE_09    1391   // 1395 - 4
#define RAW_IMIX_SIZE_10    1481   // 1485 - 4
#define RAW_IMIX_SIZE_11    1514   // 1518 - 4 (max without VLAN)

// Raw IMIX ortalama paket boyutu (DPDK ortalama - 4)
// (12173 - 16*4) / 16 = 12109 / 16 ≈ 757
#define RAW_IMIX_AVG_PACKET_SIZE 757

// PRBS offset hesabı için maksimum boyut (VLAN'lı DPDK ile uyumlu)
#define RAW_MAX_PRBS_BYTES RAW_PKT_PRBS_BYTES  // 1459

// Raw IMIX pattern (16 paketlik - VLAN'sız boyutlar)
#define RAW_IMIX_PATTERN_INIT { \
    /* x2 boyutlar (10 paket) */ \
    RAW_IMIX_SIZE_01, RAW_IMIX_SIZE_01, \
    RAW_IMIX_SIZE_02, RAW_IMIX_SIZE_02, \
    RAW_IMIX_SIZE_03, RAW_IMIX_SIZE_03, \
    RAW_IMIX_SIZE_04, RAW_IMIX_SIZE_04, \
    RAW_IMIX_SIZE_05, RAW_IMIX_SIZE_05, \
    /* x1 boyutlar (6 paket) */ \
    RAW_IMIX_SIZE_06, RAW_IMIX_SIZE_07, RAW_IMIX_SIZE_08, RAW_IMIX_SIZE_09, RAW_IMIX_SIZE_10, RAW_IMIX_SIZE_11 \
}

// Maximum VL-ID for array sizing
#define MAX_TOTAL_VL_IDS       4096

// ==========================================
// RATE LIMITER
// ==========================================

struct raw_rate_limiter {
    uint64_t tokens;
    uint64_t max_tokens;
    uint64_t tokens_per_sec;
    uint64_t last_update_ns;

    // Smooth pacing fields (timestamp-based like DPDK)
    uint64_t delay_ns;           // Inter-packet delay in nanoseconds
    uint64_t next_send_time_ns;  // Next packet send time
    bool smooth_pacing_enabled;  // Use smooth pacing instead of token bucket
};

// ==========================================
// PER-TARGET STATISTICS
// ==========================================

struct raw_target_stats {
    uint64_t tx_packets;
    uint64_t tx_bytes;
    uint64_t tx_errors;
    uint64_t rx_packets;
    uint64_t rx_bytes;
    uint64_t good_pkts;
    uint64_t bad_pkts;
    uint64_t bit_errors;
    uint64_t lost_pkts;
    uint64_t out_of_order_pkts;
    uint64_t duplicate_pkts;
    pthread_spinlock_t lock;
};

// ==========================================
// VL-ID SEQUENCE TRACKER
// ==========================================

struct raw_vl_sequence {
    uint64_t tx_sequence;       // TX sequence counter
    uint64_t rx_expected_seq;   // Expected RX sequence
    bool rx_initialized;        // RX tracker initialized?
    pthread_spinlock_t tx_lock;
    pthread_spinlock_t rx_lock;
};

// ==========================================
// TX TARGET STATE (per target)
// ==========================================

struct raw_tx_target_state {
    struct raw_tx_target_config config;      // Target configuration
    struct raw_rate_limiter limiter;         // Rate limiter for this target
    struct raw_vl_sequence *vl_sequences;    // VL-ID sequence trackers
    uint16_t current_vl_offset;              // Round-robin offset
    struct raw_target_stats stats;           // Per-target statistics
};

// ==========================================
// RX SOURCE STATE (per source)
// ==========================================

struct raw_rx_source_state {
    struct raw_rx_source_config config;      // Source configuration
    struct raw_vl_sequence *vl_sequences;    // VL-ID sequence trackers
    struct raw_target_stats stats;           // Per-source statistics
};

// ==========================================
// MULTI-QUEUE RX STATE (per queue)
// ==========================================

struct raw_rx_queue {
    int socket_fd;                          // Socket file descriptor
    void *ring;                             // PACKET_MMAP ring buffer
    size_t ring_size;                       // Ring buffer size
    uint32_t ring_offset;                   // Current ring offset
    pthread_t thread;                       // RX thread
    uint16_t queue_id;                      // Queue index (0-3)
    uint16_t cpu_core;                      // Pinned CPU core
    volatile bool *stop_flag;               // Pointer to stop flag
    bool running;                           // Thread running state

    // Per-queue statistics
    uint64_t rx_packets;
    uint64_t rx_bytes;
    uint64_t good_pkts;
    uint64_t bad_pkts;
    uint64_t bit_errors;
    uint64_t lost_pkts;
    uint64_t kernel_drops;                  // Kernel-reported packet drops

    // VL-ID tracking for debugging hash distribution
    uint16_t vl_id_min;                     // Minimum VL-ID seen
    uint16_t vl_id_max;                     // Maximum VL-ID seen
    uint32_t unique_vl_ids;                 // Count of unique VL-IDs
};

// ==========================================
// RAW SOCKET PORT (main structure)
// ==========================================

struct raw_socket_port {
    int raw_index;                          // Raw port array index (0 or 1)
    uint16_t port_id;                       // Global port ID (12 or 13)
    int tx_socket;                          // TX socket fd
    int rx_socket;                          // Legacy single RX socket fd (for Port 13)
    int if_index;                           // Interface index

    // Configuration
    struct raw_socket_port_config config;

    // Zero-copy ring buffers (TX)
    void *tx_ring;
    size_t tx_ring_size;
    uint32_t tx_ring_offset;

    // Legacy single RX ring (for Port 13)
    void *rx_ring;
    size_t rx_ring_size;
    uint32_t rx_ring_offset;

    // Multi-queue RX (for Port 12 with PACKET_FANOUT)
    bool use_multi_queue_rx;                // Enable multi-queue RX
    int rx_queue_count;                     // Number of active RX queues
    struct raw_rx_queue rx_queues[RAW_SOCKET_RX_QUEUE_COUNT];
    uint16_t rx_cpu_cores[RAW_SOCKET_RX_QUEUE_COUNT];  // Allocated CPU cores

    // Multi-target TX state
    uint16_t tx_target_count;
    struct raw_tx_target_state tx_targets[MAX_RAW_TARGETS];

    // Multi-source RX state
    uint16_t rx_source_count;
    struct raw_rx_source_state rx_sources[MAX_RAW_TARGETS];

    // DPDK External TX packets received (aggregated from all queues)
    struct raw_target_stats dpdk_ext_rx_stats;

    // PRBS cache
    uint8_t *prbs_cache;
    uint8_t *prbs_cache_ext;
    bool prbs_initialized;

    // Thread control
    pthread_t tx_thread;
    pthread_t rx_thread;                    // Legacy single RX thread (Port 13)
    volatile bool stop_flag;
    bool tx_running;
    bool rx_running;

    // Hardware MAC address
    uint8_t mac_addr[6];
};

// Global raw socket ports array
extern struct raw_socket_port raw_ports[MAX_RAW_SOCKET_PORTS];
extern struct raw_socket_port_config raw_port_configs[MAX_RAW_SOCKET_PORTS];

// ==========================================
// INITIALIZATION FUNCTIONS
// ==========================================

int init_raw_socket_ports(void);
int init_raw_socket_port(int raw_index, const struct raw_socket_port_config *config);
int setup_raw_tx_ring(struct raw_socket_port *port);
int setup_raw_rx_ring(struct raw_socket_port *port);
int init_raw_prbs_cache(struct raw_socket_port *port);

// ==========================================
// MULTI-QUEUE RX FUNCTIONS
// ==========================================

int setup_multi_queue_rx(struct raw_socket_port *port);
void *multi_queue_rx_worker(void *arg);
int start_multi_queue_rx_workers(struct raw_socket_port *port, volatile bool *stop_flag);
void stop_multi_queue_rx_workers(struct raw_socket_port *port);

// ==========================================
// TX/RX WORKER FUNCTIONS
// ==========================================

int start_raw_socket_workers(volatile bool *stop_flag);
void *raw_tx_worker(void *arg);
void *raw_rx_worker(void *arg);
void stop_raw_socket_workers(void);

// ==========================================
// PACKET BUILDING FUNCTIONS
// ==========================================

int build_raw_packet(uint8_t *buffer, const uint8_t *src_mac,
                     uint16_t vl_id, uint64_t sequence, const uint8_t *prbs_data);

// ==========================================
// RATE LIMITER FUNCTIONS
// ==========================================

void init_raw_rate_limiter(struct raw_rate_limiter *limiter, uint32_t rate_mbps);
void init_raw_rate_limiter_smooth(struct raw_rate_limiter *limiter, uint32_t rate_mbps,
                                   uint16_t target_id, uint16_t total_targets);
bool raw_consume_tokens(struct raw_rate_limiter *limiter, uint64_t bytes);
bool raw_check_smooth_pacing(struct raw_rate_limiter *limiter);

// ==========================================
// STATISTICS & UTILITY FUNCTIONS
// ==========================================

void print_raw_socket_stats(void);
void reset_raw_socket_stats(void);
void cleanup_raw_socket_ports(void);
uint64_t get_time_ns(void);

#endif /* RAW_SOCKET_PORT_H */
