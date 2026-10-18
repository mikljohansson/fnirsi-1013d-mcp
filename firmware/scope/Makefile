# FNIRSI 1013D scope firmware (base: Atlan4 v1.00o5), plain Makefile.
#
# Reproduces the configuration Atlan4 actually builds and ships, NetBeans
# "Debug" (nbproject/Makefile-Debug.mk; the "Release" config there is a stale
# STM32/cortex-m3 leftover and cannot build this code):
#   compile  arm-none-eabi-gcc $(CFLAGS) -g -c
#   link     arm-none-eabi-gcc -o elf $(OBJS) -T./fnirsi_1013d.ld -nostdlib -lgcc
#   post     objcopy -O binary elf bin ; ./mksunxi bin
# The upstream post-build copies to Atlan's own SD-card mount points and runs
# sudo sunxi-fel; those steps are not reproduced (see `make fel`/`package`).
#
#   make                build/fnirsi_1013d.elf, build/fnirsi_1013d.bin
#   make package        build/fnirsi_1013d_sdpackage.bin (scope overlaid on a
#                       bootloader+updater base at 0x5BC00 = sector 750)
#   make fel            load build/fnirsi_1013d.bin into DRAM over FEL
#   make check-reference  rebuild with Atlan's GCC 10.3.1 and compare with
#                       ../reference/fnirsi_1013d_v1.00o5_atlan.bin
#   make clean
#   make V=1            show full commands
#   make SOURCE_DATE_EPOCH=<unix time>   reproducible build_time.h (UTC)
#   make CROSS_COMPILE=/opt/gcc-arm-10.3/bin/arm-none-eabi-   other toolchain
#   make PC_PROTOCOL=0  build without the USB control protocol (= Atlan's v1.00o5)
#   make PC_DEBUG=1     protocol bench build: DEBUG_FPGA and FORCE enabled
#   make gcc10          PC_PROTOCOL build with Atlan's GCC 10.3.1 (build-gcc10-pc/)

CROSS_COMPILE ?= arm-none-eabi-
CC      := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
OBJDUMP := $(CROSS_COMPILE)objdump
SIZE    := $(CROSS_COMPILE)size
AR      := $(CROSS_COMPILE)ar
PYTHON  ?= python3

TOOLS   := ../tools
BUILD   ?= build
OBJDIR  := $(BUILD)/obj
TARGET  := fnirsi_1013d

ELF     := $(BUILD)/$(TARGET).elf
RAWBIN  := $(BUILD)/$(TARGET)_raw.bin
BIN     := $(BUILD)/$(TARGET).bin
MAP     := $(BUILD)/$(TARGET).map
PKG     := $(BUILD)/$(TARGET)_sdpackage.bin

# Base image for `make package`: Atlan's SD package (bootloader v0.8 at +0,
# updater v0.02v at +0x8000). Same as upstream overlaying dist/.../fnirsi_1013d.bin.
PACKAGE_BASE     ?= ../reference/fnirsi_1013d_v1.00o5_atlan_sdpackage.bin
PACKAGE_LOCATION := 0x5BC00

# Link order = nbproject/Makefile-Debug.mk OBJECTFILES order. It determines
# the memory layout, keep it. Not built upstream (and not here): font_6.c,
# font_7.c, font_8.c, malloc.c.
SRCS := \
	DS3231.c PC_interface.c ccu_control.c cdc_class.c diskio.c \
	display_control.c display_lib.c ff.c ffunicode.c fnirsi_1013d_scope.c \
	font_0.c font_2.c font_3.c font_4.c font_5.c fpga_control.c generator.c \
	icons.c interrupt.c mass_storage_class.c memcmp.s memcpy.s memmove.s \
	memset.s menu.c power_and_battery.c ref_and_math.c scope_functions.c \
	sd_card_interface.c sin_cos_math.c spi_control.c start.s statemachine.c \
	test.c timer.c touchpanel.c usb_interface.c variables.c

