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

**Toolchain:** VS2022 BuildTools, CMake 4.4.5 at `C:\Tools\cmake`, rustc 1.97.1
`x86_64-pc-windows-gnu`, python 3.13 (miniconda), ninja at `C:\Users\Admin\bin`,
winlibs gcc 16.2 at `C:\Tools\mingw64`.

### Build — canonical: MSVC

```powershell
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64
cmake --build build-msvc --config Release --parallel 12
# -> build-msvc\Release\FinalFantasyViSNESRecomp.exe
```

### The MinGW path: an error that is not a failure

`gcc 16.2` + Ninja configures and compiles all 414 targets, then the link prints

```
ld.exe: .rsrc merge failure: multiple non-default manifests
```

SDL3's `version.rc` and the framework's generated `snesrecomp_utf8_manifest.rc` both
embed a manifest and GNU ld refuses to merge them. **It still writes a working
executable and cmake exits 0.** Verified: that binary boots the title screen at
60 fps (frame 480 identical: mean 45.7, 5 colours) and contains `activeCodePage`, so
the UTF-8 manifest is present; the discarded one is presumably SDL3's DPI manifest.
It is 10.9 MB against MSVC's 4.8 MB.

Treat it as a silent-CI hazard, not a toolchain block: a job keyed on exit status
would ship a binary whose build logged a linker error. `build\` (the MinGW tree) was
removed so there is exactly one executable to run; `runner.cmake` branches on `MSVC`
for this reason.

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
   today; ca65's `.dbg` line spans are a cleaner input than any of them). **Done — see below.**
2. Ingest the authority into `recomp/bank*.cfg`, regenerate, and compare the emitted
   decode against the disassembly (`tools/audit_disassembly.py`). **Done — see below.**
3. Seed analysis roots from the disassembly so the authority actually governs code:
   `recomp/symbols.toml` from the 13,928 labels, `emit = true` where the analyzer
   proves the variant safe. This is what moves `covered_authority_instructions` off 0.
4. Promote the 278 captured sites / 200 targets by name, then re-run the same script
   and diff frames against a reference emulator capture.
5. Push past the intro: name entry, first field map, menu, first battle.

## Step 2 — disassembly authority installed (2026-11-12)

`tools/ingest_ff6_disasm.py` is a ca65 backend for the framework's exporter: it plugs
the generic 65816 path with cc65's `.dbg` (`ld65 --dbgfile`) instead of an assembler
symbol file. Lengths come from the linker's own span records, so the authority is
byte-verified, never decoder-derived.

```bash
python snesrecomp/tools/export_disassembly_authority.py --format asar65816 \
  --source <ff6-disassembly> --rom <rom> --assembled-rom <ff6>/build/en/rom/ff6-en.sfc \
  --assembler <cc65>/bin/ca65 --classifier tools/ingest_ff6_disasm.py \
  --symbols <ff6>/build/en/rom/ff6-en.dbg --source-commit 8130132 \
  --out _authority/ff6-authority.json          # never committed: contains ROM bytes

python snesrecomp/tools/ingest_disassembly_authority.py --rom <rom> \
  --authority _authority/ff6-authority.json --cfg-dir recomp --out-dir _authority/cfg-overlay

python snesrecomp/tools/disassembly_layout.py --authority _authority/ff6-authority.json \
  --out recomp/disassembly-layout.json         # committed: boundaries + hashes, no bytes
