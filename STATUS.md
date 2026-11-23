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

## Step 4 — a second, independent execution of the same ROM (2026-11-12)

`tools/cosim/` builds the co-simulation **B side** for this title: the framework's game-neutral
reference driver (`snesrecomp/cosim/ref_driver.c`) linked against the runner's device sources and
interp816, with no recompiled C at all. Two adjustments were needed versus the framework's SMW/MMX
targets: the game's `src/variables.h` has to be on the include path, and three device sources
reference symbols that only the recompiled runtime defines, so `tools/cosim/ref_glue.c` supplies
`g_interp_apu_driving`, the HD-frame builders and the tier-2 capture hooks as inert stubs.

```bash
cmake -S tools/cosim -B tools/cosim/build -G "Visual Studio 17 2022" -A x64
cmake --build tools/cosim/build --config Release --parallel 12
tools/cosim/build/Release/ff6_cosim_ref.exe "<rom>" --frames 620 \
    --final-frame-dump C:\Temp\refframes\final620.ppm
```

Result: 620 frames in about a minute, `PASS`, and its frame 620 is **pixel-identical to the port's
frame 620** (0 of 57,344 pixels differ). An execution path that shares no recompiled code with our
port — interp816 driving its own copy of the devices — reproduces the picture exactly. That validates
the ROM handling, the device wiring and the frame model independently of the AOT work.

Open item: the reference reports `audio_active_frames=0` (`audio_peak=0`) over 620 frames while the
port produces audio, so `ref_driver`'s own liveness gate fails at >=120 frames even though the run and
the frame dump are fine. Since the APU is inside the co-sim state hash, that needs understanding
before an A-vs-B comparison is meaningful.

Remaining for a full A-vs-B run: an A-side harness (the reference's `harness_smw.c` equivalent —
register `kGameInfo`, own `g_spc_player`, scripted Start at frame 504 so both sides see identical
input), a `-DSNES_COSIM=ON` build of the port, and the coordinator
(`tools/snes_cosim.py --a-cmd ... --b-cmd ...`) with gates 1-4 from `docs/COSIM.md`.

## Step 5 — the co-simulation A side, and what A-vs-B can actually compare (2026-11-16)

`ff6_cosim` (`-DSNESRECOMP_FF6_COSIM_A=ON`, DEV only) is the A side: the same runner and the same
generated banks with `tools/cosim/harness_ff6.c` as the entry point instead of the SDL desktop host.
No `SDL_Init`, no window, no audio device, no worker threads — Gate-1 determinism by construction.

The desktop host **cannot** be used for co-sim, and this was measured, not assumed: two instances of
`build-cosim/Release/FinalFantasyViSNESRecomp.exe` given identical input diverged in the APU and DSP
sub-hashes at checkpoint 2. `g_config.enable_audio = true; /* always on */` in `host_main.c` means the
SDL audio callback thread advances the APU on wall-clock time, and `SDL_AUDIODRIVER=nonexistent`
does not help because `SDL_Init` then fails outright.

Gates:

| gate | what | result |
|---|---|---|
| 1 | A vs A, headless harness, stride 60, 12 cps | **PASS** — no divergence (7 s) |
| 2 | B vs B, coordinated ref (no `--frames`, which would make it standalone) | **PASS** — no divergence in 8 cps |
| 3/4 | A vs B | not yet meaningful, see below |

`ref_driver` has to be launched *without* `--frames` for co-sim: passing it sets `standalone_frames`,
which skips `cosim_init()` entirely and free-runs (the first attempt ran 100,000 frames and exited).

A-vs-B does not line up as-is. `tools/cosim_align.py` (A free-running 200 frames, B 320) reports the
post-boot phase offset and the per-subsystem agreement at it:

```
BEST WRAM offset O=1: 159/200 frames with IDENTICAL ram hash
  cpu 137/200   ram 159/200   ppu 173/200   cart 200/200
  apu   0/200   dsp   0/200   spc  10/200   dma 35/200   sio 0/200
```

