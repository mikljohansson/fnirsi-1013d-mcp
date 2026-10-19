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
CMDS_SRCS  := $(SRC)/pc_cmds.c $(SRC)/pc_decim.c $(SRC)/adc_cal.c $(SRC)/acal_track.c $(SRC)/pc_ident.c $(SRC)/pc_report.c $(SRC)/pc_life.c mock_hal.c
CMDS_DEPS  := $(CMDS_SRCS) $(PROTO_SRCS) $(SRC)/pc_cmds.h $(SRC)/pc_hal.h $(SRC)/pc_decim.h $(SRC)/adc_cal.h $(SRC)/acal_track.h $(SRC)/pc_protocol.h $(SRC)/pc_ident.h $(SRC)/pc_report.h $(SRC)/pc_life.h mock_hal.h test_util.h

.PHONY: all test test-arm vectors clean
all: $(OUT)/test_protocol $(OUT)/test_rx_ring $(OUT)/vector_check $(OUT)/decim_check $(OUT)/test_session $(OUT)/test_session_debug $(OUT)/fft_cli $(OUT)/test_spec_fmt $(OUT)/test_adc_cal $(OUT)/test_phosphor $(OUT)/test_mmu $(OUT)/test_usbw $(OUT)/test_acal_track $(OUT)/test_cal_wizard $(OUT)/test_diag $(OUT)/test_ui $(OUT)/test_touch_pick

$(OUT):
	mkdir -p $@

$(OUT)/test_protocol: test_protocol.c test_util.h $(PROTO_SRCS) $(SRC)/pc_protocol.h | $(OUT)
	$(CC) $(CFLAGS) -o $@ test_protocol.c $(PROTO_SRCS)

$(OUT)/test_rx_ring: test_rx_ring.c test_util.h $(PROTO_SRCS) $(SRC)/pc_protocol.h $(SRC)/pc_rx_ring.h | $(OUT)
	$(CC) $(CFLAGS) -o $@ test_rx_ring.c $(PROTO_SRCS)

$(OUT)/decim_check: decim_check.c $(SRC)/pc_decim.c $(SRC)/pc_decim.h | $(OUT)
	$(CC) $(CFLAGS) -o $@ decim_check.c $(SRC)/pc_decim.c

$(OUT)/vector_check: vector_check.c $(PROTO_SRCS) $(SRC)/pc_protocol.h | $(OUT)
	$(CC) $(CFLAGS) -o $@ vector_check.c $(PROTO_SRCS)

# Session tests run twice: release build and PC_DEBUG=1 (DEBUG_FPGA, FORCE enabled)
$(OUT)/test_session: test_session.c $(CMDS_DEPS) | $(OUT)
	$(CC) $(CFLAGS) -o $@ test_session.c $(CMDS_SRCS) $(PROTO_SRCS)

$(OUT)/test_session_debug: test_session.c $(CMDS_DEPS) | $(OUT)
	$(CC) $(CFLAGS) -DPC_DEBUG=1 -o $@ test_session.c $(CMDS_SRCS) $(PROTO_SRCS)

# The target's fixed-point FFT, run natively and compared with numpy (test_fft.py)
FFT_SRCS := $(SRC)/fx_fft.c $(SRC)/fx_fft_tables.c
$(OUT)/fft_cli: fft_cli.c $(FFT_SRCS) $(SRC)/fx_fft.h | $(OUT)
	$(CC) $(CFLAGS) -o $@ fft_cli.c $(FFT_SRCS)

$(OUT)/test_adc_cal: test_adc_cal.c test_util.h $(SRC)/adc_cal.c $(SRC)/adc_cal.h $(SRC)/pc_decim.c $(SRC)/pc_decim.h | $(OUT)
	$(CC) $(CFLAGS) -o $@ test_adc_cal.c $(SRC)/adc_cal.c $(SRC)/pc_decim.c

$(OUT)/test_acal_track: test_acal_track.c test_util.h $(SRC)/acal_track.c $(SRC)/acal_track.h $(SRC)/adc_cal.c $(SRC)/adc_cal.h | $(OUT)
	$(CC) $(CFLAGS) -o $@ test_acal_track.c $(SRC)/acal_track.c $(SRC)/adc_cal.c -lm

$(OUT)/test_cal_wizard: test_cal_wizard.c test_util.h $(SRC)/cal_wiz.c $(SRC)/cal_wiz.h $(SRC)/adc_cal.c $(SRC)/adc_cal.h | $(OUT)
	$(CC) $(CFLAGS) -o $@ test_cal_wizard.c $(SRC)/cal_wiz.c $(SRC)/adc_cal.c

# Screens: card formatting and fit with the device's font tables, main menu rows and mode rule (fw 0.2.51)
UI_SRCS := $(SRC)/ui_fmt.c $(SRC)/ui_menu.c $(SRC)/font_2.c $(SRC)/font_3.c
$(OUT)/test_ui: test_ui.c test_util.h $(UI_SRCS) $(SRC)/ui_fmt.h $(SRC)/ui_menu.h | $(OUT)
	$(CC) $(CFLAGS) -o $@ test_ui.c $(UI_SRCS)
# fw 0.2.52 diagnostics: life record, provenance trailer, image identity (pure parts); --layouts prints the packed
# layouts for check_vectors.py (diag_layouts)
DIAG_SRCS := $(SRC)/pc_life.c $(SRC)/pc_ident.c $(SRC)/adc_cal.c mock_hal.c $(SRC)/pc_cmds.c $(SRC)/pc_decim.c $(SRC)/acal_track.c $(SRC)/pc_report.c $(SRC)/pc_protocol.c
$(OUT)/test_diag: test_diag.c test_util.h $(CMDS_DEPS) | $(OUT)
	$(CC) $(CFLAGS) -o $@ test_diag.c $(DIAG_SRCS)

