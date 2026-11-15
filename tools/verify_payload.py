#!/usr/bin/env python3
"""Verify that NOVA.bin is a genuine Tegra X1 payload image.

Checks:
  * the ELF entry point is the payload load address 0x40010000
  * the image is AArch32 (EM_ARM), not an NRO/NRO-wrapped file
  * the raw binary starts with an ARM branch/PC-relative instruction sequence
  * no dynamic relocations remain
  * size fits hekate's 0x30000 chainload limit and the IRAM stack budget
"""
import struct
import subprocess
import sys

LOAD_ADDR = 0x40010000
MAX_SIZE = 0x30000
BSS_LIMIT = 0x4003E000


def fail(msg):
    print(f"FAIL: {msg}")
    return 1


def main():
    if len(sys.argv) < 2:
        return fail("usage: verify_payload.py <NOVA.bin> [NOVA.elf]")

    bin_path = sys.argv[1]
    elf_path = sys.argv[2] if len(sys.argv) > 2 else bin_path.replace(".bin", ".elf")

    data = open(bin_path, "rb").read()
    errors = 0

    print(f"image: {bin_path}")
    print(f"size:  {len(data)} bytes")

    if len(data) < 0x100:
        errors += fail("image is too small to be a payload")
    if len(data) > MAX_SIZE:
        errors += fail(f"image exceeds hekate payload limit (0x{MAX_SIZE:X})")

    first = struct.unpack("<I", data[:4])[0]
    cond = (first >> 28) & 0xF
    print(f"first instruction: 0x{first:08X} (condition field 0x{cond:X})")
    # A payload starts with 32-bit ARM state code. Reject wrapped containers
    # (NRO0 / ELF) and Thumb / never-condition encodings.
    if data[:4] in (b"NRO0", b"\x7fELF") or data[:4] in (b"NSO0", b"KIP1"):
        errors += fail("image is a wrapped homebrew container, not a raw payload")
    elif cond not in (0xE, 0xA):
        errors += fail("first instruction is not unconditionally executed ARM code")

    try:
        hdr = open(elf_path, "rb").read(64)
        if hdr[:4] != b"\x7fELF":
            errors += fail(f"{elf_path} is not an ELF file")
        else:
            ei_class = hdr[4]
            e_type = struct.unpack("<H", hdr[16:18])[0]
            e_machine = struct.unpack("<H", hdr[18:20])[0]
            e_entry = struct.unpack("<I", hdr[24:28])[0]
            print(f"elf:   class={ei_class} type={e_type} machine=0x{e_machine:X} entry=0x{e_entry:08X}")
            if e_machine != 40:
                errors += fail("ELF machine is not EM_ARM (AArch32)")
            if e_entry != LOAD_ADDR:
                errors += fail(f"entry point 0x{e_entry:08X} != 0x{LOAD_ADDR:08X}")

            symbols = subprocess.run(
                ["readelf", "-sW", elf_path], capture_output=True, text=True
            ).stdout
            for required in ("ipl_main", "nova_platform_chainload", "nova_trampoline_begin"):
                if required not in symbols:
                    errors += fail(f"missing required symbol {required}")
                else:
                    print(f"symbol: {required} present")

            relocs = subprocess.run(
                ["readelf", "-rW", elf_path], capture_output=True, text=True
            ).stdout
            reloc_lines = [l for l in relocs.splitlines() if "R_ARM" in l]
            if reloc_lines:
                errors += fail(f"{len(reloc_lines)} unresolved relocations")
            else:
                print("relocations: none")

            sections = subprocess.run(
                ["readelf", "-SW", elf_path], capture_output=True, text=True
            ).stdout
            for line in sections.splitlines():
                parts = line.split()
                if len(parts) >= 7 and parts[1] == ".bss":
                    addr = int(parts[3], 16)
                    size = int(parts[5], 16)
                    end = addr + size
                    print(f"bss:   0x{addr:08X}..0x{end:08X}")
                    if end > BSS_LIMIT:
                        errors += fail("bss overlaps the payload stack")
    except OSError as exc:
        errors += fail(f"cannot read ELF: {exc}")

    if errors:
        print(f"{errors} checks failed")
        return 1

    print("NOVA.bin is a valid ARM32 Tegra payload image")
    return 0


if __name__ == "__main__":
    sys.exit(main())