`O=1` is structural: the port parks at the start of `RtlRunFrame` (through `common_rtl.c`) while the
ref parks after `run_one_frame()`, so B is exactly one frame ahead. `tools/cosim/cosim_run.py` is the
coordinator with a `--b-head N` option that cancels this by advancing B before the first checkpoint.

What O=1 shows is the real obstacle: the two sides boot the APU differently (the port HLE-boots it,
the ref runs the actual IPL handshake over many hardware frames), so `apu`/`dsp`/`spc` agree on
**0/200** frames and `sio`/`dma` are close behind. Even `cpu` (137/200) and `ram` (159/200) are not
clean at the best offset, because the two execution models carry different cycle currency.

So an A-vs-B hash comparison over the full chain is not yet a usable oracle for FF6: a "divergence"
at checkpoint 2 says the APU boot path differs, not that AOT broke. Options are to compare only
`cart`/`ppu`/`ram` with `--transient-grace`, or — the real fix — make the A side's APU boot faithful
instead of HLE, which is a framework-level change and the prerequisite for using co-sim as the
instrument this investigation wanted.

## Step 6 — the reference interpreter as a frame oracle: the answer (2026-11-16)

No co-sim needed: `ff6_cosim_ref` dumps a final frame for any N, and the port dumps a frame range, so
the two can be compared pixel-for-pixel. Three-way comparison at the intro fade (frame dumps:
`C:\Temp\oracle\{ref,port2,port3,port_c0}`):

| comparison | result |
|---|---|
| ref vs port (4-root baseline) | **identical content**, offset by a constant 11 frames (ref 680 = base 669, ref 684 = base 673, ref 688 = base 677) — the real IPL handshake boots ~11 frames later |
| ref vs port (267-root C0 build) | offset by 20 frames (ref 680 = c0 660, ref 684 = c0 664, ref 688 = c0 668) |
| baseline vs C0, same frame | **identical through frame 659, every pixel different from 660 onward** |

So outcome 1 from the plan, with a correction to the earlier finding. Interpretation is faithful: the
independent interp816 build reproduces the port's baseline sequence exactly, with only a constant
boot-time offset. The AOT build is the outlier — and it is not "one fade step ahead". The fade runs
659→682 in the baseline (mean luma 43.9 → 0.0) and 650→673 in the C0 build: same duration, **started 9
frames earlier**. That is a *phase shift*, not a per-step lead, which means it is injected before 660 —
the last frame where the two builds still agree pixel-for-pixel.

This replaces co-sim as the working instrument: a ~4-minute loop (regen 20 s, incremental build, 24 s
headless run) with a binary, unambiguous oracle ("does frame 660 differ?") instead of a hash that
splits for APU boot-path reasons.

## Step 7 — bisecting with the frame oracle: two independent defects (2026-11-16)

Harness: `C:\Temp\bisect\{gen_trial.py,cmp.py,trial.sh}` — select a contiguous slice of
`_authority/roots.toml`, append it after the four vectors, `regen`, incremental build of `build-c0`,
24 s headless run dumping frames 640-662, compare against the baseline dump. ~3.5 min per trial.

| slice (root indices) | AOT variants | first differing frame | class |
|---|---|---|---|
| 0:133 | 680 | 640 | **black** (1 colour, luma 0.0 — never renders) |
| 133:200 | 312 | 640 | black |
| 200:233 | 205 | 640 | black |
| 233:267 | 576 | 660 | phase shift (fade 9 frames early) |
| 200:267 | 751 | 660 | phase shift |
| 133:267 | 929 | 660 | phase shift |
| 0:267 (full) | 1362 | 660 | phase shift |

Two disjoint slices produce **different failure modes**, so there are at least two independent
defects, not one:

1. **Blackout** — the frame is a single colour from at least frame 640 (probably frame 0). The game
   still runs at ~55 fps and reports no error; it simply never draws. Reachable from slices 0:133,
   133:200 and 200:233. The 200:233 set is `InitEvent`, `ExecEvent`, `PushEventPtr`,
   `InitPalAnim`/`UpdatePalAnim` (all six), `TfrDlgTextGfx`, `DrawLetter`, `CalcCharPtr`,
   `UpdateAbilities`/`UpdateExp`/`InitMaxHP` — the event VM and the palette-animation path.
