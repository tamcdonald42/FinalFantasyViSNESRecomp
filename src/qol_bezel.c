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
 *   custom  the player's own art, one panel per side
 *
 * CUSTOM ART: ONE IMAGE OR TWO
 *
 *   BezelImage       fills both panels — the same picture, mirrored when
 *                    BezelMirror is on. The simple case.
 *   BezelImageLeft   fills the left panel only.
 *   BezelImageRight  fills the right panel only.
 *
 * The per-side keys win over BezelImage for their own side, so a pair of
 * hand-drawn panels needs no mirroring and no symmetry. Anything missing falls
 * back: explicit side -> BezelImage -> the other side (mirrored) -> slate. A
 * picture that will not load logs why and never leaves a black gap.
 *
 * See docs/BEZEL_ART.md for the geometry art should be drawn at.
 *
 * Panels are built once into two row-major ARGB buffers (extra x frame_h per
 * side), with BezelOpacity baked in, so the per-frame cost is two row copies —
 * not a per-pixel blend on every frame.
 *
 * The image decoder is stb_image, compiled static into this translation unit so
 * it cannot collide with the OpenGL presenter's own copy.
 */

#include "qol_bezel.h"
#include "widescreen.h"   /* RtlWidescreenPostPresent: the hook we install */

/* Published by the GL presenter (snesrecomp/runner/src/desktop/opengl.c). NULL
 * under the SDL renderer, which is how the framebuffer path knows to keep
 * painting its own panels instead. Declared here rather than including the
 * presenter's header so the game build stays independent of its include path. */
extern void (*SnesOpenGLBezelImage)(const unsigned char *left, int left_w,
                                    int left_h, const unsigned char *right,
                                    int right_w, int right_h);
extern void (*SnesOpenGLBezelFrame)(int visible, float margin_fraction);

/* The same art at full resolution, for the presenter's bezel layer: the panel
 * above is 64x224 because the frame is, and upscaling that is what looked
 * muddy. Kept as raw RGBA, uploaded once by the GL presenter. */
static unsigned char *s_art_l;
static unsigned char *s_art_r;
static int       s_art_lw, s_art_lh, s_art_rw, s_art_rh;
static int       s_art_sent;

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
/* The PPU never draws the outer 8 columns of its 256. In a 4:3 frame they sit
 * at the screen edge and nobody notices; beside art they read as a black gap
 * between the game and the panels - which is exactly how the player reports
 * it. The panels therefore reach 8 columns into the frame, but only while
 * those columns really are empty: a scene that paints them keeps its
 * picture. Measured from the cartridge: its own frame has first non-black
 * column 8 and last 247 in the intro. */
#define FF6_BORDER_X 8

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
static int         s_mirror = 1;      /* mirror one shared image on the right */
static char        s_img_shared[512];
static char        s_img_left[512];
static char        s_img_right[512];

/* Built lazily: the panel size depends on the frame the host asks for. */
static uint32_t *s_left;
static uint32_t *s_right;
static int       s_panel_w;
static int       s_panel_h;
/* Defined below: the verdict for the frame being presented. */
static int MarginFlat(const uint8_t *base, size_t pitch, int x0, int width,
                      int h);

/* Defined at the bottom: the presenter calls it on every frame. */

/* Tell the GL presenter what to draw. Uploads the art the first time the
 * presenter exists (after_config runs before there is a GL context). */
static void PublishGlBezel(int visible, float frac)
{
  if (!SnesOpenGLBezelImage) return;
  if (!s_art_sent) {
    s_art_sent = 1;
    SnesOpenGLBezelImage(s_art_l, s_art_lw, s_art_lh,
                         s_art_r, s_art_rw, s_art_rh);
  }
  if (SnesOpenGLBezelFrame) SnesOpenGLBezelFrame(visible, frac);
}

static void BezelPostPresent(uint8_t *dst, size_t pitch, int frame_w,
                             int frame_h);

