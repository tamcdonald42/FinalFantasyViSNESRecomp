/*
 * Final Fantasy VI — quality of life: random-encounter switch.
 *
 * See qol_encounters.h for why this needs no ROM patch and no hook: the game
 * selects its encounter rate from a table indexed by bits 0-1 of $7E11DF, and
 * row 2 (bit 1) is all zeros. We hold that bit for as long as the player has
 * encounters switched off.
 *
 * Rules this file follows:
 *
 *   - Read, modify, write. Bit 0 is the Charm Bangle (a real relic effect),
 *     so the flag is OR-ed in, never assigned over.
 *   - Clear only what we set. If the guest itself raised bit 1 (a Moogle
 *     Charm in the party), leaving the toggle alone must not disable the
 *     relic, so we remember whether the bit was ours and only drop it then.
 *   - Applied every frame, because the game recomputes $11DF when the party
 *     changes; one write at load time would evaporate on the next menu.
 *   - Host state, never guest state. The setting lives in qol.ini and in this
 *     file's statics, so rewinding across the moment it was flipped restores
 *     WRAM and the next frame re-applies the player's current choice. That is
 *     the intended behaviour: this is a preference, not a game event.
 *
 * qol.ini beside the executable:
 *
 *   [QoL]
 *   Encounters    = on     ; on = vanilla encounters, off = disabled
 *   EncountersKey = Grave  ; any SDL key name, or None
 *   (the first release spelled the key ToggleKey; that still loads)
 */

#include "qol_encounters.h"

#include "qol_ini.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common_cpu_infra.h"   /* g_snes */
#include "snes/snes.h"          /* Snes, ->ram is the 128 KiB WRAM */
#include "snes_osd.h"           /* timed toasts */
#include "host_api.h"          /* g_window */
#include "desktop/sdl_compat.h"

#if defined(_WIN32) && defined(_MSC_VER)
#define strcasecmp _stricmp
#endif

#define FF6_ENC_FLAG_ADDR 0x11DFu
#define FF6_ENC_FLAG_BIT  0x02u

#define QOL_INI_NAME "qol.ini"

static int          s_disabled;              /* player's choice */
static int          s_bit_owned;             /* we set bit 1, so we may clear it */
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

static SDL_Scancode s_toggle_key = SDL_SCANCODE_GRAVE;
static QolPadChord  s_pad;          /* optional pad chord */
static int          s_key_was_down;
static int          s_reported;

/* The settings file belongs to qol_ini.c: several QoL features share it, and a
 * per-feature writer that rewrote the whole file would delete the others. */
static void SaveIni(void)
{
  QolIniSet("Encounters", s_disabled ? "off" : "on");
}



static void LoadIni(void)
{
  QolIniEnsureDefaults();

  char val[256];
  if (QolIniGet("Encounters", val, sizeof val)) {
    s_disabled = (strcasecmp(val, "off") == 0 || strcasecmp(val, "false") == 0 ||
                  strcmp(val, "0") == 0);
  }
  /* EncountersKey is the current spelling; ToggleKey is what the first
   * release wrote, and a player's file must keep working. */
  if (!QolIniGet("EncountersKey", val, sizeof val) &&
      !QolIniGet("ToggleKey", val, sizeof val)) {
    return;
  }
  if (strcasecmp(val, "none") == 0) {
    s_toggle_key = SDL_SCANCODE_UNKNOWN;
  } else {
    SDL_Scancode sc = SDL_GetScancodeFromName(val);
    s_toggle_key = (sc == SDL_SCANCODE_UNKNOWN) ? SDL_SCANCODE_GRAVE : sc;
  }
  if (QolIniGet("EncountersPad", val, sizeof val))
    s_pad.mask = QolPadParseChord(val);
}

static void ApplyFlag(void)
{
  if (!g_snes || !g_snes->ram) return;
  uint8_t *wram = g_snes->ram;

  if (s_disabled) {
    if (!(wram[FF6_ENC_FLAG_ADDR] & FF6_ENC_FLAG_BIT)) {
      wram[FF6_ENC_FLAG_ADDR] |= (uint8_t)FF6_ENC_FLAG_BIT;
      s_bit_owned = 1;
    }
  } else if (s_bit_owned) {
    if (wram[FF6_ENC_FLAG_ADDR] & FF6_ENC_FLAG_BIT)
      wram[FF6_ENC_FLAG_ADDR] &= (uint8_t)~FF6_ENC_FLAG_BIT;
    s_bit_owned = 0;
  }
}

void QolAfterConfig(void)
{
  LoadIni();
  fprintf(stderr, "[qol] encounters=%s toggle=%s pad=%s\n",
          s_disabled ? "off" : "on",
          SDL_GetScancodeName(s_toggle_key),
          s_pad.mask ? "chord set" : "none");
  /* No toast here: after_config runs before the host owns an OSD, and the
   * first frame announces the state instead (see QolBeforeFrame). */
}

void QolBeforeFrame(void)
{
  /* Hotkey. Only when a window exists: a headless/scripted run has no player
   * to press anything, and SDL's keyboard state is not meaningful there. */
  if (g_window && SDL_WasInit(SDL_INIT_VIDEO)) {
    const int pad_fired = QolPadChordFired(&s_pad);
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    if (keys) {
      int down = keys[s_toggle_key] ? 1 : 0;
      if (down && !s_key_was_down) {
        s_disabled = !s_disabled;
        snes_osd_push(s_disabled ? "Encounters OFF" : "Encounters ON", 1500);
        SaveIni();
        fprintf(stderr, "[qol] encounters=%s\n", s_disabled ? "off" : "on");
      }
      s_key_was_down = down;
      if (pad_fired) {
        s_disabled = !s_disabled;
        snes_osd_push(s_disabled ? "Encounters OFF" : "Encounters ON", 1500);
        SaveIni();
        fprintf(stderr, "[qol] encounters=%s (pad)\n", s_disabled ? "off" : "on");
      }
    }
  }

  ApplyFlag();

  if (!s_reported && g_snes && g_snes->ram) {
    s_reported = 1;
    if (s_disabled && g_window) snes_osd_push("Encounters OFF", 2000);
    fprintf(stderr, "[qol] first frame: $11DF=0x%02X encounters=%s\n",
            g_snes->ram[FF6_ENC_FLAG_ADDR], s_disabled ? "off" : "on");
  }
}

int QolEncountersDisabled(void) { return s_disabled; }
