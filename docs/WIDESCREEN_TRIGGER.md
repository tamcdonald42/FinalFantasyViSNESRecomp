# Widescreen phase 4: detecting that a menu is on screen

Phase 4 letterboxes menus instead of widening them, because FF6 draws every menu
on 32x32 tilemaps (BG1 `$0000`, BG2 `$1000`, BG3 `$4000`, 256 px) and a wider
framebuffer would only reveal the edge of the map. What the renderer needs is a
per-frame answer to one question: **is a menu displayed right now?** — true from
the first menu pixel to the last, false on the field and in battle, without
touching guest code.

This document is the measurement behind the answer, so the trigger is a fact
about the game rather than a guess that happens to look right on one screen.

**Answer: `$1503` (and `$1507`) equal `$C3`.** Those two bytes hold the bank of
the installed NMI and IRQ handlers, and the three subsystems each install their
own.

## Mechanism

The ROM's interrupt vectors point at a stub that jumps through WRAM: `$1500`
holds a `JML` opcode (`$5C`) followed by a 24-bit target, and `$1504` does the
same for IRQ. Each subsystem rewrites that target when it takes over.

| who owns interrupts | `$1500-$1503` NMI | `$1504-$1507` IRQ |
|---|---|---|
| field | `JML $C0:0182` | `JML $C0:0262` |
| battle | `JML $C1:0BA7` | `JML $C1:0D4A` |
| menu | `JML $C3:1387` | `JML $C3:13C7` |
| world map | `JML $EE:A509` | `JML $EE:A6B7` |

Every address in that table was read out of a WRAM dump, not assumed. The menu
entry path in bank `$C3` predicts them exactly:

- `C3:001B` — menu entry: switches stack, clears DP, sets the WRAM data bank to
  `$7E`, then `JSR $00C0` at `C3:0032`.
- `C3:00C0` — installs the handlers: writes `$5C` at `$1500` and `$1504`, and
  `#\$1387` / `#\$13C7` with bank `\$C3` at `$1501` and `$1505`.
- `C3:0035` — reads `$0200` (which menu was requested), `C3:00F8` loads
  graphics and palettes, `C3:00DD` dispatches through the table at `C3:00E6`.
- `C3:01BA` — the menu loop: dispatches on `$26` every iteration and leaves when
  `$26` reads `$FF` (`C3:01BE`–`C3:01C2`); `C3:01D8` clears `$43` before the
  `RTS`, and `C3:00BF` `RTL`s back to whoever asked for the menu.

Handler installation sites, found by scanning the ROM for stores to `$1500` /
`$1504` / `$1503`: `C0:0575`, `C1:00FD`, `C2:68DD` and `C2:D432`, `C3:00C2`,
`E5:F41C`, `EE:8F9E`/`914C`/`9187`/`91B1`. The `$C2` pair is the cinematic
engine, so cutscenes are their own owner too — they do not look like menus.

So the handler installation at `C3:00C0` happens *before* the first menu frame is
drawn, and the field's own handlers are back in place by the time control returns.

## Measurement

`tools/oracle/menu_probe.sh` takes one battery save down two routes and dumps
WRAM at each state:

```
bash tools/oracle/menu_probe.sh field     # 4,357 frames, 2 dumps
bash tools/oracle/menu_probe.sh menu      # 4,551 frames, 6 dumps
```

| dump | state | `$1503` | `$1501/$1502` | `$26` |
|---|---|---|---|---|
| `f_field1`, `f_field2` | field, standing | `C0` | `82 01` | `7F` |
| `m_field` | field, same save | `C0` | `82 01` | `7F` |
| `m_menu` | menu open, still fading in | `C3` | `87 13` | `05` |
| `m_cursor` | main-menu cursor moved | `C3` | `87 13` | `05` |
| `m_sub` | first submenu | `C3` | `87 13` | `06` |
| `m_sub2` | submenu, cursor moved | `C3` | `87 13` | `06` |
| `m_closed` | two `B` presses, back on field | `C0` | `82 01` | `7F` |

The battle column of the table above comes from dumps already in the oracle set
(`tools/oracle/battle_route.sh`, dumps `b30`–`b60`): battle is `C1`, so the three
subsystems are mutually exclusive and no two states share a bank.

## The world map is a fourth owner: `$EE`

Loading the world-map save (`ff6_world.srm`, party in the airship) and dumping
gives NMI `JML $EE:A509`, IRQ `JML $EE:A6B7` — a bank no other state uses. Bank
`$EE` installs its own handlers in four places (`$EE:8F9E`, `$EE:914C`,
`$EE:9187`, `$EE:91B1`), so the world map puts them back on its own resume
paths rather than relying on whoever ran last.

**So the trigger does not fire on the world map.** `$1503` reads `EE`, not `C3`,
and the overworld stays at 4:3 — which is the correct answer anyway, since it is
Mode 7 with a minimap HUD and widening it is a separate problem.

### A menu opened from the world map

The world map asks for the menu through the same entry point as everything else:

```
EE:03AC  LDA $E9          ; menu disabled?
EE:03AE  BIT #$10
EE:03B0  BNE $03F1        ; skip if so
EE:03B2  LDA $E8
EE:03B4  BIT #$01
EE:03B6  BEQ $03F1        ; skip if no request
EE:03B8  JSR $2292
EE:03BD  STA $2100        ; force blank
EE:03CC  STZ $4200        ; NMI off
EE:03D8  ..$1F60/$1F61/$1F68   ; save map position
EE:03EA  JSL $C30000      ; the menu
EE:03EE  JMP $83F6        ; resume the world map
```