2. **9-frame phase shift** — reachable from 233:267, which is `DecTimers`, `CheckTimer`,
   `DecTimersMenuBattle`, `InitNewGame`, `RestartGame`, `OpenMenu`, `ExecBattle`, the overlay
   routines and the debug draws. The timer routines are the obvious suspects for a fade that starts
   early with unchanged duration.

The mild class wins when both slices are present (full set = phase shift, not black), which means the
blackout is masked by whatever the phase-shift slice changes — the two defects interact.

Next: bisect 200:233 down to the smallest blackout set (8 trials, ~30 min), and separately promote
the three timer routines alone.

## Offsite backup

The project now mirrors to a private GitHub repo (`tamcdonald42/FinalFantasyViSNESRecomp`): source,
configs, tools, docs and the 267-root candidate list. Not in git, by design: the ROM, `src/gen`
(regenerable in ~20 s), `_authority/ff6-authority.json` (69 MB, regenerable in ~10 min plus the
exporter run) and every build tree.

`tools/backup_push.sh` republishes the branches. The credential on this machine carries
`gist, read:org, repo` but not `workflow`, so a plain push is rejected for the commit that adds
`.github/workflows/release.yml`; the script pushes a rewritten copy with that one path stripped and
leaves local history alone. Grant the token the `workflow` scope and it becomes a plain `git push`.

## Step 8 — the blackout is one function: `ExecEvent` (2026-11-17)

Ten trials, bisecting 200:233 by halves against the frame oracle:

```
200:233 (205 variants) black      216:233 (121) black      216:225 (65) black
200:216 (104) clean               225:233 (60)  clean      216:221 (41) clean
221:225 (28)  black               221:223 (12)  clean      223:225 (20) black
223:224 (12)  BLACK    <-- ExecEvent alone      224:225 (12) clean  <-- PushEventPtr alone
```

`ExecEvent` ($C0:9A5A) promoted on its own, with nothing else from the slice, blacks the screen. It is
the first single function in this project that fails on its own: the earlier five single-root tests
(UpdateObjFrame, Decompress, DrawObjSprites, InitNPCs, ExecBattle) were all frame-identical, which is
why "failure tracks set size" looked true.

What the generated code looks like: the alias emits one exact AOT variant, `ExecEvent_M1X0`
(3,257 lines), and routes every other flag state to `interp_tier_dispatch` — with 36 further
mid-function bail-outs to LLE, most of them the RTS/RTL dispatch plumbing
(`interp_tier_dispatch_rewritten_return`, `cpu_dispatch_pc_from`, `..._popped_return`). An event VM is
exactly the shape that lives on computed returns, so this is the risky path.

Co-simulation finally earns its keep, comparing **two port builds** instead of port vs reference —
same runner, same APU boot path, so Gate-1 determinism applies and the APU noise disappears:

```
python tools/cosim/cosim_run.py --a-cmd "<baseline>" --b-cmd "<ExecEvent>" \
    --compare cpu,ram,ppu --stride 1 --max 420
=== FIRST DIVERGENCE at checkpoint 22 ===   (frame 22)
  A: A=433E X=FB44 Y=00F7 P=25   B: A=FB00 X=FB47 Y=FB46 P=A4
```

So the break is at **frame 22**, not at the fade: the boot path runs through `ExecEvent` and the two
builds part within the first third of a second. Every earlier "first bad frame 660" reading was the
downstream consequence of this.

## Step 9 — the first divergent call: `C0:BEF8` -> `C0:714A` at frame 21 (2026-11-17)

Tier-2 coverage needs `SNESRECOMP_TIER2_CAPTURE=1` as well as `SNESRECOMP_COVERAGE_DIR`; with only the
directory set nothing is written and the run looks silently uncaptured. Both builds, first 30 frames:

| build | fallback sites | sites not in the other |
|---|---|---|
| baseline (4 roots) | 37 | `C0:BEF8 -> C0:714A`, `missing_entry`, frame 21, 1 hit |
| ExecEvent promoted | 36 | none |

