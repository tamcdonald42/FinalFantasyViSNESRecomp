#ifndef QOL_BEZEL_H
#define QOL_BEZEL_H

/*
 * Final Fantasy VI — quality of life: side panels (bezel).
 *
 * Fills the widescreen frame's margin columns with a chosen panel instead of
 * leaving them black. Inert whenever the frame is the authentic 256 pixels
 * wide, so the 4:3 baseline never goes through this code.
 *
 * qol.ini [QoL]:
 *   Bezel           = none | slate | dim | ink | custom
 *   BezelImage      = bezels/panel.png    ; both sides, mirrored when BezelMirror
 *   BezelImageLeft  = bezels/left.png     ; this side only, never mirrored
 *   BezelImageRight = bezels/right.png    ; this side only, never mirrored
 *   BezelOpacity    = 100                 ; 0..100, blended toward black
 *   BezelMirror     = 1                   ; mirrors a single shared image
 *
 * Per-side keys win over BezelImage for their own side. A side with no art of
 * its own falls back to BezelImage, then to the other side (mirrored), then to
 * the slate panel — a missing file never leaves a black gap.
 *
 * Panels are drawn in the post-present pass, on the finished frame, and only
 * where the game left the margin empty: a scene that renders real content into
 * the wide columns keeps its picture and the panels step aside for it. See
 * qol_bezel.c. docs/BEZEL_ART.md carries the geometry art should be drawn at.
 */

#include <stddef.h>
#include <stdint.h>

/* Read qol.ini and drop any cached panels. Call from after_config. */
void QolBezelAfterConfig(void);

/* Host draw_frame hook, kept so the descriptor shape stays the same: it draws
 * nothing and returns 0, which sends the host down its normal present path. */
int QolBezelDrawFrame(uint8_t *dst, size_t pitch, const uint8_t *field,
                      int frame_w, int frame_h, double alpha);

#endif /* QOL_BEZEL_H */
