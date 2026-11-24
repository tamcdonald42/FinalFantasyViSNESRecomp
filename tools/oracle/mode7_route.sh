#!/usr/bin/env bash
# Exercise the Mode 7 path: load a world-map save, board the airship, fly.
#
#   mode7_route.sh <worldmap.srm> [build-dir]
#
# Mode 7 was the last PPU path with no route coverage: the opening only pokes
# $211B/$211C while the display stays in mode 1, so nothing exercised the
# perspective renderer. This routes a save that is standing on the world map
# (e.g. FF6 "18-Maranda" — World of Ruin, airship available), then flies north,
# east and south so the matrix registers vary in all four terms.
#
# What "working" looks like in the dumps this leaves behind:
#   * bgmode & 7 == 7 in .regs.json (and $2105 written mid-frame with two
#     values — mode 7 for the map, mode 1 for the HUD strip)
#   * $211B..$211E written ~450x per frame with ~100 distinct values each:
#     per-scanline HDMA, i.e. one matrix row per scanline
#   * $211D's distinct-value count rising once the airship turns (rotation term)
#   * frame hashes differing between dumps (a static image means a stuck matrix)
set -u
SRM=${1:?usage: mode7_route.sh <worldmap.srm> [build-dir]}
BUILD=${2:-build-c1}
cd "$(dirname "$0")/../.." || exit 9
EXE=$BUILD/Release/FinalFantasyViSNESRecomp.exe
[ -f "$EXE" ] || { echo "missing $EXE" >&2; exit 2; }
mkdir -p "$BUILD/Release/saves"
cp -f "$SRM" "$BUILD/Release/saves/save.srm" || exit 2

ROUTE=${SNESRECOMP_M7_SCRIPT:-C:/Temp/mode7_route.txt}
OUT=${SNESRECOMP_M7_DUMP:-C:/Temp/mode7_dump}
{
  echo "wait 2100"; echo "press start 8"
  for _ in $(seq 1 12); do echo "wait 140"; echo "press a 8"; done
  echo "wait 120"; echo "dump m1"
  echo "wait 120"; echo "dump m2"
  echo "press a 8"; echo "wait 90"; echo "dump m3"
  echo "press up 100"; echo "wait 60"; echo "dump m4"
  echo "press right 100"; echo "wait 60"; echo "dump m5"
  echo "press down 100"; echo "wait 60"; echo "dump m6"
  echo "quit"
} > "$ROUTE"

mkdir -p "$OUT"; rm -f "$OUT"/*.fb.bmp "$OUT"/*.wram.bin 2>/dev/null
taskkill //F //IM FinalFantasyViSNESRecomp.exe >/dev/null 2>&1 || true
LOG=${SNESRECOMP_M7_LOG:-C:/Temp/mode7_route.log}
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SNESRECOMP_DUMP_DIR="$OUT" \
  "$EXE" --no-launcher --rom "${SNESRECOMP_ROM:-D:/roms/snes/ff3u11.sfc}" --script "$ROUTE" > "$LOG" 2>&1
rc=$?
echo "route rc=$rc $(grep -o 'after [0-9]* frames' "$LOG" | tail -1)"
echo "abandons=$(grep -c unresolved-abandon "$LOG")  crashes=$(grep -c 'SEH CRASH' "$LOG")"
"${PYTHON:-python}" - "$OUT" <<'PY'
import glob, json, os, sys, collections, hashlib
out = sys.argv[1]
for r in sorted(glob.glob(out + '/*.regs.json')):
    tag = os.path.basename(r).replace('.regs.json', '')
    d = json.load(open(r))
    bm = d.get('bgmode', d.get('bgmodeMain'))
    try:
        mode = int(str(bm), 16) & 7
    except Exception:
        mode = '?'
    print(f"  {tag:<3} frame={d.get('frame')} bgmode={bm} mode={mode}"
          + ('  <-- MODE 7' if mode == 7 else ''))
for tsv in sorted(glob.glob(out + '/*.ppuw.tsv')):
    tag = os.path.basename(tsv).replace('.ppuw.tsv', '')
    c = collections.Counter(); vals = collections.defaultdict(set)
    for ln in open(tsv, errors='replace'):
        if ln.startswith('#'):
            continue
        p = ln.rstrip('\r\n').split('\t')
        if len(p) < 5:
            continue
        a = p[2].upper()
        if a in ('211B', '211C', '211D', '211E', '211F', '2120', '2105'):
            c[a] += 1; vals[a].add(p[3])
    if c:
        print(f"  {tag} matrix writes: " + ' '.join(f"{a}x{n}" for a, n in sorted(c.items())))
        print(f"      distinct: " + ' '.join(f"{a}={len(vals[a])}" for a in sorted(vals)))
PY
exit $rc
