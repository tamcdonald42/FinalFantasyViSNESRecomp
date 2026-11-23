import os
import struct
import sys


def read_bmp(p):
    d = open(p, 'rb').read()
    off = struct.unpack_from('<I', d, 10)[0]
    w, h = struct.unpack_from('<ii', d, 18)
    bpp = struct.unpack_from('<H', d, 28)[0]
    flip = h > 0
    h = abs(h)
    row = ((w * bpp // 8) + 3) // 4 * 4
    out = bytearray()
    for y in range(h):
        sy = h - 1 - y if flip else y
        base = off + sy * row
        for x in range(w):
            o = base + x * (bpp // 8)
            out += bytes((d[o + 2], d[o + 1], d[o]))
    return bytes(out)


def load(d):
    frames = {}
    for f in os.listdir(d):
        if f.endswith('.bmp'):
            frames[int(f.split('_')[1].split('.')[0])] = read_bmp(os.path.join(d, f))
    return frames


ref = load(sys.argv[1])
cand = load(sys.argv[2])
common = sorted(set(ref) & set(cand))
only_ref = sorted(set(ref) - set(cand))
only_cand = sorted(set(cand) - set(ref))
if not common:
    print('NO_OVERLAP ref=%d cand=%d' % (len(ref), len(cand)))
    sys.exit(2)
first = None
first_n = 0
diffs = []
for f in common:
    if ref[f] == cand[f]:
        continue
    a, b = ref[f], cand[f]
    nd = 0
    for k in range(0, len(a), 3):
        if a[k] != b[k] or a[k + 1] != b[k + 1] or a[k + 2] != b[k + 2]:
            nd += 1
    diffs.append((f, nd))
    if first is None:
        first, first_n = f, nd
total_px = len(ref[common[0]]) // 3
if only_ref or only_cand:
    print('FRAME_SET_MISMATCH ref-only=%d cand-only=%d' % (len(only_ref), len(only_cand)))
if first is None:
    print('FIRST_DIFF none (identical %d..%d, %d frames)' % (common[0], common[-1], len(common)))
else:
    print('FIRST_DIFF %d (%d/%d px = %.1f%%), %d/%d frames differ, last %d'
          % (first, first_n, total_px, 100.0 * first_n / total_px, len(diffs), len(common),
             diffs[-1][0]))
