# Side panels (bezel)

Widescreen widens the frame to 384x224, but FF6's field engine builds only a
256-pixel tilemap window, so the margin columns show the backdrop and nothing
else. Enlarging that window is its own project (`docs/WIDESCREEN_TRIGGER.md`,
field-of-view section). Side panels are the interim answer: dress the margins on
purpose instead of leaving what reads as a rendering fault.

Shipped default is `slate` — visible only while widescreen is on.

## Configuration

`qol.ini`, next to the exe:

```ini
[QoL]
Widescreen    = on          ; panels only exist when the frame is wider than 256
Bezel         = slate       ; none | slate | dim | ink | custom
BezelImage    = bezels/panel.png
BezelOpacity  = 100         ; 0..100, blended toward black
BezelMirror   = 1           ; mirror the right panel
```

| style | what it looks like |
|---|---|
| `none` | nothing painted; the margins keep the game's backdrop colour |
| `slate` | flat near-black panel, hairline rule at the inner edge — reads as a deliberate pillarbox |
| `dim` | soft vertical gradient, a little light bleeding off the picture |
| `ink` | near-black with a faint diagonal hatch |
| `custom` | the player's own picture: scaled to one panel, mirrored on the right unless `BezelMirror = 0` |

`BezelImage` accepts PNG, JPG and BMP, either absolute or relative to the exe
folder. A picture that fails to load logs why and falls back to `slate` — it
never leaves a black margin and a broken-looking frame.

## How it composites, and the rule that keeps the baseline honest

The host's `draw_frame` hook runs after the PPU has rendered and before the
frame is presented. The module paints two strips — `x < extra` and
`x >= frame_w - extra`, where `extra = (frame_w - 256) / 2` — into the PPU's
own field buffer, then returns 0 so the host runs its normal present path over
the result. The margins are backdrop pixels the PPU rewrites every frame, so
this is the equivalent of choosing the margin colour, and it costs two row
copies per frame, not a per-pixel blend.

The alternative — composing a finished frame into the host's destination and
returning 1 — was measured and rejected. Returning 1 bypasses
`RtlWidescreenPresent`, and that function is also what writes frame dumps, the
debug capture and the OSD composite; a bezel that silently disables all three is
a worse trade than painting the backdrop. Measured behaviour of the rejected
path: `frames=0` on every dumping run.

The authentic 256 columns are never written, so the picture with a bezel is
pixel-identical to the picture without one. When the frame is 256 wide
(widescreen off) the hook returns before doing anything, so the verified 4:3
baseline never enters this code.

Because the game pillarboxes itself in menus, battle, the world map and
cutscenes (see `qol_widescreen.c`), and the PPU still hands over a 384-wide
field with the authentic image centred, the panels stay put across modes instead
of popping in and out.

## Verification

Five configurations, one cold boot each, the same game frame (3000, title
screen), frames dumped through the framework's own `SNESRECOMP_FRAME_BMP_DIR`
path:

| run | frame | margins | centre 64..319 |
|---|---|---|---|
| widescreen on, `Bezel=none` | 384x224 | backdrop (24,24,33) | reference |
| widescreen on, `Bezel=slate` | 384x224 | (26,30,39) + rules | **identical** |
| widescreen on, `Bezel=ink` | 384x224 | (22,23,26) | **identical** |
| widescreen on, `Bezel=custom` (test BMP) | 384x224 | (126,0,64) / (126,255,64) mirrored | **identical** |
| widescreen off, `Bezel=slate` | 256x224 | not painted | **identical** |

`custom` proves the decoder, the panel scale and the mirror: the test image is a
64x224 gradient with red bars, and the two panels read as exact mirrors of each
other in the dumped frame.

Note for future measurements: "the margins are black" is only true when the
scene's backdrop is black. On the title screen the backdrop is (24,24,33), and
a margin-pixel test that assumes pure black will report a false positive.

## Limits

- Panels only exist while widescreen is on; with a 256-wide frame there are no
  margins to fill. A 4:3-with-side-art presentation would need a different
  window/present contract, not this module.
- The custom image is stretched to one panel (nearest-neighbour) and mirrored.
  It is not a full-frame overlay with a hole cut in it.
- No hotkey: the style is a startup setting. Adding one is cheap if it earns its
  place.
- If the field-of-view work lands, the panels become the frame around a real
  wide view rather than a substitute for one.
