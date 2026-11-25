/* Slice page (L1+B): per-step sample start/end, in 0.01 % steps. */
#ifndef RG808_SLICE_VIEW_H
#define RG808_SLICE_VIEW_H

#include "views.h"

#define RG_WAVE_COLS 480

/* Peak envelope of a sample, rebuilt only when the source changes: a full
 * 10 s sample is ~440k frames, far too many to scan every frame. */
typedef struct RgWave {
    const float *data;
    int len;
    float lo[RG_WAVE_COLS];
    float hi[RG_WAVE_COLS];
    int valid;
} RgWave;

void rg_wave_build(RgWave *w, const float *data, int len);

/* Page state kept by the shell so the handle and the cursor survive a
 * close/reopen. */
typedef struct SliceUi {
    int step;              /* 0-based cursor step */
    int handle;            /* 0 = start, 1 = end */
    int repeat;            /* consecutive same-direction calls (acceleration) */
    int repeat_dir;        /* RGV_* of the last value action */
} SliceUi;

void rg_slice_ui_open(SliceUi *ui, int step);

/* Applies one action. Returns 1 when the page wants a re-audition (the step,
 * the handle or the value changed). */
int rg_slice_page_action(ViewCtx *v, SliceUi *ui, int action);

void rg_draw_slice_page(const ViewCtx *v, const SliceUi *ui, const RgWave *wave);

#endif
