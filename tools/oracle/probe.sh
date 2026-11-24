set -u
# usage: probe.sh <script-base> <tag> [build-dir]
SCRIPT=$1
TAG=$2
BUILD=${3:-build-c1}
W="${SCRATCH:-/tmp}/ff6-oracle"/probe_$TAG
cd /h/FinalFantasyViSNESRecomp || exit 9
if [ ! -f "C:/Temp/c1/$SCRIPT.txt" ]; then echo "MISSING script C:/Temp/c1/$SCRIPT.txt"; exit 2; fi
rm -rf $W; mkdir -p $W
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SNESRECOMP_DUMP_DIR="C:/Temp/c1/probe_$TAG" \
  $BUILD/Release/FinalFantasyViSNESRecomp.exe --no-launcher --rom D:/roms/snes/ff3u11.sfc \
  --script "C:/Temp/c1/$SCRIPT.txt" > ${SCRATCH:-/tmp}/ff6-oracle/probe_$TAG.log 2>&1
RC=$?
echo "exit=$RC  script=$SCRIPT  build=$BUILD"
if grep -q 'script: cannot open' ${SCRATCH:-/tmp}/ff6-oracle/probe_$TAG.log; then echo "SCRIPT NOT FOUND"; exit 3; fi
grep -E 'script f=[0-9]+ (until|quit)|timed out|unknown command|abandon|\[brk\]' ${SCRATCH:-/tmp}/ff6-oracle/probe_$TAG.log | tail -6
echo -n "script lines seen: "; grep -c 'script f=' ${SCRATCH:-/tmp}/ff6-oracle/probe_$TAG.log
echo -n "dumps: "; ls $W/*.fb.bmp 2>/dev/null | wc -l
echo -n "last frame: "; grep -o 'frame=[0-9]*' ${SCRATCH:-/tmp}/ff6-oracle/probe_$TAG.log | tail -1