static int       s_margin_free_l;   /* verdict for the frame being presented */
static int       s_margin_free_r;
static int       s_border_free;     /* the PPU border columns are empty */
static int       s_border_run;      /* frames the border verdict has disagreed */
static int       s_share_l, s_share_r;
/* How many consecutive frames must agree before the panels follow a new
 * verdict. The opening's logo flash wanders into the margins for two or three
 * frames at a time; without this the panels strobe in step with it (the
 * flicker seen on the title screen). A genuinely wide scene holds far longer
 * than this, so it still takes over, only a fifth of a second later. */
#define kBezelDwellFrames 12
/* The border verdict moves the picture edge by 8 columns, so it changes far
 * more slowly than the panel verdict: at 12 frames the title screen flipped
 * 64 <-> 72 px several times a second and read as the art resizing itself. */
#define kBezelBorderDwell 60
/* Share of a margin that must be one colour for it to count as backdrop.
 * Measured on the title screen: its glow sits at 92-93%, a genuinely wide
 * scene at 82%, an untouched margin at 100%. 94 cut through the gloss and
 * made the panels step aside for a scene that was not there; 90 leaves the
 * glow alone and still catches every real wide scene. */
#define kBezelFlatPercent 90
static int       s_paint = 1;      /* committed verdict: panels on? */
static int       s_paint_run;      /* frames spent agreeing with a new one */
static int       s_calls;
static int       s_custom_have_l;   /* custom mode: which sides loaded */
static int       s_custom_have_r;

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

static void FreeArt(void)
{
  if (s_art_l) { stbi_image_free(s_art_l); s_art_l = NULL; }
  if (s_art_r) { stbi_image_free(s_art_r); s_art_r = NULL; }
  s_art_lw = s_art_lh = s_art_rw = s_art_rh = 0;
  s_art_sent = 0;
}

