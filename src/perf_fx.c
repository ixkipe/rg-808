/* RG-808 performance punch-in effects. */
#include "perf_fx.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PERF_SECONDS        16
#define PERF_TWO_PI         6.28318530717958647692
#define PERF_FADE_IN_MS      5.0f
#define PERF_FADE_OUT_MS     8.0f
#define PERF_TAPE_LP_HZ   6000.0f
#define PERF_PHONE_HZ     1200.0f
#define PERF_PHONE_Q         2.5f

const char *const PUNCH_NAMES[PUNCH_COUNT] = {
    "1/4 LOOP", "1/8 LOOP", "1/16 LOOP", "1/32 LOOP",
    "REWIND", "GATE", "TAPE STOP", "PHONE"
};

struct PerfFxState {
    int rate;
    int ring_frames;
    float *ring;                 /* stereo dry history */
    float *slice;                /* frozen loop / tape source */
    int ring_pos;

    volatile int request;        /* UI -> audio: next effect, -1 = none */
    volatile int release_req;    /* UI -> audio: release now */
    volatile int active;         /* audio -> UI: effect playing, -1 = none */

    int loop_frames;
    int loop_pos;
    int reverse;

    int fade_frames;
    int fade_pos;
    int releasing;

    int gate_frames;
    int gate_pos;
    float gate_gain;
    float gate_coeff;

    float tape_pos;
    float tape_progress;
    int tape_stop_frames;
    float tape_lp[2];
    float tape_lp_coeff;

    float phone_b0, phone_b1, phone_b2, phone_a1, phone_a2;
    float phone_z1[2], phone_z2[2];
};

static int perf_ms_frames(int rate, float ms)
{
    int n = (int)(rate * ms / 1000.0f);
    return n < 1 ? 1 : n;
}

/* Length of a 1/denom note in frames, clamped to the ring buffer. */
static int perf_note_frames(const PerfFxState *st, int bpm_x100, int denom)
{
    if (bpm_x100 < 400) bpm_x100 = 400;
    double bpm = bpm_x100 / 100.0;
    double sec = 60.0 / bpm * (4.0 / (double)denom);
    int n = (int)(sec * st->rate + 0.5);
    if (n < 64) n = 64;
    if (n > st->ring_frames - 1) n = st->ring_frames - 1;
    return n;
}

static void perf_copy_last(PerfFxState *st, int n)
{
    if (n > st->ring_frames) n = st->ring_frames;
    if (n < 1) return;
    int start = st->ring_pos - n;
    while (start < 0) start += st->ring_frames;
    for (int i = 0; i < n; i++) {
        int src = start + i;
        if (src >= st->ring_frames) src -= st->ring_frames;
        st->slice[2 * i] = st->ring[2 * src];
        st->slice[2 * i + 1] = st->ring[2 * src + 1];
    }
}

PerfFxState *perf_fx_create(int rate)
{
    if (rate < 8000) rate = 8000;
    if (rate > 96000) rate = 96000;

    PerfFxState *st = calloc(1, sizeof *st);
    if (!st) return NULL;
    st->rate = rate;
    st->ring_frames = rate * PERF_SECONDS;
    st->ring = calloc((size_t)st->ring_frames * 2, sizeof(float));
    st->slice = calloc((size_t)st->ring_frames * 2, sizeof(float));
    if (!st->ring || !st->slice) {
        perf_fx_destroy(st);
        return NULL;
    }

    st->request = -1;
    st->active = -1;
    st->gate_coeff = 1.0f - expf(-1.0f / (rate * 0.001f));
    st->tape_lp_coeff = 1.0f - expf(-PERF_TWO_PI * PERF_TAPE_LP_HZ / rate);

    /* RBJ band-pass, 0 dB peak gain, fixed centre for the "phone" effect. */
    float w0 = PERF_TWO_PI * PERF_PHONE_HZ / rate;
    float alpha = sinf(w0) / (2.0f * PERF_PHONE_Q);
    float a0 = 1.0f + alpha;
    st->phone_b0 = alpha / a0;
    st->phone_b1 = 0.0f;
    st->phone_b2 = -alpha / a0;
    st->phone_a1 = -2.0f * cosf(w0) / a0;
    st->phone_a2 = (1.0f - alpha) / a0;
    return st;
}

void perf_fx_destroy(PerfFxState *st)
{
    if (!st) return;
    free(st->ring);
    free(st->slice);
    free(st);
}