One difference, and it is exactly at the divergence: the baseline *interprets* `C0:714A` through the
LLE bridge; the ExecEvent build has it compiled (`bank_C0_714A_M1X0`, 1,298 lines of C) and calls it
directly. Co-sim says the two builds part at frame 22 — one frame after that call site.

So the chain is now: promote `ExecEvent` -> its closure compiles `C0:714A` -> the compiled `C0:714A`
does not behave like the interpreted one -> game state diverges at frame 22 -> nothing ever draws.

Next test (4 min): promote ExecEvent but demote `C0:714A` to `emit = false`. If the blackout goes away,
the defect is in that one function's generated code and `ExecEvent` was only the door that pulled it in.

## Step 10 — confirmed: the defect is `C0:714A`'s generated code (2026-11-17)

Demotion test: promote `ExecEvent`, force `C0:714A` to `emit = false` (name `_714a`, bank `0xC0`).
Regen then reports 20 roots / 8 exact AOT variants / 16 LLE variants and `bank_C0_714A_M1X0` is absent.

```
FIRST_DIFF none (identical through 660)
```

So with the same `ExecEvent` promotion and that one function left to the interpreter, the build is
pixel-identical to the baseline across the whole window. `ExecEvent` is cleared; **the generated code
for `C0:714A` is the defect** — 1,298 lines of C emitted for it, and the compiled version does not
behave like the interpreted one from frame 21 onward.

This is the first time the project has isolated a single function whose AOT output is provably wrong:
one function, one call site, one frame, and a four-minute reproduction.

Next: disassemble `C0:714A` from the authority JSON with the framework's own decoder
(`snesrecomp/recompiler/snes65816.py`) and walk it against the generated C line by line.

## Step 11 — the blackout was missing dispatch authorizations, not codegen (2026-11-17)

