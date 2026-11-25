/*
 * Final Fantasy VI — quality of life: side panels (bezel).
 *
 * WHY THIS EXISTS
 *
 * Widescreen widens the frame to 384x224, but FF6's field engine only ever
 * builds a 256-pixel tilemap window, so the 64 columns each side are black.
 * Real field-of-view work means enlarging the engine's map and retargeting its
 * column streaming — its own project. This module is the honest interim: dress
 * the margins on purpose, RetroArch-style, rather than leave a black void that
 * reads as a rendering fault.
 *
 * WHAT IT PAINTS, AND WHY THE BACKDROP AND NOT THE OUTPUT
 *
 * The host calls draw_frame after the PPU has rendered and before the frame is
 * copied for presentation. This module paints the two margin strips into the
 * PPU's own field buffer — the margins are backdrop pixels the PPU rewrites
 * from scratch every frame, so painting them here is exactly "choosing the
 * backdrop colour", and it leaves the frame pipeline untouched.
 *
 * The alternative — composing a finished frame into the host's destination and
 * returning 1 — was measured and rejected: returning 1 bypasses the framework's
 * present path, and that path is what also writes frame dumps, the debug
 * capture and the OSD composite. A bezel that silently disables all three is a
 * worse trade than painting four bytes per margin pixel. So this hook paints
 * and returns 0, and the host copies the field as it always did.
 *
 * THE RULE THAT KEEPS THE BASELINE HONEST
 *
 * Only x < extra and x >= frame_w - extra are written, where
 * extra = (frame_w - 256) / 2. The authentic 256 columns are never touched, so
 * the picture with a bezel is pixel-identical to the picture without one — the
 * same gate the widescreen module is held to. When the frame is the authentic
 * 256 wide (widescreen off) the hook returns immediately and the 4:3 baseline
 * never enters this code at all.
 *
 * A side consequence worth knowing: when the game pillarboxes itself (menus,
 * battle, world map — see qol_widescreen.c) the PPU still hands over a 384-wide
 * field with the authentic image centred, so the panels stay put across modes
 * instead of popping in and out.
 *
 * STYLES
 *
 *   none    nothing painted; margins stay black — the default
 *   slate   flat near-black panel with a hairline inner rule: reads as a
 *           deliberate letterbox rather than a missing picture
 *   dim     soft vertical gradient, a little light bleeding off the picture
 *   ink     near-black with a faint diagonal hatch
 *   custom  the player's own image (BezelImage), scaled to one panel and
 *           mirrored on the right when BezelMirror is on
 *
 * Panels are built once into two row-major ARGB buffers (extra x frame_h per
 * side), with BezelOpacity baked in, so the per-frame cost is two row copies —
 * not a per-pixel blend on every frame.
 *
 * The image decoder is stb_image, compiled static into this translation unit so
 * it cannot collide with the OpenGL presenter's own copy.
 */

#include "qol_bezel.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qol_ini.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include "stb/stb_image.h"

/* The authentic frame width. Anything wider is margin. */
#define FF6_AUTHENTIC_W 256

/* ARGB8888 in bytes B,G,R,A on little-endian, i.e. a uint32 of 0xAARRGGBB. */
#define ARGB(a, r, g, b) \
  ((uint32_t)(((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | \
              ((uint32_t)(g) << 8) | (uint32_t)(b)))

typedef enum {
  kBezelNone = 0,
  kBezelSlate,
  kBezelDim,
  kBezelInk,
  kBezelCustom
} BezelStyle;

static BezelStyle s_style = kBezelNone;
static int         s_opacity = 100;   /* 0..100, scaled toward black */
static int         s_mirror = 1;
static char        s_image[512];

/* Built lazily: the panel size depends on the frame the host asks for. */
static uint32_t *s_left;
static uint32_t *s_right;
static int       s_panel_w;
static int       s_panel_h;

static int EqNoCase(const char *a, const char *b)
{
  while (*a && *b) {
    char ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
    if (ca != cb) return 0;
    a++; b++;
  }
  return *a == '\0' && *b == '\0';
}

static void Trim(char *s)
{
  size_t n = strlen(s);
  while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' ||
               s[n - 1] == '\n'))
    s[--n] = '\0';
}

/* Fold a colour toward black by the opacity setting. */
static uint32_t Fade(uint32_t c)
{
  if (s_opacity >= 100) return c;
  if (s_opacity <= 0) return ARGB(255, 0, 0, 0);
  const uint32_t a = (c >> 24) & 0xffu;
  const uint32_t k = (uint32_t)s_opacity;
  const uint32_t r = ((c >> 16) & 0xffu) * k / 100u;
  const uint32_t g = ((c >> 8) & 0xffu) * k / 100u;
  const uint32_t b = (c & 0xffu) * k / 100u;
  return ARGB(a, r, g, b);
}