# Plot touch geometry: pointer position, left handle column, channel pick, shared move (fw 0.2.54)
$(OUT)/test_touch_pick: test_touch_pick.c test_util.h $(SRC)/touch_pick.c $(SRC)/touch_pick.h | $(OUT)
	$(CC) $(CFLAGS) -o $@ test_touch_pick.c $(SRC)/touch_pick.c

$(OUT)/test_phosphor: test_phosphor.c test_util.h $(SRC)/phos_map.c $(SRC)/phos_map.h | $(OUT)
	$(CC) $(CFLAGS) -o $@ test_phosphor.c $(SRC)/phos_map.c

$(OUT)/test_mmu: test_mmu.c test_util.h $(SRC)/pc_mmu.c $(SRC)/pc_mmu.h | $(OUT)
	$(CC) $(CFLAGS) -DPC_HOST_TEST -o $@ test_mmu.c $(SRC)/pc_mmu.c

$(OUT)/test_usbw: test_usbw.c test_util.h $(SRC)/pc_usbw.c $(SRC)/pc_usbw.h | $(OUT)
	$(CC) $(CFLAGS) -o $@ test_usbw.c $(SRC)/pc_usbw.c

$(OUT)/test_spec_fmt: test_spec_fmt.c test_util.h $(SRC)/spec_fmt.c $(SRC)/spec_fmt.h | $(OUT)
	$(CC) $(CFLAGS) -o $@ test_spec_fmt.c $(SRC)/spec_fmt.c

# The same FFT test on the target code: fx_fft.c built with the bench compiler (GCC 10, firmware flags, so the ARM
# assembly paths are in), the CLI linked with newlib semihosting (system arm-none-eabi-gcc), run under qemu-arm.
GCC10    ?= $(HOME)/opt/gcc10
ARM_CC10 := $(GCC10)/usr/bin/arm-none-eabi-gcc
ARM_FL   := -Wall -Wno-write-strings -Wno-char-subscripts -fno-stack-protector -DNO_STDLIB=1 -mcpu=arm926ej-s -O3 \
            -mfloat-abi=soft -isystem $(GCC10)/usr/include/newlib
ARM_OUT  := $(OUT)/arm
$(ARM_OUT)/fft_cli: fft_cli.c $(FFT_SRCS) $(SRC)/fx_fft.h
	mkdir -p $(ARM_OUT)
	$(ARM_CC10) $(ARM_FL) -c $(SRC)/fx_fft.c -o $(ARM_OUT)/fx_fft.o
	$(ARM_CC10) $(ARM_FL) -c $(SRC)/fx_fft_tables.c -o $(ARM_OUT)/fx_fft_tables.o
	arm-none-eabi-gcc -mcpu=arm926ej-s -mfloat-abi=soft -O2 -I$(SRC) --specs=rdimon.specs -o $@ fft_cli.c \
	  $(ARM_OUT)/fx_fft.o $(ARM_OUT)/fx_fft_tables.o

test-arm: $(ARM_OUT)/fft_cli $(OUT)/fft_cli
	$(PYTHON) -I test_fft.py --cli $(ARM_OUT)/fft_cli --prefix "qemu-arm -cpu arm926"
	@for L in 8 10 12 14; do for W in 0 1 2; do \
	  head -c $$((1 << L)) /dev/urandom > $(ARM_OUT)/in.bin; \
	  qemu-arm -cpu arm926 $(ARM_OUT)/fft_cli $$L $$W u8 128 < $(ARM_OUT)/in.bin > $(ARM_OUT)/a.bin; \
	  $(OUT)/fft_cli $$L $$W u8 128 < $(ARM_OUT)/in.bin > $(ARM_OUT)/h.bin; \
	  cmp -s $(ARM_OUT)/a.bin $(ARM_OUT)/h.bin || { echo "test-arm: ARM and C builds differ (N 2^$$L, window $$W)"; exit 1; }; \
	done; done; echo "test-arm: ARM assembly byte-identical to the C build (12 cases)"

test: all
	$(OUT)/test_protocol
	$(OUT)/test_rx_ring
	$(OUT)/test_session
	$(OUT)/test_session_debug
	$(OUT)/test_spec_fmt
	$(OUT)/test_adc_cal
	$(OUT)/test_acal_track
	$(OUT)/test_cal_wizard
	$(OUT)/test_ui
	$(OUT)/test_touch_pick
	$(OUT)/test_phosphor
	$(OUT)/test_mmu
	$(OUT)/test_usbw
	$(OUT)/test_diag
	$(PYTHON) -I check_vectors.py --checker $(OUT)/vector_check --decim-checker $(OUT)/decim_check --adc-cal-checker $(OUT)/test_adc_cal --diag-checker $(OUT)/test_diag --vectors $(VECTORS)
	$(PYTHON) -I test_fft.py --cli $(OUT)/fft_cli --tables $(SRC)/fx_fft_tables.c --gen ../../tools/gen_fft_tables.py
	@if command -v qemu-arm >/dev/null && command -v arm-none-eabi-gcc >/dev/null && [ -x $(ARM_CC10) ]; then \
	  $(MAKE) --no-print-directory test-arm; else echo "test-arm skipped (needs qemu-arm, arm-none-eabi-gcc + newlib, GCC 10)"; fi

vectors:
	$(PYTHON) -I gen_vectors.py -o $(VECTORS)

clean:
	rm -rf $(OUT)