Promoting `ExecEvent` alone blacked the screen. The cause was two unresolved indirect dispatches:
the runner abandons them by design (`cpu_unresolved_abandon_balanced`, "handler side effects
SKIPPED"), state decays, and the CPU eventually reaches `BRK` at `$7E:0001`. Authorizing both sites
in the bank cfgs makes the promoted build pixel-identical to the baseline through frame 660:

```
recomp/bankc0.cfg:  bank = 0xC0
                    indirect_dispatch 9B59 203 idx:X tables:98C4
recomp/bankc5.cfg:  bank = 0xC5
                    indirect_dispatch 160 8 idx:X tables:06FE
```

| site | bank:pc | instruction | table | cases |
|---|---|---|---|---|
| `$C09B59` | `C0:9B59` | `JMP [$002A]` after `LDA $C098C4,X` | `C0:98C4`, 16-bit pointers | 203 |
| `$C50160` | `C5:0160` | `JMP ($06FE,X)` | `C5:06FE`, 16-bit pointers | 8 |

Verification: `abandons=0`, `FIRST_DIFF none (identical through 660)`.

### The recipe (this is the campaign technique)

1. Run the promoted build with the frame oracle; read `[unresolved-abandon] first hit site=$XXXXXX`
   from stderr. Note the **24-bit** site, then convert: cfg wants the **local 16-bit PC** and its bank.
2. Disassemble from the site with `snesrecomp/recompiler/snes65816.py` (`decode_insn(rom, off, pc, bank,
   m=1, x=1)`); find the indirect `JMP/JSR (tbl,X)` and its table base.
3. Read the table out of the ROM. Prefer the code's own bounds ladder (`CMP`, `BCC/BCS`) for the count —
   `count = upper - lower`. Only count by scanning for the pointer run when no ladder exists (that is
   how the 203-case table was sized, and it is the risky path).
4. Write `bank = 0xNN` then `indirect_dispatch <pc16> <count> idx:<X|Y> tables:<base>` into
   `recomp/bankNN.cfg` — the header is mandatory; without it regen fails with "missing 'bank = NN'".
5. `tools/regen.sh` (assert `REGEN=0`), rebuild, rerun the oracle, read the next site. Stop at
   `abandons=0`.

Two traps cost time here and are worth remembering: regen silently reuses cached published output, so
a failed regen still lets the build succeed against stale generated code (assert on the abandon site
disappearing, not just on `REGEN=0`); and `SNESRECOMP_WRITE_WATCH` only instruments WRAM fast paths, so
it cannot observe writes to I/O ports like `$2180`.

## Step 12 — full bank C0 promotion is clean (2026-11-17)

All 267 C0 roots promoted (1,362 exact AOT variants over 2,708 analysed functions, the configuration
that used to black the screen):

```
REGEN=0  BUILD=0  RUN=0
abandons=0  BRK=0
FIRST_DIFF none (identical through 660)
```

Pixel-identical to the baseline across the window, with no abandoned dispatches and no CPU
derailment. The two cfg directives from Step 11 are the entire fix.

### What the investigation actually concluded

There was no codegen defect. `ExecEvent`, `C0:714A` and every function the bisection implicated compile
correctly. What looked like "failure at critical mass" was coverage-driven: bigger promotions reach
more unresolved indirect dispatches, `cpu_unresolved_abandon_balanced` skips their handler side
effects by design, and the resulting state decay surfaces later — as a shifted palette fade, then as
`BRK` at `$7E:0001`. The 9-frame fade shift was the same failure observed downstream.

### Where the campaign stands

| bank | known instructions | share | state |
|---|---|---|---|
| C0 | 18,469 | 18.2% | promoted, oracle-clean, 2 dispatch sites authorized |
| C1 | 27,340 | 27.0% | not started |
| C2 | 16,634 | 16.4% | not started |
| C3 | 23,083 | 22.8% | not started |
| C5 | 762 | 0.8% | one dispatch site authorized (C5:0160) |
| D4 / E5 / EE | 15,033 | 14.8% | not started (EE holds the event scripts) |

Per-bank loop: promote roots -> regen -> build -> oracle -> read `[unresolved-abandon]` -> authorize
the site -> repeat until `abandons=0` and `FIRST_DIFF none`.

Reproduce the full-C0 state: `recomp/candidate-roots-c0.toml` (267 roots) appended to `recomp/symbols.toml`
after the four vectors, plus the two bank cfgs.

## Step 13 — the `.dbg` was never the blocker; C1 and C2 promote clean (2026-11-23)

Roots without the reference disassembly. `tools/gen_symbols.py --dbg` is now optional, because the
Step-12 note was wrong about *why* a missing `.dbg` roots nothing: bank membership already came from the
call target's own address (`jsr` keeps the caller's bank bits, `jsl` carries a 24-bit operand). What the
`.dbg` supplied was **names**, and an empty label dict dropped every root before output — that is the
whole of `labels 0  call targets 2430  rooted 0  by bank: {}`. Without it the generator now roots every
instruction-aligned call target and names it from its address (`sub_C1022A`); cfg names only have to be C
identifiers, so the 30-minute cc65 + disassembly rebuild buys the analysis nothing.

| promotion | root list | analysis | frames 390-1000 vs control | abandons / BRK |
|---|---|---|---|---|
| C0 (Step 12) | 267 | 2,708 roots, 1,362 exact AOT variants | identical | 0 / 0 |
| + C1 | 562 | 9,076 roots, 5,084 exact AOT variants | **611/611 identical** | 0 / 0 |
| + C2 | 425 | 13,564 roots, 7,618 exact AOT variants | **611/611 identical** | 0 / 0 |

No new dispatch authorizations were needed: the two from Step 11 still cover the route. Cost is now
dominated by compiling generated C — a fresh `build-c1` from scratch is ~8 min (25 MB exe), adding a bank
7 min (33 MB) — and both outlive a single SSH call, so the build has to be polled
(`ps -W | grep -c cl.exe`), not awaited.

### What this route can and cannot validate

Tier-2 coverage capture over 625 frames (title -> Start -> fade -> intro) on the control build, ranked by
interpreted instruction samples: `7E` (WRAM routines) 917, `C0` 888, `C5` 536, `C2` 121, `C3` 11, `00` 6.
**Zero C1, zero EE.** The authority's `source` field per instruction says why:

