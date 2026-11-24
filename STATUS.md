# Project status

## Milestone 1 — boots to the title screen, and into the intro (2026-11-12)

Scaffolded from snesrecomp (`tools/new_project/setup_project.sh`, framework pinned
`6ca2dbbe`), generated with the LLE-first seed (4 roots: RESET/NMI/IRQ + one), built,
and run headless. All of this is reproducible from this file alone.

**ROM (the one this project is pinned to):**
`<rom-path>\Final Fantasy III (USA) (Rev 1).sfc` — sha256
`10eccc5d2fab81346dd759f6be478dcb682eef981e8d3d662da176e1f9a996bc`, crc32 `c0fa0464`.
HiROM + FastROM, 3 MB, 8 KB battery SRAM, no coprocessor, checksum valid.

**Reference disassembly:** `everything8215/ff6` (ca65/ld65, GPL-3.0). `make rip` +
`make LANG=en ROM_VERSION=1` reproduces *our* dump byte for byte (verified with `cmp`
and sha256). It emits `build/en/rom/ff6-en.dbg` — 400,869 address→source-line records
and 13,928 named labels. That is the analysis authority this port should be built
against; nothing of it is committed here.

**Toolchain:** VS2022 BuildTools, CMake 4.4.5 at `<toolchain>\cmake`, rustc 1.97.1
`x86_64-pc-windows-gnu`, python 3.13 (miniconda), ninja at `<toolchain>\bin`,
winlibs gcc 16.2 at `<toolchain>\mingw64`.

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
  --rom '<rom-path>\Final Fantasy III (USA) (Rev 1).sfc' --script boot.txt
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
    --final-frame-dump <scratch>\refframes\final620.ppm
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
`<scratch>\oracle\{ref,port2,port3,port_c0}`):

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

Harness: `<scratch>\bisect\{gen_trial.py,cmp.py,trial.sh}` — select a contiguous slice of
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

Paths that worked: work dir `<scratch>/c1`, input script `<scratch>/oracle/script.txt`
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

## Step 15 — the route works: battery save to field map to in-game menu (2026-11-23)

The Step-14 halt blocks the *prologue*, not the game. An FF6 (US) battery save skips it entirely and lands
in the Narshe mines with Terra, Wedge and Vicks: field map, in-game menu, everything downstream of it.

### The save, and the timings that actually work

- Source: fantasyanime.com's FF6 save set, `saves/ff6save01Perfect_NarsheMine.zip` ->
  `01-Narshe Mines.srm` (8192 bytes, 760 non-zero). Drop it at `<build>/Release/saves/save.srm`; the port
  loads it as-is, no conversion, no patching. Keep it out of the repo (it is third-party save data) —
  `tools/oracle/srm_route.sh` takes the path as an argument.
