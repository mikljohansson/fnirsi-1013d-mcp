# Host-side tests for the PC protocol code (gcc on WSL/Linux, not the cross compiler).
#
#   make -C firmware/scope/host_tests test     build and run everything
#   make -C firmware/scope/host_tests vectors  regenerate docs/protocol-vectors.json
#   make -C firmware/scope/host_tests clean

CC      ?= gcc
PYTHON  ?= python3
SRC     := ..
OUT     := build
CFLAGS  := -std=c99 -O2 -g -Wall -Wextra -Werror -Wno-unused-function -I$(SRC) -I.
VECTORS := ../../../docs/protocol-vectors.json

PROTO_SRCS := $(SRC)/pc_protocol.c
CMDS_SRCS  := $(SRC)/pc_cmds.c mock_hal.c
CMDS_DEPS  := $(CMDS_SRCS) $(PROTO_SRCS) $(SRC)/pc_cmds.h $(SRC)/pc_hal.h $(SRC)/pc_protocol.h mock_hal.h test_util.h

.PHONY: all test vectors clean
all: $(OUT)/test_protocol $(OUT)/test_rx_ring $(OUT)/vector_check $(OUT)/test_session $(OUT)/test_session_debug

$(OUT):
	mkdir -p $@

$(OUT)/test_protocol: test_protocol.c test_util.h $(PROTO_SRCS) $(SRC)/pc_protocol.h | $(OUT)
	$(CC) $(CFLAGS) -o $@ test_protocol.c $(PROTO_SRCS)

$(OUT)/test_rx_ring: test_rx_ring.c test_util.h $(PROTO_SRCS) $(SRC)/pc_protocol.h $(SRC)/pc_rx_ring.h | $(OUT)
	$(CC) $(CFLAGS) -o $@ test_rx_ring.c $(PROTO_SRCS)

$(OUT)/vector_check: vector_check.c $(PROTO_SRCS) $(SRC)/pc_protocol.h | $(OUT)
	$(CC) $(CFLAGS) -o $@ vector_check.c $(PROTO_SRCS)

# Session tests run twice: release build and PC_DEBUG=1 (DEBUG_FPGA, FORCE enabled)
$(OUT)/test_session: test_session.c $(CMDS_DEPS) | $(OUT)
	$(CC) $(CFLAGS) -o $@ test_session.c $(CMDS_SRCS) $(PROTO_SRCS)

$(OUT)/test_session_debug: test_session.c $(CMDS_DEPS) | $(OUT)
	$(CC) $(CFLAGS) -DPC_DEBUG=1 -o $@ test_session.c $(CMDS_SRCS) $(PROTO_SRCS)

test: all
	$(OUT)/test_protocol
	$(OUT)/test_rx_ring
	$(OUT)/test_session
	$(OUT)/test_session_debug
	$(PYTHON) -I check_vectors.py --checker $(OUT)/vector_check --vectors $(VECTORS)

vectors:
	$(PYTHON) -I gen_vectors.py -o $(VECTORS)

clean:
	rm -rf $(OUT)