| bank | instructions | what it is (top source files) | runs in this route |
|---|---|---|---|
| C0 | 18,469 | `obj`, `event`, `map`, `player` | yes |
| C1 | 27,340 | `anim_cmd` 7,627, `menu` 4,463, `cursor` 3,817, `gfx_cmd` 3,685 | **no** |
| C2 | 16,634 | `anim_init`, `action_1`, `mask_2`, `init_2` | yes (121 samples) |
| C3 | 23,083 | `ending` 4,556, `field_menu`, `menu_common`, `equip` | barely (11) |
| C5 | 762 | `sound_main` | yes (536) |
| EE | 13,590 | `sprite`, `init`, `event`, `cutscene` | **no** |
| D4 / E5 | 1,443 | `menu_init_2` / `the_end` | no |

So C2's promotion is genuinely validated — its code runs inside the window and 611 frames come out
pixel-identical. C1's is promoted and boots clean, but **identical frames prove nothing about code that
never executes**: C1 is the menu / cursor / battle-animation bank, and no menu or battle has been reached.
That reorders the campaign. The next gate is a route to the in-game menu and the first battle (the old
"Next 5"), not more promotions: C3, EE, D4 and E5 would accumulate AOT that nothing exercises, and a
defect planted in any of them would stay invisible.

### Harness — `tools/oracle/`

| file | what it does |
|---|---|
| `tools/gen_roots.py` | rebuilds `recomp/symbols.toml` from HEAD's seed plus the candidate lists: `--done C0,C1 --bank C2 --lo/--hi` (slice for bisection) |
| `tools/oracle/refdump.sh TAG START END` | dumps frames from `build-c0` — the verified C0 control |
| `tools/oracle/runonly.sh TAG REF START END` | runs `build-c1`, greps abandons/BRK, compares to REF |
| `tools/oracle/cmp_frames.py` | BMP-vs-BMP, prints the first differing frame with its pixel count |
| `tools/oracle/capture.sh 0\|1 TAG` | tier-2 coverage capture; `tools/oracle/poll.sh` reads build progress |

Paths that worked: work dir `C:/Temp/c1`, input script `C:/Temp/oracle/script.txt`
(`wait 500` / `press start 4` / `wait 900` / `quit`), ROM `D:/roms/snes/ff3u11.sfc` (same sha256 as the
pinned dump). They are Windows scripts driven through `C:/Program Files/Git/bin/bash.exe`.

Three things that cost time here:

- **`ps -W | grep -iE "..."` inside a nested `bash -c` over ssh is parsed by cmd.exe**, which answers
  `'MSBuild' is not recognized as an internal or external command`. Put the polling in a script file.
- **A repo-wide phantom diff.** Every file was CRLF in the worktree and LF in the index, so `git status`
  reported 20 modified files with equal +/- counts and `git checkout` refused. `git config core.autocrlf
  true` in the repo *and* in each submodule (including `snesrecomp/lib/*`) cleared all of it; the tree now
  reports only real changes.
- **`regen` republishes `src/gen` for whatever `symbols.toml` says.** The committed seed symbols has 3
  vectors, so a build tree and `src/gen` can disagree with the branch; always regenerate before a build
  whose result you intend to quote.

## Step 14 — the route dies at frame ~5088, and the reference dies with it (2026-11-23)

The goal was a route to the in-game menu and the first battle so that C1 and C3 would actually execute.
It cannot get there. With input, and with none at all, the game forces blank about 85 seconds in
(~frame 5088) and never comes back: frames 5390, 11390 and 17390 of a no-input run are the same
single-colour frame, and the state dump at any of them shows `inidisp=0x80` (forced blank).

**It is not the AOT work.** `build-c0` (C0 promoted, Step 12) and `build-c1` (C0+C1+C2, 13,564 analysis
roots) produce byte-identical framebuffers at the stall — same md5 — and both report `abandons=0`,
`brk=0`. Removing the SRAM directory changes nothing either.

**The reference interpreter reproduces it exactly.** `ff6_cosim_ref` at `--frames 8000 / 12000 / 20000`
with `--input 2100:4:8` reports `video_active_frames=4022` and `video_frozen_max=911` at every one of
them: its picture also stops at ~frame 5089 — and its own final PC is `C0:B7A0`, which is the hottest
site in the port's coverage:

