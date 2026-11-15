CC ?= cc
CFLAGS ?= -std=gnu11 -O2 -Wall -Wextra -Werror
ROOT := ../..
INCLUDES := -I$(ROOT)/include -I$(ROOT)/source/core -I$(ROOT)/source/installer

CORE := $(ROOT)/source/core/nova_str.c $(ROOT)/source/core/nova_ini.c \
        $(ROOT)/source/core/nova_detect.c $(ROOT)/source/core/nova_hekate.c \
        $(ROOT)/source/core/nova_sha256.c

ENGINE := $(ROOT)/source/installer/installer.c $(ROOT)/source/installer/nova_fs.c \
          $(ROOT)/source/installer/autoboot.c

all: test_sha256 test_installer test_autoboot
	./test_sha256
	rm -rf sdmc:
	./test_installer
	rm -rf sdmc:
	./test_autoboot

test_sha256: test_sha256.c $(ROOT)/source/core/nova_sha256.c
	$(CC) $(CFLAGS) -I$(ROOT)/source/core -o $@ $^

test_installer: test_installer.c $(ENGINE) $(CORE)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^

test_autoboot: test_autoboot.c $(ENGINE) $(CORE)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^

clean:
	rm -rf test_sha256 test_installer test_autoboot sdmc:

.PHONY: all clean
