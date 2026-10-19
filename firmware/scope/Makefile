# FNIRSI 1013D scope firmware (base: Atlan4 v1.00o5), plain Makefile.
#
# Reproduces the configuration Atlan4 actually builds and ships, NetBeans
# "Debug" (nbproject/Makefile-Debug.mk; the "Release" config there is a stale
# STM32/cortex-m3 leftover and cannot build this code):
#   compile  arm-none-eabi-gcc $(CFLAGS) -g -c
#   link     arm-none-eabi-gcc -o elf $(OBJS) -T./fnirsi_1013d.ld -nostdlib -lgcc
#   post     objcopy -O binary elf bin ; ./mksunxi bin
# The upstream post-build copies to Atlan's own SD-card mount points and runs
# sudo sunxi-fel; those steps are not reproduced (see `make fel`; SD packages:
# tools/make_release_sd_package.py).
#
#   make                build/fnirsi_1013d.elf, build/fnirsi_1013d.bin
#   make fel            load build/fnirsi_1013d.bin into DRAM over FEL
#   make clean
#   make V=1            show full commands
#   make PC_FW_BUILD=<8 hex> SOURCE_DATE_EPOCH=<unix time>   identity without git
#   make CROSS_COMPILE=/opt/gcc-arm-10.3/bin/arm-none-eabi-   other toolchain
#   make PC_DEBUG=1     protocol bench build: DEBUG_FPGA and FORCE enabled
#   make gcc10          the release image, Atlan's GCC 10.3.1 (build-gcc10-pc/)
#   make gcc10-nommu    the same with the MMU left off (fw 0.2.48 A/B, build-gcc10-nommu/)

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
	test.c timer.c touchpanel.c usb_interface.c variables.c adc_cal.c spec_fmt.c

# USB control protocol and the views built on it (docs/protocol.md, firmware/README.md),
# linked after the upstream objects. Options:
#   PC_DEBUG=1     DEBUG_FPGA + FORCE (bench builds only)
#   PC_FORCE_CDC=1 CDC at boot even when the saved USB setting says MSC
#   PC_FIFO_MAP=n  USB endpoint FIFO layout (pc_transport.h), 0 default, 1 OUT FIFO at 1536
#   PC_MMU=0       fw 0.2.48: leave the MMU off (D-cache ineffective, the pre-0.2.48 state), version suffix -nommu
#   PC_MMU_SRAM=0  map SRAM A strongly ordered instead of write-back (FFT_BENCH A/B)
PC_DEBUG     ?= 0
PC_FORCE_CDC ?= 1
PC_FIFO_MAP  ?= 0
PC_MMU       ?= 1
PC_MMU_SRAM  ?= 1
SRCS += pc_protocol.c pc_cmds.c pc_transport.c pc_hal_scope.c fx_fft.c fx_fft_tables.c spectrum.c pc_decim.c phos_map.c phosphor.c pc_mmu.c pc_usbw.c
# Calibration wizard: the steps (pure C, host_tests/test_cal_wizard.c) and the view with its measurements
SRCS += cal_wiz.c cal_wiz_ui.c
# Calibration v4: interleave tracking and the class probe (pure maths in acal_track.c, host_tests/test_acal_track.c)
SRCS += acal_track.c acal_live.c
# Screens (fw 0.2.51): card formatting, main menu rows and mode rule (pure C, host_tests/test_ui.c)
# fw 0.2.55: the Settings fold-out and screen tables (ui_settings.c, pure C, host_tests/test_ui.c)
SRCS += ui_fmt.c ui_menu.c ui_settings.c
# Last: the identity data (build_id.c), so its bytes are the only ones a new commit changes.
SRCS += build_id.c
PC_DEFS := -DPC_DEBUG=$(PC_DEBUG) -DPC_FORCE_CDC=$(PC_FORCE_CDC) -DPC_FIFO_MAP=$(PC_FIFO_MAP) \
           -DPC_MMU=$(PC_MMU) -DPC_MMU_SRAM=$(PC_MMU_SRAM)

