set -u
# usage: refdump.sh TAG [START] [END]   (dumps from build-c0 = the verified C0 control)
TAG=${1:-ref}
START=${2:-390}
END=${3:-660}
W="${SCRATCH:-/tmp}/ff6-oracle"
cd /h/FinalFantasyViSNESRecomp || exit 9
rm -rf $W/$TAG; mkdir -p $W/$TAG
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
SNESRECOMP_FRAMEDUMP_PIXELS=1 SNESRECOMP_FRAMEDUMP_START=$START SNESRECOMP_FRAMEDUMP_END=$END \
  build-c0/Release/FinalFantasyViSNESRecomp.exe --no-launcher --rom D:/roms/snes/ff3u11.sfc \
  --script C:/Temp/oracle/script.txt --framedump "C:/Temp/c1/$TAG" > $W/refdump_$TAG.log 2>&1
echo -n "$TAG frames: $(ls $W/$TAG/*.bmp 2>/dev/null | wc -l)  "
echo -n "abandons: $(grep -c 'unresolved-abandon' $W/refdump_$TAG.log)  brk: $(grep -c '\[brk\]' $W/refdump_$TAG.log)  "
grep -o "fps=[0-9.]*" $W/refdump_$TAG.log | tail -1
