/*
 * Final Fantasy VI — quality of life: widescreen.
 *
 * Default off. Off means 256x224 and the verified 4:3 baseline, unchanged in
 * every respect; the width decision lives in QolWidescreenPrepareFrame so that
 * "off" is not a policy the renderer has to honour, it is a width the renderer
 * is never given.
 *
 * WHAT WIDENS, AND WHAT THE MEASUREMENT ACTUALLY SHOWED
 *
 * The framework can render up to 256 + 2*extra columns, and renders them for
 * real: with this module on, the frame is 384x224 and the authentic 256
 * columns sit at x=64..319, pixel-identical to the 4:3 baseline (52 of 52
 * sampled frames over a field/menu/open/close route, diff 0).
 *
 * The margin columns, though, are black on the field, and that is not this
 * module's doing: FF6's field renderer fills a 256-pixel tilemap window, so
 * there is nothing in VRAM for a widened PPU to sample beside it. The PPU is
 * willing; the engine has not drawn the world out there. Genuinely wider
 * fields therefore need the field engine's camera clamp and tilemap window,
 * which is its own phase of work — not a setting.
 *
 *   field        frame widens; margins currently black (engine window).
 *   menu  ($C3)  every menu window is a 32x32 tilemap, i.e. exactly 256 px,
 *                with the rest of VRAM belonging to graphics. -> pillarbox.
 *   battle ($C1) battle backs are one screen wide. -> pillarbox.
 *   world ($EE)  Mode 7 plus a minimap HUD. -> pillarbox.
 *   cinema ($C2) the cinematic engine's own scenes. -> pillarbox.
 *
 * So the picture does not resize when a menu opens: the game simply stops using
 * the extra columns, and PpuSetExtraSpaceCentered keeps the wide framebuffer as
 * the centering budget while the authentic 256 render in the middle.
 *
 * THE TRIGGER
 *
 * $7E:1503 holds the bank of the installed NMI handler (a JML trampoline at
 * $1500-1503 that each subsystem rewrites when it takes over). Measured, not
 * assumed:
 *
 *   field  $C0     battle $C1     menu $C3     world map $EE
 *
 * docs/WIDESCREEN_TRIGGER.md records the dumps behind that table. Reading one
 * byte per frame costs nothing and needs no hook, no patch and no guest write,
 * which is the same shape as the encounter switch.
 *
 * qol.ini:
 *
 *   [QoL]
 *   Widescreen    = off     ; off = 4:3 baseline, on = 384x224 frame
 *   WidescreenKey = F10     ; any SDL key name, or None
 */

#include "qol_widescreen.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qol_ini.h"
#include "common_cpu_infra.h"   /* g_snes */
#include "common_rtl.h"         /* g_ppu */
#include "snes/snes.h"          /* Snes, ->ram is the 128 KiB WRAM */
#include "snes/ppu.h"           /* PpuSetExtraSpaceCentered */
#include "widescreen.h"         /* g_ws_active, g_ws_extra */
#include "snes_osd.h"           /* timed toasts */
#include "host_api.h"           /* g_window */
#include "desktop/sdl_compat.h"

#if defined(_WIN32) && defined(_MSC_VER)
#define strcasecmp _stricmp
#endif

/* Extra columns per side when wide: 256 + 2*64 = 384x224. */
#define FF6_WS_EXTRA 64

/* Bank of the installed NMI handler: who owns interrupts right now. */
#define FF6_INT_OWNER_ADDR 0x1503u
#define FF6_OWNER_FIELD    0xC0u

static int          s_enabled;                 /* player's choice */
/* ── pad chord ──────────────────────────────────────────────────────────────
 * qol.ini: `EncountersPad` / `WidescreenPad` — a '+' separated list of pad
 * buttons, or `None`. Stick clicks (L3/R3), shoulders and triggers are not SNES
 * buttons, so a chord built from them never reaches the game and cannot
 * disturb play. Polled every frame from the host's raw held-button mask and
 * fired on the rising edge, so holding it down toggles once.
 *
 *   L3 R3 sticks   L1 R1 shoulders   A B X Y face   Up Down Left Right d-pad
 *   Select Start Guide
 */
typedef struct QolPadChord { unsigned mask; unsigned was; } QolPadChord;

static int s_pad_bad_reported;

