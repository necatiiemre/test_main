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

// Interface names - MODIFY THESE FOR YOUR SYSTEM
// Use: ip link show | grep "enp\|eth\|mlx"
static const char *interface_names[NUM_PORTS] = {
    "enp1s0f0",   // Port 0
    "enp1s0f1",   // Port 1
    "enp2s0f0",   // Port 2
    "enp2s0f1",   // Port 3
    "enp3s0f0",   // Port 4
    "enp3s0f1",   // Port 5
    "enp4s0f0",   // Port 6
    "enp4s0f1",   // Port 7
};

// Port pairing: TX port -> RX port
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

// VLAN IDs per port
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

// VL-ID calculation: matches DPDK version
// vl_id = vlan_index * 128 + (port_id * 4 + local_index) + 3
static uint16_t get_vl_id(int port_id, int vlan_index) {
    return (uint16_t)(vlan_index * 128 + (port_id * 4) + 3);
}

#define PACKET_SIZE     1500
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
    int64_t latency_ns;     // Wire latency (ns)
    bool valid;
};

static struct latency_result results[NUM_PORTS][VLANS_PER_PORT];
static int sockets[NUM_PORTS] = {-1};
static volatile bool g_running = true;

// ============================================
// VLAN PACKET STRUCTURE
// ============================================

struct vlan_ethhdr {
    uint8_t  h_dest[ETH_ALEN];
    uint8_t  h_source[ETH_ALEN];
    uint16_t h_vlan_proto;      // 0x8100
    uint16_t h_vlan_TCI;        // VLAN ID
    uint16_t h_vlan_encap_proto; // Inner protocol (0x0800 = IPv4)
} __attribute__((packed));

struct test_payload {
    uint16_t magic;         // 0xABCD
    uint16_t tx_port;
    uint16_t vlan_id;
    uint16_t vl_id;
    uint64_t sequence;
    uint64_t tx_timestamp;  // Filled by sender
} __attribute__((packed));

