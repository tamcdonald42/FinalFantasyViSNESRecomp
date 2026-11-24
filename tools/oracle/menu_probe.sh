#!/usr/bin/env bash
# Menu-path probe: the same battery save taken down two routes, dumps and
# tier-2 coverage captured on both.
#
#   field  - load the save, stand still, dump twice
#   menu   - load the save, open the field menu, enter a submenu, walk it, close
#
# usage: bash tools/oracle/menu_probe.sh field|menu
# outputs: $WORK/{field,menu}/*.wram.bin (+ full frame dumps), $WORK/cov_$MODE/
#
# What it establishes: the field menu installs its own interrupt handlers
# (JML $C3:1387 / $C3:13C7 at $1500-$1507) for exactly as long as it runs, and
# restores the field's ($C0:0182 / $C0:0262) on exit. See docs/WIDESCREEN_TRIGGER.md.
set -u
MODE=${1:-menu}
REPO=$(cd "$(dirname "$0")/../.." && pwd)
EXE=$REPO/build-c1/Release/FinalFantasyViSNESRecomp.exe
ROM=${ROM:-D:/roms/snes/ff3u11.sfc}
SRM=${SRM:-/c/Temp/c1/ff6_narshe.srm}
WORK=${WORK:-/c/Temp/menuprobe}
WIN=${WORK#/c}; WIN="C:$WIN"

[ -x "$EXE" ]   || { echo "missing exe: $EXE"; exit 3; }
[ -f "$SRM" ]   || { echo "missing save: $SRM (set SRM=)"; exit 4; }
mkdir -p "$WORK"

# A load costs about 2,100 frames of logos, then a press of Start and a press of
# A through the file list. 12 x 140 frames is enough for the slot list and the
# "continue" prompt on every save tested so far.
load_block() {
  echo "wait 2100"
  echo "press start 8"
  for _ in $(seq 1 12); do echo "wait 140"; echo "press a 8"; done
  echo "wait 220"
}

if [ "$MODE" = "field" ]; then
  { load_block; echo "dump f_field1"; echo "wait 240"; echo "dump f_field2"; echo "quit"; } > "$WORK/field.txt"
else
  {
    load_block
    echo "dump m_field"                  # standing on the field
    echo "press x 8";    echo "wait 70";  echo "dump m_menu"    # menu up, fading in
    echo "press down 3"; echo "wait 20";  echo "dump m_cursor"  # cursor moved
    echo "press a 8";    echo "wait 110"; echo "dump m_sub"     # first submenu
    echo "press down 3"; echo "wait 30";  echo "dump m_sub2"    # deeper
    echo "press b 8";    echo "wait 40"
    echo "press b 8";    echo "wait 120"; echo "dump m_closed"  # back on the field
    echo "quit"
  } > "$WORK/menu.txt"
fi

cp -f "$SRM" "$(dirname "$EXE")/saves/save.srm" || exit 5
rm -rf "$WORK/$MODE"; mkdir -p "$WORK/$MODE"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
SNESRECOMP_TIER2_CAPTURE=1 \
SNESRECOMP_COVERAGE_DIR="$WIN/cov_$MODE" \
SNESRECOMP_DUMP_DIR="$WIN/$MODE" \
  "$EXE" --no-launcher --rom "$ROM" --script "$WIN/$MODE.txt" \
  > "$WORK/$MODE.log" 2>&1
RC=$?
echo "mode=$MODE rc=$RC  dumps=$(ls "$WORK/$MODE" | wc -l)"
grep -o "exit: script quit after [0-9]* frames" "$WORK/$MODE.log" | tail -1
grep -oE "unresolved-abandon|SEH CRASH" "$WORK/$MODE.log" | sort | uniq -c
echo "coverage: $WORK/cov_$MODE"