static unsigned QolPadButtonBit(const char *name) {
  static const struct { const char *n; int bit; } kMap[] = {
    {"a", SDL_CONTROLLER_BUTTON_A}, {"b", SDL_CONTROLLER_BUTTON_B},
    {"x", SDL_CONTROLLER_BUTTON_X}, {"y", SDL_CONTROLLER_BUTTON_Y},
    {"back", SDL_CONTROLLER_BUTTON_BACK}, {"select", SDL_CONTROLLER_BUTTON_BACK},
    {"guide", SDL_CONTROLLER_BUTTON_GUIDE}, {"start", SDL_CONTROLLER_BUTTON_START},
    {"l3", SDL_CONTROLLER_BUTTON_LEFTSTICK},
    {"leftstick", SDL_CONTROLLER_BUTTON_LEFTSTICK},
    {"r3", SDL_CONTROLLER_BUTTON_RIGHTSTICK},
    {"rightstick", SDL_CONTROLLER_BUTTON_RIGHTSTICK},
    {"l1", SDL_CONTROLLER_BUTTON_LEFTSHOULDER},
    {"leftshoulder", SDL_CONTROLLER_BUTTON_LEFTSHOULDER},
    {"r1", SDL_CONTROLLER_BUTTON_RIGHTSHOULDER},
    {"rightshoulder", SDL_CONTROLLER_BUTTON_RIGHTSHOULDER},
    {"up", SDL_CONTROLLER_BUTTON_DPAD_UP}, {"down", SDL_CONTROLLER_BUTTON_DPAD_DOWN},
    {"left", SDL_CONTROLLER_BUTTON_DPAD_LEFT},
    {"right", SDL_CONTROLLER_BUTTON_DPAD_RIGHT},
  };
  for (size_t i = 0; i < sizeof(kMap) / sizeof(kMap[0]); ++i)
    if (strcasecmp(name, kMap[i].n) == 0) return 1u << kMap[i].bit;
  return 0u;   /* triggers are axes in SDL2, not buttons: named nowhere on purpose */
}

static unsigned QolPadParseChord(const char *spec) {
  if (!spec || !*spec || strcasecmp(spec, "none") == 0) return 0u;
  unsigned mask = 0u;
  char buf[128];
  snprintf(buf, sizeof buf, "%s", spec);
  for (char *tok = strtok(buf, "+ ,"); tok; tok = strtok(NULL, "+ ,")) {
    const unsigned b = QolPadButtonBit(tok);
    if (b) { mask |= b; continue; }
    if (!s_pad_bad_reported) {
      s_pad_bad_reported = 1;
      fprintf(stderr, "[qol] pad chord: '%s' is not a pad button "
                      "(L3 R3 L1 R1 A B X Y Select Start d-pad)\n", tok);
    }
  }
  return mask;
}

static int QolPadChordFired(QolPadChord *ch) {
  if (!ch->mask) return 0;
  const unsigned held = snesrecomp_pad_held_buttons(0) & 0x1FFFFFu;
  const int down = (held & ch->mask) == ch->mask;
  const int fired = down && !ch->was;
  ch->was = (unsigned)down;
  return fired;
}

static SDL_Scancode s_toggle_key = SDL_SCANCODE_F10;
static QolPadChord  s_pad;               /* optional pad chord */
static int          s_key_was_down;
static int          s_reported_owner = -1;




static SDL_Scancode ParseKey(const char *val, SDL_Scancode fallback)
{
  if (strcasecmp(val, "none") == 0) return SDL_SCANCODE_UNKNOWN;
  SDL_Scancode sc = SDL_GetScancodeFromName(val);
  return (sc == SDL_SCANCODE_UNKNOWN) ? fallback : sc;
}

/* Current interrupt owner, or -1 before WRAM exists. */
static int Owner(void)
{
  if (!g_snes || !g_snes->ram) return -1;
  return (int)g_snes->ram[FF6_INT_OWNER_ADDR];
}

/* Only these banks are ever installed at $1503 once the game is running.
 * Anything else is boot-time garbage: on a cold start the byte reads $00, then
 * $7E, before the first NMI handler is written. Trusting it flipped the width
 * 384 -> 256 -> 384 five times inside the first 62 frames, which the player
 * sees as the art resizing itself on the title screen. */
static int OwnerKnown(int owner) {
  switch (owner) {
    case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xEE: return 1;
    default: return 0;
  }
}

/* A known owner still has to hold for kOwnerDwell frames before the frame
 * width follows it, so a one-frame stub cannot resize the picture. */
