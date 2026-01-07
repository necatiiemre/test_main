/**
 * Wire Latency Test - Kernel SO_TIMESTAMPING Version
 *
 * Uses hardware timestamps from Mellanox ConnectX-6 NICs
 * to measure true wire-to-wire latency.
 *
 * Must run BEFORE DPDK takes over the interfaces!
 *
 * Compile: gcc -o wire_latency_test wire_latency_test.c -lpthread -lm
 * Run: sudo ./wire_latency_test
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <poll.h>

#include <stdarg.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/net_tstamp.h>
#include <linux/sockios.h>
#include <linux/errqueue.h>
#include <net/if.h>

// ============================================
// CONFIGURATION
// ============================================

#define NUM_PORTS       8
#define VLANS_PER_PORT  4
#define PACKETS_PER_VLAN 1

// Log file configuration
#define LOG_DIR         "/home/user/test_main/logs"
#define LOG_PREFIX      "wire_latency"
static FILE *g_log_file = NULL;
static char g_log_filename[256];

// ============================================
// LOG FILE FUNCTIONS
// ============================================

static void open_log_file(void) {
    // Create log directory if it doesn't exist
    char mkdir_cmd[300];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", LOG_DIR);
    if (system(mkdir_cmd) != 0) {
        // Ignore errors - directory may already exist
    }

    // Generate filename with timestamp
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", tm_info);

    snprintf(g_log_filename, sizeof(g_log_filename), "%s/%s_%s.log",
             LOG_DIR, LOG_PREFIX, timestamp);

    g_log_file = fopen(g_log_filename, "w");
    if (g_log_file) {
        printf("Log file: %s\n", g_log_filename);
        // Write header
        fprintf(g_log_file, "Wire Latency Test Log\n");
        fprintf(g_log_file, "Started: %s", ctime(&now));
        fprintf(g_log_file, "=========================================\n\n");
        fflush(g_log_file);
    } else {
        fprintf(stderr, "Warning: Could not create log file: %s\n", g_log_filename);
    }
}

static void close_log_file(void) {
    if (g_log_file) {
        time_t now = time(NULL);
        fprintf(g_log_file, "\n=========================================\n");
        fprintf(g_log_file, "Finished: %s", ctime(&now));
        fclose(g_log_file);
        g_log_file = NULL;
        printf("Log saved: %s\n", g_log_filename);
    }
}

// Printf to both stdout and log file
static void log_printf(const char *format, ...) {
    va_list args;

    // Print to stdout
    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    // Print to log file
    if (g_log_file) {
        va_start(args, format);
        vfprintf(g_log_file, format, args);
        va_end(args);
        fflush(g_log_file);
    }
}

// Interface names - Mellanox ConnectX-6 interfaces
// Use: ip link show | grep "enp\|eth\|mlx"
static const char *interface_names[NUM_PORTS] = {
    "ens2f0np0",   // Port 0
    "ens2f1np1",   // Port 1
    "ens1f0np0",   // Port 2
    "ens1f1np1",   // Port 3
    "ens3f0np0",   // Port 4
    "ens3f1np1",   // Port 5
    "ens5f0np0",   // Port 6
    "ens5f1np1",   // Port 7
};

// Port pairing: TX port -> RX port (cross-NIC pairs via switch)
// Port 0 <-> Port 7
// Port 1 <-> Port 6
// Port 2 <-> Port 5
// Port 3 <-> Port 4
static const int port_pairs[NUM_PORTS] = {
    7,  // Port 0 -> Port 7
    6,  // Port 1 -> Port 6
    5,  // Port 2 -> Port 5
    4,  // Port 3 -> Port 4
    3,  // Port 4 -> Port 3
    2,  // Port 5 -> Port 2
    1,  // Port 6 -> Port 1
    0,  // Port 7 -> Port 0
};

// VLAN IDs per port (from config.h)
static const uint16_t vlan_ids[NUM_PORTS][VLANS_PER_PORT] = {
    {105, 106, 107, 108},  // Port 0
    {109, 110, 111, 112},  // Port 1
    {97,  98,  99,  100},  // Port 2
    {101, 102, 103, 104},  // Port 3
    {113, 114, 115, 116},  // Port 4
    {117, 118, 119, 120},  // Port 5
    {121, 122, 123, 124},  // Port 6
    {125, 126, 127, 128},  // Port 7
};

// VL-ID calculation from VLAN:
// VL-ID = (VLAN - 97) * 128 + 3
// Examples:
//   VLAN 97  -> VL-ID 3
//   VLAN 98  -> VL-ID 131
//   VLAN 99  -> VL-ID 259
//   VLAN 100 -> VL-ID 387
//   VLAN 101 -> VL-ID 515
//   VLAN 105 -> VL-ID 1027
//   VLAN 109 -> VL-ID 1539
static uint16_t vlan_to_vl_id(uint16_t vlan_id) {
    return (uint16_t)((vlan_id - 97) * 128 + 3);
}

// Get VL-ID for a specific port and vlan index
static uint16_t get_vl_id(int port_id, int vlan_index) {
    if (port_id < 0 || port_id >= NUM_PORTS) return 0;
    if (vlan_index < 0 || vlan_index >= VLANS_PER_PORT) return 0;
    return vlan_to_vl_id(vlan_ids[port_id][vlan_index]);
}

#define TIMEOUT_SEC     5

// ============================================
// DATA STRUCTURES
// ============================================

struct latency_result {
    int tx_port;
    int rx_port;
    uint16_t vlan_id;
    uint16_t vl_id;
    uint64_t tx_hw_ts;      // Hardware TX timestamp (ns)
    uint64_t rx_hw_ts;      // Hardware RX timestamp (ns)
    uint64_t tx_sw_ts;      // Software TX timestamp (ns) - fallback
    uint64_t rx_sw_ts;      // Software RX timestamp (ns) - fallback
    int64_t latency_ns;     // Wire latency (ns)
    bool hw_ts_valid;       // True if hardware timestamp available
    bool valid;
};

static struct latency_result results[NUM_PORTS][VLANS_PER_PORT];
static int sockets[NUM_PORTS] = {-1};
static volatile bool g_running = true;
static bool g_hw_ts_available = false;  // Set after first successful HW timestamp

// ============================================
// PACKET STRUCTURE (DPDK compatible - NO VLAN tag)
// ============================================

// Packet sizes (matching DPDK raw_socket_port.c)
#define RAW_PKT_ETH_HDR_SIZE  14
#define RAW_PKT_IP_HDR_SIZE   20
#define RAW_PKT_UDP_HDR_SIZE  8
#define RAW_PKT_PAYLOAD_SIZE  100
#define RAW_PKT_TOTAL_SIZE    (RAW_PKT_ETH_HDR_SIZE + RAW_PKT_IP_HDR_SIZE + RAW_PKT_UDP_HDR_SIZE + RAW_PKT_PAYLOAD_SIZE)

// IP checksum calculation
static uint16_t calculate_ip_checksum(uint8_t *ip_header) {
    uint32_t sum = 0;
    for (int i = 0; i < RAW_PKT_IP_HDR_SIZE; i += 2) {
        sum += ((uint16_t)ip_header[i] << 8) | ip_header[i + 1];
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return ~sum;
}

// ============================================
// SOCKET SETUP WITH SO_TIMESTAMPING
// ============================================

static int setup_socket(int port_id) {
    const char *ifname = interface_names[port_id];

    // Create raw socket
    int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    // Get interface index
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);

    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        perror("SIOCGIFINDEX");
        close(sock);
        return -1;
    }
    int ifindex = ifr.ifr_ifindex;

    // Bind to interface
    struct sockaddr_ll sll;
    memset(&sll, 0, sizeof(sll));
    sll.sll_family = AF_PACKET;
    sll.sll_ifindex = ifindex;
    sll.sll_protocol = htons(ETH_P_ALL);

    if (bind(sock, (struct sockaddr *)&sll, sizeof(sll)) < 0) {
        perror("bind");
        close(sock);
        return -1;
    }

    // Enable hardware timestamping
    struct hwtstamp_config hwconfig;
    memset(&hwconfig, 0, sizeof(hwconfig));
    hwconfig.tx_type = HWTSTAMP_TX_ON;
    hwconfig.rx_filter = HWTSTAMP_FILTER_ALL;

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    ifr.ifr_data = (void *)&hwconfig;

    if (ioctl(sock, SIOCSHWTSTAMP, &ifr) < 0) {
        fprintf(stderr, "Warning: SIOCSHWTSTAMP failed for %s: %s\n",
                ifname, strerror(errno));
        fprintf(stderr, "  Hardware timestamping may not work!\n");
    }

    // Enable SO_TIMESTAMPING
    int flags = SOF_TIMESTAMPING_TX_HARDWARE |
                SOF_TIMESTAMPING_RX_HARDWARE |
                SOF_TIMESTAMPING_RAW_HARDWARE |
                SOF_TIMESTAMPING_OPT_CMSG;

    if (setsockopt(sock, SOL_SOCKET, SO_TIMESTAMPING, &flags, sizeof(flags)) < 0) {
        perror("SO_TIMESTAMPING");
        close(sock);
        return -1;
    }

    // Enable receiving TX timestamps from error queue
    int val = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_SELECT_ERR_QUEUE, &val, sizeof(val)) < 0) {
        // Not critical, continue
    }

    log_printf("  Port %d (%s): Socket ready, HW timestamping enabled\n", port_id, ifname);
    return sock;
}

// ============================================
// GET HARDWARE TIMESTAMP FROM CMSG
// ============================================

static uint64_t get_hw_timestamp(struct msghdr *msg) {
    struct cmsghdr *cmsg;

    for (cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET &&
            cmsg->cmsg_type == SO_TIMESTAMPING) {
            struct timespec *ts = (struct timespec *)CMSG_DATA(cmsg);
            // ts[0] = software, ts[1] = hw transformed, ts[2] = hw raw
            // We want ts[2] for raw hardware timestamp
            return (uint64_t)ts[2].tv_sec * 1000000000ULL + ts[2].tv_nsec;
        }
    }
    return 0;
}

// Get software timestamp (fallback when HW timestamp not available)
static uint64_t get_sw_timestamp(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

// ============================================
// GET TX TIMESTAMP FROM ERROR QUEUE
// ============================================

static uint64_t get_tx_timestamp(int sock) {
    char control[1024];
    char data[64];
    struct iovec iov = { .iov_base = data, .iov_len = sizeof(data) };
    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = control,
        .msg_controllen = sizeof(control),
    };

    // Poll for TX timestamp on error queue
    struct pollfd pfd = { .fd = sock, .events = POLLERR };
    int ret = poll(&pfd, 1, 100);  // 100ms timeout
    if (ret <= 0) {
        return 0;
    }

    // Read from error queue
    ret = recvmsg(sock, &msg, MSG_ERRQUEUE);
    if (ret < 0) {
        return 0;
    }

    return get_hw_timestamp(&msg);
}

// ============================================
// BUILD AND SEND TEST PACKET
// ============================================

static int send_test_packet(int port_id, int vlan_idx) {
    uint16_t vlan_id = vlan_ids[port_id][vlan_idx];
    uint16_t vl_id = get_vl_id(port_id, vlan_idx);
    int rx_port = port_pairs[port_id];
    (void)vlan_id;  // Not used in untagged packets

    uint8_t packet[RAW_PKT_TOTAL_SIZE];
    memset(packet, 0, sizeof(packet));

    // ==========================================
    // ETHERNET HEADER (14 bytes) - NO VLAN TAG
    // Same format as DPDK raw_socket_port.c
    // ==========================================

    // Destination MAC: 03:00:00:00:VV:VV (multicast with VL-ID)
    packet[0] = 0x03;
    packet[1] = 0x00;
    packet[2] = 0x00;
    packet[3] = 0x00;
    packet[4] = (vl_id >> 8) & 0xFF;
    packet[5] = vl_id & 0xFF;

    // Source MAC: 02:00:00:00:00:20 (fixed, same as DPDK)
    packet[6] = 0x02;
    packet[7] = 0x00;
    packet[8] = 0x00;
    packet[9] = 0x00;
    packet[10] = 0x00;
    packet[11] = 0x20;

    // EtherType: 0x0800 (IPv4) - NO VLAN
    packet[12] = 0x08;
    packet[13] = 0x00;

    // ==========================================
    // IPv4 HEADER (20 bytes)
    // ==========================================
    uint8_t *ip = packet + RAW_PKT_ETH_HDR_SIZE;
    ip[0] = 0x45;  // Version + IHL
    ip[1] = 0x00;  // DSCP + ECN
    uint16_t ip_total_len = RAW_PKT_IP_HDR_SIZE + RAW_PKT_UDP_HDR_SIZE + RAW_PKT_PAYLOAD_SIZE;
    ip[2] = (ip_total_len >> 8) & 0xFF;
    ip[3] = ip_total_len & 0xFF;
    ip[4] = 0x00;  // ID
    ip[5] = 0x00;
    ip[6] = 0x40;  // Flags (DF)
    ip[7] = 0x00;  // Fragment offset
    ip[8] = 0x01;  // TTL
    ip[9] = 0x11;  // Protocol (UDP)
    ip[10] = 0x00; // Checksum (will be calculated)
    ip[11] = 0x00;
    // Source IP: 10.0.0.0
    ip[12] = 10;
    ip[13] = 0;
    ip[14] = 0;
    ip[15] = 0;
    // Destination IP: 224.224.VV.VV (multicast)
    ip[16] = 224;
    ip[17] = 224;
    ip[18] = (vl_id >> 8) & 0xFF;
    ip[19] = vl_id & 0xFF;

    // Calculate IP checksum
    uint16_t ip_checksum = calculate_ip_checksum(ip);
    ip[10] = (ip_checksum >> 8) & 0xFF;
    ip[11] = ip_checksum & 0xFF;

    // ==========================================
    // UDP HEADER (8 bytes)
    // ==========================================
    uint8_t *udp = ip + RAW_PKT_IP_HDR_SIZE;
    udp[0] = 0x00;  // Source port (100)
    udp[1] = 0x64;
    udp[2] = 0x00;  // Destination port (100)
    udp[3] = 0x64;
    uint16_t udp_len = RAW_PKT_UDP_HDR_SIZE + RAW_PKT_PAYLOAD_SIZE;
    udp[4] = (udp_len >> 8) & 0xFF;
    udp[5] = udp_len & 0xFF;
    udp[6] = 0x00;  // Checksum (optional for IPv4)
    udp[7] = 0x00;

    // ==========================================
    // PAYLOAD (sequence number)
    // ==========================================
    uint8_t *payload = udp + RAW_PKT_UDP_HDR_SIZE;
    uint64_t sequence = 0;
    memcpy(payload, &sequence, sizeof(sequence));

    // Send packet
    struct sockaddr_ll sll;
    memset(&sll, 0, sizeof(sll));
    sll.sll_family = AF_PACKET;
    sll.sll_ifindex = 0;
    sll.sll_halen = ETH_ALEN;
    memcpy(sll.sll_addr, packet, ETH_ALEN);  // Copy dest MAC

    // Get interface index
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, interface_names[port_id], IFNAMSIZ - 1);
    if (ioctl(sockets[port_id], SIOCGIFINDEX, &ifr) == 0) {
        sll.sll_ifindex = ifr.ifr_ifindex;
    }

    // Get software timestamp BEFORE send (for fallback)
    uint64_t tx_sw_ts = get_sw_timestamp();

    ssize_t sent = sendto(sockets[port_id], packet, RAW_PKT_TOTAL_SIZE, 0,
                          (struct sockaddr *)&sll, sizeof(sll));

    if (sent < 0) {
        perror("sendto");
        return -1;
    }

    // Try to get TX hardware timestamp
    uint64_t tx_hw_ts = get_tx_timestamp(sockets[port_id]);

    // Store result
    results[port_id][vlan_idx].tx_port = port_id;
    results[port_id][vlan_idx].rx_port = rx_port;
    results[port_id][vlan_idx].vlan_id = vlan_id;
    results[port_id][vlan_idx].vl_id = vl_id;
    results[port_id][vlan_idx].tx_hw_ts = tx_hw_ts;
    results[port_id][vlan_idx].tx_sw_ts = tx_sw_ts;
    results[port_id][vlan_idx].hw_ts_valid = (tx_hw_ts > 0);
    results[port_id][vlan_idx].valid = false;

    if (tx_hw_ts > 0) {
        g_hw_ts_available = true;  // HW timestamp works
    }

    return 0;
}

// ============================================
// RECEIVE AND MATCH PACKETS
// ============================================

static void *rx_thread(void *arg) {
    int port_id = *(int *)arg;
    int sock = sockets[port_id];

    uint8_t buffer[2048];
    char control[1024];
    struct iovec iov = { .iov_base = buffer, .iov_len = sizeof(buffer) };

    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    while (g_running) {
        // Check timeout
        clock_gettime(CLOCK_MONOTONIC, &now);
        if ((now.tv_sec - start.tv_sec) > TIMEOUT_SEC) {
            break;
        }

        struct msghdr msg = {
            .msg_iov = &iov,
            .msg_iovlen = 1,
            .msg_control = control,
            .msg_controllen = sizeof(control),
        };

        struct pollfd pfd = { .fd = sock, .events = POLLIN };
        int ret = poll(&pfd, 1, 100);
        if (ret <= 0) continue;

        // Get software timestamp immediately
        uint64_t rx_sw_ts = get_sw_timestamp();

        ssize_t len = recvmsg(sock, &msg, 0);
        if (len < 0) continue;

        // Get RX hardware timestamp (may be 0 if not available)
        uint64_t rx_hw_ts = get_hw_timestamp(&msg);

        // Check if IPv4 packet (0x0800) - NO VLAN tag
        struct ethhdr *eth = (struct ethhdr *)buffer;
        if (ntohs(eth->h_proto) != ETH_P_IP) continue;

        // Check minimum packet size
        if (len < RAW_PKT_TOTAL_SIZE) continue;

        // Check destination MAC format: 03:00:00:00:VV:VV
        if (buffer[0] != 0x03 || buffer[1] != 0x00 ||
            buffer[2] != 0x00 || buffer[3] != 0x00) continue;

        // Extract VL-ID from destination MAC
        uint16_t vl_id = ((uint16_t)buffer[4] << 8) | buffer[5];

        // Match with sent packet by VL-ID
        for (int v = 0; v < VLANS_PER_PORT; v++) {
            int expected_tx_port = port_pairs[port_id];
            if (results[expected_tx_port][v].vl_id == vl_id &&
                !results[expected_tx_port][v].valid) {

                results[expected_tx_port][v].rx_hw_ts = rx_hw_ts;
                results[expected_tx_port][v].rx_sw_ts = rx_sw_ts;

                // Calculate latency - prefer HW timestamp, fallback to SW
                if (rx_hw_ts > 0 && results[expected_tx_port][v].tx_hw_ts > 0) {
                    // Use hardware timestamps (most accurate)
                    results[expected_tx_port][v].latency_ns =
                        (int64_t)rx_hw_ts - (int64_t)results[expected_tx_port][v].tx_hw_ts;
                    results[expected_tx_port][v].hw_ts_valid = true;
                    results[expected_tx_port][v].valid = true;
                } else {
                    // Fallback to software timestamps
                    results[expected_tx_port][v].latency_ns =
                        (int64_t)rx_sw_ts - (int64_t)results[expected_tx_port][v].tx_sw_ts;
                    results[expected_tx_port][v].hw_ts_valid = false;
                    results[expected_tx_port][v].valid = true;
                }
                break;
            }
        }
    }

    return NULL;
}

// ============================================
// PRINT RESULTS
// ============================================

static void print_results(void) {
    log_printf("\n");
    log_printf("╔══════════════════════════════════════════════════════════════════════════════════════════╗\n");
    if (g_hw_ts_available) {
        log_printf("║                    WIRE LATENCY TEST RESULTS (Hardware Timestamps)                       ║\n");
    } else {
        log_printf("║                    WIRE LATENCY TEST RESULTS (Software Timestamps)                       ║\n");
    }
    log_printf("╠══════════╦══════════╦══════════╦══════════╦═══════════════════╦═══════════════════════════╣\n");
    log_printf("║ TX Port  ║ RX Port  ║  VLAN    ║  VL-ID   ║  Latency (us)     ║  Status                   ║\n");
    log_printf("╠══════════╬══════════╬══════════╬══════════╬═══════════════════╬═══════════════════════════╣\n");

    int success_count = 0;
    int hw_ts_count = 0;
    double total_latency = 0;
    double min_latency = 1e9;
    double max_latency = 0;

    for (int p = 0; p < NUM_PORTS; p++) {
        for (int v = 0; v < VLANS_PER_PORT; v++) {
            struct latency_result *r = &results[p][v];

            log_printf("║   %3d    ║   %3d    ║   %3d    ║  %5d   ║",
                   r->tx_port, r->rx_port, r->vlan_id, r->vl_id);

            if (r->valid && r->latency_ns > 0) {
                double lat_us = r->latency_ns / 1000.0;
                const char *ts_type = r->hw_ts_valid ? "HW" : "SW";
                log_printf("     %10.3f    ║  OK (%s)                   ║\n", lat_us, ts_type);
                success_count++;
                if (r->hw_ts_valid) hw_ts_count++;
                total_latency += lat_us;
                if (lat_us < min_latency) min_latency = lat_us;
                if (lat_us > max_latency) max_latency = lat_us;
            } else if (!r->valid && r->vlan_id == 0) {
                log_printf("         -         ║  Not tested               ║\n");
            } else {
                log_printf("         -         ║  No RX (timeout/lost)     ║\n");
            }
        }
    }

    log_printf("╠══════════╩══════════╩══════════╩══════════╩═══════════════════╩═══════════════════════════╣\n");

    if (success_count > 0) {
        log_printf("║  SUMMARY: %d/%d successful (%d HW, %d SW timestamps)                                     ║\n",
               success_count, NUM_PORTS * VLANS_PER_PORT, hw_ts_count, success_count - hw_ts_count);
        log_printf("║  Min: %.3f us  |  Avg: %.3f us  |  Max: %.3f us                                      ║\n",
               min_latency, total_latency / success_count, max_latency);
    } else {
        log_printf("║  SUMMARY: No successful measurements                                                    ║\n");
        log_printf("║  Note: Packets may not be reaching destination (check switch/cable)                    ║\n");
    }

    log_printf("╚══════════════════════════════════════════════════════════════════════════════════════════╝\n");

    // Also write CSV format to log file for easy parsing
    if (g_log_file && success_count > 0) {
        fprintf(g_log_file, "\n=== CSV FORMAT ===\n");
        fprintf(g_log_file, "tx_port,rx_port,vlan_id,vl_id,latency_ns,latency_us,timestamp_type\n");
        for (int p = 0; p < NUM_PORTS; p++) {
            for (int v = 0; v < VLANS_PER_PORT; v++) {
                struct latency_result *r = &results[p][v];
                if (r->valid && r->latency_ns > 0) {
                    fprintf(g_log_file, "%d,%d,%d,%d,%ld,%.3f,%s\n",
                           r->tx_port, r->rx_port, r->vlan_id, r->vl_id,
                           r->latency_ns, r->latency_ns / 1000.0,
                           r->hw_ts_valid ? "HW" : "SW");
                }
            }
        }
        fflush(g_log_file);
    }
}

// ============================================
// SIGNAL HANDLER
// ============================================

static void signal_handler(int sig) {
    (void)sig;
    printf("\nStopping...\n");
    if (g_log_file) {
        fprintf(g_log_file, "\nStopped by signal\n");
    }
    g_running = false;
}

// ============================================
// MAIN
// ============================================

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║           WIRE LATENCY TEST (Kernel SO_TIMESTAMPING)             ║\n");
    printf("║  Hardware TX/RX timestamps for true wire-to-wire latency         ║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝\n\n");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Check root
    if (geteuid() != 0) {
        fprintf(stderr, "Error: Must run as root (need raw socket access)\n");
        return 1;
    }

    // Open log file
    open_log_file();

    // Initialize sockets
    log_printf("=== Initializing Sockets ===\n");
    for (int p = 0; p < NUM_PORTS; p++) {
        sockets[p] = setup_socket(p);
        if (sockets[p] < 0) {
            fprintf(stderr, "Failed to setup socket for port %d\n", p);
            // Continue with other ports
        }
    }

    // Start RX threads
    log_printf("\n=== Starting RX Threads ===\n");
    pthread_t rx_threads[NUM_PORTS];
    int port_ids[NUM_PORTS];

    for (int p = 0; p < NUM_PORTS; p++) {
        if (sockets[p] < 0) continue;
        port_ids[p] = p;
        pthread_create(&rx_threads[p], NULL, rx_thread, &port_ids[p]);
        log_printf("  RX thread started for port %d\n", p);
    }

    // Small delay for RX threads to start
    usleep(100000);

    // Send test packets
    log_printf("\n=== Sending Test Packets ===\n");
    for (int p = 0; p < NUM_PORTS; p++) {
        if (sockets[p] < 0) continue;

        for (int v = 0; v < VLANS_PER_PORT; v++) {
            send_test_packet(p, v);
            log_printf("  TX: Port %d -> VLAN %d, VL-ID %d\n",
                   p, vlan_ids[p][v], get_vl_id(p, v));
            usleep(10000);  // 10ms between packets
        }
    }

    // Wait for RX threads
    log_printf("\n=== Waiting for Packets (timeout: %d sec) ===\n", TIMEOUT_SEC);
    for (int p = 0; p < NUM_PORTS; p++) {
        if (sockets[p] < 0) continue;
        pthread_join(rx_threads[p], NULL);
    }

    // Print results
    print_results();

    // Cleanup
    for (int p = 0; p < NUM_PORTS; p++) {
        if (sockets[p] >= 0) {
            close(sockets[p]);
        }
    }

    // Close log file
    close_log_file();

    return 0;
}
