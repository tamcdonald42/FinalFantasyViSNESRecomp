set -u
cd /h/FinalFantasyViSNESRecomp || exit 9
printf 'wait 2100\npress start 8\nwait 3400\nquit\n' > ${SCRATCH:-/tmp}/ff6-oracle/route2.txt
W="${SCRATCH:-/tmp}/ff6-oracle"/wdump
rm -rf $W; mkdir -p $W
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
SNESRECOMP_FRAMEDUMP_START=4950 SNESRECOMP_FRAMEDUMP_END=5400 \
  build-c1/Release/FinalFantasyViSNESRecomp.exe --no-launcher --rom D:/roms/snes/ff3u11.sfc \
  --script C:/Temp/c1/route2.txt --framedump "C:/Temp/c1/wdump" > ${SCRATCH:-/tmp}/ff6-oracle/wdump.log 2>&1
echo "rc=$? wram files: $(ls $W/*_wram.bin 2>/dev/null | wc -l)"
