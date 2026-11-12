# Project status

## Milestone 1 — boots to the title screen, and into the intro (2026-11-12)

Scaffolded from snesrecomp (`tools/new_project/setup_project.sh`, framework pinned
`6ca2dbbe`), generated with the LLE-first seed (4 roots: RESET/NMI/IRQ + one), built,
and run headless. All of this is reproducible from this file alone.

**ROM (the one this project is pinned to):**
`D:\roms\snes\Final Fantasy III (USA) (Rev 1).sfc` — sha256
`10eccc5d2fab81346dd759f6be478dcb682eef981e8d3d662da176e1f9a996bc`, crc32 `c0fa0464`.
HiROM + FastROM, 3 MB, 8 KB battery SRAM, no coprocessor, checksum valid.

**Reference disassembly:** `everything8215/ff6` (ca65/ld65, GPL-3.0). `make rip` +
`make LANG=en ROM_VERSION=1` reproduces *our* dump byte for byte (verified with `cmp`
and sha256). It emits `build/en/rom/ff6-en.dbg` — 400,869 address→source-line records
and 13,928 named labels. That is the analysis authority this port should be built
against; nothing of it is committed here.

**Toolchain:** VS2022 BuildTools (`cmake -G "Visual Studio 17 2022" -A x64`),
CMake 4.4.5 at `C:\Tools\cmake`, rustc 1.97.1 `x86_64-pc-windows-gnu`,
python 3.13 (miniconda), ninja at `C:\Users\Admin\bin`.

### Build

```powershell
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64
cmake --build build-msvc --config Release --parallel 12
```

**Trap:** the MinGW path (gcc 16.2.0 UCRT + Ninja) configures fine but fails at the
link with `ld.exe: .rsrc merge failure: multiple non-default manifests` — SDL3's
`version.rc` and the framework's generated `snesrecomp_utf8_manifest.rc` both embed a
manifest and GNU ld refuses to merge them. `runner.cmake` already branches: MSVC gets
the manifest as a source, everyone else gets an RC. Use the MSVC generator on Windows.

### Headless run

```powershell
$env:SDL_VIDEODRIVER="dummy"; $env:SDL_AUDIODRIVER="dummy"
.\build-msvc\Release\FinalFantasyViSNESRecomp.exe --no-launcher `
  --rom 'D:\roms\snes\Final Fantasy III (USA) (Rev 1).sfc' --script boot.txt
```

A dummy video driver implies headless, so the launcher is skipped automatically.
`boot.txt` is the framework's script DSL (`wait`, `press start 4`, `until`, `dump`,
`quit`). Frame grabs: `SNESRECOMP_FRAMEDUMP_PIXELS=1`, `SNESRECOMP_FRAMEDUMP_START`,
`SNESRECOMP_FRAMEDUMP_END`, `--framedump <dir>` (writes one BGRA BMP per frame).

Result: 1405 frames, 60.03 fps, no crash. Title screen at ~frame 420; `press start`
at frame 500 fades out at 669 and the opening cinematic fades in from ~683.

### What the interpreter is doing (coverage capture)

`SNESRECOMP_TIER2_CAPTURE=1` over those 1405 frames:

| measure | value |
|---|---|
| interpreted instructions | 21,044,568 (~15k/frame) |
| distinct fallback sites | 278 (256 `call_gap`, 22 `goto_gap`) |
| unique targets | 200, all `missing_entry` |
| observed tier hits | 40,798 |
| entry widths seen | M1X0 267, M0X0 11 |
| WRAM routines | 76+ (64-slot table overflowed by 12), each hashed |

So nothing is AOT yet beyond the four vectors; the game is being interpreted at
60 fps with headroom (4.6 s of CPU for 11.7 s of wall on the intro run). The 278
sites are the promotion worklist, and the 76 WRAM routines are FF6 copying code into
`$7E:` — those need `ram_routine` handling rather than `func` declarations.

## Next

1. Turn the `.dbg` into analysis authority: a ca65 adapter for
   `snesrecomp/tools/export_disassembly_authority.py` (it speaks asar/smwdisx/usdasm
   today; ca65's `.dbg` line spans are a cleaner input than any of them).
2. Ingest the authority into `recomp/bank*.cfg`, regenerate, and compare the emitted
   decode against the disassembly (`tools/audit_disassembly.py`).
3. Promote the 278 captured sites / 200 targets by name from `symbols.toml`
   (`emit = true`), then re-run the same script and diff frames against a reference
   emulator capture.
4. Push past the intro: name entry, first field map, menu, first battle.