- The slot screen renders the name as `?????` while everything else resolves (LV 4, party HP/MP, `0:23`,
  steps 301, and the donor save's maxed GP). The data loads and plays.
- Measured on a 256x224 dump sweep with a press every 150 frames: `press start` at 2100 -> slot list
  ~2400 -> next press selects the slot and shows "This data?" ~2550 -> next press confirms and the map
  loads, gameplay from **~2700**. `press x` on the field opens the in-game menu, `b` closes it.
  Extra presses are harmless, so the robust form is the loop in `tools/oracle/srm_route.sh`: twelve
  `wait 140 / press a 8` entries after `press start`, whatever lands, gameplay arrives by ~2700.
  Do not hand-schedule individual presses — the slot list appears ~300 frames after Start and an earlier
  press is simply swallowed, which is what made the first four attempts look like the confirm press
  "not working".

### Frame oracle: clean over gameplay and menu code

Eight dump points across the route (map, menu, cursor, submenu, closed, walk, post-walk) are all
byte-identical between `build-c0` and `build-c1` (C0+C1+C2), `abandons=0` in both logs. The C1/C2
promotion now has an oracle window over *gameplay and menu* code, not just boot and title.

### Coverage: C3 is now the top target, C1 is still untouched

`build-c1`, 4,209 frames, 2,869 interpreted C3 sites:

| bank | interpreted instructions | sites | new-code hits |
|---|---|---|---|
| 7E | 27.8M | 1,514 | 93,946 |
| C0 | 19.7M | 2,713 | 17,552 |
| C3 | 8.94M | 2,869 | 52,626 |
| C5 | 5.86M | 636 | - |
| C2 | 670K | 64 | 0 |
| C1 | 0 | 0 | 0 |

Top C3 targets: `C3:12FC` (24,320 hits, the menu's main loop), `C3:1203`, `C3:1227`, `C3:0B12`,
`C3:1221`, `C3:0B2A`. Two things fall out of this:

- **C3 is the next promotion, and it is now provably exercised** — 23% of the remaining campaign, and the
  menu runs it millions of instructions per route.
- **C0 still interprets 19.7M instructions** because only its 267 roots are AOT and the field engine's
  main loop is not one of them. Extending C0 roots is cheaper per instruction than any new bank.
- **C1 runs zero instructions** — it is `anim_cmd`/`gfx_cmd`, i.e. battle-side, and no battle happens yet.
  `ram_routines_overflow: 40,662`: FF6 runs enough code out of WRAM that the coverage tool's RAM-routine
  table overflows, so all `7E` attribution is lossy.

### New defect: walking the field crashes both builds

About 150-300 frames of movement (five direction presses) segfaults:

```
*** SEH CRASH code=0xC0000005 at 00007FF6F87F60C5, last recomp func: (none) ***
    access violation: read at 0x0000022FD705CDA8
  CpuState: A=0000 X=0029 Y=0000 S=15F9 D=0000 DB=00 PB=C0 P=22 m=1 x=0 e=0
```

- identical crash, identical frame, in `build-c0` and `build-c1`, `abandons=0` — not the promotions;
- `last recomp func: (none)` and a wild 44-bit read address put the fault in host/interpreter code, not in
  a generated function;
- reproducible: five direction presses crash it, two short taps (96 frames of movement) and a
  1,000-frame idle do not — the shape of a random encounter, though the trigger is not proven. A minidump
  lands in `<build>/Release/crash_minidump_*.dmp` and the coverage JSON survives the crash, so the window
  between the `r_closed` dump and the fault is where to bisect.
- Consequence: **battle code (C1, C2 battle paths) still cannot be validated.** C3 can.

Measurement caveat worth keeping: during gameplay the dump's register snapshot reads `inidisp=0x80`
(forced blank) while the framebuffer has full content (`luma~23`, ~97 colours). The dumps agree with each
other and across builds, so this is snapshot timing in the dump, not a rendering difference — but never
read `inidisp` alone as "blank" when the picture has content.

## Step 16 — C3 promoted: oracle clean, 26% of its interpretation gone, the rest is one wait loop (2026-11-23)

Chosen because Step 15's coverage made it the top executed un-promoted bank: 8.94M interpreted
instructions, 2,869 sites, 52,626 new-code hits on the 4,209-frame save route.

### Pass 1 — roots only

- `gen_symbols.py --bank C3` -> **1,033 call targets**, address-named `sub_C3xxxx` (the `.dbg` is still not
  needed; Step 13 settled that).
- `gen_roots.py --bank C3 --done C0,C1,C2` -> 2,290 funcs; regen published **25,644 roots / 12,217 exact
  AOT variants / 17,263 LLE** and 15 `bankc3_part*.c` files (asserted on, so this is fresh output and not
  the stale-cache trap).
- Oracle on the save route (`srm_route.sh <srm>`, 4,301 frames): **all five dump points byte-identical**
  to the C0 control, `abandons=0` in both logs, no crash.
- Coverage against the same route before the promotion: C3 **8.94M -> 6.65M** interpreted instructions
  (25.6% fewer), 2,869 -> 1,471 sites, new-code entries 52,626 -> **6,887** (87% fewer).
  `C3:12FC` — the top target beforehand at 24,320 hits — has left the interpreter entirely.

### Where the remaining 6.65M is: three wait loops

| address | interpreted instr | entry_mx |
|---|---|---|
| `C3:1370` / `C3:1372` | 3,037,631 each | M1X0 |
| `C3:11CE .. 11FA` | 33,344 each | M0X0 |
| `C3:1577 .. 1583` | 7,490 each | M1X0 |

Those three are 6.32M of 6.65M — 95% of what C3 still interprets is loops, the hottest at ~706
instructions per frame (~353 iterations per frame waiting for vblank, the same shape as `C0:B7A0` in
Step 14). They are idle waits, not throughput.

### Two attempts to convert them — both oracle-clean, both ineffective

1. **Rooted the loop heads explicitly** (`recomp/candidate-roots-c3.toml`, 1,033 -> 1,036 roots). The code
   was emitted (`bankc3_part00_v2.c` contains `sub_C31370`, `symbols.toml` has it), frames stayed
   identical, interpretation did not change.
2. **`entry_mx_at` overrides** in `recomp/bankc3.cfg` — `1370 1 0`, `11CE 0 0`, `1577 1 0`, the documented
   hook ("kept outside auto-ingested func blocks") for exactly this. Frames identical, emit stats
   identical, interpretation identical.

Conclusion: the residual is **not** an MX-variant problem. The likely cause is that these addresses are
*interior* to functions the generator cannot bound — nothing `jsr`/`jsl`-targets them, so the analysis is
not exact and the root emits LLE regardless of entry width. The next lever is a boundary hint
(`end_at` / `authority_data`), not another `entry_mx_at`. Cost/benefit is against spending more on it
now: it is a wait loop.

### The bigger prize is C0, not more C3

C0 still interprets **18.4M** instructions despite its "full" promotion because only its 267 roots are
AOT and the field engine's main loop is not one of them; C5 interprets **5.86M**. If the goal is "as much
of the game AOT as possible", extending C0's root list is the single largest win available, and it needs
no new bank work.

## Step 17 — the walk crash is the mosaic renderer, and battles now run (2026-11-23)

Steps 14–16 left a walking crash (`0xC0000005`) that reproduced in both builds with identical
emulated-CPU state (`A=0000 X=0029 S=15F9 P=22`), an address ending `0xCDA8` every time, and
`last recomp func: (none)`. Three sessions read that state as a signature of the game's own code.
It was the arithmetic of a renderer bug.

### The instrument that was missing

- `Release` is built without `/Zi`, so `build-c1/Release/` has **no PDB** and the faulting address
  had no name. That is why the crash stayed anonymous.
- `build-dbg` (Visual Studio generator, `RelWithDebInfo` = `/Zi /O2 /Ob1`) reproduces the fault
  identically — same CpuState, same fault-address pattern — so symbolization is valid for the
  Release binary's behaviour.
- `tools/oracle/symdump.py` (new) parses the minidump directly: exception stream for the faulting
  address, module list for the load base (→ RVA), `CONTEXT_AMD64` for RIP/RSP/argument registers,
  then a stack scan for code-pointer candidates. Everything is symbolized through
  `llvm-symbolizer --relative-address ... --pdb=`.
- Result: **`PpuDrawBackground_4bpp_mosaic`, `snesrecomp/runner/src/snes/ppu.c:1830`.**

### Root cause

```c
int w = mosaic_size - (sx - PpuMosaicAt(ppu, sx));   // run length of this mosaic cell
...
do {
  if (VIEWPORT_ALLOWED(i) && z > dstz[i])            // <- crash
    dstz[i] = pixel + z;
} while (++i != w);
```

`mosaicModulo` (which `PpuMosaicAt` reads) is rebuilt by `PpuPrepareMosaic` only when
`PPU_mosaicSize` changes at line start. FF6 writes **`$2106` by HDMA on every scanline** in the
Narshe mines — 2,568 HDMA writes of `0x00` across 20 field dumps, i.e. mosaic size 1 — while the
table can still describe a larger cell. `w` then goes **negative**, `while (++i != w)` can never
terminate (`i` counts up through every integer and never equals a negative `w`), and `dstz += w`
walks the zbuffer pointer *backwards* until it reads unmapped memory.

The crash context proves it: `Rbp = 0xFFFFFFFA` = **-6** = `w`, `Rdi = 0xFFFFFFF8` = **-8** =
`8 - 0` = the cell offset. The guard's own diagnostic agrees: `sx=8 size=2 cell=0 -> 1`, and
`2 - 8 = -6`.

### Fix

`PpuMosaicRunLength()` clamps a non-positive run length to one pixel (plus a diagnostic capped at
eight prints), applied at all three mosaic draw sites: the widescreen/`WsShadow` 4bpp path, the plain
4bpp path, and the mode-7 mosaic path. Carried as a local `snesrecomp` submodule commit plus an
exported patch under `patches/framework/`. **Upstream-worthy:** any game that HDMA-writes `$2106`
mid-line can trip it. The mosaic effect itself is unaffected — a run length of 0 means "draw one
pixel", which is what non-mosaic drawing is.

### Verification

- Walk route (the exact route that died): `rc=0`, **20/20 dumps** (previously 12 then `0xC0000005`),
  no crash, guard fired 8× (cap reached).
- The 12 pre-crash frames are **byte-identical** to the unpatched run — the fix changed no pixels.
- Canonical oracle `srm_route.sh <srm>`: **5/5 dump points identical** between the unpatched C0
  control and the patched build over 4,301 frames.
- Behaviour: walking past the old crash point now triggers a random encounter — three Were-Rats vs
  Terra (`?????`) and Wedge in magitek armor, enemy-name and HP windows live.

### What this changes

- **Battles are reachable.** C1 and C2 battle code — promoted in Step 13 with zero observed execution
  — now runs. This is the first route that executes it.
- The crash was never about the game. It is a renderer robustness bug, and the trigger is an HDMA
  register write the field engine does constantly.
- The Step-14 prologue halt is untouched and still parked.

## Step 18 — battles run: three C1 dispatches authorized, the fight resolves (2026-11-23)

Step 17 made the field walkable, so the first random encounter finally happened. It exposed the
next layer: battle code is built almost entirely from **computed jump tables**, and every unresolved
one freezes the battle in a counter handshake.

### The stall, and what it was

- `C1:0000` is a dispatcher: `PHA / TDC / PLA / ASL A / TAX / JMP ($0008,X)` — A selects one of four
  handlers (`$01EC $01FB $0257 $024C`). The recompiler never resolved it, so the port *skipped the
  handler's side effects* (`[unresolved-abandon] site=$C10005 ... hrv=3`).
- `C1:1A24` is the handshake the battle waits on: `INC $47 / LDA $47 / BNE $1A26` — bump DP `$47`,
  spin until something zeroes it. With the handler skipped, nothing did.
- Measured: DP `$47` = 1 in the dump, the branch taken **999,997 times** per capture window, and
  the run crawling at **~1.5 s/frame** (38 minutes for ~1,500 frames) until it was killed.
- This is the same authorization gap as Step 12's `C0:9B59` and the sound driver's `C5:0160` — it
  only appeared now because battle code had never executed before (C1: 0 hits on every prior route).

### The fixes — three directives, found one run at a time

`recomp/bankc1.cfg`, above the regen marker:

```
bank = c1
indirect_dispatch 5E4E 32 idx:X tables:5E51
indirect_dispatch 450F 48 idx:X tables:4514
indirect_dispatch 0005 4 idx:X tables:0008
```

- Sites arrive one per run, as the documented loop says: authorizing `0005` removed the 38-minute
  crawl but exposed `450F` (frame 5860) and `5E4E` (frame 7348).
- Counts come from the tables themselves: entries are two-byte code pointers, so scan until a value
  stops landing in the bank's code (`450F` → 48 entries, `5E4E` → 32, `0005` → 4, each a clean break).
- Emit stats moved with every authorization, which is how a directive is confirmed applied:
  exact AOT variants **12,229 → 12,273 → 12,391**, LLE 17,275 → 17,497.

### Verification

| | before | after |
|---|---|---|
| `unresolved-abandon` | 1, then 2 | **0** |
| walk+battle route | 38 min for ~1,500 frames | **8,481 frames in 143 s** (~17 ms/frame) |
| DP `$47` (handshake) | stuck at 1 | moves again (49 mid-fight) |
| crashes | none | none |

Behaviour: the encounter is fought to a finish — `f50` (frame 7749) is the **field map**, i.e. the
victory sequence ran and the game returned to Narshe's mines; the final dump is a *second* encounter
triggered by walking on. Battle start, battle execution, victory and return-to-field all execute.

### The route that reaches it

`tools/oracle/battle_route.sh`: save slot → field → 12 alternating direction taps (encounter) →
50 × `wait 30 / press a 8` → walk on. Two lessons are baked in:

- **Once the fight starts, press A only.** Direction presses walk the command cursor, and the fight
  then sits in a menu forever looking exactly like a hang.
- **Kill the old process before rebuilding.** A killed run leaves `FinalFantasyViSNESRecomp.exe`
  holding its own image, and the next build dies with `LNK1104: cannot open file ...exe`.

## Step 19 — promoting C1 coverage gaps: 30 roots, 56% of C1's interpretation gone (2026-11-23)

> **Correction (Step 21): the 56% below counts interpreted *entries*. Measured by
> `costs[].interpreted_instructions` the same promotion moved 104,636,202 -> 104,628,241
> (-0.008%). Read Step 21 before quoting these numbers.**

First promotion driven by battle coverage rather than by bank size.

### Baseline

Battle-route capture (`battle_route.sh <srm> build-c1 --coverage`, 8,481 frames):
**163,941 interpreted hits across 547 sites** — 7E 93,946, C1 35,282, C0 28,884, C3 3,778,
C2 2,051. The failure classes split cleanly: **143,811 hits `missing_entry`** (no compiled function
at the target at all) versus **20,130 `missing_exact_variant`** (function exists, wrong M/X mode).
So the bottleneck was promotion, not cfg modes.

C1 was the target: 0 hits on the field-only route, then 35,282 once battles ran, top 10 sites = 47%.

### What was promoted

The top 30 C1 `missing_entry` targets by hit count (22,800 hits covered), appended to
`recomp/candidate-roots-c1.toml` with `emit = true` and an address-derived name.

- Emit: 25,668 → **25,916 roots**, 12,391 → **12,479 exact AOT variants** (+88), LLE 17,497 → 17,663.
- **The counter moving is the gate, and it is what separated this from Step 16.** Rooting C3's hot
  loop heads moved nothing because they were interior addresses with no observed edge; these are
  `goto_gap` targets — the coverage recorded an actual jump into them — so the analysis can enter
  and compile them.

### Verification

- Pixel oracle on the same battle route: **7/7 dump points byte-identical** to the pre-promotion
  run, `abandons=0`, no crash (8,481 frames both times).
- Re-captured coverage, same route: total **163,941 → 144,260** (−12%), sites 547 → 523.
  **C1 35,282 → 15,601 (−55.8%)**, C1 sites 198 → 174. C0, C3, C2 and 7E are unchanged to the hit,
  which is the control that says the promotion did what it claimed and nothing else.

### What is left

- **C1 15,601 across 174 sites** — a long tail now, not a hot spot. Promoting the whole remaining
  set is a larger job for a smaller return; sample it before deciding.
- **7E 93,946 is not promotable**: those are RAM-resident routines (`call_gap/missing_entry` at
  `$7E53E9` etc. — code the game copies into WRAM and runs from there). Also note the figure is
  *identical* across runs of different length, so it reads as deduped or capped accounting rather
  than a per-run count; treat the total as a floor, not an exact number.

## Step 20 — Mode 7 works: the world map renders and the matrix is driven per scanline (2026-11-24)

Mode 7 was the last PPU path with no route coverage. Everything until now ran in mode 1 (field,
menus) or mode 9; the opening's snow march only *pokes* `$211B`/`$211C` while the display stays in
mode 1, so nothing had ever executed the perspective renderer.

### Getting there

A world map needs a save that is standing on it. `ff6save18Perfect_Maranda` ("you now have an
airship, and the freedom to roam around the World of Ruin") is exactly that — and the donor `.srm`
stays out of the repo, as always; it is a path argument to `tools/oracle/mode7_route.sh`.

Route: load the slot → `m1`/`m2` on the world map → `A` to board the airship (`m3`) → fly north,
east, south (`m4`–`m6`). Flying matters: a static world map exercises the matrix, but only turning
the airship varies all four terms.

### What the machine says

- `bgmode = 7` in all six dumps. `$2105` is written **4× per frame with 2 distinct values**, i.e.
  the frame switches to mode 7 for the map and back to mode 1 for the HUD strip.
- `$211B`–`$211E` (M7A–M7D) are written **450× per frame each**, with **~110 / 39 / 1 / 110
  distinct values** while flying straight — one matrix row per scanline, which is what a
  perspective plane *is*.
- After turning: distinct counts go to **101 / 106 / 92 / 101**, i.e. `$211D` (the rotation term)
  stops being constant. That is the airship turning, not a stuck matrix.
- All six frame hashes differ; `abandons=0`, no crash, 4,719 frames in 80 s.
- Visually (`m1`): the World of Ruin from the air — Mode 7 terrain receding into a banded horizon,
  the airship sprite centred, minimap lower-right. Coherent, no striping or garbage.

### Why this one has no frame oracle

The reference driver cannot load a `.srm` (`ref_driver.c` takes only frames/dump/input), so there is
no independent second implementation to diff against on the world map. What stands in for it: the
register evidence above (mode 7 + per-scanline matrix + a turntaking rotation term) plus a visual
check. That is weaker than the byte-identical dumps used for every promotion — worth remembering if
a world-map rendering bug ever appears. Adding SRAM support to `ref_driver` (~30 lines) would close
the gap and also give the walk/battle crash class a second implementation to compare against.

### The tool

`tools/oracle/mode7_route.sh <worldmap.srm> [build-dir]` — installs the save, runs the six-dump
route, and prints `bgmode`, the matrix write counts and the distinct-value counts per dump, so
"working" is a report rather than an impression.

## Step 21 — metric correction: 73% of "interpreted instructions" is three poll loops (2026-11-24)

Before choosing another step, I measured the thing Step 19 should have measured: a clean run (no tier-2
capture, so no forced interpretation) reports **74.54% interp / 25.46% AOT cycles**, not the 97% a
capture run reports. The capture inflates interpretation by construction. Fine — but the deeper problem
was the *ranking* metric.

### The correction

Step 19 reported "C1 interpretation 35,282 → 15,601 (−56%)". Those were **entry counts**. The tier-2
manifest also carries an instruction-weighted table (`costs[]`, `interpreted_instructions` per
PC/x-mode), which nobody had read:

```
before (b5)  104,636,202 interpreted instructions
after  (b7)  104,628,241 interpreted instructions   -> -0.008%
```

The promotion removed 12% of interpreted *entries* and 0.008% of interpreted *instructions*, because
the gap targets it compiled were entered often and ran briefly, while the volume lives in long-lived
loops. Every future pass must rank by `costs[].interpreted_instructions`, never by `observed_hits`.

### What the volume actually is

```
C0:0566/0568  M1X0  16,674,156 each  (31.8%)   LDA $55 / BEQ  -- spin until NMI sets $55
C1:1A26/1A28  M1X0  11,144,122 each  (21.3%)   INC $47 / LDA $47 / BNE -- handler handshake spin
7E:7206/7208  M1X0  10,580,118 each  (20.2%)   WRAM-resident flag spin
                                       73.4% of all interpreted instructions
```

Decoded `C0:0564`: `STZ $55 / LDA $55 / BEQ -4 / STZ $55 / RTS` — a wait-for-frame-flag spin, ~1,965
iterations per frame at 8,481 frames. `C1:1A24` is the battle-side equivalent (~1,300/frame). These
are the game *waiting*, which is what games do; they cost wall time, not correctness.

Real code is 27,831,449 instructions (26.6%): **C5 7,971,770 (28.6% of non-poll), 7E 6,632,766, C3
4,283,547, C1 4,215,789, C0 3,473,708, C2 1,233,359**. The top non-poll loops are C3:1370/1372 (1.92M
each — the loop Step 16 could not root) and C5:01B5/01B8, C5:06AF/06B2.

### What this changes

1. **Promotion is close to done for its cost/benefit.** The remaining 27.8M is spread over hundreds of
   buckets and, at 17 ms/frame, is not what limits the project. Another gap-target pass is polish.
2. **The polls are the interesting target, for a non-performance reason.** A poll that never resolves
   is exactly the failure mode that burned a whole session on `C1:0000` (38 minutes crawling on a
   never-cleared `$47`). Recognising these three spins and yielding to the host would turn "flag never
   set" from an invisible crawl into an immediate, diagnosable stall — and would collapse the
   interpreted share as a side effect. The framework already has poll-shape recognition machinery.
3. **`7E:7206` is WRAM-resident and AOT cannot reach it** — spin handling is the only lever there.

Recorded rather than fixed: the honest state is "the port runs the whole game, one quarter of cycles are
AOT, three quarters are 73% idle polling, and the metric I was reporting was entry counts".

## Step 22 — measuring the polls instead of optimising them: no work needed (2026-11-24)

Step 21 left one question open: 73.4% of interpreted instructions are three flag-poll spins, so should
they be fast-forwarded? Measured before building anything:

```
turbo on, Mode 7 route:  4,197 frames in 15.8 s = 265 fps
paced (no turbo):        4,719 frames in 80 s   =  59 fps
```

**4.5× headroom.** The port is not CPU-bound; the spins cost nothing observable at real time. A
poll fast-forward would be a framework patch with a real regression surface (the Star Ocean `$4212`
table shows how carefully those have to be validated per site) for a win nobody can see.

The remaining motive was diagnosis — naming a stall instead of crawling. That is already covered:
the post-mortem cost table reports the top interpreted PC after any run, which is exactly how the
38-minute `C1:0000` crawl was diagnosed (`[interp_cap] entry=$C10005 last=$C11A28` plus `C1:1A26/1A28`
at the top of `costs[]`). A live watchdog would surface the same fact earlier; it is convenience, not
capability.

**Decision: no poll work.** The three hot spins are the game waiting for its own frame flag, faithfully
reproduced. Do not revisit without a measurement showing the port CPU-bound (i.e. turbo ≈ paced fps).

### What that means for the campaign

Every instrumentation question now has an answer: what's reachable (routes), what's wrong (symbolised
crashes), what's unexecuted (coverage by instruction), what's fast enough (turbo headroom). The open
risk is no longer performance or tooling — it is **unvisited content**. Every real bug so far was found
by running somewhere new: the mosaic crash by walking, the three dispatches by fighting. Field, battle
and world map are covered; the opera, towns, shops/esper menus, multi-party battles and the ending are not.

Next step is therefore scene coverage, not optimisation: one save per unvisited region, one standard
poke-around route, and the same three acceptance checks (`abandons=0`, no `SEH CRASH`, frames sane).

## Step 23 — scene coverage: opera, Kefka's Tower, World-of-Ruin battle — no bugs, and bank EE is untouched (2026-11-24)

Three saves from the community set (`fantasyanime` ff6save10/13/29) run through the standard poke route.
All three load and run: `abandons=0`, `crashes=0`, 5,083 frames on the fixed route, 7–8 dumps each, three
visually distinct scenes (identical hashes across saves would have meant the save never loaded — checked).

What the frames show:

- **Opera House** — interior, working dialogue box (`LOCKE: Right! If we could only grab him then...!`).
  A deeper run (8,903 frames, 14 distinct dumps, coverage on) reached the **stage performance dialogue**
  (`But I don't want Maria to be abducted...!`) and then the backstage decoy scene
  (`LOCKE: That's why the decoy! We'll hide Maria somewhere safe!`). Font, dialogue layout, sprite layering
  and tile alignment all correct.
- **Albrook (World of Ruin)** — not the town: the route walked into a **WoR field battle**, Terra + Locke
  with HP windows and `Fight / Morph / Magic / Item`. Menu, cursor and status windows all correct.
- **Kefka's Tower (save 29, "Final Battle")** — the interior before the fight, one party sprite, coherent.
  The final battle itself needs navigation the poke route did not do.
- **Ending** — save 30 ships only a `.zst` savestate, no battery save, so it is unreachable this way until
  something can load savestates.

**No bugs.** Three new scenes, ~11,000 frames, zero abandons, zero crashes, every frame coherent. That is
the first evidence the mosaic fix was not a one-off and the port holds up outside the routes it was built on.

### The one real finding: bank EE

Coverage on those runs, against the 857 sites already observed:

| scene | sites | new sites | where they are |
|---|---|---|---|
| operahouse | 213 | 45 | **EE 41**, C0 2, C3 2 |
| albrook | 365 | 66 | **EE 47**, C1 14, C2 3, C0 2 |
| finalbattle | 172 | 2 | C3 2 |
| opera (deep) | — | see run | EE-dominated |

Every new EE site reports `fallback_reason=missing_entry` — the bank has no compiled functions at all, so
all event/sprite/cutscene code runs in the interpreter. That is the largest wholly-unpromoted frontier left,
and it is the bank every story scene lands in.

Sizing it before spending a build is free: `tools/gen_symbols.py --bank EE` gives the root count, and
`costs[]` gives the instruction share. Do that first — the Step-21 lesson is that a promotion's headline
should be measured in interpreted *instructions*, not entries.

## Step 24 — bank EE promoted from coverage: −6.53M interpreted instructions, frames identical (2026-11-24)

The biggest real win of the campaign, and it came from 37 roots.

### Why static analysis could not do it

`gen_symbols.py --bank EE --stats`: **2,430 call targets, only 97 rooted**. EE is entered through computed
dispatch (event scripts, sprite/object tables), so almost none of it is reachable as a `jsr`/`jsl` target
from analysed code. Promoting EE the C1/C3 way would have added 97 addresses and compiled little of the
bank that every story scene actually runs.

Coverage had already *observed* those entries. `recomp/candidate-roots-ee.toml` holds the 37 EE targets
that were both seen as `missing_entry` and costed, ranked by `interpreted_instructions` (Step 21's rule).

### Gates

```
emit       25,916 -> 26,300 roots, 12,479 -> 12,789 exact AOT variants (+310), LLE 17,663 -> 17,747
build      46,951,936 B
oracle A   battle route   7/7 dumps byte-identical to b8,   abandons=0 crashes=0
oracle B   opera deep    14/14 dumps byte-identical to the pre-promotion run, abandons=0 crashes=0
```

### Effect, measured in instructions

```
opera route total   119,830,328 -> 113,312,265   (-5.4%)
        bank EE      20,572,000 ->  14,038,873   (-6,533,127, -31.8%)
        EE share          17.2% ->       12.4%
```

### The estimate was wrong by two orders of magnitude, in the good direction

The selected 37 PCs carried only **43,268** instructions of cost data — 0.1% of EE — so the honest
prediction was "this will do nothing". It removed **6.5M**, because the cost table only counts PCs the
capture attributed to EE, while rooting a dispatcher entry lets the whole call subtree compile at once.

Lessons worth carrying:

- **Rank by instructions to choose roots, but do not predict a promotion's size from the root's own cost.**
  A dispatcher root's value is its subtree. The only reliable measure is before/after on the same route.
- **A promotion can be large and still be free of risk when two routes are byte-identical** — the battle
  route and the opera route exercise different banks, so both passing is meaningful corroboration.
- 37 roots is cheap: this took one regen, one build, two route runs. There was no reason to leave EE
  unpromoted for the whole campaign except that nobody had looked at where the code was entered from.

### Where this leaves the interpretation budget

EE is down to 12.4% of the opera route. The remaining bulk is the three flag-poll spins (Step 22, not worth
touching) plus C5 (sound driver) and 7E (WRAM-resident, not AOT-able). The next promotion candidates should
be chosen the same way: find banks whose entry points are only reached by computed dispatch, and root what
coverage actually saw.

## Step 25 — walk regression clean; promotion is exhausted at 0.04% (2026-11-24)

### Regression

The walk route is the only route with a crash history (the mosaic fault), and it had not been exercised since
the three dispatch authorizations and the EE promotion landed. Against the current build (46,951,936 B):

```
rc=0  5,515 frames  crashes=0  minidumps=0  abandons=0  dumps=20
mosaic guard hits=8   (the fix still engaging, as designed)
walk frames vs the post-fix run: identical=20 different=0
```

No regression. The mosaic fix, the dispatches and EE all coexist.

### Promotion is finished as a lever

Across all seven captures (715,395,075 interpreted instructions), classified by cause:

```
three flag-poll spins            478,562,107   66.9%
bodies with no entry row         189,973,204   26.6%
WRAM-resident (not AOT-able)      46,429,362    6.5%
missing_entry                        280,719    0.04%
missing_exact_variant                120,122    0.02%
compiled_entry_available_check        29,561    0.004%
```

Every remaining `missing_entry` target in every capture now totals **320,027 instructions** (C0 186k, C1 62k,
EE 46k, C3 22k, C2 3k). Another root-promotion pass would be fighting over four hundredths of a percent.

Combined with Step 22 (265 fps turbo vs 59 paced), the conclusion is that code generation is no longer where
the project's value is. Two thirds of the interpretation is the game waiting for its own frame flag — correct
behaviour, faithfully reproduced — a fifteenth is WRAM-resident code AOT cannot reach by construction, and the
rest is the bodies of functions that are already entered through compiled entries.

### What that leaves

1. **The prologue freeze (Step 14) is now the top defect, not a parked curiosity.** Every route validates from
   a battery save; the game still cannot be played from power-on. Both the port and the reference interpreter
   freeze at ~frame 5088 with the same PC (`C0:B7A0`, `LDA $4210 / BPL`), which points at a shared device-model
   issue (APU/SPC handshake or NMI timing) rather than generated code — harder, but it is the only known bug
   left and it is the difference between "validated" and "playable".
2. **Scene breadth** — shops, the esper/magic menus, the final battle proper, and the ending (which needs a
   savestate loader, since save 30 ships only `.zst`).
3. **`ref_driver` SRAM support** (~30 lines) — a second engine for any save route.

Not worth doing: more root promotion (0.04%), poll fast-forward (no headroom need), symbol work (crashes are
already symbolised).

## Resume here (new session)

**Head:** the Step-25 commit on `main`, tree clean, mirrored.
**Verified state:** 2,360 funcs; **26,300 roots / 12,789 exact AOT variants / 17,747 LLE**; build 46,951,936 B.
Routes that pass with `abandons=0`, no crash, byte-identical frames: battle (7 dumps), opera deep (14),
walk/mosaic repro (20), Mode 7 world map (6). Clean battle route 74.5% interp / 25.5% AOT; turbo 265 fps vs 59
paced.

**Promotion is done (Step 25):** remaining `missing_entry` across every capture = 320,027 instructions of
715,395,075 (0.04%). Interpretation is 66.9% three flag-poll spins, 26.6% bodies of already-entered functions,
6.5% WRAM-resident (not AOT-able). Do not promote further without a new reason.

**The top defect is the prologue freeze (Step 14).** Port and reference both stop at ~frame 5088, final PC
`C0:B7A0` (`LDA $4210 / BPL`), forced blank, `video_active_frames=4022`, last APU port traffic frame 4924.
Every route sidesteps it with a battery save. Same PC in both engines ⇒ shared device-model issue
(APU/SPC handshake or NMI timing), not generated code.

**Routes:** `tools/oracle/srm_route.sh <save.srm>`, `battle_route.sh <save.srm> [--coverage]`,
`mode7_route.sh <worldmap.srm>`; scene routes `<scratch>/{scenes,scenes2,opera_deep,walknow}.sh`; walk
regression `verify_fix.sh` semantics = wfix vs wnow (20/20 identical).

**Next actions, in order:**

1. **Attack the prologue freeze** — the only bug between this port and "playable from power-on". Start from
   the fact that the reference freezes identically: compare APU/SPC state and NMI timing at frame 5088, not
   generated code. `snesrecomp/runner/src/snes/snes.c:445-515` (`$4210`/RDNMI), `common_rtl.c:1740` (WMADD),
   `common_rtl.c:2081` (APU handshake ports).
2. **Scene breadth**: shops, esper/magic menus, the final battle proper; the ending needs savestate loading
   (save 30 has only `.zst`).
3. **`ref_driver` SRAM support** (~30 lines) — second engine for any save route.
4. Keep every promotion gated on byte-identical frames across two routes; keep measuring with `costs[]`.

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
- A crash report's `CpuState` is the last *emulated* CPU state, not the faulting code. With
  `last recomp func: (none)` it is noise — get symbols instead of reading the game out of it.
- `Release` is built without `/Zi`, so there is no PDB and every fault address is anonymous. Build
  `RelWithDebInfo` (`build-dbg`) and run `tools/oracle/symdump.py <dmp> <exe> <pdb>` before theorising.
- `python3` does not exist on the Windows host — tool scripts must use `"${PYTHON:-python}"`.
- Never read AOT/interpreted share from a `SNESRECOMP_TIER2_CAPTURE` run: capture forces
  interpretation (~97% interp) and the numbers mean nothing.
- The mosaic cell run length is computed independently in three draw paths (widescreen 4bpp, plain
  4bpp, mode 7). Guarding one leaves the other two able to walk the zbuffer backwards.
- **Kill the exe before rebuilding.** A killed or timed-out run leaves `FinalFantasyViSNESRecomp.exe`
  alive holding its own image; the next link dies with
  `LNK1104: cannot open file ...FinalFantasyViSNESRecomp.exe`. `taskkill //F //IM` first — a 38-minute
  "slow run" and a failed build are the same mistake made twice.
- **In a battle, press A and nothing else.** Direction presses walk the command cursor into a submenu,
  and the fight then waits on input forever while enemies keep chipping HP — a screen that is
  indistinguishable from a hang. A route that "hangs mid-battle" should be re-run with A-only before
  suspecting the port.
- **Battle code is computed jump tables; expect an authorization per newly reachable scene.** FF6's
  `C1:0000` (`PHA/TDC/PLA/ASL A/TAX/JMP ($0008,X)`), `C1:450F` and `C1:5E4E` all resolved to 2-byte
  code-pointer tables (4, 48 and 32 entries) that the analysis could not see. One abandoned handler
  freezes the caller's `INC $47 / LDA $47 / BNE` handshake, so the symptom is a *stall*, not a crash.

## Step 27 — Prologue unblocked: $4210 NMI latch starvation

**Symptom.** From power-on the intro played, faded out at frame 5078, then the machine sat on a
blank frame forever (byte-identical at 6k/8k/10k/12k/14k). CPU alive, `rc=0`, `abandons=0` — a
livelock, not a crash.

**Ground truth.** Built the repo's `tools/snesref` frontend (mingw g++ + SDL2-mingw) around
`bsnes2014_accuracy_libretro.dll` and ran it headless against the same scene scripts. Recomp vs bsnes
WRAM was byte-identical through frame 5084, diverged at 5085 (103 bytes) and 5100 (13,006), and the
event pointer froze at `$CC:985D` while bsnes executed the `$F0` "play song" command and moved on.

**Ruled out by measurement** (not inference): DMA (zero general `$420B` triggers after frame ~3960),
the divider (133 accesses total, last at frame ~30), the APU handshake (CPU write `$2140=F0` -> SPC
read -> SPC echo `F0` at `0E73` -> CPU read back `F0`, sync `step=0`), SPC `$F1` port wipe, and a
guest-cycle/APU-clock unit mismatch.

**Root cause.** `EventCmd_f0` ends in `lda hRDNMI / bpl` and never returns. `$4210`'s pending latch is
one-shot: the read clears it. The beam driver runs the NMI handler the instant the beam reaches v=225,
and the handler's own ack read consumed the latch, so the main loop's poll read `pend=0` on all 399
reads of every frame and starved indefinitely.

**Fix** (`runner/src/snes/snes.c`, case `$4210`): a read taken while `inNmi` acks the interrupt without
consuming the pending latch (`if (!snes->inNmi) snes->rdnmiPending = false;`).

**Verification.** vs bsnes at frame 5200: 13,006 bytes apart (permanent) -> 80 bytes, with identical
event state (`$E5-E7 = 78 98 CC`, `$EA = 92`); frames 5200 vs 5600 now differ by 398 bytes (running);
picture pattern matches bsnes 5300-5850 (85.1% vs 85.0% non-black). Cold boot now reaches the Narshe
opening mission (Vicks/Wedge dialogue, Terra, frozen Esper) through intro and title. Regression:
C0 control and C0+C1+C2+C3 candidate both rebuilt under the fix, battery-save route 4,681 frames,
7/7 dumps IDENTICAL, `ORACLE: clean`.