static void FreePanels(void)
{
  free(s_left);
  free(s_right);
  s_left = s_right = NULL;
  s_panel_w = s_panel_h = 0;
  FreeArt();
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

/* ── the player's own art ───────────────────────────────────────────────── */

/* Resolve a configured path against the exe folder, then load, scale to one
 * panel and (optionally) mirror it. Returns 0 when the file cannot be read. */
static int LoadPanel(const char *image, uint32_t *dst, int w, int h, int flip)
{
  if (!image || !image[0]) return 0;
  char path[768];
  if (image[0] == '\\' || image[0] == '/' ||
      (image[1] == ':' && (image[2] == '\\' || image[2] == '/'))) {
    snprintf(path, sizeof path, "%s", image);
  } else {
    char dir[512];
    ExeDir(dir, sizeof dir);
    snprintf(path, sizeof path, "%s/%s", dir, image);
  }

  int iw = 0, ih = 0, ic = 0;
  unsigned char *px = stbi_load(path, &iw, &ih, &ic, 4);
  if (!px || iw <= 0 || ih <= 0) {
    fprintf(stderr, "[qol] bezel: cannot load '%s' (%s)\n",
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
      const int dx = flip ? (w - 1 - x) : x;
      dst[(size_t)y * (size_t)w + (size_t)dx] = Fade(ARGB(p[3], p[0], p[1], p[2]));
    }
  }
  stbi_image_free(px);
  fprintf(stderr, "[qol] bezel: loaded '%s' %dx%d -> panel %dx%d%s\n",
          path, iw, ih, w, h, flip ? " (mirrored)" : "");
  return 1;
}

static const char *LeftSource(void)
{
  return s_img_left[0] ? s_img_left : s_img_shared;
}

static const char *RightSource(void)
{
  return s_img_right[0] ? s_img_right : s_img_shared;
}



/* Resolve a configured path against the exe folder. */
static void ArtPath(const char *image, char *out, size_t cap)
{
  if (image[0] == '\\' || image[0] == '/' ||
      (image[1] == ':' && (image[2] == '\\' || image[2] == '/'))) {
    snprintf(out, cap, "%s", image);
  } else {
    char dir[512];
    ExeDir(dir, sizeof dir);
    snprintf(out, cap, "%s/%s", dir, image);
  }
}

/* Full-resolution copy for the presenter, opacity baked in so the GPU pass and
 * the framebuffer pass agree. NULL when the file cannot be read. */
static unsigned char *LoadNative(const char *image, int *w, int *h)
{
  if (!image || !image[0]) return NULL;
  char path[768];
  ArtPath(image, path, sizeof path);
  int iw = 0, ih = 0, ic = 0;
  unsigned char *px = stbi_load(path, &iw, &ih, &ic, 4);
  if (!px || iw <= 0 || ih <= 0) {
    if (px) stbi_image_free(px);
    return NULL;
  }
  if (s_opacity < 100) {
    const size_t n = (size_t)iw * (size_t)ih;
    for (size_t i = 0; i < n; i++) {
      unsigned char *c = px + i * 4u;
      c[0] = (unsigned char)(c[0] * s_opacity / 100);
      c[1] = (unsigned char)(c[1] * s_opacity / 100);
      c[2] = (unsigned char)(c[2] * s_opacity / 100);
    }
  }
  *w = iw; *h = ih;
  return px;
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

  if (s_style == kBezelCustom) {
    const char *ls = LeftSource();
    const char *rs = RightSource();
    int have_l = 0, have_r = 0;
    if (ls && ls[0]) have_l = LoadPanel(ls, s_left, w, h, 0);
    /* One shared picture on both sides gets mirrored unless BezelMirror says
     * otherwise; a dedicated right panel is drawn the way the artist made it. */
    const int same_file = ls && rs && strcmp(rs, ls) == 0;
    if (rs && rs[0]) have_r = LoadPanel(rs, s_right, w, h, same_file && s_mirror);
    /* A side with nothing of its own borrows the other side, mirrored. This sits
     * outside the block above on purpose: with only BezelImageLeft set, the right
     * source is empty, and the documented fallback is "mirror the left", not
     * "paint slate". */
    if (!have_r && ls && ls[0]) have_r = LoadPanel(ls, s_right, w, h, s_mirror);
    if (!have_l && rs && rs[0]) have_l = LoadPanel(rs, s_left, w, h, s_mirror);
    /* Art for the GL layer: same files, native size. Uploaded by the presenter
     * on the first frame it exists, so this can run before any GL context. */
    FreeArt();
    s_art_l = LoadNative(ls, &s_art_lw, &s_art_lh);
    s_art_r = LoadNative(rs, &s_art_rw, &s_art_rh);
    if (!s_art_r && s_art_l) {              /* shared picture: reuse it */
      s_art_r = LoadNative(ls, &s_art_rw, &s_art_rh);
    }
    if (!s_art_l && s_art_r) {
      s_art_l = LoadNative(rs, &s_art_lw, &s_art_lh);
    }
    if (have_l && have_r) {
      fprintf(stderr, "[qol] bezel: custom panels %dx%d, opacity %d%%%s\n",
              w, h, s_opacity,
              (ls && rs && strcmp(ls, rs) == 0 && s_mirror) ? ", one image mirrored" : "");
      return 1;
    }
    /* One side (or both) failed to load: keep whatever did load and let the
     * style fill only the gap, never paint over good art. */
    s_custom_have_l = have_l;
    s_custom_have_r = have_r;
    fprintf(stderr, "[qol] bezel: custom art incomplete; filling gaps with slate\n");
  }

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
      if (!(s_style == kBezelCustom && s_custom_have_l))
        s_left[(size_t)y * (size_t)w + (size_t)x] = f;
      const int rx = s_mirror ? (w - 1 - x) : x;
      if (!(s_style == kBezelCustom && s_custom_have_r))
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
  s_img_shared[0] = '\0';
  s_img_left[0] = '\0';
  s_img_right[0] = '\0';
  s_custom_have_l = s_custom_have_r = 0;

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
    snprintf(s_img_shared, sizeof s_img_shared, "%s", v);
  }
  if (QolIniGet("BezelImageLeft", v, sizeof v)) {
    Trim(v);
    snprintf(s_img_left, sizeof s_img_left, "%s", v);
  }
  if (QolIniGet("BezelImageRight", v, sizeof v)) {
    Trim(v);
    snprintf(s_img_right, sizeof s_img_right, "%s", v);
  }
  if (QolIniGet("BezelOpacity", v, sizeof v)) {
    const int n = atoi(v);
    s_opacity = n < 0 ? 0 : (n > 100 ? 100 : n);
  }
  if (QolIniGet("BezelMirror", v, sizeof v)) {
    s_mirror = !(v[0] == '0' || v[0] == 'n' || v[0] == 'N' ||
                 v[0] == 'f' || v[0] == 'F');
  }

  s_margin_free_l = s_margin_free_r = 0;
  s_border_free = 0;
  FreePanels();
  s_custom_have_l = s_custom_have_r = 0;
  /* The presenter calls this on every frame; keep the pointer even when the
   * style is none so a later F10 or ini edit does not need a restart. */
  RtlWidescreenPostPresent = BezelPostPresent;
  if (s_style == kBezelNone) {
    fprintf(stderr, "[qol] bezel=none (margins stay black)\n");
  } else if (s_style == kBezelCustom) {
    fprintf(stderr, "[qol] bezel=custom shared='%s' left='%s' right='%s' "
                    "opacity=%d%% mirror=%d\n",
            s_img_shared, s_img_left, s_img_right, s_opacity, s_mirror);
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
  /* Nothing is drawn here any more. Panels go on in RtlWidescreenPostPresent,
   * because the frame the player sees is the one the presenter builds: painting
   * the PPU field meant a scene that renders real content into the wide columns
   * (the opening's snow, for one) came out from under the art, leaving panels
   * cut off halfway down the screen. */
  (void)dst; (void)pitch; (void)alpha;
  s_margin_free_l = s_margin_free_r = 0;
  /* s_border_free is NOT reset here. The margin flags are raw per-frame
   * observations, but the border verdict is settled state: clearing it every
   * frame restarted the dwell run, so a scene whose border columns drift in
   * and out of flatness rebuilt the panels 1,963 times in 12,600 frames and
   * shifted the picture edge at 60 Hz. */
  if (s_style == kBezelNone || !field) return 0;
  if (frame_w <= FF6_AUTHENTIC_W || frame_h <= 0) return 0;
  const int extra = (frame_w - FF6_AUTHENTIC_W) / 2;
  if (extra <= 0) return 0;
  /* Measured on the PPU field, never on the buffer we paint: the presenter's
   * output is the same buffer from one frame to the next, so a scan of it would
   * find last frame's panels and call them picture (panels flickered on and off
   * once per frame before this moved here). */
  s_calls++;
  const int share_l = MarginShare(field, (size_t)frame_w * 4u, 0, extra, frame_h);
  const int share_r = MarginShare(field, (size_t)frame_w * 4u, frame_w - extra,
                                  extra, frame_h);
  s_share_l = share_l; s_share_r = share_r;
  s_margin_free_l = share_l >= kBezelFlatPercent;
  s_margin_free_r = share_r >= kBezelFlatPercent;
  /* The 8 columns just inside the picture, where the PPU's border lives. */
  const int edge_l = MarginShare(field, (size_t)frame_w * 4u, extra,
                                 FF6_BORDER_X, frame_h);
  const int edge_r = MarginShare(field, (size_t)frame_w * 4u,
                                 frame_w - extra - FF6_BORDER_X,
                                 FF6_BORDER_X, frame_h);
  /* Dwell, like the margin verdict: a scene whose border columns drift in
   * and out of flatness would otherwise rebuild the panels at 60 Hz, which
   * re-decodes the art every frame and shifts the picture edge as it goes. */
  const int border_want = edge_l >= kBezelFlatPercent && edge_r >= kBezelFlatPercent;
  if (border_want != s_border_free) {
    if (++s_border_run >= kBezelBorderDwell) {
      s_border_free = border_want;
      s_border_run = 0;
    }
  } else {
    s_border_run = 0;
  }
  const int want = s_margin_free_l && s_margin_free_r;
  if (want != s_paint) {
    if (++s_paint_run >= kBezelDwellFrames) {
      s_paint = want;
      s_paint_run = 0;
    }
  } else {
    s_paint_run = 0;
  }
  return 0;
}

/* Is this strip still empty, or has the renderer put picture in it? A margin
 * the game leaves alone is a single flat colour; a wide BG fills it with tiles.
 * Flat means "safe to paint panels over". */
static int MarginShare(const uint8_t *base, size_t pitch, int x0, int width,
                       int h)
{
  if (width <= 0 || h <= 0) return 0;
  uint16_t hist[4096];
  memset(hist, 0, sizeof hist);
  int sampled = 0;
  for (int y = 0; y < h; y += 2) {
    const uint8_t *p = base + (size_t)y * pitch + (size_t)x0 * 4u;
    for (int x = 0; x < width; x += 2) {
      const uint8_t *c = p + (size_t)x * 4u;
      const int key = ((c[2] >> 4) << 8) | ((c[1] >> 4) << 4) | (c[0] >> 4);
      hist[key]++;
      sampled++;
    }
  }
  if (!sampled) return 0;
  int best = 0;
  for (int i = 0; i < 4096; i++)
    if (hist[i] > best) best = hist[i];
  /* Share of the strip taken by its most common colour, in percent. */
  return (int)((long long)best * 100 / (long long)sampled);
}

static void BezelPostPresent(uint8_t *dst, size_t pitch, int frame_w,
                             int frame_h)
{
  if (s_style == kBezelNone || !dst) return;
  {
    static int w_was = -1;
    if (frame_w != w_was) {
      w_was = frame_w;
      fprintf(stderr, "[qol] bezel: frame width %d @frame %d\n", frame_w, s_calls);
    }
  }
  if (frame_w <= FF6_AUTHENTIC_W || frame_h <= 0) {
    /* A 4:3 scene is presented pillarboxed, so the picture IS the whole
     * frame and the space beside it is the presenter's, not the game's:
     * art there can never cover live pixels. Handing the GL layer 'not
     * visible' instead left that band black and made the panels vanish in
     * every battle; the stray menu fragment the user saw was a leftover in
     * it. Margin 0 means 'no inner strip, fill from the window edge to the
     * picture edge'. */
    if (s_style != kBezelNone) {
      static int said;
      if (!said) { said = 1; fprintf(stderr,
          "[qol] bezel: 4:3 frame %d wide @frame %d - art fills the pillarbox band\n",
          frame_w, s_calls); }
      PublishGlBezel(1, 0.0f);
    }
    return;
  }

  const int extra = (frame_w - FF6_AUTHENTIC_W) / 2;
  if (extra <= 0) return;
  /* Tight against the picture: cover the PPU border when it is empty. */
  const int span = extra + (s_border_free ? FF6_BORDER_X : 0);
  if (!BuildPanels(span, frame_h)) return;

  /* Both or nothing. One panel beside a strip of live picture reads as a
   * mistake; a scene that fills either margin is showing real widescreen, so
   * the pair steps aside and the picture has the frame to itself. */
  const int paint = s_paint;
  PublishGlBezel(paint, (float)span / (float)frame_w);

  static int was = -1;
  static int span_was = -1;
  if (paint && span != span_was) {
    span_was = span;
    fprintf(stderr, "[qol] bezel: panels %d px wide (frame edge to picture edge)%s"
            " @frame %d\n", span, s_border_free ? ", border covered" : "", s_calls);
  }
  if (paint != was) {
    was = paint;
    fprintf(stderr, "[qol] bezel: frame %d %s (share %d%%/%d%%)\n", s_calls, paint
            ? "margins empty - panels on"
            : "scene fills a margin - panels paused", s_share_l, s_share_r);
  }
  if (!paint) return;

  for (int y = 0; y < frame_h; y++) {
    uint8_t *row = dst + (size_t)y * pitch;
    memcpy(row, s_left + (size_t)y * (size_t)span, (size_t)span * 4u);
    memcpy(row + (size_t)(frame_w - span) * 4u,
           s_right + (size_t)y * (size_t)span, (size_t)span * 4u);
  }
}