```
C0:B7A0: AD 10 42   LDA $4210      ; RDNMI
C0:B7A3: 10 FB      BPL C0:B7A0    ; spin until the NMI flag sets
C0:B7A5: A9 02      LDA #$02
C0:B7A7: 4C 5C 9B   JMP C0:9B5C    ; continue (event dispatcher)
```

**What the game is doing while stuck** — from a per-frame WRAM dump of frames 4950-5400:

| measure | value |
|---|---|
| last frame a large state write happens | 5078 (694 bytes for the last time), 5082 (65 more) |
| bytes that still change per frame after 5082 | 6: `$0045`, `$0046`, `$0055`, `$021D`, `$021E`, `$15EE` (the NMI indicator) |
| per-frame code, coverage differential 5000→5400 on the all-interpreted build | `C0:B7A0/B7A3` 1.14M each, `$7E:7206/7208` 558k each, `C0:0566/0568` 26.8k, event-VM loop `$7E:7528`/`$7E:7699` 11.4k |

`C0:0566` is `LDA $55 / BEQ` — another frame-tick latch, satisfied every frame. `$7E:7206` is the WRAM
copy of the same NMI wait (`LDA $17 / BNE`). So the event VM runs every frame, the frame ticks arrive,
and the script advances nothing. It is waiting on a **flag**, not a countdown (no counter decrements)
and not input (the joypad variables never change again — input processing is not running).

**Last thing before the freeze:** at frames 5078-5083 the event dispatcher at `C0:9B59` runs its biggest
batch yet (69 first-time targets) and two of those calls go to bank C5 — `C5:0004`, the sound driver. The
last APU port traffic anywhere in the trace is at frame 4924 (write `F0` to port 0, echoed back, then
`00`).

### Two hypotheses, and the instrument needed to separate them

1. **The game waits on the SPC700 sound driver.** Bank C5 is the last thing called, the sound driver is
   the one subsystem whose state the game can wait on indefinitely, and the blackout begins at a scene
   transition where the music changes.
2. **A shared device-model deviation.** The reference links the runner's *own* device sources, so
   port-vs-reference agreement rules out generated code and nothing else — a bug in the CPU bus, DMA/
   HDMA, PPU register or APU port model would appear in both. Everything FF6 does that SMW and MMX never
   did (its HDMA-heavy intro, its decompression DMA, its sound driver) is still suspect.

Separating them needs a view the project does not have yet: the debug server. `debug_server.c` is
compiled in and `host_main.c` calls `debug_server_init(port)`, but setting `SNESRECOMP_DEBUG_PORT=9123`
opens **no listener** in either build (0 LISTEN sockets over 6 s, no `failed to bind` line either), so
that path is either not reached in this host or stubbed. Once it answers, `call_stack`, `read_ram`,
`spc_dump`, `trace_addr` and `break_add` answer both hypotheses directly; the fallback experiment is a
build with `g_config.enable_audio = false` (forced `true` at `host_main.c:3127`) to see whether the
stall is audio-coupled.

### Instruments worth keeping (all verified today)

- **`dump <tag>` in the input script** writes a full state dump to `$SNESRECOMP_DUMP_DIR`:
  `.fb.bmp`, `.fb.bgrx`, `.regs.json` (inidisp/bgmode/tm/ts/hdmaen/dma), `.info.json` (frame, vpos,
  master_clock), `.wram.bin` (128 KB), `.vram.bin`, `.oam.bin`, `.cgram.bin`, `.sram.bin`,
  `.ppuw.tsv` (PPU register writes with the writing PC), `.dma.tsv`, `.apu.tsv` (APU port traffic,
  ring). Zero frames consumed.
- **The rest of the DSL**: `turbo on` (runs faster than real time), `until <addr> <op> <hex> <timeout>`
  (block on a WRAM byte/word), `poke`/`pokefor`/`forcepoke`, `savestate`/`loadstate <slot>`,
  `press <buttons> <frames>` with `+` combinations and `p2:` prefixes.
