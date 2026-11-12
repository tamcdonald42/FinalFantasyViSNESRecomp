#!/bin/bash
# One bisection trial: promote a subset of bank-C0 roots, rebuild, replay the
# fade window, and byte-compare against the pre-authority baseline capture.
# $1 = path to a TOML fragment holding the [[func]] entries to promote.
set -u
cd /h/FinalFantasyViSNESRecomp || exit 9
export PATH="/c/Tools/cmake/bin:/c/Users/Admin/bin:$PATH"
FRAG="$1"
cp _authority/symbols_base.toml recomp/symbols.toml
cat "$FRAG" >> recomp/symbols.toml

t0=$(date +%s)
bash tools/regen.sh > /c/Temp/trial_regen.log 2>&1
RG=$?
t1=$(date +%s)
if [ $RG -ne 0 ]; then
  echo "TRIAL=REGEN_FAIL regen=$((t1-t0))s"
  grep -v "^$" /c/Temp/trial_regen.log | tail -3
  exit 0
fi

cmake --build build-msvc --config Release --parallel 12 > /c/Temp/trial_build.log 2>&1
BD=$?
t2=$(date +%s)
if [ $BD -ne 0 ]; then
  echo "TRIAL=BUILD_FAIL build=$((t2-t1))s"
  grep -E "error|Error" /c/Temp/trial_build.log | head -5
  exit 0
fi

cd build-msvc/Release || exit 9
export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*'
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
export SNESRECOMP_FRAMEDUMP_PIXELS=1
export SNESRECOMP_FRAMEDUMP_START=480
export SNESRECOMP_FRAMEDUMP_END=670
rm -rf /c/Temp/ff6trial
timeout 240 ./FinalFantasyViSNESRecomp.exe --no-launcher \
  --rom 'D:\roms\snes\Final Fantasy III (USA) (Rev 1).sfc' \
  --framedump 'C:\Temp\ff6trial' --script 'C:\Temp\bisect.txt' > /c/Temp/trial_run.log 2>&1
RUN=$?
t3=$(date +%s)

n=0; bad=0; first=""
for f in /c/Temp/ff6trial/*.bmp; do
  [ -e "$f" ] || continue
  n=$((n+1))
  b=$(basename "$f")
  if ! cmp -s "$f" "/c/Temp/ff6start/$b"; then
    bad=$((bad+1))
    [ -z "$first" ] && first="$b"
  fi
done
echo "TRIAL=OK run_exit=$RUN frames=$n ndiff=$bad first=$first regen=$((t1-t0))s build=$((t2-t1))s run=$((t3-t2))s"
