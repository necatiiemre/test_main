# Wire Latency Test - Makefile
# Uses kernel SO_TIMESTAMPING for hardware timestamps

CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
LDFLAGS = -lpthread -lm

TARGET = wire_latency_test
SRC = wire_latency_test.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET)

run: $(TARGET)
	@echo "Running as root..."
	sudo ./$(TARGET)

# Check interface names
check-interfaces:
	@echo "Available network interfaces:"
	@ip link show | grep -E "^[0-9]+:" | awk '{print $$2}' | tr -d ':'
	@echo ""
	@echo "Mellanox interfaces (look for mlx5/ConnectX):"
	@ls -la /sys/class/net/*/device/driver 2>/dev/null | grep mlx || echo "No mlx interfaces found"

# Check PTP devices
check-ptp:
	@echo "PTP devices:"
	@ls -la /dev/ptp* 2>/dev/null || echo "No PTP devices found"
	@echo ""
	@echo "PHC to interface mapping:"
	@for iface in $$(ls /sys/class/net/); do \
		ptp=$$(ethtool -T $$iface 2>/dev/null | grep "PTP Hardware Clock" | awk '{print $$NF}'); \
		if [ -n "$$ptp" ]; then echo "  $$iface -> /dev/ptp$$ptp"; fi; \
	done

# Check timestamping support
check-timestamping:
	@echo "Checking timestamping support for all interfaces:"
	@for iface in $$(ls /sys/class/net/); do \
		echo ""; \
		echo "=== $$iface ==="; \
		ethtool -T $$iface 2>/dev/null || echo "  Not available"; \
	done
