#!/usr/bin/env python3
"""Build the NOVA release archive.

Layout inside NOVA-v1.0.0.zip (extract to the SD card root):

    Nova/NOVA.bin
    Nova/version.txt
    Nova/config/nova.ini
    Nova/themes/README.txt
    Nova/README.txt
    switch/Nova-Installer.nro
    hekate_ctcaer_<version>.bin

Usage:
    release.py --installer Nova-Installer.nro --payload data/NOVA.bin \
               --hekate release/hekate_ctcaer_6.5.5.bin --out dist/NOVA-v1.0.0.zip
"""
import argparse
import hashlib
import os
import re
import struct
import sys
import zipfile

VERSION = "1.0.0"
HEKATE_MAGIC = b"ICTC"


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def check_nro(data):
    if data[0x10:0x14] != b"NRO0":
        raise SystemExit("installer is not a valid NRO (missing NRO0 signature)")
    return struct.unpack_from("<I", data, 0x18)[0]


def check_payload(data):
    if len(data) < 0x400 or len(data) > 0x30000:
        raise SystemExit("payload size is outside the hekate limit")
    cond = struct.unpack_from("<I", data, 0)[0] >> 28
    if cond not in (0xE, 0xA):
        raise SystemExit("payload does not contain ARM32 instructions")
    if b"NOVA " + VERSION.encode() not in data:
        raise SystemExit("payload was not built for this release version")
    return len(data)


def check_hekate(data):
    if len(data) < 0x200 or len(data) > 0x30000:
        raise SystemExit("hekate payload has an unexpected size")
    if data[0x118:0x11C] != HEKATE_MAGIC:
        raise SystemExit("hekate payload is missing its ICTC metadata magic")
    return len(data)


def build_zip(args):
    nro = open(args.installer, "rb").read()
    payload = open(args.payload, "rb").read()
    hekate = open(args.hekate, "rb").read()

    nro_size = check_nro(nro)
    check_payload(payload)
    check_hekate(hekate)

    hekate_name = os.path.basename(args.hekate)
    if not re.match(r"^hekate_ctcaer_[0-9]+\.[0-9]+\.[0-9]+\.bin$", hekate_name):
        raise SystemExit(f"unexpected hekate file name: {hekate_name}")

    files = {
        "Nova/NOVA.bin": payload,
        "Nova/version.txt": (VERSION + "\n").encode(),
        "Nova/config/nova.ini": (
            "; NOVA boot configuration\n"
            f"[nova]\nversion={VERSION}\ntheme=aurora\n"
        ).encode(),
        "Nova/themes/README.txt": (
            "NOVA 1.0.0 uses the built-in \"aurora\" theme (pure black background,\n"
            "blue and purple accents). No external theme assets are required.\n"
        ).encode(),
        "Nova/README.txt": (
            "NOVA - custom boot system for Nintendo Switch\n"
            f"Version {VERSION}\n\n"
            "NOVA.bin is a real Tegra boot payload. Load it from hekate\n"
            "(bootloader/payloads/NOVA.bin or the NOVA entry created by the\n"
            "installer). The installer writes the same file to both locations\n"
            "and validates every copy with SHA-256.\n\n"
            "NOVA never writes BOOT0/BOOT1, the NAND, atmosphere/ or emummc/,\n"
            "and never edits bootloader/hekate_ipl.ini.\n"
        ).encode(),
        "switch/Nova-Installer.nro": nro,
        hekate_name: hekate,
    }

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    with zipfile.ZipFile(args.out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for name in sorted(files):
            info = zipfile.ZipInfo(name)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            zf.writestr(info, files[name])

    print(f"wrote {args.out}")
    print(f"  installer       {len(nro)} bytes (code {nro_size}) sha256 {sha256(nro)}")
    print(f"  payload         {len(payload)} bytes sha256 {sha256(payload)}")
    print(f"  hekate          {len(hekate)} bytes sha256 {sha256(hekate)}")
    for name in sorted(files):
        print(f"  {name:<34} {len(files[name]):>8} bytes")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--installer", default="Nova-Installer.nro")
    parser.add_argument("--payload", default="data/NOVA.bin")
    parser.add_argument("--hekate", required=True)
    parser.add_argument("--out", default=f"dist/NOVA-v{VERSION}.zip")
    args = parser.parse_args()

    for path in (args.installer, args.payload, args.hekate):
        if not os.path.isfile(path):
            raise SystemExit(f"missing input: {path}")

    build_zip(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
