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

.PHONY: all test vectors clean
all: $(OUT)/test_protocol $(OUT)/vector_check

$(OUT):
	mkdir -p $@

$(OUT)/test_protocol: test_protocol.c test_util.h $(PROTO_SRCS) $(SRC)/pc_protocol.h | $(OUT)
	$(CC) $(CFLAGS) -o $@ test_protocol.c $(PROTO_SRCS)

$(OUT)/vector_check: vector_check.c $(PROTO_SRCS) $(SRC)/pc_protocol.h | $(OUT)
	$(CC) $(CFLAGS) -o $@ vector_check.c $(PROTO_SRCS)

test: all
	$(OUT)/test_protocol
	$(PYTHON) -I check_vectors.py --checker $(OUT)/vector_check --vectors $(VECTORS)

vectors:
	$(PYTHON) -I gen_vectors.py -o $(VECTORS)

clean:
	rm -rf $(OUT)
