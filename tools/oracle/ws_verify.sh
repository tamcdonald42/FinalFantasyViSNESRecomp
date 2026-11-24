#!/usr/bin/env bash
# Widescreen verification: same route twice, once with the mod off and once on.
#   bash /c/Temp/ws_verify.sh off|on
# Writes the setting to the verification build's qol.ini, then loads a save,
# dumps the field, opens the menu, dumps, closes, dumps.
set -u
MODE=${1:-off}
REPO=/h/FinalFantasyViSNESRecomp
EXE=$REPO/build-c1/Release/FinalFantasyViSNESRecomp.exe
DIR=$(dirname "$EXE")
ROM='D:/roms/snes/ff3u11.sfc'
SRM=/c/Temp/c1/ff6_narshe.srm
WORK=/c/Temp/ws

[ -x "$EXE" ] || { echo "missing exe"; exit 3; }
mkdir -p "$WORK"

cat > "$DIR/qol.ini" <<EOF
# verification build settings
[QoL]
Encounters    = on
EncountersKey = Grave
Widescreen    = $MODE
WidescreenKey = F10
EOF

{
  echo "wait 2100"
  echo "press start 8"
  for _ in $(seq 1 12); do echo "wait 140"; echo "press a 8"; done
  echo "wait 240"; echo "dump x_field"
  echo "press x 8";    echo "wait 90";  echo "dump x_menu"
  echo "press down 3"; echo "wait 30";  echo "dump x_cursor"
  echo "press a 8";    echo "wait 110"; echo "dump x_sub"
  echo "press b 8";    echo "wait 40"
  echo "press b 8";    echo "wait 120"; echo "dump x_closed"
  echo "quit"
} > "$WORK/route.txt"

cp -f "$SRM" "$DIR/saves/save.srm" || exit 5
rm -rf "$WORK/$MODE"; mkdir -p "$WORK/$MODE/frames"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
SNESRECOMP_DUMP_DIR="C:/Temp/ws/$MODE" \
SNESRECOMP_FRAME_BMP_DIR="C:/Temp/ws/$MODE/frames" \
SNESRECOMP_FRAME_BMP_START=4050 SNESRECOMP_FRAME_BMP_END=4620 \
SNESRECOMP_FRAME_BMP_STEP=10 \
  "$EXE" --no-launcher --rom "$ROM" --script "C:/Temp/ws/route.txt" \
  > "$WORK/$MODE.log" 2>&1
echo "mode=$MODE rc=$? dumps=$(ls "$WORK/$MODE"/*.fb.bmp 2>/dev/null | wc -l)"
grep -o "exit: script quit after [0-9]* frames" "$WORK/$MODE.log" | tail -1
grep -oE "unresolved-abandon|SEH CRASH" "$WORK/$MODE.log" | sort | uniq -c
grep -E "^\[qol\]" "$WORK/$MODE.log" | sort -u | head -6
