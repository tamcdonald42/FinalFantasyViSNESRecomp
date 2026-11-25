#!/usr/bin/env bash
# Reproduce the attract-loop black screen with no input at all.
#
#   attract_probe.sh [build-dir] [frames]
#
# The game plays its opening demo, fades back to the title, starts the demo
# again and blanks the screen for good at frame 26,280 -- deterministically,
# three runs so far. Nothing is pressed: the whole route is one `wait`.
# Background and measurements: docs/ATTRACT_LOOP_BLACK.md.
#
# Writes a BMP every 120 frames into $OUT/frames; the first all-black frame
# after the title is the bug. Compare with the reference emulator by running
# the same route under tools/oracle/probe.sh.
set -u
BUILD=${1:-build-c1}
FRAMES=${2:-30600}
OUT=${SNESRECOMP_ATTRACT_DUMP:-C:/Temp/attract}

cd "$(dirname "$0")/../.." || exit 9
EXE=$BUILD/Release/FinalFantasyViSNESRecomp.exe
[ -f "$EXE" ] || { echo "missing $EXE" >&2; exit 2; }

ROUTE=C:/Temp/attract_route.txt
{
  echo "wait $FRAMES"
  echo "quit"
} > /c/Temp/attract_route.txt 2>/dev/null || printf 'wait %s\nquit\n' "$FRAMES" > "$ROUTE"

mkdir -p "$OUT/frames"
rm -f "$OUT/frames"/*.bmp 2>/dev/null
taskkill //F //IM FinalFantasyViSNESRecomp.exe >/dev/null 2>&1 || true

SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
SNESRECOMP_FRAME_BMP_DIR="$OUT/frames" \
SNESRECOMP_FRAME_BMP_START=120 SNESRECOMP_FRAME_BMP_END="$FRAMES" \
SNESRECOMP_FRAME_BMP_STEP=120 \
  "$EXE" --no-launcher --rom "${SNESRECOMP_ROM:-D:/roms/snes/ff3u11.sfc}" \
         --script "$ROUTE" > "$OUT/run.log" 2>&1
rc=$?
echo "route rc=$rc  $(grep -o 'after [0-9]* frames' "$OUT/run.log" | tail -1)"
echo "abandons=$(grep -c unresolved-abandon "$OUT/run.log")  crashes=$(grep -c 'SEH CRASH' "$OUT/run.log")"
echo "frames dumped: $(ls "$OUT/frames"/*.bmp 2>/dev/null | wc -l) in $OUT/frames"
echo "expect the first all-black frame at 26280 if this build has the bug"
exit $rc
