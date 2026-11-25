# Bezel art spec

Side panels fill the margins of the widescreen frame. This page is what an
artist — or you with an image editor — needs to make art that drops in without
scaling surprises.

## The geometry

Widescreen on gives a 384x224 frame. The game always occupies the middle 256
columns, full height:

```
0                                                              383
+----------------+--------------------------------+----------------+
|   left panel   |          the game              |  right panel   |
|    64 x 224    |          256 x 224             |    64 x 224    |
+----------------+--------------------------------+----------------+
   x 0 .. 63          x 64 .. 319                     x 320 .. 383
```

- **Left panel:** x 0–63 — its right edge touches the picture.
- **Right panel:** x 320–383 — its left edge touches the picture.
- **Panel shape:** 64x224, a 1:3.5 strip. Tall and narrow; design vertically.

## Sizes to author at

The module scales a panel with nearest-neighbour sampling, so integer multiples
of 64x224 stay crisp and everything else gets slightly soft:

| size | use |
|---|---|
| 64x224 | 1x — exact, matches the frame on a 1x window |
| 192x672 | 3x — recommended; the shipped default window is 3x |
| 256x896 | 4x — for 4K windows |

Landscape pictures will be squashed. If you have a wide piece of art, crop the
part you want rather than handing over the whole thing.

## Two ways to supply art

`qol.ini`, next to the exe:

```ini
[QoL]
Widescreen      = on
Bezel           = custom
BezelImage      = bezels/panel.png     ; both sides, mirrored
BezelImageLeft  = bezels/left.png      ; left only
BezelImageRight = bezels/right.png     ; right only
BezelOpacity    = 100                  ; 0..100, fade toward black
BezelMirror     = 1                    ; only affects a single shared image
```

- **One image** (`BezelImage`): used for both panels; the right one is mirrored
  when `BezelMirror = 1`. It must read correctly in both orientations — so keep
  it symmetric, or accept the mirror.
- **Two images** (`BezelImageLeft` / `BezelImageRight`): independent art, never
  mirrored. This is the mode for portraits, logos, per-character panels, or art
  that would look wrong flipped.

Per-side keys beat `BezelImage` for their own side. Missing or unreadable files
fall back: explicit side → `BezelImage` → the other side (mirrored) → the slate
panel. A broken path never leaves a black gap, and the log says what happened.

## Rules the renderer imposes

- **Opaque RGB only.** The panels are opaque strips: the alpha channel is stored
  but never composited, so a transparent hole shows as black, not as the game.
  Use an opaque background colour.
- **Keep the picture edge clean.** Whatever sits at x = 63 (left panel) or
  x = 0 (right panel) is what borders the game. Fine detail there reads as
  noise against the picture; a border, a rule or a soft gradient works better.
- **Panels are static.** They are built once at startup, so an animated bezel
  would need a renderer change.
- **No per-game art.** One pair of panels applies to the whole session; the
  module has no title-aware switching.

## Reusing a third-party bezel pack

Two hard limits, both measured:

1. **Licensing.** The Bezel Project's 157 repositories declare no licence — no
   LICENSE or COPYING in the pack, no terms anywhere — which means all rights
   reserved. Their art is game-derived (logos, characters) on top of that. It
   cannot be bundled with this port or in its release zip. Personal use of a
   pack you installed yourself is your call, exactly like any other mod.
2. **Geometry.** Their files are 1920x1080 full-frame overlays whose clear hole
   is 73.2% of the width (measured from the Final Fantasy III (USA) (Rev 1)
   bezel: hole 1405x1046). Our frame's hole is 66.7% of the width. Their panel
   is ~53 px wide at our height; ours is 64. Dropping one in whole would overlap
   the picture by ~17 px per side.

Adapting one is still straightforward, because only the panel strip matters:

- Left panel: crop their PNG to `x 0..255`, `y 17..1062`, then scale to 64x224.
- Right panel: crop `x 1661..1919`, `y 17..1062`, then scale to 64x224.
- Save as PNG, point `BezelImageLeft` / `BezelImageRight` at the two files.

The stretch is 256→64 horizontally, so any fine texture (scanline patterns,
grille masks) will alias. Art drawn for the panel beats art cropped into it.

## Design cues that work at this size

- Vertical framing: columns, pillars, etched rules, RPG-menu motifs.
- A 1–2 px rule at the picture edge, then a falloff, reads as deliberate.
- Stay dark. The picture is the subject; a panel that competes with it gets
  tiring during a 40-hour game.
- Match the palette to the game's blues and browns, or go deliberately neutral.
