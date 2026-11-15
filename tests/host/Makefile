# Host tests for the NOVA platform independent core.
ROOT    := ../..
CORE    := $(ROOT)/payload/core
INCLUDE := $(ROOT)/include

CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -Wall -Wextra -Werror -I$(INCLUDE) -I$(CORE) -I.
LDFLAGS +=

SRCS := test_core.c nova_posix_fs.c \
        $(CORE)/nova_str.c $(CORE)/nova_ini.c $(CORE)/nova_detect.c $(CORE)/nova_hekate.c
OBJS := $(patsubst %,$(ROOT)/build/host/%.o,$(notdir $(SRCS)))

BIN := $(ROOT)/build/host/test_core

.PHONY: all run clean

all: $(BIN)

$(BIN): $(SRCS) | dirs
	@mkdir -p $(ROOT)/build/host
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

dirs:
	@mkdir -p $(ROOT)/build/host

run: $(BIN)
	@cd $(ROOT) && ./build/host/test_core

clean:
	@rm -rf $(ROOT)/build/host
