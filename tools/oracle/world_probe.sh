#!/usr/bin/env bash
# World-map probe: load the world-map save, dump the overworld, press X, dump,
# press B, dump. Establishes which bank owns the interrupt vectors there -- EE --
# and shows that X disembarks in this save rather than opening the menu ($E8 bit 0
# is clear while airborne, EE:03B4).
# usage: bash tools/oracle/world_probe.sh   (SRM=, WORK=, ROM= override)
set -u
REPO=/h/FinalFantasyViSNESRecomp
EXE=$REPO/build-c1/Release/FinalFantasyViSNESRecomp.exe
ROM=${ROM:-D:/roms/snes/ff3u11.sfc}
SRM=${SRM:-/c/Temp/c1/ff6_world.srm}
WORK=${WORK:-/c/Temp/worldprobe}
WIN="C:${WORK#/c}"
mkdir -p "$WORK"
{
  echo "wait 2100"
  echo "press start 8"
  for _ in $(seq 1 12); do echo "wait 140"; echo "press a 8"; done
  echo "wait 240"; echo "dump w_map"
  echo "press x 8";    echo "wait 90";  echo "dump w_menu"
  echo "press down 3"; echo "wait 30";  echo "dump w_cursor"
  echo "press b 8";    echo "wait 120"; echo "dump w_closed"
  echo "quit"
} > "$WORK/world.txt"
cp -f "$SRM" "$(dirname "$EXE")/saves/save.srm" || exit 5
rm -rf "$WORK/dumps"; mkdir -p "$WORK/dumps"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
SNESRECOMP_DUMP_DIR="$WIN/dumps" \
  "$EXE" --no-launcher --rom "$ROM" --script "$WIN/world.txt" \
  > "$WORK/world.log" 2>&1
echo "rc=$? dumps=$(ls "$WORK/dumps"/*.wram.bin 2>/dev/null | wc -l)"
grep -o "exit: script quit after [0-9]* frames" "$WORK/world.log" | tail -1
grep -oE "unresolved-abandon|SEH CRASH" "$WORK/world.log" | sort | uniq -c
