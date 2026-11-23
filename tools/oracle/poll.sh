set -u
TAG=${1:-c2full}
cd /h/FinalFantasyViSNESRecomp || exit 9
NP=$(ps -W 2>/dev/null | grep -icE '/(cl|link|MSBuild|cmake)\.exe' || true)
echo "buildprocs: $NP"
if [ -f build-c1/Release/FinalFantasyViSNESRecomp.exe ]; then
  ls -l --time-style=+%H:%M:%S build-c1/Release/FinalFantasyViSNESRecomp.exe
fi
echo "--- log tail ($TAG) ---"
tail -3 /c/Temp/c1/build_$TAG.log 2>/dev/null
echo -n "--- errors: "
grep -icE ' error |MSB6006' /c/Temp/c1/build_$TAG.log 2>/dev/null || true
