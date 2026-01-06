#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/sockios.h>
#include <linux/net_tstamp.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("Usage: %s <interface> [interface2] ...\n", argv[0]);
        printf("Example: %s ens1f0np0 ens1f1np1\n", argv[0]);
        return 1;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        struct ifreq ifr;
        struct hwtstamp_config config;

        memset(&ifr, 0, sizeof(ifr));
        memset(&config, 0, sizeof(config));

        strncpy(ifr.ifr_name, argv[i], IFNAMSIZ - 1);

        // Enable hardware timestamps for all RX packets
        config.tx_type = HWTSTAMP_TX_ON;
        config.rx_filter = HWTSTAMP_FILTER_ALL;

        ifr.ifr_data = (void *)&config;

        if (ioctl(sock, SIOCSHWTSTAMP, &ifr) < 0) {
            perror(argv[i]);
            printf("  Failed to enable hardware timestamp on %s\n", argv[i]);
        } else {
            printf("  %s: Hardware timestamp enabled (tx=%d, rx_filter=%d)\n",
                   argv[i], config.tx_type, config.rx_filter);
        }
    }

    close(sock);
    return 0;
}
