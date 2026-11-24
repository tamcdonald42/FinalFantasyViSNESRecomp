#ifndef QOL_WIDESCREEN_H
#define QOL_WIDESCREEN_H

/*
 * Final Fantasy VI — quality of life: widescreen (default off).
 * See qol_widescreen.c for the policy and the $1503 measurement behind it.
 */

/* Read qol.ini. Call from the host's after_config hook. */
void QolWidescreenAfterConfig(void);

/* Hotkey + per-frame PPU policy. Call from before_run_frame, i.e. after the
 * host has prepared the frame's PPU capacity. */
void QolWidescreenBeforeFrame(void);

/* Frame width for this frame: 256 when off, 384 when on. */
void QolWidescreenPrepareFrame(int drawable_w, int drawable_h,
                               int *frame_w, int *frame_h);

int QolWidescreenEnabled(void);

#endif /* QOL_WIDESCREEN_H */
