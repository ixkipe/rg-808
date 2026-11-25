/* Shared slice page: waveform strip, 16 step cells and the two percentages.
 *
 * Keys: left/right moves the step cursor, up/down nudges the selected value
 * (0.01 % per tap, accelerating while held), A swaps START/END, X hears the
 * slice, B leaves. A step whose slice is untouched plays the whole sample. */
#include "slice_view.h"
#include "engine.h"
#include "machine.h"

#include <stdio.h>
#include <string.h>

void rg_wave_build(RgWave *w, const float *data, int len)
{
    if (!w) return;
    if (w->valid && w->data == data && w->len == len) return;
    w->data = data;
    w->len = len;
    w->valid = 1;
    for (int i = 0; i < RG_WAVE_COLS; i++) {
        w->lo[i] = 0.0f;
        w->hi[i] = 0.0f;
    }
    if (!data || len <= 0) return;

    for (int i = 0; i < RG_WAVE_COLS; i++) {
        int a = (int)((long long)len * i / RG_WAVE_COLS);
        int b = (int)((long long)len * (i + 1) / RG_WAVE_COLS);
        if (b <= a) b = a + 1;
        if (b > len) b = len;
        float lo = 0.0f, hi = 0.0f;
        for (int f = a; f < b; f++) {
            float v = data[f];
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        w->lo[i] = lo;
        w->hi[i] = hi;
    }
}

void rg_slice_ui_open(SliceUi *ui, int step)
{
    if (!ui) return;
    if (step < 0) step = 0;
    if (step >= N_STEPS) step = N_STEPS - 1;
    ui->step = step;
    ui->repeat = 0;
    ui->repeat_dir = -1;
}

int rg_slice_page_action(ViewCtx *v, SliceUi *ui, int action)
{
    if (!v || !v->eng || !ui) return 0;
    int t = v->track;
    if (t < 0 || t >= N_TRACKS) return 0;

    Engine *e = v->eng;
    int len = engine_length_clamp(e->length);
    if (ui->step < 0) ui->step = 0;
    if (ui->step >= len) ui->step = len - 1;
    if (ui->handle < 0 || ui->handle > 1) ui->handle = 0;

    int before_step = ui->step;
    int before_handle = ui->handle;
    int before = ui->handle == 0 ? e->trim_s[t][ui->step] : e->trim_e[t][ui->step];

    switch (action) {
    case RGV_LEFT:
    case RGV_RIGHT: {
        int dir = action == RGV_RIGHT ? 1 : -1;
        ui->repeat = ui->repeat_dir == action ? ui->repeat + 1 : 0;
        ui->repeat_dir = action;
        int s = ui->step + dir;
        if (s < 0) s = len - 1;
        if (s >= len) s = 0;
        ui->step = s;
        break;
    }
    case RGV_UP:
    case RGV_DOWN: {
        int dir = action == RGV_UP ? 1 : -1;
        ui->repeat = ui->repeat_dir == action ? ui->repeat + 1 : 0;
        ui->repeat_dir = action;
        /* 0.01 % per tap, then quicker: a held key sweeps a whole sample
         * without making a single unit tedious. */
        static const int inc[] = { 1, 1, 2, 4, 8, 16, 32, 64, 100, 200, 400 };
        int n = ui->repeat < 10 ? ui->repeat : 10;
        int delta = inc[n] * dir;

        int lo = 0, hi = 10000;
        if (ui->handle == 0) hi = (int)e->trim_e[t][ui->step] - 1;
        else                 lo = (int)e->trim_s[t][ui->step] + 1;
        if (hi < lo) hi = lo;

        int nv = (int)(ui->handle == 0 ? e->trim_s[t][ui->step]
                                       : e->trim_e[t][ui->step]) + delta;
        if (nv < lo) nv = lo;
        if (nv > hi) nv = hi;
        if (ui->handle == 0) e->trim_s[t][ui->step] = (unsigned short)nv;
        else                 e->trim_e[t][ui->step] = (unsigned short)nv;
        break;
    }
    case RGV_OK:
        ui->handle ^= 1;
        ui->repeat = 0;
        break;
    default:
        return 0;
    }

    int after = ui->handle == 0 ? e->trim_s[t][ui->step] : e->trim_e[t][ui->step];
    return ui->step != before_step || ui->handle != before_handle || after != before;
}

/* 564 -> "5.64" */
static void pct_value(char *buf, size_t cap, int units)
{
    if (units < 0) units = 0;
    if (units > 10000) units = 10000;
    snprintf(buf, cap, "%d.%02d", units / 100, units % 100);
}

void rg_draw_slice_page(const ViewCtx *v, const SliceUi *ui, const RgWave *wave)
{
    if (!v || !v->cv || !v->eng || !ui) return;
    Canvas *c = v->cv;
    Engine *e = v->eng;
    int t = v->track;
    if (t < 0 || t >= N_TRACKS) t = 0;
    int len = engine_length_clamp(e->length);
    int step = ui->step < len ? ui->step : len - 1;
    int ts = e->trim_s[t][step];
    int te = e->trim_e[t][step];

    int px = 20, py = 76, pw = 600, ph = 328;
    rg_view_panel(c, px, py, pw, ph, v->panel, v->text);

    char buf[96];
    snprintf(buf, sizeof buf, "SLICE  -  %s  -  STEP %02d", TRACKS[t].name, step + 1);
    cv_text(c, px + 16, py + 10, buf, 1, v->text);
    if (e->sample_missing[t])
        cv_text(c, px + pw - cv_text_w("NO SAMPLE", 1) - 16, py + 10,
                "NO SAMPLE", 1, v->warn);

    /* ---- waveform strip ---- */
    int wx = px + 16, wy = py + 30, ww = pw - 32, wh = 116;
    cv_rect(c, wx, wy, ww, wh, 0x141414);
    cv_border(c, wx, wy, ww, wh, 2, v->dim);
    int mid = wy + wh / 2;
    cv_rect(c, wx + 2, mid, ww - 4, 1, cv_dim(v->text, 0.12f));

    /* The slice being edited is drawn brighter than the rest. */
    int sx = wx + 2 + (int)((long long)(ww - 4) * ts / 10000);
    int ex = wx + 2 + (int)((long long)(ww - 4) * te / 10000);
    if (ex <= sx) ex = sx + 1;
    int half = wh / 2 - 3;
    if (wave && wave->valid) {
        for (int i = 0; i < RG_WAVE_COLS; i++) {
            int x = wx + 2 + (int)((long long)(ww - 4) * i / RG_WAVE_COLS);
            int lo = mid - (int)(wave->lo[i] * half);
            int hi = mid - (int)(wave->hi[i] * half);
            if (hi < lo) { int tmp = hi; hi = lo; lo = tmp; }
            if (hi - lo < 1) hi = lo + 1;
            int inside = x >= sx && x <= ex;
            cv_rect(c, x, lo, 1, hi - lo,
                    inside ? TRACKS[t].color : cv_dim(TRACKS[t].color, 0.30f));
        }
    }
    /* Start/end handles. */
    cv_rect(c, sx, wy + 2, 2, wh - 4, v->led);
    cv_rect(c, ex - 1, wy + 2, 2, wh - 4, v->led);

    /* ---- step cells ---- */
    const int cell = 30, cgap = 4;
    int total = N_STEPS * cell + (N_STEPS - 1) * cgap;
    int cx0 = px + (pw - total) / 2, cy0 = py + 158;
    for (int s = 0; s < N_STEPS; s++) {
        int x = cx0 + s * (cell + cgap);
        int in = s < len;
        int cs = e->trim_s[t][s], ce = e->trim_e[t][s];
        int cut = (cs != 0 || ce != 10000);
        Uint32 fill = !in ? 0x141414 : (cut ? cv_dim(TRACKS[t].color, 0.55f) : 0x242424);
        cv_rect(c, x, cy0, cell, 26, fill);
        if (in && cut) {
            int bx = x + (int)((long long)cell * cs / 10000);
            int bw = (int)((long long)cell * ce / 10000) - (int)((long long)cell * cs / 10000);
            if (bw < 1) bw = 1;
            cv_rect(c, bx, cy0 + 20, bw, 3, TRACKS[t].color);
        }
        cv_border(c, x, cy0, cell, 26, 2,
                  s == step ? v->cursor : (in ? v->dim : 0x2a2a2a));
        if (s == step) {
            char d[4];
            snprintf(d, sizeof d, "%d", s + 1);
            cv_text(c, x + (cell - cv_text_w(d, 1)) / 2, cy0 + 6, d, 1, v->cursor);
        } else if (in && (cs != 0 || ce != 10000)) {
            cv_rect(c, x + cell / 2 - 1, cy0 + 8, 2, 6, cv_dim(TRACKS[t].color, 0.8f));
        }
    }

    /* ---- values ---- */
    int vy = py + 198;
    char val[32];
    pct_value(val, sizeof val, ts);
    cv_text(c, px + 40, vy, "START", 1, ui->handle == 0 ? v->led : v->dim);
    cv_text(c, px + 130, vy - 6, val, 2, ui->handle == 0 ? v->text : v->dim);

    pct_value(val, sizeof val, te);
    cv_text(c, px + 40, vy + 34, "END", 1, ui->handle == 1 ? v->led : v->dim);
    cv_text(c, px + 130, vy + 28, val, 2, ui->handle == 1 ? v->text : v->dim);

    /* Selected-handle marker on the left edge of the value block. */
    cv_rect(c, px + 26, (ui->handle == 0 ? vy : vy + 34) + 2, 4, 14, v->led);

    /* Slice length, in seconds when the rate is known. */
    int slen = e->sample_len[t];
    if (slen > 0) {
        int start = (int)((long long)slen * ts / 10000);
        int end = (int)((long long)slen * te / 10000);
        if (end <= start) end = start + 1;
        double secs = (double)(end - start) / (double)(e->rate > 0 ? e->rate : 44100);
        snprintf(buf, sizeof buf, "%d FRAMES   %.2f S", end - start, secs);
        cv_text(c, px + 380, vy + 12, buf, 1, v->dim);
    } else {
        cv_text(c, px + 380, vy + 12, "NO SAMPLE ASSIGNED", 1, v->warn);
    }

    cv_text(c, px + 16, py + ph - 18,
            "LR:STEP  UD:VALUE  A:HANDLE  X:HEAR  B:BACK", 1, v->dim);
}
