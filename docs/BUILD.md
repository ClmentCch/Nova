# Building NOVA Boot

## Requirements

* **devkitARM** from devkitPro (32-bit ARM bare-metal toolchain, GCC 15).
* A **hekate** checkout at the pinned tag (`v6.5.5`) for the BDK drivers.
* `make`, `python3` (verifier and UI preview).
* `gcc` on the host for the unit tests.

devkitPro's package server is not reachable from every network. The supported
paths are the official Docker image and a normal devkitPro installation:

```bash
# Option A: official image (matches CI)
docker run --rm -it -v "$PWD":/src -w /src devkitpro/devkitarm:latest bash
# inside:
git clone --depth 1 --branch v6.5.5 https://github.com/CTCaer/hekate /opt/hekate
export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM
make host-test
make -C payload HEKATE=/opt/hekate verify
```

```bash
# Option B: local devkitPro installation
# https://devkitpro.org/wiki/devkitPro_pacman
dkp-pacman -S devkitARM
export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM

git clone --depth 1 --branch v6.5.5 https://github.com/CTCaer/hekate /tmp/hekate
make host-test
make -C payload HEKATE=/tmp/hekate verify
```

The Makefile can also clone hekate itself:

```bash
make -C payload            # clones .deps/hekate at HEKATE_TAG
make -C payload HEKATE_TAG=v6.5.5
```

## Targets

| Command | Result |
| --- | --- |
| `make host-test` | builds and runs the core unit tests (78 checks) |
| `make -C payload` | `payload/build/NOVA.bin` + `payload/build/NOVA.elf` |
| `make -C payload verify` | structural verification of the payload |
| `make -C tools preview` | renders the UI to `build/preview_full.png` |

## Payload verification

`tools/verify_payload.py` rejects:

* anything that is not a raw payload (`NRO0`, `ELF`, `NSO0`, `KIP1` containers);
* an ELF that is not `EM_ARM`;
* an entry point other than `0x40010000`;
* leftover relocations;
* images larger than hekate's `0x30000` chainload limit;
* a `.bss` that would overlap the payload stack.

## Installing the result

`NOVA.bin` is installed by **Nova Installer** to:

```
sdmc:/Nova/NOVA.bin
sdmc:/bootloader/payloads/NOVA.bin   (loadable from hekate's Payloads menu)
```

To boot NOVA automatically, inject it from RCM/modchip, or let hekate chainload
it as a payload entry. Injecting `NOVA.bin` directly is the intended path: it is
a first-stage payload linked at the standard address.
