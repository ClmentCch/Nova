# NOVA Boot - build entry points.
#
#   make host-test   build and run the platform independent core tests
#   make payload     cross compile NOVA.bin with devkitARM + hekate BDK
#   make all         host-test + payload
#   make clean

.PHONY: all host-test payload clean dist

all: host-test payload

host-test:
	@$(MAKE) -C tests/host run

payload:
	@$(MAKE) -C payload all

clean:
	@$(MAKE) -C tests/host clean
	@$(MAKE) -C payload clean
	@rm -rf build dist

# Convenience target: verify the payload is a real ARM payload image.
dist: payload
	@mkdir -p dist
	@cp payload/build/NOVA.bin dist/NOVA.bin
	@$(MAKE) -C tools verify