# USB control protocol (docs/protocol.md, firmware/README.md). Linked after the
# upstream objects so the upstream part of the layout keeps its order.
#   PC_PROTOCOL=0  hooks compile to nothing, image identical to upstream
#   PC_DEBUG=1     DEBUG_FPGA + FORCE (bench builds only)
#   PC_FORCE_CDC=1 CDC at boot even when the saved USB setting says MSC
#   PC_FIFO_MAP=n  USB endpoint FIFO layout (pc_transport.h), 0 default, 1 OUT FIFO at 1536
#   PC_USB_TICK_FALLBACK=0  no USB lost-interrupt fallback in the 1 ms tick (version suffix -nofb; A/B for the
#                  self-restart, firmware/AGENTS.md section 9)
PC_PROTOCOL  ?= 1
PC_DEBUG     ?= 0
PC_FORCE_CDC ?= 1
PC_FIFO_MAP  ?= 0
PC_USB_TICK_FALLBACK ?= 1
ifeq ($(PC_PROTOCOL),1)
SRCS += pc_protocol.c pc_cmds.c pc_transport.c pc_hal_scope.c
PC_FW_BUILD := $(shell git rev-parse --short=8 HEAD 2>/dev/null)
ifeq ($(PC_FW_BUILD),)
PC_FW_BUILD := 00000000
endif
PC_DEFS := -DPC_PROTOCOL=1 -DPC_DEBUG=$(PC_DEBUG) -DPC_FORCE_CDC=$(PC_FORCE_CDC) -DPC_FW_BUILD=0x$(PC_FW_BUILD)u -DPC_FIFO_MAP=$(PC_FIFO_MAP) -DPC_USB_TICK_FALLBACK=$(PC_USB_TICK_FALLBACK)
else
PC_DEFS := -DPC_PROTOCOL=0
endif

OBJS := $(addprefix $(OBJDIR)/,$(addsuffix .o,$(basename $(SRCS))))
DEPS := $(OBJS:.o=.d)

# Not in upstream's link. Archived so a member is only linked when referenced:
# GCC >= 12 emits strlen calls for counting loops (GCC 10.3 used by Atlan
# does not, so an upstream-toolchain build stays identical).
SUPPORT_SRCS := strlen.s strcpy.s
SUPPORT_LIB  := $(BUILD)/libsupport.a
SUPPORT_OBJS := $(addprefix $(OBJDIR)/,$(addsuffix .o,$(basename $(SUPPORT_SRCS))))

# Flags exactly as upstream Debug config (CFLAGS + per-file -g; ASFLAGS + -g)
CFLAGS  := -Wall -Wno-write-strings -Wno-char-subscripts -fno-stack-protector \
           -DNO_STDLIB=1 -mcpu=arm926ej-s -O3 -mfloat-abi=soft -g
ASFLAGS := -x assembler-with-cpp -c -O0 -mcpu=arm926ej-s -mthumb -Wall \
           -fmessage-length=0 -g
# build_time.h is generated into $(BUILD) (not the source dir)
CPPFLAGS := -I$(BUILD) $(PC_DEFS)
LDFLAGS := -T fnirsi_1013d.ld -nostdlib -Wl,-Map=$(MAP)
LDLIBS  := $(SUPPORT_LIB) -lgcc

# Extra user flags without touching the upstream set
CFLAGS  += $(EXTRA_CFLAGS)

ifeq ($(V),1)
Q :=
else
Q := @
endif

.PHONY: all clean package fel info check-reference gcc10 gcc10-nofb FORCE
all: $(BIN)

# ----------------------------------------------------------------- build_time.h
# Upstream regenerates build_time.h (local time) on every make. Same here,
# unless SOURCE_DATE_EPOCH is set (then UTC of that time, reproducible).
# The file is only replaced when its content changes.
ifdef SOURCE_DATE_EPOCH
DATE := date -u -d @$(SOURCE_DATE_EPOCH)
else
DATE := date
endif

$(BUILD)/build_time.h: FORCE
	@mkdir -p $(BUILD)
	@{ echo "#ifndef BUILD_TIME_H"; echo "#define BUILD_TIME_H"; \
	   $(DATE) '+#define COMPILE_YEAR %y%n#define COMPILE_MONTH %-m%n#define COMPILE_DAY %-d%n#define COMPILE_HOUR %-H%n#define COMPILE_MIN %-M%n#define COMPILE_SEC %-S'; \
	   echo "#endif"; } > $@.tmp
	@if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv -f $@.tmp $@; echo "  GEN     $@"; fi

$(OBJDIR)/variables.o: $(BUILD)/build_time.h

# ----------------------------------------------------------------- compile/link
$(OBJDIR)/%.o: %.c | $(OBJDIR)
	@$(if $(Q),echo "  CC      $<")
	$(Q)$(CC) $(CFLAGS) $(CPPFLAGS) -c -MMD -MP -MF $(@:.o=.d) -o $@ $<

