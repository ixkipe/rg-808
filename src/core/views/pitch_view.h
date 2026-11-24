/* Pitch page (L1+X): semitone transpose with the duration preserved. */
#ifndef RG808_PITCH_VIEW_H
#define RG808_PITCH_VIEW_H

#include "views.h"
#include "pitch.h"

/* Value actions: up/down step one semitone, left/right jump an octave.
 * Returns 1 when the value changed (the caller re-resolves the sample). */
int rg_pitch_page_action(ViewCtx *v, int action);

void rg_draw_pitch_page(const ViewCtx *v);

#endif
