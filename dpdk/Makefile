# DPDK Application Makefile
APP = dpdk_app
CC = gcc

# Directories
SRCDIR = src
INCDIR = include
OBJDIR = obj

# Mellanox HW Latency directories (integrated into DPDK)
MLX_SRCDIR = ../mellanox_latency/src
MLX_INCDIR = ../mellanox_latency/include

NUM_TX_CORES ?= 4
NUM_RX_CORES ?= 4
TARGET_GBPS_FAST ?= 3.91
TARGET_GBPS_MID ?= 3.67
TARGET_GBPS_SLOW ?= 3.86
USE_VLAN ?=1

# Enable raw socket ports (non-DPDK NICs)
ENABLE_RAW_SOCKET_PORTS ?= 1

# Enable Mellanox HW latency test (runs before DPDK init)
MELLANOX_HW_LATENCY_ENABLED ?= 1

# Common defines
COMMON_DEFINES = -DNUM_TX_CORES=$(NUM_TX_CORES) -DNUM_RX_CORES=$(NUM_RX_CORES) -DUSE_VLAN=$(USE_VLAN) -DTARGET_GBPS_FAST=$(TARGET_GBPS_FAST) -DTARGET_GBPS_MID=$(TARGET_GBPS_MID) -DTARGET_GBPS_SLOW=$(TARGET_GBPS_SLOW) -DENABLE_RAW_SOCKET_PORTS=$(ENABLE_RAW_SOCKET_PORTS) -DMELLANOX_HW_LATENCY_ENABLED=$(MELLANOX_HW_LATENCY_ENABLED)

# DPDK compiler flags (DPDK include first, then MLX for mellanox_hw_latency.c)
DPDK_CFLAGS = -O3 -march=native -flto -ffast-math -funroll-loops -Wextra -I$(INCDIR) -I$(MLX_INCDIR) $(COMMON_DEFINES)

# Mellanox compiler flags (MLX include first so mellanox sources find their headers)
MLX_CFLAGS = -O3 -march=native -flto -ffast-math -funroll-loops -Wextra -I$(MLX_INCDIR) -I$(INCDIR) $(COMMON_DEFINES)

DEBUG_CFLAGS = -g -O3 -DDEBUG -march=native -Wall -Wextra -I$(INCDIR) -I$(MLX_INCDIR) $(COMMON_DEFINES)

# Additional libraries
EXTRA_LIBS = -lpthread

# DPDK source files
DPDK_SOURCES = $(wildcard $(SRCDIR)/*.c)

# Mellanox HW latency sources (exclude main.c - we use our own integration)
MLX_SOURCES = $(MLX_SRCDIR)/latency_test.c \
              $(MLX_SRCDIR)/hw_timestamp.c \
              $(MLX_SRCDIR)/packet.c \
              $(MLX_SRCDIR)/results.c

# Object files
DPDK_OBJS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/dpdk_%.o,$(DPDK_SOURCES))
MLX_OBJS = $(patsubst $(MLX_SRCDIR)/%.c,$(OBJDIR)/mlx_%.o,$(MLX_SOURCES))
ALL_OBJS = $(DPDK_OBJS) $(MLX_OBJS)

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

# Create object directory
$(OBJDIR):
	@mkdir -p $(OBJDIR)

# Compile DPDK sources (uses DPDK_CFLAGS - dpdk/include first)
$(OBJDIR)/dpdk_%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(DPDK_CFLAGS) $(shell pkg-config --cflags libdpdk) -c $< -o $@

# Compile Mellanox sources (uses MLX_CFLAGS - mellanox/include first)
$(OBJDIR)/mlx_%.o: $(MLX_SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(MLX_CFLAGS) -c $< -o $@

# Link application
$(APP): $(ALL_OBJS)
	@echo "Building $(APP) with Mellanox HW Latency integration..."
	@echo "DPDK Objects: $(DPDK_OBJS)"
	@echo "Mellanox Objects: $(MLX_OBJS)"
	@echo "Raw Socket Ports: $(ENABLE_RAW_SOCKET_PORTS)"
	@echo "Mellanox HW Latency: $(MELLANOX_HW_LATENCY_ENABLED)"
	$(CC) $(DPDK_CFLAGS) $(ALL_OBJS) -o $(APP) $(DPDK_FLAGS) $(EXTRA_LIBS)
	@echo "✓ Build completed: $(APP)"

# Debug build (single step for simplicity)
debug:
	@echo "Building debug version..."
	$(CC) $(DEBUG_CFLAGS) $(DPDK_SOURCES) $(MLX_SOURCES) -o $(APP)-debug $(DPDK_FLAGS) $(EXTRA_LIBS)
	@echo "✓ Debug build completed: $(APP)-debug"

# Static build
static:
	@echo "Building static version..."
	$(CC) $(DPDK_CFLAGS) $(DPDK_SOURCES) $(MLX_SOURCES) -o $(APP)-static $(DPDK_STATIC_FLAGS) $(EXTRA_LIBS)
	@echo "✓ Static build completed: $(APP)-static"

# Clean
clean:
	@echo "Cleaning..."
	@rm -rf $(OBJDIR) $(APP) $(APP)-debug $(APP)-static
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
	@echo "DPDK CFLAGS: $(DPDK_CFLAGS)"
	@echo "MLX CFLAGS: $(MLX_CFLAGS)"
	@echo "DPDK Sources: $(DPDK_SOURCES)"
	@echo "Mellanox Sources: $(MLX_SOURCES)"
	@echo "Include dirs: $(INCDIR), $(MLX_INCDIR)"
	@echo "DPDK available: $(DPDK_CHECK)"
	@echo "Mellanox HW Latency: $(MELLANOX_HW_LATENCY_ENABLED)"

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
	@echo ""
	@echo "Build options:"
	@echo "  MELLANOX_HW_LATENCY_ENABLED=0  - Disable Mellanox HW latency test"
	@echo "  ENABLE_RAW_SOCKET_PORTS=0      - Disable raw socket ports"