void perf_fx_request(PerfFxState *st, int effect)
{
    if (!st || effect < 0 || effect >= PUNCH_COUNT) return;
    if (st->active >= 0 || st->request >= 0) return;
    st->request = effect;
}

void perf_fx_release(PerfFxState *st)
{
    if (!st) return;
    if (st->active < 0) {
        st->request = -1;      /* cancel a pending effect before it starts */
        return;
    }
    st->release_req = 1;
}

int perf_fx_active(const PerfFxState *st)
{
    return st ? st->active : -1;
}

int perf_fx_pending(const PerfFxState *st)
{
    return st ? st->request : -1;
}

static void perf_begin(PerfFxState *st, int effect, int bpm_x100)
{
    st->active = effect;
    st->releasing = 0;
    st->fade_pos = 0;
    st->fade_frames = perf_ms_frames(st->rate, PERF_FADE_IN_MS);

    switch (effect) {
    case PUNCH_LOOP_4:
    case PUNCH_LOOP_8:
    case PUNCH_LOOP_16:
    case PUNCH_LOOP_32:
    case PUNCH_REWIND: {
        int denom = effect == PUNCH_LOOP_4 ? 4
                  : effect == PUNCH_LOOP_8 ? 8
                  : effect == PUNCH_LOOP_16 ? 16
                  : effect == PUNCH_LOOP_32 ? 32 : 4;
        st->loop_frames = perf_note_frames(st, bpm_x100, denom);
        perf_copy_last(st, st->loop_frames);
        st->reverse = (effect == PUNCH_REWIND);
        st->loop_pos = st->reverse ? st->loop_frames - 1 : 0;
        break;
    }
    case PUNCH_GATE:
        st->gate_frames = perf_note_frames(st, bpm_x100, 16);
        st->gate_pos = 0;
        st->gate_gain = 1.0f;
        break;
    case PUNCH_TAPESTOP:
        /* Read the live ring at a falling rate: the read pointer starts at the
         * newest sample and falls behind the writer as the "tape" slows. */
        st->tape_pos = (float)st->ring_pos;
        st->tape_progress = 0.0f;
        /* A tape stop lasts one 1/8 note at the tempo captured at trigger. */
        st->tape_stop_frames = perf_note_frames(st, bpm_x100, 8);
        st->tape_lp[0] = st->tape_lp[1] = 0.0f;
        break;
    case PUNCH_PHONE:
        st->phone_z1[0] = st->phone_z2[0] = 0.0f;
        st->phone_z1[1] = st->phone_z2[1] = 0.0f;
        break;
    default:
        break;
    }
}

