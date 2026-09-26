ROOT    := ../..
CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -g \
           -fsanitize=address,undefined
CPPFLAGS += -I$(ROOT)/shared/protocol -I$(ROOT)/shared/common

SRCS := test_main.c \
        $(ROOT)/shared/protocol/edge_protocol.c \
        $(ROOT)/shared/common/sensor_sim.c \
        $(ROOT)/shared/common/rt_stats.c

build/host_tests: $(SRCS)
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRCS) -o $@

.PHONY: test clean
test: build/host_tests
	./build/host_tests

clean:
	rm -rf build