```

Result: **101,321 instructions (226,811 bytes), 1,938 data regions (2,688,670 bytes),
16,834 unknown spans (50,231 bytes)** — 92.7% of the 3 MiB image is now either
byte-verified code or an explicit data exclusion, and 1.6% is explicitly unknown.
(The first naive pass gave 93,057 / 2,966 / 322,067 — the same instruction count to
within 8%, but only ~8% of the image as data and 66% of spans unconstrained.)

Four things that pass got wrong, in order of cost:

1. cc65 writes `.mac`/`.endmac`, not `.macro`/`.endmacro` — the definition regexes
   missed every FF6 macro, so 113k invocation lines fell through as unknown.
2. An anonymous span (macro-expanded bytes) has no `line` record; it must inherit the
   line of the span containing it, or every expansion is an unconstrained hole.
3. Macros in FF6 *are* languages: `shorta0`/`clr_ax` expand to instructions,
   `obj_script`/`ptr_tbl`/`move`/`dlg` to script or table bytes. Classification has to
   be a fixpoint over the macro graph (bodies invoke other macros), with a fourth
   outcome — `none`, emits nothing — so helpers like `set_script_mode` don't poison a
   data macro into `unknown`.
4. `name := expr` / `name .set expr` lines emit nothing; without that they classify as
   unknown and cascade into every macro that uses a computed pointer.

Audit (`tools/audit_disassembly.py --disassembly-entry-modes`): **0 invalid variants**,
`covered_authority_instructions: 0`. That zero is the honest state, not a failure: the
seed scaffold has 4 AOT roots (the vectors), so the emitter never decodes the game and
the authority constrains nothing yet. 192 bank cfgs materialise — the constraints are
in place, waiting for roots.

Regression check: rebuilt and replayed the same 1405-frame route with the layout
installed — **521/521 captured frames (480–1000: title, Start, fade, intro) are
pixel-identical** to the pre-authority build. Installing the authority is a no-op on
the observed route, which is the correct result for an LLE-first tree.


## Step 3 — analysis roots, first AOT promotion (2026-11-12, branch `exp/c0-aot-roots`)

Authority constrains nothing until analysis has roots, so `tools/gen_symbols.py` derives
them from both byte-verified sources: `sym ... type=lab, seg=` records in the `.dbg`
(47,310 labels) filtered to addresses that the authority says are instruction starts, and
a scan of the instruction list for `jsr abs` / `jsl long` (2,430 distinct targets, 2,410
with a label). Local labels (`@0000`) and duplicates get address-based C identifiers,
because a cfg name must be a C identifier.

First slice, bank `C0` (field code), `emit = true`:

```
symbols: 269 function labels (267 AOT requests)
analysis: 2708 roots -> 2872 exact variants, 1633 edges
analysis: 1362 AOT-eligible, 1510 LLE-only          (20 s end to end)
audit:    observed 1362, invalid 0,
          covered_authority_instructions 19717 (was 0),
          instruction instances match 61921 / unknown 10679
