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
static SDL_Scancode s_toggle_key = SDL_SCANCODE_GRAVE;
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
  fprintf(stderr, "[qol] encounters=%s toggle=%s\n",
          s_disabled ? "off" : "on",
          SDL_GetScancodeName(s_toggle_key));
  /* No toast here: after_config runs before the host owns an OSD, and the
   * first frame announces the state instead (see QolBeforeFrame). */
}

void QolBeforeFrame(void)
{
  /* Hotkey. Only when a window exists: a headless/scripted run has no player
   * to press anything, and SDL's keyboard state is not meaningful there. */
  if (g_window && SDL_WasInit(SDL_INIT_VIDEO)) {
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
