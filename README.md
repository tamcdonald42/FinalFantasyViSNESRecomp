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

## Prerequisites

| Component | Notes |
|---|---|
| **Rust** (cargo) | `regen.sh` builds the recompiler's native analyzer on first use. Install from rustup.rs, or set `SNESRECOMP_NATIVE_ANALYZER` to a prebuilt one. |
| **Python 3** | Drives `tools/regen.sh`. |
| **CMake 3.20+** | Build system. |
| **A C/C++ compiler** | MSVC 2022, clang, or GCC (C11 / C++17). |
| **Ninja** (optional) | Faster than the default generator. |

## Build

```sh
git submodule update --init --recursive

# Required: compose the analysis roots. Skipping this does not fail — regen
# quietly emits a 4-root skeleton that boots to a blank screen instead.
python3 tools/gen_roots.py --done C0,C1,C2,C3,EE --bank none
#   (on Windows without python3 on PATH, use: python tools/gen_roots.py ...)

# Optional: name bank $C3's functions instead of addressing them. Generate the
# name file first from your own copy of the bank C3 compendium (see Credits):
#   python3 tools/import_labels.py --disasm <ff6_bank_c3.txt> --bank C3
python3 tools/apply_labels.py

bash tools/regen.sh --rom "/path/to/Final Fantasy III (USA) (Rev 1).sfc"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

`gen_roots.py` composes `recomp/symbols.toml` from the committed
`recomp/candidate-roots-*.toml` lists: 2,360 functions, which regen expands to
26,300 roots (12,789 ahead-of-time, 17,747 interpreted) and about 210 MB of
generated C. Expect the build to take 10–15 minutes.

`apply_labels.py` is optional and changes identifiers only: `emit`, `entry_mx`
and every tier decision stay as `gen_roots.py` wrote them. It merges the block
names from `recomp/labels-c3.toml` over the address-derived ones. That file is
built by `import_labels.py` from a commented bank `$C3` disassembly whose
instruction bytes agree with this ROM revision (25,440 of 25,441 sampled rows),
and it is **not committed** — the source document's terms do not allow
redistributing its contents, so you generate the file from your own copy.
Checked by regenerating the same ROM with and without the names: 349 of 367
generated files are byte-identical, the other 18 differ only by those names and
the alphabetical order of each translation unit's forward declarations; the
analysis is unchanged at 26,300 roots and 12,789 AOT variants.

A verified from-scratch run of exactly these steps produced WRAM and
framebuffers byte-identical to the reference build at frames 5200 and 9000.

The ROM does not have to live in the repository — keeping it on your own
drive is the better habit, and `SNESRECOMP_ROM` sets the path once for a
shell. Without either, `tools/regen.sh` finds `Final Fantasy III (USA) (Rev 1).sfc` at the repo root,
then the path the setup wizard recorded in `rom.cfg`; `.gitignore` blocks
both from ever being committed.

`tools/regen.sh` verifies the ROM, generates `src/gen/*.c`, and re-syncs
`recomp/funcs.h`. Re-run it whenever you change anything under `recomp/`.

## Framework

`snesrecomp` is pinned to a fork
([tamcdonald42/snesrecomp](https://github.com/tamcdonald42/snesrecomp), branch
`ff6-pin`) that carries two fixes this port cannot run without. They are real
commits, not patches, so `git submodule update --init --recursive` is all you
need — but be aware that skipping the submodule update leaves an empty
framework tree, and the build will fail confusingly rather than tell you why.

- **Mosaic run length.** A stale `mosaicModulo` table let a 4bpp background read
  one run past its entry, corrupting mosaic'd layers.
- **NMI-pending latch.** A `$4210` read taken while the NMI is being serviced no
  longer consumes the pending latch, and the field wrap no longer clears it.
  Without this the game's `lda hRDNMI / bpl` poll starved — the handler's own
  acknowledgement read always won the race — and the intro ended in a permanent
  blank screen at frame 5080.

Both are worth upstreaming; they are general fixes, not FF6-specific ones.

## Run

```sh
./build/Release/FinalFantasyViSNESRecomp                     # MSVC / multi-config
./build/FinalFantasyViSNESRecomp                             # Ninja / Make
```

With no ROM on the command line the build opens the recomp-ui launcher: a ROM
picker with a verification badge, plus display / audio / input settings. Your
choice is cached in `rom.cfg` beside the executable, so the next launch opens
on it and "Skip launcher on boot" makes it immediate. If the launcher is not
built in (`--no-recomp-ui`), the host falls back to a native file picker.
Pass the ROM as an argument to skip it entirely:

```sh
./build/Release/FinalFantasyViSNESRecomp "/path/to/Final Fantasy III (USA) (Rev 1).sfc"
```

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

## Quality of life

Vanilla behaviour by default. These are additions, not changes: with every
option at its default the game runs exactly as the cartridge does, and the
frame oracle proves it (see below).

### Widescreen (off by default)

Press <kbd>F10</kbd> for a 384x224 frame. Off is the authentic 256x224 picture,
byte-for-byte the verified baseline; the mod only ever hands the renderer a
wider frame, so with it off there is nothing to honour.

```ini
[QoL]
Widescreen    = off    ; off = 4:3 baseline, on = 384x224 frame
WidescreenKey = F10    ; any SDL key name, or None
```

**What is verified today.** With it on, the frame is 384x224 and the authentic
256 columns sit at x=64..319, pixel-identical to the 4:3 baseline: 52 of 52
sampled frames across a field / menu / submenu / close route, zero differing
pixels. Menus, battles, the world map and cutscenes are pillarboxed rather than
stretched, so the picture does not resize when a menu opens — the game simply
stops using the extra columns.

**What is not there yet.** The margin columns are black on the field. The PPU
will render them, but FF6's field renderer only fills a 256-pixel tilemap
window, so there is nothing in VRAM to sample beside it. A genuinely wider
field needs the field engine's camera clamp and tilemap window, which is engine
work rather than a setting; the renderer side is wired and waiting for it.

**The trigger** is `$7E1503`, the bank of the installed NMI handler, which each
subsystem rewrites when it takes over: `$C0` field, `$C1` battle, `$C3` menu,
`$EE` world map. That table came out of WRAM dumps rather than the listing —
`docs/WIDESCREEN_TRIGGER.md` has the routes, the dumps and the candidates that
were rejected. Reading one byte per frame needs no hook, no patch and no guest
write, the same shape as the encounter switch.

**Known limitation:** sprites use 9-bit OAM x-positions, so an actor standing in
the margin is drawn at the edge of the authentic window rather than beyond it.

### Random encounters on/off

Press the <kbd>`</kbd> key (backquote, left of `1`) to switch random battles
off and on. A toast confirms the state, and the choice is saved to
`qol.ini` beside the executable, so it survives quitting.

```ini
[QoL]
Encounters    = on     ; on = vanilla random battles, off = disabled
EncountersKey = Grave  ; any SDL key name, or None to disable the hotkey
```

**How it works, and why it needs no patch.** FF6 chooses its encounter rate
from a table of step incrementors in bank `$C0`, selected by bits 0-1 of
`$7E11DF` — `$C0:C29F` on the world map, `$C0:C2BF` in dungeons, four rows of
four random variants:

| `$11DF` | World map | Dungeon |
|---|---|---|
| neither | 192, 96, 384, 0 | 112, 64, 352, 512 |
| bit 0 (Charm Bangle) | 96, 48, 192, 0 | 56, 32, 176, 256 |
| **bit 1** | **0, 0, 0, 0** | **0, 0, 0, 0** |

Row 2 is all zeros: that is the switch the Moogle Charm relic uses. Holding
bit 1 from the host stops encounters everywhere with no ROM edit, so it works
alongside code patches and graphic hacks rather than fighting them. The host
ORs the bit in (bit 0's Charm Bangle effect is left alone) and clears only a
bit it set itself, so a party carrying the relic keeps working.

Because the setting is host state rather than guest state, rewinding across
the moment you flipped it restores memory and the next frame re-applies your
current choice — a preference, not a game event.

**Verified** on the Narshe battle route (same battery save, 7,749 frames,
12 dumps): with the switch off the run never enters battle; with it on the
run is byte-for-byte the vanilla control. Both 0 abandons, 0 crashes.

## Layout

| Path | What lives there |
|---|---|
| `recomp/` | Analysis input: `bank*.cfg`, `symbols.toml`, generated `funcs.h` |
| `rom_identity.txt` | ROM digests — read by the build, `tools/regen.sh` and CI |
| `src/` | Host code you own: `main.c`, `game_rtl.c` |
| `src/gen/` | Generated C. Never committed — regenerate locally |
| `snesrecomp/` | Framework submodule (owns `lib/recomp-net`, `lib/retcomm-rbengine`) |
| `framework_pins.txt` | The framework commit this project builds against |
| `tools/` | `regen.sh` — the ROM → C pipeline; `oracle/` — the frame-oracle routes |
| `scripts/` | `package_release.sh` — player-facing zip |
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

## Credits

**Documentation.** During development, the function names used for bank `$C3`
come from the *Final Fantasy VI Bank C3 Compendium* by **Novalia Spirit (Jame
Coban)** — a fully commented, reassemblable disassembly of the bank that holds
every field menu and the ending scenes, released July 2016 and revised that
October. It is a reference of the first order: 971 of the bank's 1,036 function
entries stop being `sub_C30138` and start having names, and his instruction
listings agree with this ROM revision in 25,440 of 25,441 rows, so the document
describes exactly the code this project compiles.

Function names in this repository are address-derived. The name extract built
from his document (`recomp/labels-*.toml`) is generated locally, ignored by git
and never redistributed, because his release notes reserve reproduction of the
documents' contents for personal use and license distribution to
romhacking.net alone. The documents themselves are published at
[ff6hacking.com](https://www.ff6hacking.com/wiki/doku.php?id=ff3:ff3us:doc:game)
under his terms. If he would rather not be cited here at all, one line to that
effect removes the citation.

Thank you, Novalia. Reading the menu code as annotated code instead of as an
address range is the difference between guessing and knowing.

**Dependencies.** The disassembly authority is built from `everything8215/ff6`;
the frame oracle runs on bsnes accuracy through `snesref`; the runtime, PPU and
tooling are `snesrecomp` (see `snesrecomp/THIRD_PARTY_ATTRIBUTION.md`).

## License

This project's own source is under the license in `LICENSE`. The framework
carries its own terms — see `snesrecomp/LICENSE` and
`snesrecomp/THIRD_PARTY_ATTRIBUTION.md`. Neither covers the game data, which
is not distributed here.
