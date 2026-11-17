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
