/*
 * harness_ff6.c -- headless, deterministic entry point for the differential
 * co-simulation A side (docs/COSIM.md), the FF6 equivalent of the framework's
 * cosim/harness_smw.c. Replaces the SDL/OpenGL desktop host: NO SDL_Init, NO
 * window, NO host audio device, NO worker threads -- which is the Gate-1
 * determinism requirement, and the reason the desktop host cannot be used
 * (it forces g_config.enable_audio = true, so the SDL audio callback thread
 * advances the APU on wall-clock time and two identical processes diverge in
 * the APU/DSP sub-hashes within three checkpoints).
 *
 * DEV/DIAGNOSTICS ONLY. Built only under SNES_COSIM.
 *
 * Usage: ff6_cosim <rom.sfc>      (env SNES_COSIM_PORT / SNES_COSIM_STRIDE)
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "common_rtl.h"
#include "common_cpu_infra.h"
#include "snes/snes.h"
#include "snes/ppu.h"
#include "snes_overlay_draw.h"   /* SNES_PAD_START */

/* Provided by game_rtl.c. */
extern const RtlGameInfo kGameInfo;

/* The desktop host (desktop/host_main.c) owns this global. NULL is legal: the
 * runner then leaves the APU untouched by a host player and the guest's own
 * upload path runs, which is what we want for a faithful A side. */
struct SpcPlayer *g_spc_player;

static uint8_t *read_file(const char *path, uint32_t *size_out) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return NULL; }
    uint8_t *buf = (uint8_t *)malloc((size_t)n);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    if (buf) *size_out = (uint32_t)n;
    return buf;
}

/* Input script, matching the capture the A side is compared against:
 * Start held for frames 504-507 (0-based frames, as --script counts them). */
#define START_FRAME 504
#define START_FRAMES 4

int main(int argc, char **argv) {
    const char *rom = (argc > 1) ? argv[1] : "ff6.sfc";
    uint32_t size = 0;
    uint8_t *data = read_file(rom, &size);
    if (!data) { fprintf(stderr, "cosim-harness: cannot read ROM '%s'\n", rom); return 1; }

    RtlRegisterGame(&kGameInfo);
    Snes *snes = SnesInit(data, (int)size);
    if (!snes) { fprintf(stderr, "cosim-harness: SnesInit failed\n"); return 1; }

    /* The beam frame driver renders PPU lines and fires the raster IRQ/HDMA
     * side effects; give it a buffer so a headless run has identical device
     * behaviour to a presented one. We never show these pixels. */
    {
        extern Ppu *g_ppu;
        static uint8_t s_pixels[256 * 4 * 256];
        if (g_ppu) PpuBeginDrawing(g_ppu, s_pixels, 256 * 4, 0);
    }

    fprintf(stderr, "cosim-harness: booted headless; Start at frame %d for %d frames\n",
            START_FRAME, START_FRAMES);
    for (uint64_t frame = 0;; frame++) {
        uint32_t inputs = (frame >= START_FRAME && frame < START_FRAME + START_FRAMES)
                        ? SNES_PAD_START : 0u;
        RtlRunFrame(inputs);
    }
    return 0;
}