static void FreePanels(void)
{
  free(s_left);
  free(s_right);
  s_left = s_right = NULL;
  s_panel_w = s_panel_h = 0;
}

/* Directory of the executable, taken from qol.ini's own path. */
static void ExeDir(char *out, size_t cap)
{
  char path[512];
  QolIniPath(path, sizeof path);
  snprintf(out, cap, "%s", path);
  char *slash = strrchr(out, '\\');
  char *fslash = strrchr(out, '/');
  if (fslash > slash) slash = fslash;
  if (slash) *slash = '\0';
  else out[0] = '\0';
}

/* ── the built-in looks ─────────────────────────────────────────────────── */

static uint32_t SlatePixel(int x, int w, int y, int h)
{
  /* Vertical shade so a flat panel still has depth. */
  const int shade = 0x0f + 4 * (h > 1 ? (h - 1 - y) : 0) / (h > 1 ? h - 1 : 1);
  uint32_t c = ARGB(255, shade, shade + 2, shade + 5);
  if (x == w - 1) c = ARGB(255, 0x2b, 0x31, 0x40);  /* hairline by the game */
  if (x == 0)     c = ARGB(255, 0x1a, 0x1e, 0x27);  /* outer edge rule    */
  return c;
}

static uint32_t DimPixel(int x, int w, int y, int h)
{
  /* Darkest at the outer edge, a little light bleeding off the picture. */
  const int ww = w > 1 ? w : 1;
  const int hh = h > 1 ? h : 1;
  const int t = (x * 6) / ww + ((hh - 1 - y) * 3) / hh;
  int v = 0x12 + t * 2 / 3;
  if (v > 0x1e) v = 0x1e;
  return ARGB(255, v, v + 1, v + 3);
}

static uint32_t InkPixel(int x, int w, int y, int h)
{
  (void)w; (void)h;
  const int v = ((x + y) % 7 == 0) ? 0x16 : 0x0a;
  return ARGB(255, v, v + 1, v + 4);
}

/* ── the player's own image ─────────────────────────────────────────────── */

static int BuildCustom(int w, int h)
{
  if (!s_image[0]) return 0;
  char path[768];
  if (s_image[0] == '\\' || s_image[0] == '/' ||
      (s_image[1] == ':' && (s_image[2] == '\\' || s_image[2] == '/'))) {
    snprintf(path, sizeof path, "%s", s_image);
  } else {
    char dir[512];
    ExeDir(dir, sizeof dir);
    snprintf(path, sizeof path, "%s/%s", dir, s_image);
  }

  int iw = 0, ih = 0, ic = 0;
  unsigned char *px = stbi_load(path, &iw, &ih, &ic, 4);
  if (!px || iw <= 0 || ih <= 0) {
    fprintf(stderr, "[qol] bezel: cannot load '%s' (%s); using slate\n",
            path, stbi_failure_reason());
    if (px) stbi_image_free(px);
    return 0;
  }

  /* Nearest-neighbour scale to one panel: panels are small strips, so a
   * sampler with ringing would show. */
  for (int y = 0; y < h; y++) {
    const int sy = (int)((long long)y * ih / (h > 0 ? h : 1));
    for (int x = 0; x < w; x++) {
      const int sx = (int)((long long)x * iw / (w > 0 ? w : 1));
      const unsigned char *p = px + ((size_t)sy * (size_t)iw + (size_t)sx) * 4u;
      const uint32_t c = Fade(ARGB(p[3], p[0], p[1], p[2]));
      s_left[(size_t)y * (size_t)w + (size_t)x] = c;
      const int rx = s_mirror ? (w - 1 - x) : x;
      s_right[(size_t)y * (size_t)w + (size_t)rx] = c;
    }
  }
  stbi_image_free(px);
  fprintf(stderr, "[qol] bezel: custom '%s' %dx%d -> panel %dx%d%s\n",
          path, iw, ih, w, h, s_mirror ? " (mirrored)" : "");
  return 1;
}

/* ── panel cache ────────────────────────────────────────────────────────── */

