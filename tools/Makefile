# Host-side helpers for NOVA Boot.
ROOT    := ..
CC      ?= cc
CFLAGS  ?= -O2 -g -std=c11 -Wall -Wextra
CFLAGS  += -I$(ROOT)/include -I$(ROOT)/payload -I$(ROOT)/payload/core \
           -I$(ROOT)/payload/ui -I$(ROOT)/payload/platform

CORE := $(ROOT)/payload/core/nova_str.c $(ROOT)/payload/core/nova_ini.c \
        $(ROOT)/payload/core/nova_detect.c $(ROOT)/payload/core/nova_hekate.c

.PHONY: all preview verify clean

all: preview

$(ROOT)/build/ui_preview: ui_preview.c $(ROOT)/payload/ui/ui.c $(CORE)
	@mkdir -p $(ROOT)/build
	$(CC) $(CFLAGS) -o $@ ui_preview.c $(ROOT)/payload/ui/ui.c $(CORE)

preview: $(ROOT)/build/ui_preview
	@$(ROOT)/build/ui_preview 0 0 $(ROOT)/build/preview_full.raw
	@python3 preview_png.py $(ROOT)/build/preview_full.raw $(ROOT)/build/preview_full.png
	@$(ROOT)/build/ui_preview 1 1 $(ROOT)/build/preview_simple.raw
	@python3 preview_png.py $(ROOT)/build/preview_simple.raw $(ROOT)/build/preview_simple.png

verify:
	@python3 verify_payload.py $(ROOT)/payload/build/NOVA.bin $(ROOT)/payload/build/NOVA.elf

clean:
	@rm -rf $(ROOT)/build