/* Capture the dry input and optionally replace it with the active effect. */
static void perf_process_segment(PerfFxState *st, float *dst, int frames,
                                 int do_effect)
{
    for (int f = 0; f < frames; f++) {
        int idx = st->ring_pos * 2;
        float dry_l = dst[2 * f];
        float dry_r = dst[2 * f + 1];
        st->ring[idx] = dry_l;
        st->ring[idx + 1] = dry_r;
        if (++st->ring_pos >= st->ring_frames) st->ring_pos = 0;

        if (!do_effect || st->active < 0) continue;

        float wet_l = dry_l;
        float wet_r = dry_r;
        switch (st->active) {
        case PUNCH_LOOP_4:
        case PUNCH_LOOP_8:
        case PUNCH_LOOP_16:
        case PUNCH_LOOP_32:
        case PUNCH_REWIND: {
            int p = st->loop_pos;
            wet_l = st->slice[2 * p];
            wet_r = st->slice[2 * p + 1];
            if (st->reverse) {
                if (--p < 0) p = st->loop_frames - 1;
            } else {
                if (++p >= st->loop_frames) p = 0;
            }
            st->loop_pos = p;
            break;
        }
        case PUNCH_GATE: {
            st->gate_pos++;
            if (st->gate_pos >= st->gate_frames) st->gate_pos = 0;
            float target = (st->gate_pos < st->gate_frames / 2) ? 1.0f : 0.0f;
            st->gate_gain += (target - st->gate_gain) * st->gate_coeff;
            wet_l = dry_l * st->gate_gain;
            wet_r = dry_r * st->gate_gain;
            break;
        }
        case PUNCH_TAPESTOP: {
            float p = st->tape_stop_frames > 0
                    ? st->tape_progress / (float)st->tape_stop_frames : 1.0f;
            if (p > 1.0f) p = 1.0f;
            float rate = 1.0f - p;
            int i0 = (int)st->tape_pos;
            if (i0 >= st->ring_frames) i0 -= st->ring_frames;
            if (i0 < 0) i0 += st->ring_frames;
            int i1 = i0 + 1;
            if (i1 >= st->ring_frames) i1 = 0;
            float frac = st->tape_pos - (float)(int)st->tape_pos;
            float l = st->ring[2 * i0] * (1.0f - frac) +
                      st->ring[2 * i1] * frac;
            float r = st->ring[2 * i0 + 1] * (1.0f - frac) +
                      st->ring[2 * i1 + 1] * frac;
            st->tape_lp[0] += (l - st->tape_lp[0]) * st->tape_lp_coeff;
            st->tape_lp[1] += (r - st->tape_lp[1]) * st->tape_lp_coeff;
            float fade = 1.0f - p;
            wet_l = st->tape_lp[0] * fade;
            wet_r = st->tape_lp[1] * fade;
            st->tape_pos += rate;
            if (st->tape_pos >= st->ring_frames)
                st->tape_pos -= st->ring_frames;
            st->tape_progress += 1.0f;
            break;
        }
        case PUNCH_PHONE: {
            float y = st->phone_b0 * dry_l + st->phone_z1[0];
            st->phone_z1[0] = st->phone_b1 * dry_l - st->phone_a1 * y + st->phone_z2[0];
            st->phone_z2[0] = st->phone_b2 * dry_l - st->phone_a2 * y;
            wet_l = y;

            y = st->phone_b0 * dry_r + st->phone_z1[1];
            st->phone_z1[1] = st->phone_b1 * dry_r - st->phone_a1 * y + st->phone_z2[1];
            st->phone_z2[1] = st->phone_b2 * dry_r - st->phone_a2 * y;
            wet_r = y;
            break;
        }
        default:
            break;
        }

        if (st->releasing) {
            float t = st->fade_frames > 0
                    ? (float)st->fade_pos / (float)st->fade_frames : 1.0f;
            if (t > 1.0f) t = 1.0f;
            dst[2 * f] = wet_l * (1.0f - t) + dry_l * t;
            dst[2 * f + 1] = wet_r * (1.0f - t) + dry_r * t;
            if (++st->fade_pos >= st->fade_frames) {
                st->active = -1;
                st->releasing = 0;
            }
        } else if (st->fade_pos < st->fade_frames) {
            float t = st->fade_frames > 0
                    ? (float)st->fade_pos / (float)st->fade_frames : 1.0f;
            dst[2 * f] = wet_l * t + dry_l * (1.0f - t);
            dst[2 * f + 1] = wet_r * t + dry_r * (1.0f - t);
            st->fade_pos++;
        } else {
            dst[2 * f] = wet_l;
            dst[2 * f + 1] = wet_r;
        }
    }
}

void perf_fx_process(PerfFxState *st, float *dst, int frames, int bpm_x100,
                     int start_at)
{
    if (!st || frames <= 0) return;

    if (st->release_req) {
        st->release_req = 0;
        if (st->active >= 0) {
            st->releasing = 1;
            st->fade_pos = 0;
            st->fade_frames = perf_ms_frames(st->rate, PERF_FADE_OUT_MS);
        } else {
            st->request = -1;
        }
    }

    int start = start_at;
    if (start > frames) start = frames;
    if (start < 0) start = -1;

    if (st->active < 0 && st->request >= 0 && start >= 0) {
        if (start > 0) perf_process_segment(st, dst, start, 0);
        perf_begin(st, st->request, bpm_x100);
        st->request = -1;
        if (frames - start > 0)
            perf_process_segment(st, dst + start * 2, frames - start, 1);
    } else {
        perf_process_segment(st, dst, frames, st->active >= 0);
    }
}

/* ------------------------------------------------------------- self-test */

