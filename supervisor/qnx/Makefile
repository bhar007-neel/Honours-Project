# Cross-compiles the QNX supervisor tools for the Raspberry Pi 4 (aarch64le).
#
# Run from a shell prepared by the SDP environment script, for example in cmd:
#   %USERPROFILE%\qnx800\qnxsdp-env.bat
#   make
#   make deploy PI=qnxuser@<pi-address>

ROOT     := ../..
TARGET   ?= gcc_ntoaarch64le
CC       := qcc
CFLAGS   := -V$(TARGET) -std=gnu11 -O2 -g -Wall -Wextra -Werror
CPPFLAGS := -I$(ROOT)/shared/protocol -I$(ROOT)/shared/common
BUILD    := build/aarch64le

PROTOCOL := $(ROOT)/shared/protocol/edge_protocol.c
RT_STATS := $(ROOT)/shared/common/rt_stats.c
SIM      := $(ROOT)/shared/common/sensor_sim.c

APPS := qnx_sysinfo rt_threads msg_ipc edge_rx edge_tx
BINS := $(addprefix $(BUILD)/,$(APPS))

.PHONY: all clean deploy
all: $(BINS)

$(BUILD)/qnx_sysinfo: src/qnx_sysinfo.c
$(BUILD)/rt_threads:  src/rt_threads.c $(RT_STATS)
$(BUILD)/msg_ipc:     src/msg_ipc.c
$(BUILD)/edge_rx:     src/edge_rx.c $(PROTOCOL)
$(BUILD)/edge_tx:     src/edge_tx.c $(PROTOCOL) $(SIM)

$(BUILD)/edge_rx $(BUILD)/edge_tx: LDLIBS := -lsocket

$(BINS):
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@ $(LDLIBS)

PI ?= qnxuser@qnxpi.local
deploy: all
	scp $(BINS) $(PI):

clean:
	rm -rf build
