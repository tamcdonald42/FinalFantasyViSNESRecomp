# Final Fantasy VI Recomp

A native recompilation of **Final Fantasy VI** (USA, v1.1), built on
[snesrecomp](https://github.com/mstan/snesrecomp). The 65816 code is translated
to portable C ahead of time and run against a cycle-aware hardware model, so the
game runs as a native executable instead of an emulated ROM.

> **You must legally own a copy of the game.** No ROM data is distributed with
> this project, in the repository or in any release. The recompiled C is
> generated locally from your own copy and is never committed.

## Status

**Pre-release: it plays.** A cold boot — no save file, no cheats — runs the
opening narration, the title screen, and into the Narshe magitek mission.

Verified so far:

| Area | State |
|---|---|
| Boot → intro → title → new game | Plays |
| Opening mission (Narshe, magitek armour) | Plays |
| Battles (Kefka's Tower route, 8,481 frames) | Runs clean, `abandons=0` |
| Field/menu routes from battery saves (opera house, Albrook) | Runs clean, dumps identical to the C0 control |
| World map / Mode 7 airship | Renders in Mode 7 |
| Save battery (`.srm`) | Loads and writes plain SRAM; emulator saves load |

Correctness is measured against **bsnes accuracy**, not against "it looks
right". Over the whole intro the recompiler tracks the reference frame-for-frame
(77 of 79 sampled frames align at zero lag, ~0.09% of WRAM differing), and the
event VM's state is identical where it previously deadlocked.

**Known gaps** — these are untested, not known-broken:

- The ending. No savestate for it has been published yet, so it is unverified.
- Shops and the esper menu have not been toured.
- No full story playthrough yet.
- At fade boundaries the picture can lag the reference by a frame (rendering
  phase, not game state).

## ROM identity

| | |
|---|---|
| File | `Final Fantasy III (USA) (Rev 1).sfc` |
| Publisher | Square |
| Developer | Square |
| Year | 1994 |
| Mapping | hirom |
| Region | USA (North America) |
| Coprocessor | none |
| CRC32 | `c0fa0464` |
| SHA-256 | `10eccc5d2fab81346dd759f6be478dcb682eef981e8d3d662da176e1f9a996bc` |

`tools/regen.sh` refuses to run against anything else, so a mismatched
revision fails immediately instead of producing subtly wrong output.

## Build

```sh
git submodule update --init --recursive

# Required: the framework fixes this port depends on are not upstream yet.
git -C snesrecomp apply ../patches/framework/0001-ppu-guard-stale-mosaic-run-length.patch
git -C snesrecomp apply ../patches/framework/0002-nmi-pending-latch.patch

bash tools/regen.sh --rom "/path/to/Final Fantasy III (USA) (Rev 1).sfc"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The ROM does not have to live in the repository — keeping it on your own
drive is the better habit, and `SNESRECOMP_ROM` sets the path once for a
shell. Without either, `tools/regen.sh` finds `Final Fantasy III (USA) (Rev 1).sfc` at the repo root,
then the path the setup wizard recorded in `rom.cfg`; `.gitignore` blocks
both from ever being committed.

`tools/regen.sh` verifies the ROM, generates `src/gen/*.c`, and re-syncs
`recomp/funcs.h`. Re-run it whenever you change anything under `recomp/`.

## Framework patches

`snesrecomp` is pinned to a commit that `patches/framework/` patches on top of.
Both patches are required, and skipping them produces a build that silently
deadlocks in the prologue:

- **0001 — mosaic run length.** A stale `mosaicModulo` table let a 4bpp
  background read one run past its entry, corrupting mosaic'd layers.
- **0002 — NMI-pending latch.** A `$4210` read taken while the NMI is being
  serviced no longer consumes the pending latch, and the field wrap no longer
  clears it. Without this the game's `lda hRDNMI / bpl` poll starved — the
  handler's own acknowledgement read always won the race — and the intro ended
  in a permanent blank screen at frame 5080.

The patches are LF line endings and apply on Windows, Linux and macOS.

## Run

```sh
./build/FinalFantasyViSNESRecomp                     # launcher picks the ROM
./build/FinalFantasyViSNESRecomp "/path/to/Final Fantasy III (USA) (Rev 1).sfc"   # name it and skip the launcher
```

With no ROM on the command line the build opens the recomp-ui launcher: a ROM
picker with a verification badge, plus display / audio / input settings. Your
choice is cached in `rom.cfg` beside the executable, so the next launch opens
on it and "Skip launcher on boot" makes it immediate. If the launcher is not
built in (`--no-recomp-ui`), the host falls back to a native file picker.

Either way the ROM is checked against the digests above before anything boots
— a dump that could not have produced this build is refused at the door rather
than mis-executing ten frames in.

Saves are written to `saves/save.srm` beside the executable.

## Controls

Keyboard defaults, from `keybinds.ini` (edit and restart to rebind):

| Key | SNES | Key | SNES |
|---|---|---|---|
| Arrows | D-pad | X | A |
| Enter | Start | Z | B |
| R-Shift | Select | S | X |
| C | L | A | Y |
| V | R | Alt+Enter | fullscreen |

Gamepads are supported and map to the same buttons.

## Layout

| Path | What lives there |
|---|---|
| `recomp/` | Analysis input: `bank*.cfg`, `symbols.toml`, generated `funcs.h` |
| `rom_identity.txt` | ROM digests — read by the build, `tools/regen.sh` and CI |
| `src/` | Host code you own: `main.c`, `game_rtl.c` |
| `src/gen/` | Generated C. Never committed — regenerate locally |
| `snesrecomp/` | Framework submodule (owns `lib/recomp-net`, `lib/retcomm-rbengine`) |
| `patches/framework/` | Required fixes applied on top of the pinned framework |
| `tools/` | `regen.sh` — the ROM → C pipeline; `oracle/` — the frame-oracle routes |
| `scripts/` | `package_release.sh` — player-facing zip |
| `framework_pins.txt` | Exact framework commit this project is built against |
| `STATUS.md` | Step-by-step engineering log, including what was ruled out and how |

## How this was built

The method matters as much as the result, and it generalises:

1. **Promote by measurement, never by guess.** Banks are moved from the
   interpreter into ahead-of-time compilation ranked by
   `costs[].interpreted_instructions` actually executed on a real route — not by
   how often a symbol was observed.
2. **Every promotion is gated on a frame oracle.** A route is captured as a
   script of waits and button presses; the same script runs against the
   recompiler and against bsnes accuracy; WRAM and framebuffers are compared
   byte for byte. A promotion that changes a frame is rejected.
3. **Resolve dispatch misses before anything else.** Unresolved indirect
   targets are why a port diverges; they are cheapest to fix early.
4. **Never synthesise a result** to get past uncovered code, and never edit
   `src/gen/` by hand. Fix the config or the framework and regenerate.

The ground-truth harness lives in `snesrecomp/tools/snesref` (headless libretro
frontend + bsnes accuracy). `STATUS.md` records each step, including the
hypotheses that were measured and eliminated along the way.

## Multiplayer

Single player.

## License

This project's own source is under the license in `LICENSE`. The framework
carries its own terms — see `snesrecomp/LICENSE` and
`snesrecomp/THIRD_PARTY_ATTRIBUTION.md`. Neither covers the game data, which
is not distributed here.
