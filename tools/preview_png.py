#!/usr/bin/env python3
"""Convert NOVA's raw BGRA framebuffer dump to PNG without third-party deps."""
import struct
import sys
import zlib

W, H = 720, 1280


def write_png(path, rgb_rows):
    def chunk(tag, data):
        out = struct.pack(">I", len(data)) + tag + data
        return out + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + row for row in rgb_rows)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "build/preview.raw"
    dst = sys.argv[2] if len(sys.argv) > 2 else "build/preview.png"

    data = open(src, "rb").read()
    if len(data) != W * H * 4:
        raise SystemExit(f"unexpected framebuffer size {len(data)}")

    rows = []
    for y in range(H):
        row = bytearray()
        base = y * W * 4
        for x in range(W):
            b, g, r, _a = data[base + x * 4: base + x * 4 + 4]
            row += bytes((r, g, b))
        rows.append(bytes(row))

    write_png(dst, rows)
    print(f"wrote {dst}")


if __name__ == "__main__":
    main()