```

It boots and runs: **1405 frames at 59.6 fps, no tripwires, no fallback warnings** — and
the picture is *almost* right. First divergence is frame 660, the intro fade-out: the
entire frame is a **uniform 1:1 palette remap**, every channel 17 lower (255→238, 239→223,
24→22), five colours in five colours out. That is the AOT build sitting exactly **one fade
step ahead**, not corrupted arithmetic. By frame 700 the mapping is many-to-one, i.e. the
content itself is shifted, which is what a persistent one-step phase error looks like once
the scene changes.

So wholesale promotion of a bank is the wrong instrument: something frame-level advanced.
Prime suspect is the NMI-wait / idle-poll path (`docs/IDLE_POLL.md`): a `WaitForNMI` spin
loop that the runner HLEs as an idle poll becomes ordinary recompiled code, and the frame
it yields on moves. Next move is to bisect the 267 roots — promote in halves, or check
whether the poll loop is in the promoted set and pin it LLE — before promoting any more
banks.

State: `main` is back to the verified tree (4 roots, 521/521 frames pixel-identical to the
pre-authority baseline, re-checked after the restore). The experiment lives on
`exp/c0-aot-roots` with its root list at `recomp/roots/c0.toml`.

## Step 3b — bisection: what actually breaks (2026-11-12)

Harness: `tools/bisect/trial.sh` promotes a TOML fragment, regenerates, rebuilds,
replays frames 480–670 and byte-compares them against the pre-authority capture.
`tools/bisect/bisect2.py` classes each outcome — PASS / MILD (some frames differ after
the first) / BLACK (nothing renders) — and recurses only towards MILD, because the
plain bisect's assumption (a failing subset stays failing when narrowed) is false here.

| promoted roots | analysis | frames differing |
| --- | --- | --- |
| none (4 vectors) | 4 roots / 4 variants | 0 |
| `Rand`, `HexToDec`, `UpdateBattleRng`, `UpdateMaxHP`, `OptimizeCharEquip` | 1 each | 0 each |
| `Decompress` | 12 / 12 | 0 |
| `InitNPCs` | 12 / 22 | 0 |
| `RestartGame` (`emit = true`) | 20 / 309 | **11**, first 660 |
| `RestartGame` (`emit = false`) | 4 / 4 | 0 |
| first half of C0 (133 roots) | — | **191, screen black** |
| second half of C0 (134) | — | 11, first 660 |
| all 267 | 2708 / 1362 AOT | 11, first 660 |
| all 267 minus 15 fade/palette/PPU functions | — | 11, first 660 |

Two distinct failure modes, and neither is "the fade code is wrong":

1. **Black screen.** Promoting the low-address half (boot, `InitPPU`, sprite/palette
   transfer) leaves the display off for the whole window while the game still runs at
   60 fps — rendering is never enabled, not a crash. Separate ticket.
2. **One fade step ahead.** From frame 660 every colour is uniformly one step darker
   (1:1 remap), then content diverges structurally. Excluding the fade/palette/PPU
   functions from the promotion does *not* fix it, so the fade code being AOT is not
   the cause.

The mild mode narrows to one function, `C0:C4B3 RestartGame` — and the 1405-frame
coverage capture contains no reference to `C0C4B3` at all, i.e. it is never entered
during the intro. `emit = false` for the same symbol is pixel-identical, so it is the
promotion, not the cfg entry: promoting it pulls in a 20-function / 309-variant
closure (`LoadSavedGame_ext` and friends). Neighbours and other multi-root promotions
(`Decompress`, `InitNPCs`) stay clean, so it is not "promoting anything moves the
clock".

Next: get per-frame `master_cycles` for baseline vs the `RestartGame` promotion
(`docs/COSIM.md` is the tool meant for this) and bisect inside that 20-function
closure; the black-screen class is its own ticket in the PPU init path.

## Step 3c — it is the scale of AOT coverage, not one function (2026-11-12)

The plan was "find which closure function actually runs and promote it alone". That question
dissolved: promoting `RestartGame` does not AOT 20 functions, it AOTs **286 variants across ~275
entry points** (`v2_emit: 20 roots, 286 exact AOT variants`), i.e. most of bank C0's entry points.
The analysis follows calls, so a root with a wide call graph sweeps in the bank.

So the experiment became: does divergence track the *size* of the AOT set? Same harness, one root
each, same 190-frame window:

| promoted root | analysis | outcome |
| --- | --- | --- |
| `UpdateObjFrame` | 12 roots / 12 variants | 0 diffs |
| `Decompress` | 12 / 12 | 0 |
| `DrawObjSprites` | 12 / 18 | 0 |
| `InitNPCs` | 12 / 22 | 0 |
| `ExecBattle` | 20 / 77 | **MSVC `CL.exe` crashed (0xC0000005) compiling the generated C** |
| `LoadWorldMap` | 12 / **118** | **191 diffs — screen never renders** |
| `RestartGame` | 20 / **286** | 11 diffs — fade one step ahead |

Small AOT sets are exact; large ones are not, and they fail in two different ways. That rules out a
single bad routine and points at AOT execution itself — cycle/timing accounting, or a width/flag
convention that only shows when a critical mass of per-frame code runs natively. The two failure
classes (blank display, one-step-early fade) look like two different manifestations of the same
systemic split.

New bug banked separately: the emitted C for the `ExecBattle` config kills the compiler
(`error MSB6006: CL.exe exited with code -1073741819`). Regenerating and building again is fine, so
it is specific to that configuration's generated code — likely one oversized generated function.

Co-simulation is in-tree after all (`cosim/`, `tools/snes_cosim.py`, `tools/cosim_align.py`,
`tools/cosim_fbdump.py`) — `docs/COSIM.md` names a worktree that does not exist on this machine, but
the sources ship in the pinned framework. That is the next instrument: it steps the recompiled build
against a reference interpreter with full CPU/APU/PPU state hashing and reports the first
instruction that splits, which is precisely the question this step could not answer by probing.
