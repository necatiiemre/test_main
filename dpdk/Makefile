# DPDK Application Makefile
APP = dpdk_app
CC = gcc

# Directories
SRCDIR = src
INCDIR = include
EMBLATDIR = src/embedded_latency

NUM_TX_CORES ?= 4
NUM_RX_CORES ?= 4
TARGET_GBPS_FAST ?= 3.91
TARGET_GBPS_MID ?= 3.67
TARGET_GBPS_SLOW ?= 3.86
USE_VLAN ?=1

# Enable raw socket ports (non-DPDK NICs)
ENABLE_RAW_SOCKET_PORTS ?= 1

# Compiler flags
CFLAGS = -O3 -march=native -flto -ffast-math -funroll-loops -Wextra -I$(INCDIR) -I$(SRCDIR) -DNUM_TX_CORES=$(NUM_TX_CORES) -DNUM_RX_CORES=$(NUM_RX_CORES) -DUSE_VLAN=$(USE_VLAN) -DTARGET_GBPS_FAST=$(TARGET_GBPS_FAST) -DTARGET_GBPS_MID=$(TARGET_GBPS_MID) -DTARGET_GBPS_SLOW=$(TARGET_GBPS_SLOW) -DENABLE_RAW_SOCKET_PORTS=$(ENABLE_RAW_SOCKET_PORTS)
DEBUG_CFLAGS = -g -O3 -DDEBUG -march=native -Wall -Wextra -I$(INCDIR) -I$(SRCDIR) -DENABLE_RAW_SOCKET_PORTS=$(ENABLE_RAW_SOCKET_PORTS)

# Additional libraries for raw socket ports (pthread for threading)
EXTRA_LIBS = -lpthread

# Source files (include embedded latency)
SOURCES = $(wildcard $(SRCDIR)/*.c) $(wildcard $(EMBLATDIR)/*.c)

# DPDK flags
DPDK_FLAGS = $(shell pkg-config --cflags --libs libdpdk)
DPDK_STATIC_FLAGS = $(shell pkg-config --static --cflags --libs libdpdk)

# Check if DPDK is available
DPDK_CHECK := $(shell pkg-config --exists libdpdk && echo "yes" || echo "no")
ifeq ($(DPDK_CHECK), no)
    $(error "DPDK not found! Install DPDK and ensure pkg-config can find it")
endif

# Default target
.PHONY: all clean debug static run info help

all: $(APP)

# Build application
$(APP):
	@echo "Building $(APP)..."
	@echo "Sources: $(SOURCES)"
	@echo "Raw Socket Ports: $(ENABLE_RAW_SOCKET_PORTS)"
	$(CC) $(CFLAGS) $(SOURCES) -o $(APP) $(DPDK_FLAGS) $(EXTRA_LIBS)
	@echo "✓ Build completed: $(APP)"

# Debug build
debug:
	@echo "Building debug version..."
	$(CC) $(DEBUG_CFLAGS) $(SOURCES) -o $(APP)-debug $(DPDK_FLAGS) $(EXTRA_LIBS)
	@echo "✓ Debug build completed: $(APP)-debug"

# Static build
static:
	@echo "Building static version..."
	$(CC) $(CFLAGS) $(SOURCES) -o $(APP)-static $(DPDK_STATIC_FLAGS) $(EXTRA_LIBS)
	@echo "✓ Static build completed: $(APP)-static"

# Clean
clean:
	@echo "Cleaning..."
	@rm -f $(APP) $(APP)-debug $(APP)-static
	@echo "✓ Clean completed"

# Run with basic EAL parameters
run: $(APP)
	@echo "Running $(APP)..."
	sudo ./$(APP) -l 0-255 -n 16 

# Show build information
info:
	@echo "=== Build Configuration ==="
	@echo "Application: $(APP)"
	@echo "Compiler: $(CC)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "Sources: $(SOURCES)"
	@echo "Include dir: $(INCDIR)"
	@echo "DPDK available: $(DPDK_CHECK)"

# Help
help:
	@echo "Available targets:"
	@echo "  all      - Build application (default)"
	@echo "  debug    - Build with debug symbols"
	@echo "  static   - Build with static linking"
	@echo "  clean    - Remove build artifacts"
	@echo "  run      - Build and run application"
	@echo "  info     - Show build configuration"
	@echo "  help     - Show this help"
