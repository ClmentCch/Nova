# NOVA Boot - payload build.
#
# Produces build/NOVA.bin: a real Tegra X1 AArch32 payload linked at
# 0x40010000, built against the hekate BDK (display, SDMMC, HID, UART).
#
#   make            build NOVA.bin
#   make HEKATE=/path/to/hekate    use an existing hekate checkout
#   make clean
#
# Requires devkitARM. The hekate BDK is GPL-2.0; NOVA is distributed under
# the same license. The hekate checkout is pinned for reproducible builds.

DEVKITPRO ?= /opt/devkitpro
DEVKITARM ?= $(DEVKITPRO)/devkitARM
HEKATE    ?= .deps/hekate
HEKATE_TAG ?= v6.5.5

CC      := $(DEVKITARM)/bin/arm-none-eabi-gcc
OBJCOPY := $(DEVKITARM)/bin/arm-none-eabi-objcopy
SIZE    := $(DEVKITARM)/bin/arm-none-eabi-size

ROOT := ..
INCLUDES := -I$(ROOT)/include -I$(ROOT)/payload -I$(ROOT)/payload/core \
            -I$(ROOT)/payload/platform -I$(ROOT)/payload/ui -I$(HEKATE)/bdk

ARCH := -march=armv4t -mtune=arm7tdmi -mthumb -mthumb-interwork

BUILD_TAG := $(shell git -C $(ROOT) rev-parse --short HEAD 2>/dev/null || echo local)

CFLAGS := $(ARCH) -O2 -g -gdwarf-4 -std=gnu11 \
          -ffunction-sections -fdata-sections -fno-common -fno-pic \
          -fno-stack-protector -fomit-frame-pointer \
          -Wall -Wextra -Wno-unused-parameter -Wno-array-bounds \
          -Wno-stringop-overflow -Wno-address-of-packed-member \
          -DBDK_MALLOC_NO_DEFRAG -DBDK_EMUMMC_ENABLE \
          -DGFX_INC='"gfx_compat.h"' \
          -DBUILD_TAG='"$(BUILD_TAG)"' \
          $(INCLUDES)

LDFLAGS := $(ARCH) -nostartfiles -lgcc -Wl,--nmagic,--gc-sections -T link.ld

SOURCES := start.S \
           platform/trampoline.S \
           platform/platform.c \
           platform/gfx_compat.c \
           platform/diskio_sd.c \
           ui/ui.c \
           main.c \
           core/nova_str.c \
           core/nova_ini.c \
           core/nova_detect.c \
           core/nova_hekate.c

BDK_SOURCES := $(shell find $(HEKATE)/bdk -name '*.c' \
                 ! -path '*/usb/*' ! -path '*/libs/compr/*' 2>/dev/null)
BDK_ASM := $(HEKATE)/bdk/exception_handlers.S

OBJECTS := $(patsubst %,$(CURDIR)/build/obj/%.o,$(SOURCES)) \
           $(patsubst $(HEKATE)/%.c,$(CURDIR)/build/obj/hekate/%.o,$(BDK_SOURCES)) \
           $(CURDIR)/build/obj/hekate/bdk/exception_handlers.S.o

ELF := build/NOVA.elf
BIN := build/NOVA.bin

.PHONY: all clean hekate deps verify

all: $(BIN)

deps: $(HEKATE)/bdk/bdk.h

$(HEKATE)/bdk/bdk.h:
	@mkdir -p $(dir $(HEKATE))
	git clone --depth 1 --branch $(HEKATE_TAG) https://github.com/CTCaer/hekate.git $(HEKATE)

$(ELF): $(OBJECTS) link.ld
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@
	@$(SIZE) $@

$(BIN): $(ELF)
	@mkdir -p build
	@$(OBJCOPY) -S -O binary $(ELF) $(BIN)
	@SIZE=$$(wc -c < $(BIN)); \
	 echo "----------------------------------------"; \
	 echo "NOVA.bin: $$SIZE bytes"; \
	 if [ $$SIZE -gt 0x30000 ]; then echo "ERROR: payload exceeds hekate 0x30000 limit"; rm -f $(BIN); exit 1; fi; \
	 echo "----------------------------------------"

$(CURDIR)/build/obj/%.S.o: %.S $(HEKATE)/bdk/bdk.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(CURDIR)/build/obj/%.c.o: %.c $(HEKATE)/bdk/bdk.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(CURDIR)/build/obj/hekate/%.o: $(HEKATE)/%.c $(HEKATE)/bdk/bdk.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(CURDIR)/build/obj/hekate/%.S.o: $(HEKATE)/%.S $(HEKATE)/bdk/bdk.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

verify: $(BIN)
	@python3 ../tools/verify_payload.py $(BIN)

clean:
	@rm -rf build

-include $(shell find build/obj -name '*.d' 2>/dev/null)
