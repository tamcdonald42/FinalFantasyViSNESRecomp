set -u
# usage: runonly.sh TAG REFDIR [START] [END]
TAG=${1:-c1full}
REF=${2:-/c/Temp/c1/ref}
START=${3:-390}
END=${4:-660}
W=/c/Temp/c1
cd /h/FinalFantasyViSNESRecomp || exit 9
rm -rf $W/out_$TAG; mkdir -p $W/out_$TAG
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
SNESRECOMP_FRAMEDUMP_PIXELS=1 SNESRECOMP_FRAMEDUMP_START=$START SNESRECOMP_FRAMEDUMP_END=$END \
  build-c1/Release/FinalFantasyViSNESRecomp.exe --no-launcher --rom D:/roms/snes/ff3u11.sfc \
  --script C:/Temp/oracle/script.txt --framedump "C:/Temp/c1/out_$TAG" > $W/run_$TAG.log 2>&1
echo -n "frames: $(ls $W/out_$TAG/*.bmp 2>/dev/null | wc -l)  abandons: $(grep -c 'unresolved-abandon' $W/run_$TAG.log)  brk: $(grep -c '\[brk\]' $W/run_$TAG.log)  "
grep -o "fps=[0-9.]*" $W/run_$TAG.log | tail -1
grep -m4 -E 'unresolved-abandon|\[brk\]' $W/run_$TAG.log | sed 's/^/    /'
echo -n "ORACLE vs $REF: "
python C:/Temp/c1/cmp_frames.py "$(echo $REF | sed 's|^/c/|C:/|')" "C:/Temp/c1/out_$TAG"