# ----------------------------------------------------------------- identity
# The image depends only on the source, the toolchain and the commit: the identity is the commit hash
# (PC_FW_BUILD) and the commit time (SOURCE_DATE_EPOCH, also the RTC default), both read from git, never the
# wall clock or the build directory. In a worktree whose .git file holds a Windows path (D:/...), WSL's git
# cannot follow it, so the gitdir is translated to /mnt/d/... . Without git pass both values by hand.
GIT_WSLDIR := $(shell sed -n 's|^gitdir: \([A-Za-z]\):/\(.*\)|/mnt/\L\1\E/\2|p' ../../.git 2>/dev/null)
GIT := git$(if $(wildcard $(GIT_WSLDIR)), --git-dir=$(GIT_WSLDIR))
ifeq ($(origin PC_FW_BUILD),undefined)
PC_FW_BUILD := $(shell $(GIT) rev-parse --short=8 HEAD 2>/dev/null)
endif
ifeq ($(origin SOURCE_DATE_EPOCH),undefined)
SOURCE_DATE_EPOCH := $(shell $(GIT) log -1 --format=%ct 2>/dev/null)
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
# The ELF's debug info names the source directory: map it to "." so the ELF does not depend on it either
CFLAGS  += -ffile-prefix-map=$(CURDIR)=.
ASFLAGS += -ffile-prefix-map=$(CURDIR)=. -Wa,--debug-prefix-map=$(CURDIR)=.
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

.PHONY: all clean fel info gcc10 gcc10-nommu FORCE
all: $(BIN)

# ----------------------------------------------------------------- build_time.h
# Commit time (UTC) and hash for variables.c (RTC defaults) and build_id.c. Upstream used the local wall clock.
# The file is only replaced when its content changes, so a rebuild of the same commit recompiles nothing.
$(BUILD)/build_time.h: FORCE
	@test -n "$(PC_FW_BUILD)" -a -n "$(SOURCE_DATE_EPOCH)" || { echo "error: git cannot be read here;" \
	   "pass PC_FW_BUILD=<git rev-parse --short=8 HEAD> SOURCE_DATE_EPOCH=<git log -1 --format=%ct>" >&2; exit 1; }
	@mkdir -p $(BUILD)
	@{ echo "#ifndef BUILD_TIME_H"; echo "#define BUILD_TIME_H"; \
	   date -u -d @$(SOURCE_DATE_EPOCH) '+#define COMPILE_YEAR %y%n#define COMPILE_MONTH %-m%n#define COMPILE_DAY %-d%n#define COMPILE_HOUR %-H%n#define COMPILE_MIN %-M%n#define COMPILE_SEC %-S'; \
	   echo "#define PC_FW_BUILD 0x$(PC_FW_BUILD)u"; \
	   echo "#endif"; } > $@.tmp
	@if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv -f $@.tmp $@; echo "  GEN     $@ (git $(PC_FW_BUILD), $$(date -u -d @$(SOURCE_DATE_EPOCH) '+%F %T') UTC)"; fi

$(OBJDIR)/variables.o $(OBJDIR)/build_id.o: $(BUILD)/build_time.h

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

# Needs the scope in FEL mode with DRAM initialised (boot mode 2 / menu "FEL").
# Needs USB access to 1f3a:efe8: run as root or `make fel SUNXI_FEL="sudo sunxi-fel"`.
SUNXI_FEL ?= sunxi-fel
fel: $(BIN)
	$(SUNXI_FEL) -p write 0x7FFFFFE0 $(BIN) exe 0x80000000

info: $(ELF)
	$(OBJDUMP) -h $(ELF)

# The release image: Atlan's compiler, GCC 10.3.1 (../tools/fetch_gcc10.sh)
GCC10 ?= $(HOME)/opt/gcc10
gcc10:
	$(MAKE) BUILD=build-gcc10-pc CROSS_COMPILE=$(GCC10)/usr/bin/arm-none-eabi- 	  EXTRA_CFLAGS="-isystem $(GCC10)/usr/include/newlib"

# fw 0.2.48 A/B: the same with the MMU left off (version suffix -nommu)
gcc10-nommu:
	$(MAKE) BUILD=build-gcc10-nommu CROSS_COMPILE=$(GCC10)/usr/bin/arm-none-eabi- 	  EXTRA_CFLAGS="-isystem $(GCC10)/usr/include/newlib" PC_MMU=0

clean:
	rm -rf $(BUILD) build-gcc10-pc build-gcc10-nommu

FORCE:

-include $(DEPS)