- **Per-frame WRAM dumping** (`SNESRECOMP_FRAMEDUMP_START/END`, no `PIXELS`) is the cheapest way to see
  *what stops*: diff consecutive `_wram.bin` files and count which bytes change per frame.
- **Coverage differential**: capture the same route twice, ending at frame A and frame B, and subtract
  per-`(site,target)` instruction counts — the remainder is the code that ran in (A,B] even when the
  bank is mostly AOT.
- **`tools/oracle/probe.sh <script> <tag> [build]`** runs an input script and collects the state
  dumps; **`tools/oracle/wdump.sh`** dumps WRAM every frame over a window for the change-count analysis.
- **The reference's tail counters** (`final_pc`, `pc_tail`, `video_frozen_max`, `audio_active_frames`,
  `joy_reads`) are a free timeline of what the other implementation is doing at its last frame.

## Resume here (new session)

**Head:** the Step-14 commit on `main`, tree clean, mirrored.
**Verified state:** C0+C1+C2 promoted (13,564 analysis roots / 7,618 exact AOT variants) and
pixel-identical to the C0 control across frames 390-1000 (611/611), `abandons=0`, `BRK=0`. C2 is
genuinely exercised by that window; C1 is not (it is the menu/animation bank).
`build-c1/Release/FinalFantasyViSNESRecomp.exe` is the C0+C1+C2 binary; `build-c0/...` the C0 control.

**The blocker, and the next action:**

1. **The game halts ~85 s into the boot (~frame 5088) and so does the reference interpreter** — see
   Step 14. Until that is understood, no route reaches the field map, the menu or a battle, and C1/C3
   stay unvalidated. Everything below is downstream of this.
2. Get the debug server answering (`SNESRECOMP_DEBUG_PORT` opens no listener today) — `call_stack`,
   `read_ram`, `spc_dump`, `trace_addr` and `break_add` are the instruments that separate
   "waiting on the sound driver" from "shared device-model bug". If it stays dead, the fallback is a
   build with `g_config.enable_audio = false` and a `C5:0004` breakpoint.
3. Then the route itself: name entry is not used on the SNES version, so `press start` at the title
   (~frame 2100), through the intro, to the Narshe snowfield; `press x` opens the in-game menu the
   moment the player controls Terra; the first battle follows the magitek walk. Re-run
   `tools/oracle/capture.sh` and require C1 and C3 samples in the cost histogram before believing any
   promotion of them.
4. Re-verify C0+C1+C2 on that longer route, then continue the campaign in order of *observed*
   execution (C5 first, then C3, EE, D4/E5).

**Traps that cost real time — do not relearn these:**
- `tools/regen.sh` reuses cached published output. After a *failed* regen the build still succeeds
  against stale `src/gen`, producing a meaningless `FIRST_DIFF none`. Assert on the abandon site
  disappearing, not on `REGEN=0` alone.
- Bank cfg directives must sit **above** the `# >>> BEGIN symbols.toml (generated — do not edit)` marker.
  Below it, regen owns the file.
- A bank cfg needs `bank = 0xNN` on line one or regen fails with "missing 'bank = NN' line", and
  `indirect_dispatch` takes the **local 16-bit PC**, not the 24-bit address the abandon log prints
  (`site=$C50160` -> bank C5, pc `0160`).
- Size tables from the code's own bounds ladder (`CMP`/`BCC`/`BCS`, count = upper - lower). Scanning for a
  contiguous run of plausible pointers is the risky fallback (that is how the 203-case table was sized).
- `SNESRECOMP_WRITE_WATCH` only instruments WRAM fast paths — it cannot see I/O-port writes like `$2180`.
- `SNESRECOMP_TIER2_CAPTURE` needs both the env var *and* a directory; with only the directory it writes
  nothing and exits cleanly.
- Coverage records only *interpreted* execution: a promotion that produces an identical frame set may
  simply not have been run. Check the cost histogram's bank mix before calling a promotion validated.
- Do not trust an ssh call's exit status for a long build — the channel dies first and the build keeps
  going (or is killed). Poll `cl.exe`/`link.exe` from a host script.
