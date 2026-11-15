#!/usr/bin/env python3
"""Verify a Nova-Installer NRO built by devkitPro's elf2nro.

Checks the NroStart/NroHeader layout used by Atmosphère's hbmenu, the asset
section (icon + NACP), and that the embedded NOVA payload is byte-identical
to the payload built in the Nova-boot branch.

Usage: verify_nro.py Nova-Installer.nro [--payload data/NOVA.bin]
"""
import argparse
import hashlib
import struct
import sys

NRO_START_SIZE = 0x10
NROHEADER_SIZE = 0x70
NACP_SIZE = 0x4000


def fail(message):
    print(f"FAIL: {message}")
    return 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("nro")
    parser.add_argument("--payload", default="data/NOVA.bin")
    args = parser.parse_args()

    data = open(args.nro, "rb").read()
    payload = open(args.payload, "rb").read()

    if len(data) < NRO_START_SIZE + NROHEADER_SIZE:
        return fail("file too small to be an NRO")

    if data[NRO_START_SIZE:NRO_START_SIZE + 4] != b"NRO0":
        return fail("NRO0 signature missing at offset 0x10")

    version, total_size = struct.unpack_from("<II", data, NRO_START_SIZE + 4)
    if version != 0:
        return fail(f"unexpected NRO version {version}")
    if total_size > len(data):
        return fail("NRO header size larger than the file")

    flags, = struct.unpack_from("<I", data, NRO_START_SIZE + 0xC)
    text_off, text_size = struct.unpack_from("<II", data, NRO_START_SIZE + 0x10)
    ro_off, ro_size = struct.unpack_from("<II", data, NRO_START_SIZE + 0x18)
    data_off, data_size = struct.unpack_from("<II", data, NRO_START_SIZE + 0x20)
    bss_size, = struct.unpack_from("<I", data, NRO_START_SIZE + 0x28)

    if text_off + text_size > total_size or ro_off + ro_size > total_size:
        return fail("segments extend past the NRO code size")
    if data_off + data_size > total_size:
        return fail("data segment extends past the NRO code size")

    # Entry point must be a branch into the code, not metadata.
    branch, = struct.unpack_from("<I", data, 0)
    if branch >> 26 != 0b000101:
        return fail("entrypoint is not an ARM64 branch instruction")

    if len(data) <= total_size:
        return fail("no asset section after the NRO code")

    asset_off = total_size
    if data[asset_off:asset_off + 4] != b"ASET":
        return fail("ASET signature missing after the NRO code")

    asset_version, = struct.unpack_from("<I", data, asset_off + 4)
    if asset_version != 0:
        return fail(f"unexpected asset version {asset_version}")

    icon_off, icon_size, nacp_off, nacp_size, romfs_off, romfs_size = struct.unpack_from(
        "<QQQQQQ", data, asset_off + 8)

    if icon_size == 0 or asset_off + icon_off + icon_size > len(data):
        return fail("icon asset missing or out of bounds")
    if nacp_size != NACP_SIZE or asset_off + nacp_off + nacp_size > len(data):
        return fail("NACP asset missing or wrong size")
    if romfs_size != 0:
        return fail("unexpected RomFS in the installer NRO")

    if payload not in data:
        return fail("embedded NOVA payload not found verbatim in the NRO")
    if b"NOVA " + b"1.0.0" not in payload:
        return fail("payload version marker missing")

    sha = hashlib.sha256(payload).hexdigest()
    print(f"OK: {args.nro}")
    print(f"  code size      {total_size} bytes")
    print(f"  file size      {len(data)} bytes")
    print(f"  .text          {text_off:#x} + {text_size:#x}")
    print(f"  .rodata        {ro_off:#x} + {ro_size:#x}")
    print(f"  .data          {data_off:#x} + {data_size:#x}")
    print(f"  .bss           {bss_size:#x}")
    print(f"  flags          {flags:#x}")
    print(f"  icon           {icon_size} bytes")
    print(f"  NACP           {nacp_size} bytes")
    print(f"  payload        {len(payload)} bytes sha256 {sha}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
