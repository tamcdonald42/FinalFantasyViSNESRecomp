#!/usr/bin/env bash
# Drive FF6 from a battery save into a random encounter and fight it out.
#
#   battle_route.sh <save.srm> [build-dir] [--coverage]
#
# Reaches the first were-rat encounter in the Narshe mines and resolves it: the
# fight ends with the victory sequence and a return to the field, then a second
# encounter triggers. Use it as the oracle route for battle code (C1/C2) the way
# srm_route.sh is used for field and menu code.
#
# Two things this route encodes, both learned the hard way:
#   * Once the fight starts, press A ONLY. Direction presses walk the command
#     cursor into a submenu and the battle then waits forever in a menu that
#     looks exactly like a hang.
#   * A killed run leaves the exe holding its own image; kill it before any
#     rebuild or the link step fails with LNK1104.
set -u
SRM=${1:?usage: battle_route.sh <save.srm> [build-dir] [--coverage]}
BUILD=${2:-build-c1}
MODE=${3:-}

cd "$(dirname "$0")/../.." || exit 9
EXE=$BUILD/Release/FinalFantasyViSNESRecomp.exe
[ -f "$EXE" ] || { echo "missing $EXE" >&2; exit 2; }
mkdir -p "$BUILD/Release/saves"
cp -f "$SRM" "$BUILD/Release/saves/save.srm" || exit 2

ROUTE=${SNESRECOMP_BATTLE_SCRIPT:-C:/Temp/battle_route.txt}
OUT=${SNESRECOMP_BATTLE_DUMP:-C:/Temp/battle_dump}
{
  echo "wait 2100"; echo "press start 8"
  for _ in $(seq 1 12); do echo "wait 140"; echo "press a 8"; done   # slot list -> map
  echo "press x 8"; echo "wait 100"; echo "press b 8"; echo "wait 80" # poke the menu
  for _ in $(seq 1 12); do echo "press left 45"; echo "wait 25"; echo "press right 45"; echo "wait 25"; done
  echo "dump field"
  n=0
  for _ in $(seq 1 50); do
    echo "wait 30"; echo "press a 8"; n=$((n+1))
    [ $((n % 10)) -eq 0 ] && echo "dump f$(printf %02d $n)"
  done
  for _ in $(seq 1 6); do echo "press left 40"; echo "wait 20"; echo "press right 40"; echo "wait 20"; done
  echo "dump end"; echo "quit"
} > "$ROUTE"

mkdir -p "$OUT"
rm -f "$OUT"/*.fb.bmp "$OUT"/*.wram.bin 2>/dev/null
taskkill //F //IM FinalFantasyViSNESRecomp.exe >/dev/null 2>&1 || true

LOG=${SNESRECOMP_BATTLE_LOG:-C:/Temp/battle_route.log}
if [ "$MODE" = "--coverage" ]; then
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SNESRECOMP_DUMP_DIR="$OUT" \
  SNESRECOMP_TIER2_CAPTURE=1 SNESRECOMP_COVERAGE_DIR="$OUT/cov" \
    "$EXE" --no-launcher --rom "${SNESRECOMP_ROM:-D:/roms/snes/ff3u11.sfc}" --script "$ROUTE" > "$LOG" 2>&1
else
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SNESRECOMP_DUMP_DIR="$OUT" \
    "$EXE" --no-launcher --rom "${SNESRECOMP_ROM:-D:/roms/snes/ff3u11.sfc}" --script "$ROUTE" > "$LOG" 2>&1
fi
rc=$?
echo "route rc=$rc $(grep -o 'after [0-9]* frames' "$LOG" | tail -1)"
echo "abandons=$(grep -c unresolved-abandon "$LOG")  crashes=$(grep -c 'SEH CRASH' "$LOG")"
grep -E 'unresolved-abandon' "$LOG" | head -4
ls "$OUT"/*.fb.bmp 2>/dev/null | wc -l
exit $rc