$(OBJDIR)/%.o: %.s | $(OBJDIR)
	@$(if $(Q),echo "  AS      $<")
	$(Q)$(CC) $(ASFLAGS) $(PC_DEFS) -o $@ $<

$(OBJDIR):
	$(Q)mkdir -p $@

$(SUPPORT_LIB): $(SUPPORT_OBJS)
	@$(if $(Q),echo "  AR      $@")
	$(Q)rm -f $@ && $(AR) rcs $@ $^

$(ELF): $(OBJS) $(SUPPORT_LIB) fnirsi_1013d.ld
	@$(if $(Q),echo "  LD      $@")
	$(Q)$(CC) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)

$(RAWBIN): $(ELF)
	@$(if $(Q),echo "  OBJCOPY $@")
	$(Q)$(OBJCOPY) -O binary $< $@

# mksunxi: pad to 512, round header length, eGON checksum (in place upstream)
$(BIN): $(RAWBIN) $(TOOLS)/mksunxi.py
	@$(if $(Q),echo "  MKSUNXI $@")
	$(Q)$(PYTHON) $(TOOLS)/mksunxi.py -q $< -o $@
	$(Q)$(SIZE) $(ELF)
	@ls -l $@ | awk '{print "  image   " $$NF ": " $$5 " bytes"}'

# Upstream: ./flashfilepacker -o dist/.../fnirsi_1013d.bin -i ..._scope.bin -l 0x5BC00
$(PKG): $(BIN) $(PACKAGE_BASE) $(TOOLS)/make_sd_image.py
	@$(if $(Q),echo "  PACK    $@")
	$(Q)cp $(PACKAGE_BASE) $@
	$(Q)$(PYTHON) $(TOOLS)/make_sd_image.py pack -o $@ -i $(BIN) -l $(PACKAGE_LOCATION)

package: $(PKG)

# Needs the scope in FEL mode with DRAM initialised (boot mode 2 / menu "FEL").
# Needs USB access to 1f3a:efe8: run as root or `make fel SUNXI_FEL="sudo sunxi-fel"`.
SUNXI_FEL ?= sunxi-fel
fel: $(BIN)
	$(SUNXI_FEL) -p write 0x7FFFFFE0 $(BIN) exe 0x80000000

info: $(ELF)
	$(OBJDUMP) -h $(ELF)

# Rebuild with Atlan's exact toolchain (GCC 10.3.1, see ../tools/fetch_gcc10.sh)
# and Atlan's build_time.h (2026-02-13 18:59:33), compare with the reference.
# Always PC_PROTOCOL=0: proves the protocol hooks leave the base untouched.
GCC10      ?= $(HOME)/opt/gcc10
REF_EPOCH  := 1771009173
REF_BIN    := ../reference/fnirsi_1013d_v1.00o5_atlan.bin
check-reference:
	$(MAKE) BUILD=build-gcc10 CROSS_COMPILE=$(GCC10)/usr/bin/arm-none-eabi- 	  EXTRA_CFLAGS="-isystem $(GCC10)/usr/include/newlib" SOURCE_DATE_EPOCH=$(REF_EPOCH) PC_PROTOCOL=0
	cmp build-gcc10/$(TARGET).bin $(REF_BIN) && echo "IDENTICAL to $(REF_BIN)"

# The protocol build with Atlan's compiler (warning check / conservative image)
gcc10:
	$(MAKE) BUILD=build-gcc10-pc CROSS_COMPILE=$(GCC10)/usr/bin/arm-none-eabi- 	  EXTRA_CFLAGS="-isystem $(GCC10)/usr/include/newlib" PC_PROTOCOL=1

# Same, without the USB tick fallback (version -nofb), for the self-restart A/B
gcc10-nofb:
	$(MAKE) BUILD=build-gcc10-pc-nofb CROSS_COMPILE=$(GCC10)/usr/bin/arm-none-eabi- 	  EXTRA_CFLAGS="-isystem $(GCC10)/usr/include/newlib" PC_PROTOCOL=1 PC_USB_TICK_FALLBACK=0

clean:
	rm -rf $(BUILD) build-gcc10 build-gcc10-pc build-gcc10-pc-nofb

FORCE:

-include $(DEPS)
