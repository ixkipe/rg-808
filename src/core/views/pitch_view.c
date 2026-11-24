/* Shared pitch page: -24..+24 semitones, one semitone per tap of up/down and
 * an octave per tap of left/right. The value lives in Engine.pitch[] and is
 * persisted with the pattern (PIT lines). */
#include "pitch_view.h"

#include <stdio.h>

int rg_pitch_page_action(ViewCtx *v, int action)
{
    if (!v || !v->eng) return 0;
    int t = v->track;
    if (t < 0 || t >= N_TRACKS) return 0;

    int before = pitch_clamp(v->eng->pitch[t]);
    int after = before;
    switch (action) {
    case RGV_UP:    after = before + 1;  break;
    case RGV_DOWN:  after = before - 1;  break;
    case RGV_RIGHT: after = before + 12; break;
    case RGV_LEFT:  after = before - 12; break;
    default:        return 0;
    }
    after = pitch_clamp(after);
    if (after == before) return 0;
    v->eng->pitch[t] = after;
    return 1;
}

void rg_draw_pitch_page(const ViewCtx *v)
{
    if (!v || !v->cv || !v->eng) return;
    Canvas *c = v->cv;
    int t = v->track;
    if (t < 0 || t >= N_TRACKS) t = 0;
    int st = pitch_clamp(v->eng->pitch[t]);

    int px = 20, py = 96, pw = 600, ph = 250;
    rg_view_panel(c, px, py, pw, ph, v->panel, v->text);

    char buf[64];
    snprintf(buf, sizeof buf, "PITCH  -  %s", TRACKS[t].name);
    cv_text(c, px + 16, py + 10, buf, 1, v->text);
    cv_text(c, px + 16, py + 26,
            "TIME-STRETCHED: THE SAMPLE KEEPS ITS LENGTH", 1, v->dim);

    /* Big value */
    if (st == 0) snprintf(buf, sizeof buf, "0 ST");
    else         snprintf(buf, sizeof buf, "%+d ST", st);
    cv_text(c, px + pw / 2 - cv_text_w(buf, 3) / 2, py + 62, buf, 3,
            st == 0 ? v->dim : v->led);

    /* -24 .. +24 ruler with a marker */
    int bx = px + 40, by = py + 122, bw = pw - 80, bh = 14;
    cv_rect(c, bx, by, bw, bh, cv_dim(v->panel, 0.55f));
    for (int i = 0; i <= 4; i++) {
        int x = bx + bw * i / 4;
        cv_rect(c, x - 1, by, 2, bh, v->dim);
    }
    int mid = bx + bw / 2;
    cv_rect(c, mid - 1, by - 6, 2, bh + 12, v->text);
    int mx = bx + (int)((long)(st - PITCH_MIN) * bw / (PITCH_MAX - PITCH_MIN));
    cv_rect(c, mx - 3, by - 4, 6, bh + 8, v->led);

    snprintf(buf, sizeof buf, "%d", PITCH_MIN);
    cv_text(c, bx - 4, by + bh + 6, buf, 1, v->dim);
    snprintf(buf, sizeof buf, "%d", PITCH_MAX);
    cv_text(c, bx + bw - cv_text_w(buf, 1) + 4, by + bh + 6, buf, 1, v->dim);
    cv_text(c, mid - cv_text_w("0", 1) / 2, by + bh + 6, "0", 1, v->dim);

    cv_text(c, px + 16, py + ph - 40,
            "UP/DOWN: 1 SEMITONE    LEFT/RIGHT: 12 SEMITONES", 1, v->text);
    cv_text(c, px + 16, py + ph - 22,
            "A: HEAR    B: BACK", 1, v->dim);
}