#define kOwnerDwell 12
static int OwnerSettled(void) {
  static int settled = -1, raw_was = -1, seen_at;
  const int now = snes_frame_counter;
  const int raw = Owner();
  if (!OwnerKnown(raw)) { raw_was = -1; return settled; }
  if (raw == settled) { raw_was = raw; return settled; }
  if (raw != raw_was) { raw_was = raw; seen_at = now; return settled; }
  /* Dwell counts SNES frames, not calls: this hook is entered many times per
   * frame, so a call-counting dwell expired inside a single frame and still
   * let a menu blip resize the picture for one frame. */
  if (now - seen_at >= kOwnerDwell) settled = raw;
  return settled;
}

void QolWidescreenAfterConfig(void)
{
  QolIniEnsureDefaults();

  char val[256];
  if (QolIniGet("Widescreen", val, sizeof val))
    s_enabled = (strcasecmp(val, "on") == 0 || strcasecmp(val, "true") == 0 ||
                 strcmp(val, "1") == 0);
  if (QolIniGet("WidescreenPad", val, sizeof val))
    s_pad.mask = QolPadParseChord(val);
  if (QolIniGet("WidescreenKey", val, sizeof val))
    s_toggle_key = ParseKey(val, SDL_SCANCODE_F10);

  {
    const char *name = (s_toggle_key == SDL_SCANCODE_UNKNOWN)
                           ? "none"
                           : SDL_GetScancodeName(s_toggle_key);
    fprintf(stderr, "[qol] widescreen=%s (key=%s)\n", s_enabled ? "on" : "off",
            name && *name ? name : "none");
  }
}

int QolWidescreenEnabled(void) { return s_enabled; }

void QolWidescreenPrepareFrame(int drawable_w, int drawable_h,
                               int *frame_w, int *frame_h)
{
  (void)drawable_w;
  (void)drawable_h;

  /* The whole switch. Off hands the host the same 256 the port always asked
   * for, so g_ws_extra stays 0 and the host never activates its wide path. */
  /* A pillarboxed scene gets a 256-wide FRAME, not a 384 frame with the
   * picture centred inside it. Centring kept the extra columns live, so the
   * battle engine -- which lays its windows out for a 256-wide screen -- could
   * put a fragment of a menu box in the margin. That band then stopped reading
   * as empty, the side panels stepped aside by the both-or-nothing rule, and
   * the player saw the panels vanish with a stray window in the void beside a
   * black gap. A real 256 frame cannot show anything outside the picture, and
   * the presenter fills the space beside it with art. */
  const int wide = s_enabled && OwnerSettled() == (int)FF6_OWNER_FIELD;
  *frame_w = wide ? (256 + 2 * FF6_WS_EXTRA) : 256;
  *frame_h = 224;

  if (!wide || !g_ppu) return;

  /* The host calls PpuSetExtraSpace(extra) on every frame in
   * PreparePpuFrame, which restores the full margin on both sides. */
  PpuSetExtraSpaceCentered(g_ppu, (uint16_t)g_ws_extra);
}

void QolWidescreenBeforeFrame(void)
{
  /* Hotkey. Only when a window exists: a headless/scripted run has no player
   * to press anything, and SDL's keyboard state is not meaningful there. */
  if (g_window && SDL_WasInit(SDL_INIT_VIDEO)) {
    const int pad_fired = QolPadChordFired(&s_pad);
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    if (keys) {
      int down = (s_toggle_key != SDL_SCANCODE_UNKNOWN && keys[s_toggle_key]) ? 1 : 0;
      if (down && !s_key_was_down) {
        s_enabled = !s_enabled;
        QolIniSet("Widescreen", s_enabled ? "on" : "off");
        snes_osd_push(s_enabled ? "Widescreen ON" : "Widescreen OFF", 1500);
        fprintf(stderr, "[qol] widescreen=%s\n", s_enabled ? "on" : "off");
      }
      s_key_was_down = down;
      if (pad_fired) {
        s_enabled = !s_enabled;
        QolIniSet("Widescreen", s_enabled ? "on" : "off");
        snes_osd_push(s_enabled ? "Widescreen ON" : "Widescreen OFF", 1500);
        fprintf(stderr, "[qol] widescreen=%s (pad)\n", s_enabled ? "on" : "off");
      }
    }
  }

  if (!s_enabled) return;

  static unsigned long s_tick;
  s_tick++;
  int owner = OwnerSettled();
  if (owner != s_reported_owner) {
    s_reported_owner = owner;
    fprintf(stderr, "[qol] ws owner now $%02X (%s) @tick %lu\n", owner & 0xFF,
            owner == (int)FF6_OWNER_FIELD ? "field: wide"
                                          : "not field: pillarboxed", s_tick);
  }
}
