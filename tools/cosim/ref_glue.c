/* ref_glue.c -- the handful of recomp-side symbols the reference build needs
 * but must not link.
 *
 * The B side (docs/COSIM.md, Track A) is runner DEVICE sources + interp816: no
 * common_rtl.c, no cpu_state.c, no generated banks. Three device sources still
 * reference symbols that only the recompiled runtime defines, so they are
 * satisfied here:
 *   - g_interp_apu_driving    (common_rtl.c)  -- APU pacing flag
 *   - SnesHdFrame*            (hd_jobs.c)     -- HD capture, unused headless
 *   - tier2_capture_*         (tier2_capture.c) -- coverage journal, off here
 * None of them affect state hashing when the features are inactive.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "snes/hd_jobs.h"
#include "snes/tier2_capture.h"

int g_interp_apu_driving = 0;

SnesHdFrame *SnesHdFrameCreate(unsigned width, unsigned height) {
    (void)width; (void)height;
    return NULL;
}
void SnesHdFrameDestroy(SnesHdFrame *frame) { (void)frame; }
int SnesHdFrameAddPicture(SnesHdFrame *frame, const uint16_t vram[32768]) {
    (void)frame; (void)vram;
    return -1;
}
bool SnesHdFrameSetLine(SnesHdFrame *frame, unsigned y, const SnesHdLine *line) {
    (void)frame; (void)y; (void)line;
    return false;
}
bool SnesHdFrameSeal(SnesHdFrame *frame) { (void)frame; return false; }

int g_tier2_capture_active = 0;
void tier2_capture_cpu_instruction(uint8_t processor, uint32_t pc, uint8_t mx,
                                   uint8_t emulation, unsigned guest_cycles) {
    (void)processor; (void)pc; (void)mx; (void)emulation; (void)guest_cycles;
}