`JSL $C30000` occurs exactly four times in the ROM: `C0:C6F9` (field),
`C3:008E` and `C3:00AF` (the two naming menus) and `EE:03EB` (world map). A menu
opened from the overworld is therefore the same menu, entered the same way, and
`$C3` owns the handlers for as long as it runs — the trigger fires and the
letterbox engages.

That last step is the one thing here not confirmed by a dump. In this save the
party is airborne, where `$E8` bit 0 stays clear and the menu request never
happens: `X` disembarks to the airship deck instead (a normal field map, `$C0`),
and `A`, `Y`, `Start`, `Select` all leave `$1503` at `EE`. Holding `$E8` bit 0
with `poke` does not change it, because the disembark path consumes the button
first. A world-map save on foot would settle it.

## Why this trigger and not another

- **It leads the picture.** `$1503` is already `C3` in `m_menu`, dumped 70
  frames after the `X` press while the menu is still fading in, so the letterbox
  can be in place before the first menu pixel appears — no flash of wide field.
- **It survives navigation.** Main menu, cursor movement, submenu, deeper
  submenu: all `C3`. Nothing about the trigger depends on which menu is open,
  which is why it also covers the shop, load, lineup and naming menus — they all
  enter through `C3:0000`.
- **It is restored.** `m_closed` is back to `C0`, so the trigger is not sticky.
- **It is a subsystem fact, not a UI side effect.** The byte says which code owns
  the interrupt handlers; it does not depend on whether a window happens to be
  drawn.
- **It costs one WRAM read per frame** and needs no guest-code write, no hook,
  no patch — the same shape as the encounter toggle.

Candidates that were measured and rejected:

| byte | why not |
|---|---|
| `$26` (menu command) | `$7F` on the field is an idle value, not a guarantee; the field does not write it reliably |
| `$46` (menu flag bits) | noisy (`BD` → `02` → `15`) and does **not** return to its field value after closing (`13`) |
| `$43` (HDMA enable shadow) | nonzero in battle too (`3E`), so it does not separate menus from battle |
| `$59` (BG wrap flags) | flips `00`→`80` on menu entry, but that is a rendering side effect of the menu, not an ownership signal |
| `$0200` (menu number) | `0` means "main menu", so it is not a boolean, and the save left `02` behind |
| PC range in bank `C3` | the menu loop calls handlers across `$C3` and into `$D4`; being in `$C3` is necessary, not sufficient |

## Menu-only code that is still interpreted

The same runs captured tier-2 coverage on both routes. Comparing the sets, 16
unrooted call targets in bank `$C3` are reached **only** with the menu open:

```
C3:011A  from C3:00E3      C3:1D94  from C3:01CA
C3:07FF  from C3:1203      C3:1DA4  from C3:01CA
C3:080B  from C3:0800      C3:1DE8  from C3:01CA
C3:082B  from C3:0800      C3:2E90  from C3:2E69
C3:1A8A  from C3:01CA      C3:2F42  from C3:1203
C3:2F4A  from C3:2F43      C3:2FC0  from C3:1203
C3:2F6B  from C3:2F43      C3:2FC8  from C3:2FC1
C3:3014  from C3:2FC1      C3:30F5  from C3:1AB2
```

These are entry points the menu reaches by computed call (`JMP ($xxxx,X)`,
`JSR ($xxxx,X)`), so the root finder could not name them statically. They are
currently interpreted at run time, which is correct but slower; promoting them is
a candidate-roots job, and the list is the shortlist of what phase 4 actually
exercises.

## Residual unknowns

- **World map on foot.** The airship case is measured (`$EE`); walking on the
  overworld was not, because the save in the oracle set is airborne and the
  menu request is gated off there. Same engine, so `$EE` is expected, but it is
  an expectation rather than a dump.
- **Battle menus** stay `C1`, so this trigger does not fire inside battles. That
  is intended: the battle HUD is a separate problem, handled by the sprite and
  OAM budget rules rather than by letterboxing.
- **Ending scenes** live in bank `C3` too. If they install `C3` handlers they
  will letterbox, which agrees with the "cutscenes forced 4:3" rule.

---

## What the widened frame actually shows

The trigger decides *when* the margins are offered; it says nothing about
whether there is anything in them. Measured on `build-c1/Release`, 52 frames
sampled every 10 frames across a field / menu / submenu / close route:

| | result |
|---|---|
| frame width, off / on | 256x224 / 384x224, 52/52 each |
| authentic centre vs 4:3 baseline | identical, 52/52, zero differing pixels |
| margin pixels (both sides, all frames) | 0 non-black |

So the PPU does render the extra columns -- that is what moves the authentic
window to x=64 -- and finds nothing there. FF6's field renderer fills a
256-pixel tilemap window around the camera, so the columns beside it hold no
tilemap data and the backdrop comes through as black.

Consequence for the plan: the renderer half of widescreen is done and verified;
a wider field of view is engine work -- the field camera clamp and the tilemap
window the engine writes -- and belongs with the `$C3` menu work, not with the
settings layer. Until then, widescreen on means the authentic picture centred in
a 384-wide frame with menus pillarboxed on the `$1503` trigger.

Reproduce with `tools/oracle/ws_verify.sh off` then `ws_verify.sh on` on the host,
which writes the setting into the build's `qol.ini`, runs the route and dumps
presented frames to `C:\Temp\ws\{off,on}\frames`.