static int BuildPanels(int w, int h)
{
  if (s_style == kBezelNone || w <= 0 || h <= 0) return 0;
  if (s_left && s_right && s_panel_w == w && s_panel_h == h) return 1;

  FreePanels();
  const size_t count = (size_t)w * (size_t)h;
  s_left = (uint32_t *)malloc(count * sizeof(uint32_t));
  s_right = (uint32_t *)malloc(count * sizeof(uint32_t));
  if (!s_left || !s_right) {
    FreePanels();
    fprintf(stderr, "[qol] bezel: out of memory for a %dx%d panel\n", w, h);
    return 0;
  }
  s_panel_w = w;
  s_panel_h = h;

  if (s_style == kBezelCustom && BuildCustom(w, h)) return 1;

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      uint32_t c;
      switch (s_style) {
      case kBezelSlate:     c = SlatePixel(x, w, y, h); break;
      case kBezelDim:       c = DimPixel(x, w, y, h);   break;
      case kBezelInk:       c = InkPixel(x, w, y, h);   break;
      case kBezelCustom:    c = SlatePixel(x, w, y, h); break; /* fallback */
      case kBezelNone:
      default:              c = ARGB(255, 0, 0, 0);     break;
      }
      const uint32_t f = Fade(c);
      s_left[(size_t)y * (size_t)w + (size_t)x] = f;
      const int rx = s_mirror ? (w - 1 - x) : x;
      s_right[(size_t)y * (size_t)w + (size_t)rx] = f;
    }
  }
  fprintf(stderr, "[qol] bezel: %s panels %dx%d, opacity %d%%%s\n",
          s_style == kBezelSlate ? "slate" :
          s_style == kBezelDim   ? "dim" :
          s_style == kBezelInk   ? "ink" : "custom",
          w, h, s_opacity, s_mirror ? ", mirrored" : "");
  return 1;
}

/* ── public ─────────────────────────────────────────────────────────────── */

void QolBezelAfterConfig(void)
{
  char v[512];

  s_style = kBezelNone;
  s_opacity = 100;
  s_mirror = 1;
  s_image[0] = '\0';

  if (QolIniGet("Bezel", v, sizeof v)) {
    Trim(v);
    if (EqNoCase(v, "slate"))       s_style = kBezelSlate;
    else if (EqNoCase(v, "dim"))    s_style = kBezelDim;
    else if (EqNoCase(v, "ink"))    s_style = kBezelInk;
    else if (EqNoCase(v, "custom")) s_style = kBezelCustom;
    else                            s_style = kBezelNone;
  }
  if (QolIniGet("BezelImage", v, sizeof v)) {
    Trim(v);
    snprintf(s_image, sizeof s_image, "%s", v);
  }
  if (QolIniGet("BezelOpacity", v, sizeof v)) {
    const int n = atoi(v);
    s_opacity = n < 0 ? 0 : (n > 100 ? 100 : n);
  }
  if (QolIniGet("BezelMirror", v, sizeof v)) {
    s_mirror = !(v[0] == '0' || v[0] == 'n' || v[0] == 'N' ||
                 v[0] == 'f' || v[0] == 'F');
  }

  FreePanels();
  if (s_style == kBezelNone) {
    fprintf(stderr, "[qol] bezel=none (margins stay black)\n");
  } else if (s_style == kBezelCustom) {
    fprintf(stderr, "[qol] bezel=custom image=%s opacity=%d%% mirror=%d\n",
            s_image, s_opacity, s_mirror);
  } else {
    fprintf(stderr, "[qol] bezel=%s opacity=%d%% mirror=%d\n",
            s_style == kBezelSlate ? "slate" :
            s_style == kBezelDim   ? "dim" : "ink",
            s_opacity, s_mirror);
  }
}

int QolBezelDrawFrame(uint8_t *dst, size_t pitch, const uint8_t *field,
                      int frame_w, int frame_h, double alpha)
{
  (void)dst;    /* returning 0 asks the host to copy the field for us */
  (void)pitch;
  (void)alpha;  /* panels are static, so an interpolated frame is the same */
  if (s_style == kBezelNone || !field) return 0;
  if (frame_w <= FF6_AUTHENTIC_W || frame_h <= 0) return 0;

  const int extra = (frame_w - FF6_AUTHENTIC_W) / 2;
  if (extra <= 0) return 0;
  if (!BuildPanels(extra, frame_h)) return 0;

  /* The field is the PPU's render buffer; the margins are backdrop pixels it
   * rewrites every frame, which is why writing them here is safe and why the
   * host's own copy then carries the panels into presentation, the frame
   * dumps and the debug capture without further plumbing. */
  uint8_t *f = (uint8_t *)(uintptr_t)field;
  const size_t row_bytes = (size_t)frame_w * 4u;
  const size_t panel_bytes = (size_t)extra * sizeof(uint32_t);
  const size_t right_off = (size_t)(frame_w - extra) * 4u;

  for (int y = 0; y < frame_h; y++) {
    uint8_t *row = f + (size_t)y * row_bytes;
    memcpy(row, s_left + (size_t)y * (size_t)extra, panel_bytes);
    memcpy(row + right_off, s_right + (size_t)y * (size_t)extra, panel_bytes);
  }
  return 0;   /* let the host present the field, as it always does */
}
