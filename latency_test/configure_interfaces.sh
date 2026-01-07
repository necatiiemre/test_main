#!/bin/bash
# Script to find Mellanox interfaces and configure wire_latency_test.c

echo "╔══════════════════════════════════════════════════════════════════╗"
echo "║           Interface Configuration Helper                          ║"
echo "╚══════════════════════════════════════════════════════════════════╝"
echo ""

# Find Mellanox interfaces
echo "=== Mellanox ConnectX Interfaces ==="
echo ""

# Method 1: Check driver symlinks
for iface in $(ls /sys/class/net/ 2>/dev/null); do
    driver=$(readlink /sys/class/net/$iface/device/driver 2>/dev/null | xargs basename 2>/dev/null)
    if [[ "$driver" == "mlx5_core" ]]; then
        pci=$(readlink /sys/class/net/$iface/device 2>/dev/null | xargs basename 2>/dev/null)
        ptp=$(ethtool -T $iface 2>/dev/null | grep "PTP Hardware Clock" | awk '{print $NF}')
        mac=$(cat /sys/class/net/$iface/address 2>/dev/null)
        state=$(cat /sys/class/net/$iface/operstate 2>/dev/null)

        echo "Interface: $iface"
        echo "  PCI: $pci"
        echo "  MAC: $mac"
        echo "  State: $state"
        echo "  PTP: /dev/ptp$ptp"
        echo ""
    fi
done

echo ""
echo "=== Current Configuration in wire_latency_test.c ==="
grep -A 10 "interface_names\[NUM_PORTS\]" wire_latency_test.c 2>/dev/null | head -12
echo ""

echo "=== To Update Interface Names ==="
echo "Edit wire_latency_test.c and update the interface_names array:"
echo ""
echo "static const char *interface_names[NUM_PORTS] = {"
echo "    \"<port0_interface>\",   // Port 0 -> Port 7"
echo "    \"<port1_interface>\",   // Port 1 -> Port 6"
echo "    \"<port2_interface>\",   // Port 2 -> Port 5"
echo "    \"<port3_interface>\",   // Port 3 -> Port 4"
echo "    \"<port4_interface>\",   // Port 4 -> Port 3"
echo "    \"<port5_interface>\",   // Port 5 -> Port 2"
echo "    \"<port6_interface>\",   // Port 6 -> Port 1"
echo "    \"<port7_interface>\",   // Port 7 -> Port 0"
echo "};"
echo ""

echo "=== DPDK Port to Interface Mapping ==="
echo "Check DPDK's port detection order with:"
echo "  dpdk-devbind.py --status"
echo ""
echo "Or look at your DPDK app's output for port MAC addresses,"
echo "then match with interface MAC addresses above."