static int st_fail;
#define ST_CHECK(cond, msg) do { \
    if (cond) printf("PASS: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); st_fail++; } \
} while (0)

static void st_const(float *buf, int frames, float v)
{
    for (int i = 0; i < frames * 2; i++) buf[i] = v;
}

static void st_ramp(float *buf, int frames)
{
    for (int i = 0; i < frames; i++) {
        float v = (float)i / (float)frames;
        buf[2 * i] = v;
        buf[2 * i + 1] = v;
    }
}

int perf_fx_selftest(int rate)
{
    st_fail = 0;
    float small[512 * 2];

    /* Idle must be bit-transparent. */
    PerfFxState *st = perf_fx_create(rate);
    if (!st) { ST_CHECK(0, "perf FX allocation"); return st_fail; }
    st_const(small, 256, 0.25f);
    float copy[512 * 2];
    memcpy(copy, small, sizeof(float) * 2 * 256);
    perf_fx_process(st, small, 256, 12000, -1);
    ST_CHECK(memcmp(copy, small, sizeof(float) * 2 * 256) == 0,
             "punch FX idle is bit-transparent");
    perf_fx_destroy(st);

    /* Request, quantization wait, one-at-a-time and release-before-start. */
    st = perf_fx_create(rate);
    if (!st) return st_fail;
    st_const(small, 256, 0.1f);
    perf_fx_request(st, PUNCH_LOOP_4);
    perf_fx_process(st, small, 256, 12000, -1);
    ST_CHECK(perf_fx_active(st) < 0 && perf_fx_pending(st) == PUNCH_LOOP_4,
             "punch request waits for a step boundary");
    perf_fx_request(st, PUNCH_PHONE);
    ST_CHECK(perf_fx_pending(st) == PUNCH_LOOP_4,
             "second punch request is ignored while one is pending");
    perf_fx_process(st, small, 256, 12000, 0);
    ST_CHECK(perf_fx_active(st) == PUNCH_LOOP_4,
             "punch starts at the supplied step offset");
    perf_fx_release(st);
    perf_fx_process(st, small, 2048, 12000, -1);
    ST_CHECK(perf_fx_active(st) < 0, "release ends the punch effect");
    perf_fx_request(st, PUNCH_GATE);
    perf_fx_release(st);
    perf_fx_process(st, small, 256, 12000, -1);
    ST_CHECK(perf_fx_pending(st) < 0 && perf_fx_active(st) < 0,
             "release cancels a pending punch");
    perf_fx_destroy(st);

    /* 1/4 stutter repeats the captured slice. */
    st = perf_fx_create(rate);
    if (!st) return st_fail;
    int warm = 100000;      /* more than the 2 s tape-stop source */
    float *warmbuf = calloc((size_t)warm * 2, sizeof(float));
    if (!warmbuf) { perf_fx_destroy(st); return st_fail; }
    st_ramp(warmbuf, warm);
    perf_fx_process(st, warmbuf, warm, 12000, -1);
    int loop = (int)(60.0 / 120.0 * rate + 0.5);
    int out_n = loop * 2 + 2048;
    float *out = calloc((size_t)out_n * 2, sizeof(float));
    if (!out) { free(warmbuf); perf_fx_destroy(st); return st_fail; }
    perf_fx_request(st, PUNCH_LOOP_4);
    perf_fx_process(st, out, out_n, 12000, 0);
    int fade = (int)(rate * 0.005f) + 64;
    int i = fade;
    ST_CHECK(fabsf(out[2 * i] - out[2 * (i + loop)]) < 1e-5f,
             "1/4 stutter loops at the 1/4-note length");
    free(out);
    free(warmbuf);
    perf_fx_destroy(st);

    /* Rewind walks the frozen slice backwards. */
    st = perf_fx_create(rate);
    if (!st) return st_fail;
    warmbuf = calloc((size_t)warm * 2, sizeof(float));
    out_n = loop * 2;
    out = calloc((size_t)out_n * 2, sizeof(float));
    if (!warmbuf || !out) {
        free(warmbuf); free(out); perf_fx_destroy(st); return st_fail;
    }
    st_ramp(warmbuf, warm);
    perf_fx_process(st, warmbuf, warm, 12000, -1);
    perf_fx_request(st, PUNCH_REWIND);
    perf_fx_process(st, out, out_n, 12000, 0);
    i = fade;
    ST_CHECK(out[2 * (i + 1)] <= out[2 * i] + 1e-6f,
             "rewind plays the slice backwards");
    free(out);
    free(warmbuf);
    perf_fx_destroy(st);

    /* Gate pulses at 1/16. */
    st = perf_fx_create(rate);
    if (!st) return st_fail;
    int gate = (int)(60.0 / 120.0 * rate * 0.25 + 0.5);
    int gate_n = gate * 2 + 2048;
    out = calloc((size_t)gate_n * 2, sizeof(float));
    if (!out) { perf_fx_destroy(st); return st_fail; }
    st_const(out, gate_n, 0.5f);
    perf_fx_request(st, PUNCH_GATE);
    perf_fx_process(st, out, gate_n, 12000, 0);
    ST_CHECK(out[2 * (gate / 2 - 100)] > 0.3f, "1/16 gate opens");
    ST_CHECK(fabsf(out[2 * (gate / 2 + 500)]) < 0.1f, "1/16 gate closes");
    free(out);
    perf_fx_destroy(st);

    /* Tape stop reaches silence in one 1/8 note at the trigger tempo. */
    st = perf_fx_create(rate);
    if (!st) return st_fail;
    warmbuf = calloc((size_t)warm * 2, sizeof(float));
    out_n = rate + 4096;
    out = calloc((size_t)out_n * 2, sizeof(float));
    if (!warmbuf || !out) {
        free(warmbuf); free(out); perf_fx_destroy(st); return st_fail;
    }
    st_const(warmbuf, warm, 0.5f);
    perf_fx_process(st, warmbuf, warm, 12000, -1);
    st_const(out, out_n, 0.5f);
    perf_fx_request(st, PUNCH_TAPESTOP);
    perf_fx_process(st, out, out_n, 12000, 0);
    int tape_end = (int)(60.0 / 120.0 * rate * 0.5 + 0.5);
    ST_CHECK(out[2 * (fade + 100)] > 0.1f, "tape stop starts audible");
    ST_CHECK(out[2 * (tape_end / 2)] > 0.1f,
             "tape stop is audible halfway through its 1/8 note");
    ST_CHECK(fabsf(out[2 * (tape_end + 500)]) < 0.02f,
             "tape stop reaches silence in one 1/8 note");
    ST_CHECK(fabsf(out[2 * (int)(rate * 0.95)]) < 0.05f,
             "tape stop stays silent after its ramp");
    free(out);
    free(warmbuf);
    perf_fx_destroy(st);

    /* Phone band-pass rejects DC. */
    st = perf_fx_create(rate);
    if (!st) return st_fail;
    out_n = rate / 4;
    out = calloc((size_t)out_n * 2, sizeof(float));
    if (!out) { perf_fx_destroy(st); return st_fail; }
    st_const(out, out_n, 0.5f);
    perf_fx_request(st, PUNCH_PHONE);
    perf_fx_process(st, out, out_n, 12000, 0);
    ST_CHECK(fabsf(out[2 * (out_n - 1)]) < 0.05f,
             "phone filter rejects DC");
    free(out);
    perf_fx_destroy(st);

    /* Release crossfade returns to the dry signal smoothly. */
    st = perf_fx_create(rate);
    if (!st) return st_fail;
    warmbuf = calloc((size_t)warm * 2, sizeof(float));
    int pre_n = 20000;
    out = calloc((size_t)pre_n * 2, sizeof(float));
    float *rel = calloc(2048 * 2, sizeof(float));
    if (!warmbuf || !out || !rel) {
        free(warmbuf); free(out); free(rel); perf_fx_destroy(st); return st_fail;
    }
    st_const(warmbuf, warm, 0.3f);
    perf_fx_process(st, warmbuf, warm, 12000, -1);
    st_const(out, pre_n, 0.1f);
    perf_fx_request(st, PUNCH_LOOP_8);
    perf_fx_process(st, out, pre_n, 12000, 0);
    perf_fx_release(st);
    st_const(rel, 2048, 0.1f);
    perf_fx_process(st, rel, 2048, 12000, -1);
    ST_CHECK(perf_fx_active(st) < 0, "release crossfade completes");
    ST_CHECK(fabsf(rel[2 * 2047] - 0.1f) < 0.01f,
             "release returns to the dry signal");
    float max_jump = 0.0f;
    for (int j = 1; j < 2048; j++) {
        float d = fabsf(rel[2 * j] - rel[2 * (j - 1)]);
        if (d > max_jump) max_jump = d;
    }
    ST_CHECK(max_jump < 0.01f, "release crossfade is click-free");
    free(rel);
    free(out);
    free(warmbuf);
    perf_fx_destroy(st);

    return st_fail;
}
