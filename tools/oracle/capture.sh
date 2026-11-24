set -u
# usage: capture.sh 0|1 TAG   (0 = build-c0 control, 1 = build-c1 candidate)
SIDE=$1
TAG=$2
W="${SCRATCH:-/tmp}/ff6-oracle"
cd /h/FinalFantasyViSNESRecomp || exit 9
EXE=build-c$SIDE/Release/FinalFantasyViSNESRecomp.exe
rm -rf $W/cov_$TAG; mkdir -p $W/cov_$TAG
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
SNESRECOMP_TIER2_CAPTURE=1 SNESRECOMP_COVERAGE_DIR="C:/Temp/c1/cov_$TAG" \
  $EXE --no-launcher --rom D:/roms/snes/ff3u11.sfc \
  --script C:/Temp/oracle/short.txt > $W/covrun_$TAG.log 2>&1
echo -n "$TAG files: $(ls $W/cov_$TAG | wc -l)  size: $(du -sh $W/cov_$TAG | cut -f1)  "
grep -o "exit: script quit after [0-9]* frames" $W/covrun_$TAG.log | tail -1
ls -l $W/cov_$TAG | head -5
