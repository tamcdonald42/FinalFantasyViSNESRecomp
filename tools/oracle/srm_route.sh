#!/usr/bin/env bash
# Route the game through a battery save instead of the prologue.
#
#   srm_route.sh <save.srm> [walk]
#
# Puts the given FF6 (US) .srm into both builds' saves/ dirs, runs the same
# input script on the C0 control and the C0+C1+C2 candidate, and compares the
# dump frames. Exit 0 only when every dump is byte-identical.
#
# The prologue of this ROM halts at ~frame 5088 in both the port and the
# reference emulator (see STATUS.md, Step 14), so a battery save is the only
# way in: Continue -> Narshe mines -> field map -> in-game menu.
#
# Script timing (frame numbers, measured): press start at 2100 brings up the
# slot list ~2400, the next press shows "This data?", the one after loads the
# map at ~2700. Pressing A every ~150 frames from 2250 is robust - whichever
# press lands, gameplay arrives by ~2700.
#
# Set walk=walk to append field movement. NOTE: ~150-300 frames of walking
# currently crashes both builds with an access violation (0xC0000005,
# "last recomp func: (none)") - the likely first random encounter. Short taps
# do not crash. See STATUS.md Step 15 before enabling it.
set -u
SRM=${1:-}
WALK=${2:-nowalk}
[ -n "$SRM" ] && [ -f "$SRM" ] || { echo "usage: $0 <save.srm> [walk]" >&2; exit 2; }

cd "$(dirname "$0")/../.." || exit 9          # repo root
DIR=${SNESRECOMP_DUMP_ROOT:-C:/Temp/srmroute}
SCRIPT=${SNESRECOMP_ROUTE_SCRIPT:-C:/Temp/srmroute.txt}

for b in build-c0 build-c1; do
  [ -d "$b/Release" ] || { echo "missing $b/Release" >&2; exit 3; }
  mkdir -p "$b/Release/saves"
  cp -f "$SRM" "$b/Release/saves/save.srm"
done

{
  echo "wait 2100"; echo "press start 8"
  for _ in $(seq 1 12); do echo "wait 140"; echo "press a 8"; done
  echo "dump r_map"
  echo "press x 8";   echo "wait 100"; echo "dump r_menu"
  echo "press down 3"; echo "wait 24";  echo "dump r_cursor"
  echo "press a 8";   echo "wait 100"; echo "dump r_sub"
  echo "press b 8";   echo "wait 60";  echo "press b 8"; echo "wait 80"; echo "dump r_closed"
  if [ "$WALK" = "walk" ]; then
    echo "press left 45";  echo "wait 20"; echo "press right 45"; echo "wait 20"
    echo "press left 45";  echo "wait 30"; echo "dump r_walk1"
    echo "press right 60"; echo "wait 20"; echo "press left 60";  echo "wait 30"; echo "dump r_walk2"
  fi
  echo "quit"
} > "$SCRIPT"

for b in build-c0 build-c1; do
  out="$DIR/$b"; rm -rf "$out" 2>/dev/null; mkdir -p "$out"
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SNESRECOMP_DUMP_DIR="$out" \
    "$b/Release/FinalFantasyViSNESRecomp.exe" --no-launcher \
    --rom "${SNESRECOMP_ROM:-D:/roms/snes/ff3u11.sfc}" --script "$SCRIPT" \
    > "$DIR/$b.log" 2>&1
  echo "$b rc=$? $(grep -o 'exit: script quit after [0-9]* frames' "$DIR/$b.log" | tail -1)"
  grep -E 'SEH CRASH|last recomp func' "$DIR/$b.log" | head -2
done

"${PYTHON:-python3}" - "$DIR" <<'PY'
import hashlib, json, os, sys
root = sys.argv[1]
tags = [f[: -len('.fb.bmp')] for f in sorted(os.listdir(os.path.join(root, 'build-c1')))
        if f.endswith('.fb.bmp')]
bad = 0
for t in tags:
    a = os.path.join(root, 'build-c1', t + '.fb.bmp')
    b = os.path.join(root, 'build-c0', t + '.fb.bmp')
    if not os.path.exists(b):
        print(f"  {t:10s} MISSING in control"); bad += 1; continue
    ha = hashlib.md5(open(a, 'rb').read()).hexdigest()
    hb = hashlib.md5(open(b, 'rb').read()).hexdigest()
    f = json.load(open(os.path.join(root, 'build-c1', t + '.info.json'))).get('frame')
    print(f"  {t:10s} frame={f} {'IDENTICAL' if ha == hb else 'DIFF'}")
    bad += ha != hb
print("ORACLE:", "clean" if bad == 0 else f"{bad} frame(s) differ")
sys.exit(0 if bad == 0 else 1)
PY
