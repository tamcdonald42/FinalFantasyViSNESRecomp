#!/usr/bin/env python3
"""Compare the widescreen off/on frame series the way the game must be judged.

For every paired frame (same number in both directories):
  * off must be 256x224 -- the authentic baseline geometry
  * on  must be 384x224
  * the authentic 256 columns of the on frame, at x=64..319, must be
    pixel-identical to the off frame: widening may add columns, never change
    the ones the cartridge would have drawn
  * margins are counted, not judged: a game whose field renderer fills only a
    256-pixel tilemap window leaves them black, and this reports that rather
    than pretending otherwise

Usage: python3 ws_cmp.py [off_dir] [on_dir]
Defaults to C:/Temp/ws/off/frames and C:/Temp/ws/on/frames, which is where
tools/oracle/ws_verify.sh writes them. Exit 0 = every pair matched.
"""
import struct
import sys
from pathlib import Path

OFF_DIR = Path(sys.argv[1] if len(sys.argv) > 1 else "/mnt/c/Temp/ws/off/frames")
ON_DIR = Path(sys.argv[2] if len(sys.argv) > 2 else "/mnt/c/Temp/ws/on/frames")
EXTRA = 64  # FF6_WS_EXTRA in src/qol_widescreen.c


def read_bmp(path):
    """-> (width, height, rows, bytes_per_pixel), rows[0] = top scanline."""
    data = Path(path).read_bytes()
    if data[:2] != b"BM":
        raise ValueError(f"not a BMP: {path}")
    off = struct.unpack_from("<I", data, 10)[0]
    w, h = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    top_down = h < 0
    h = abs(h)
    stride = ((w * bpp + 31) // 32) * 4
    px = bpp // 8
    rows = []
    for y in range(h):
        sy = y if top_down else h - 1 - y
        base = off + sy * stride
        rows.append(data[base:base + w * px])
    return w, h, rows, px


def main():
    names = sorted(p.name for p in OFF_DIR.glob("frame_*.bmp"))
    if not names:
        print(f"no frames in {OFF_DIR}")
        return 2
    missing = [n for n in names if not (ON_DIR / n).exists()]
    if missing:
        print(f"on-run is missing {len(missing)} frame(s), e.g. {missing[0]}")
        return 2

    bad = 0
    with_margin = 0
    for name in names:
        ow, oh, orows, ob = read_bmp(OFF_DIR / name)
        nw, nh, nrows, nb = read_bmp(ON_DIR / name)
        if (ow, oh) != (256, 224):
            print(f"{name}: off is {ow}x{oh}, expected 256x224")
            bad += 1
            continue
        if (nw, nh) != (256 + 2 * EXTRA, 224):
            print(f"{name}: on is {nw}x{nh}, expected {256 + 2 * EXTRA}x224")
            bad += 1
            continue

        diff = 0
        for y in range(oh):
            a, b = orows[y], nrows[y]
            for x in range(256):
                if a[x * ob:x * ob + 3] != b[(x + EXTRA) * nb:(x + EXTRA) * nb + 3]:
                    diff += 1
        if diff:
            print(f"{name}: authentic columns differ in {diff} px")
            bad += 1

        margin = 0
        for y in range(nh):
            row = nrows[y]
            for x in list(range(EXTRA)) + list(range(nw - EXTRA, nw)):
                if row[x * nb:x * nb + 3] != b"\x00\x00\x00":
                    margin += 1
        if margin:
            with_margin += 1

    print(f"{len(names)} paired frames")
    print(f"authentic columns identical: {len(names) - bad}/{len(names)}")
    print(f"frames with any margin pixel: {with_margin}/{len(names)}")
    print("RESULT:", "PASS" if bad == 0 else f"{bad} frame(s) failed")
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
