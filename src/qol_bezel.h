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
 *   Bezel        = none | slate | dim | ink | custom
 *   BezelImage   = bezels/panel.png     ; custom only; PNG/JPG/BMP
 *   BezelOpacity = 100                  ; 0..100, blended toward black
 *   BezelMirror  = 1                    ; mirror the right panel
 *
 * See qol_bezel.c for what each style is, and for why the panels are painted
 * into the field's backdrop rather than composed into a finished frame.
 */

#include <stddef.h>
#include <stdint.h>

/* Read qol.ini and drop any cached panels. Call from after_config. */
void QolBezelAfterConfig(void);

/* Host draw_frame hook. Paints the margin columns of the PPU field in place
 * and always returns 0, so the host runs its normal present path (copy, frame
 * dumps, OSD, debug capture) over a field that already carries the panels. */
int QolBezelDrawFrame(uint8_t *dst, size_t pitch, const uint8_t *field,
                      int frame_w, int frame_h, double alpha);

#endif /* QOL_BEZEL_H */