#define MAGIC_VALUE 0xABCD

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

    printf("  Port %d (%s): Socket ready, HW timestamping enabled\n", port_id, ifname);
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

    uint8_t packet[PACKET_SIZE];
    memset(packet, 0, sizeof(packet));

    // VLAN Ethernet header
    struct vlan_ethhdr *eth = (struct vlan_ethhdr *)packet;

    // Destination MAC: 03:00:00:00:VV:VV (multicast with VL-ID)
    eth->h_dest[0] = 0x03;
    eth->h_dest[1] = 0x00;
    eth->h_dest[2] = 0x00;
    eth->h_dest[3] = 0x00;
    eth->h_dest[4] = (vl_id >> 8) & 0xFF;
    eth->h_dest[5] = vl_id & 0xFF;

    // Source MAC: 02:00:00:00:00:PP (port ID)
    eth->h_source[0] = 0x02;
    eth->h_source[1] = 0x00;
    eth->h_source[2] = 0x00;
    eth->h_source[3] = 0x00;
    eth->h_source[4] = 0x00;
    eth->h_source[5] = port_id;

    // VLAN tag
    eth->h_vlan_proto = htons(ETH_P_8021Q);
    eth->h_vlan_TCI = htons(vlan_id);
    eth->h_vlan_encap_proto = htons(ETH_P_IP);

    // Test payload (after VLAN header)
    struct test_payload *payload = (struct test_payload *)(packet + sizeof(struct vlan_ethhdr) + 20 + 8);  // +IP+UDP
    payload->magic = htons(MAGIC_VALUE);
    payload->tx_port = htons(port_id);
    payload->vlan_id = htons(vlan_id);
    payload->vl_id = htons(vl_id);
    payload->sequence = 0;

    // Send packet
    struct sockaddr_ll sll;
    memset(&sll, 0, sizeof(sll));
    sll.sll_family = AF_PACKET;
    sll.sll_ifindex = 0;  // Will be set by kernel
    sll.sll_halen = ETH_ALEN;
    memcpy(sll.sll_addr, eth->h_dest, ETH_ALEN);

    // Get interface index
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, interface_names[port_id], IFNAMSIZ - 1);
    if (ioctl(sockets[port_id], SIOCGIFINDEX, &ifr) == 0) {
        sll.sll_ifindex = ifr.ifr_ifindex;
    }

    ssize_t sent = sendto(sockets[port_id], packet, PACKET_SIZE, 0,
                          (struct sockaddr *)&sll, sizeof(sll));

    if (sent < 0) {
        perror("sendto");
        return -1;
    }

    // Get TX hardware timestamp
    uint64_t tx_ts = get_tx_timestamp(sockets[port_id]);

    // Store result
    results[port_id][vlan_idx].tx_port = port_id;
    results[port_id][vlan_idx].rx_port = rx_port;
    results[port_id][vlan_idx].vlan_id = vlan_id;
    results[port_id][vlan_idx].vl_id = vl_id;
    results[port_id][vlan_idx].tx_hw_ts = tx_ts;
    results[port_id][vlan_idx].valid = false;

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

        ssize_t len = recvmsg(sock, &msg, 0);
        if (len < 0) continue;

        // Get RX hardware timestamp
        uint64_t rx_ts = get_hw_timestamp(&msg);
        if (rx_ts == 0) continue;

        // Check if VLAN tagged
        struct ethhdr *eth = (struct ethhdr *)buffer;
        if (ntohs(eth->h_proto) != ETH_P_8021Q) continue;

        // Extract VLAN ID
        uint16_t *vlan_tci = (uint16_t *)(buffer + 14);
        uint16_t vlan_id = ntohs(*vlan_tci) & 0xFFF;

        // Extract VL-ID from destination MAC
        uint16_t vl_id = ((uint16_t)buffer[4] << 8) | buffer[5];

        // Extract source port from source MAC
        int src_port = buffer[11];

        // Match with sent packet
        for (int v = 0; v < VLANS_PER_PORT; v++) {
            int expected_tx_port = port_pairs[port_id];
            if (results[expected_tx_port][v].vlan_id == vlan_id &&
                results[expected_tx_port][v].vl_id == vl_id &&
                !results[expected_tx_port][v].valid) {

                results[expected_tx_port][v].rx_hw_ts = rx_ts;

                // Calculate latency
                if (results[expected_tx_port][v].tx_hw_ts > 0) {
                    results[expected_tx_port][v].latency_ns =
                        (int64_t)rx_ts - (int64_t)results[expected_tx_port][v].tx_hw_ts;
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
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════════════════════════════════╗\n");
    printf("║                    WIRE LATENCY TEST RESULTS (Hardware Timestamps)                       ║\n");
    printf("╠══════════╦══════════╦══════════╦══════════╦═══════════════════╦═══════════════════════════╣\n");
    printf("║ TX Port  ║ RX Port  ║  VLAN    ║  VL-ID   ║  Latency (µs)     ║  Status                   ║\n");
    printf("╠══════════╬══════════╬══════════╬══════════╬═══════════════════╬═══════════════════════════╣\n");

    int success_count = 0;
    double total_latency = 0;
    double min_latency = 1e9;
    double max_latency = 0;

    for (int p = 0; p < NUM_PORTS; p++) {
        for (int v = 0; v < VLANS_PER_PORT; v++) {
            struct latency_result *r = &results[p][v];

            printf("║   %3d    ║   %3d    ║   %3d    ║  %5d   ║",
                   r->tx_port, r->rx_port, r->vlan_id, r->vl_id);

            if (r->valid && r->latency_ns > 0) {
                double lat_us = r->latency_ns / 1000.0;
                printf("     %10.3f    ║  ✓ OK                     ║\n", lat_us);
                success_count++;
                total_latency += lat_us;
                if (lat_us < min_latency) min_latency = lat_us;
                if (lat_us > max_latency) max_latency = lat_us;
            } else if (r->tx_hw_ts == 0) {
                printf("         -         ║  ✗ No TX timestamp        ║\n");
            } else if (r->rx_hw_ts == 0) {
                printf("         -         ║  ✗ No RX (timeout/lost)   ║\n");
            } else {
                printf("         -         ║  ✗ Invalid                ║\n");
            }
        }
    }

    printf("╠══════════╩══════════╩══════════╩══════════╩═══════════════════╩═══════════════════════════╣\n");

    if (success_count > 0) {
        printf("║  SUMMARY: %d/%d successful                                                              ║\n",
               success_count, NUM_PORTS * VLANS_PER_PORT);
        printf("║  Min: %.3f µs  |  Avg: %.3f µs  |  Max: %.3f µs                                    ║\n",
               min_latency, total_latency / success_count, max_latency);
    } else {
        printf("║  SUMMARY: No successful measurements                                                    ║\n");
    }

    printf("╚══════════════════════════════════════════════════════════════════════════════════════════╝\n");
}

// ============================================
// SIGNAL HANDLER
// ============================================

static void signal_handler(int sig) {
    printf("\nStopping...\n");
    g_running = false;
}

// ============================================
// MAIN
// ============================================

int main(int argc, char *argv[]) {
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

    // Initialize sockets
    printf("=== Initializing Sockets ===\n");
    for (int p = 0; p < NUM_PORTS; p++) {
        sockets[p] = setup_socket(p);
        if (sockets[p] < 0) {
            fprintf(stderr, "Failed to setup socket for port %d\n", p);
            // Continue with other ports
        }
    }

    // Start RX threads
    printf("\n=== Starting RX Threads ===\n");
    pthread_t rx_threads[NUM_PORTS];
    int port_ids[NUM_PORTS];

    for (int p = 0; p < NUM_PORTS; p++) {
        if (sockets[p] < 0) continue;
        port_ids[p] = p;
        pthread_create(&rx_threads[p], NULL, rx_thread, &port_ids[p]);
        printf("  RX thread started for port %d\n", p);
    }

    // Small delay for RX threads to start
    usleep(100000);

    // Send test packets
    printf("\n=== Sending Test Packets ===\n");
    for (int p = 0; p < NUM_PORTS; p++) {
        if (sockets[p] < 0) continue;

        for (int v = 0; v < VLANS_PER_PORT; v++) {
            send_test_packet(p, v);
            printf("  TX: Port %d -> VLAN %d, VL-ID %d\n",
                   p, vlan_ids[p][v], get_vl_id(p, v));
            usleep(10000);  // 10ms between packets
        }
    }

    // Wait for RX threads
    printf("\n=== Waiting for Packets (timeout: %d sec) ===\n", TIMEOUT_SEC);
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

    return 0;
}
