# Shaders

The framework carries a full RetroArch-format GLSL pipeline — `.glslp` presets,
multi-pass chains, LUT textures, `#pragma` parameters, `FrameCount`,
`PassPrev`/`Prev` feedback and per-pass scale types (`glsl_shader.c`). This port
declares `.shader_supported = 1`, so the launcher lists every preset it finds
under `assets/shaders` and the OpenGL presenter compiles the chain.

Shaders run at presentation. They need `OutputMethod = OpenGL` in `config.ini`
(the shipped default); the SDL presenters ignore them. Nothing is applied until
one is picked — `Shader` ships empty.

## What ships

Four presets from the framework itself (CC0, in `snesrecomp/runner/assets/shaders`):

`crt-soft`, `lcd-grid`, `sharp`, `warm-composite` — see the picker at the top
level, no folder.

Twelve curated CRT presets from [libretro/glsl-shaders](https://github.com/libretro/glsl-shaders),
grouped by what they cost on **weak** hardware:

| tier | preset | passes | notes |
|---|---|---|---|
| Tier 1 Potato | Zfast CRT | 1 | one fetch plus scanline math; the one to pick on integrated graphics |
| Tier 1 Potato | CRT Lottes Fast | 1 | Lottes mask, cheap path |
| Tier 1 Potato | CRT CGWG Fast | 1 | CGWG geometry, reduced taps |
| Tier 1 Potato | CRT Easymode | 1 | soft scanlines + slot mask, cheapest "looks like a TV" |
| Tier 2 Balanced | CRT Geom | 1 | curvature + halation, the classic look |
| Tier 2 Balanced | CRT Lottes | 1 | full Lottes mask |
| Tier 2 Balanced | CRT Hyllian | 1 | Hyllian texture + scanlines |
| Tier 2 Balanced | CRT Aperture | 1 | aperture-grille mask |
| Tier 2 Balanced | CRT Guest SM | 2 | Guest's scanline model, two passes |
| Tier 3 Heavy | CRT Guest Dr Venom | 11 | Guest's full pipeline with LUT masks |
| Tier 3 Heavy | CRT Guest Dr Venom Fast | 5 | same look, halved passes |
| Tier 3 Heavy | CRT Royale | 12 | mipmapped masks, bloom, the heaviest of the set |

Folder names start with `Tier N`, so the picker (which sorts labels
alphabetically) lists them in order. `_retroarch/` holds the vendored sources the
presets reference; the picker skips it because its name starts with `_`, and
`NOTICE.txt` in that folder carries the provenance and licence terms. Each
`.glsl` keeps its original header, author and licence.

## What was measured, and what it means

Machine: RTX 4060, window 1600x1200, vsync off, frame delay off, 900 frames:

| preset | fps |
|---|---|
| no shader | 328 |
| Tier 1, all four | 313 – 324 |
| Tier 2, all five | 308 – 326 |
| Tier 3 Guest Dr Venom (fast / full) | 323 / 313 |
| Tier 3 Royale | **234** |

At 3200x2400 every preset landed within noise of the 266 fps baseline
(255–272), except Royale, which is the only one with a measurable cost on this
GPU. With vsync on, all twelve hold 60.09 fps at 1600x1200.

So the tiers are **not** an fps ladder measured here — on a discrete GPU the
whole set is free. They are ordered by what each shader does per pixel (pass
count, texture taps, mask fetches), which is what decides the outcome on an
integrated GPU, an old laptop or a handheld. Royale is the only one where a
modern GPU shows the cost at all.

The honest summary: on this machine, pick on looks. On weaker hardware, start at
Tier 1 and climb.

## Compatibility

- **Presets must not carry their own `#version` directive.** The presenter
  injects one at the top, and a second is a compile error
  (`version directive must be first statement and may not be repeated`). The
  vendored copies strip `#version` when they are brought in; without that, CRT
  Hyllian fails to compile.
- **Preset keys the framework ignores** (`float_framebuffer`, `mipmap_input`,
  `srgb_framebuffer`, `scale_type_x/y`) are harmless — the chain still compiles
  and renders, though a shader that depends on them for correct colour may not
  look exactly as it does in RetroArch.
- **Shaders that need frame history** work only where the framework feeds it:
  `Prev1..Prev7`, `PassPrev`, `Orig`. Anything reaching beyond that gets
  whatever is unbound.
- **Frame dumps do not contain the shader.** Dumps are captured before
  presentation, so verification of a shader is by eye or a desktop capture —
  the compile log is the machine-checkable part, and it is what the table above
  was validated with (12 of 12 compile clean from the staged assets).

## Picking one

- Launcher: the shader row lists all sixteen entries; choosing one writes
  `Shader` into `config.ini`.
- Hand edit: `[Graphics] Shader = assets/shaders/Tier 2 Balanced/CRT Geom.glslp`
  — paths beginning `assets/` resolve against the exe folder.
- Custom: drop any compatible `.glslp` (plus its `.glsl` files, keeping their
  relative layout) under `assets/shaders/` and it appears in the list.
