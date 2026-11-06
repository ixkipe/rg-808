#include "engine.h"
#include "samples.h"
#include <string.h>

/* Curated 808 kit families, mapped from the sample pack. Colors evoke the TR-808 panel. */
const TrackInfo TRACKS[N_TRACKS] = {
    { "bd", "BD", "BASS DRUM",  0xD94F3B, 1.00f,  7 },
    { "sd", "SD", "SNARE",      0xE08A2E, 0.85f,  1 },
    { "lt", "LT", "LOW TOM",    0xD8B62C, 0.80f,  1 },
    { "mt", "MT", "MID TOM",    0x9CC832, 0.75f,  4 },
    { "ht", "HT", "HIGH TOM",   0x3FA93C, 0.70f,  6 },
    { "ch", "CH", "CLOSED HAT", 0x2FA3A0, 0.50f,  1 },
    { "oh", "OH", "OPEN HAT",   0x3579C8, 0.50f,  1 },
    { "cl", "CL", "CLAVE",      0x7A4FC0, 0.65f,  1 },
    { "cp", "CP", "HAND CLAP",  0xC24F8A, 0.60f,  1 },
    { "cb", "CB", "COWBELL",    0xB0A03C, 0.45f,  1 },
    { "cy", "CY", "CYMBAL",     0x8A8F98, 0.40f,  6 },
};

static const float LVL_GAIN[3] = { 0.0f, 0.72f, 1.0f };

static double samples_per_step(const Engine *e)
{
    int bx = e->bpm_x100;
    if (bx < 100) bx = 100;               /* 1.00 bpm floor */
    double bpm = bx / 100.0;
    return (double)e->rate * 60.0 / (bpm * 4.0);   /* 16th note */
}

void engine_init(Engine *e, int rate)
{
    memset(e, 0, sizeof *e);
    e->rate = rate;
    e->master = 0.72f;
    e->cur_step = -1;
    e->bpm_x100 = 12000;
    e->next_step = 0;
    for (int t = 0; t < N_TRACKS; t++) e->kit[t] = TRACKS[t].dflt - 1;
}

/* Steal the oldest voice, preferring the same track. */
static Voice *alloc_voice(Engine *e, int track)
{
    Voice *best = NULL;
    unsigned long long oldest = ~0ull;
    for (int i = 0; i < MAX_VOICES; i++) {
        Voice *v = &e->voices[i];
        if (!v->data) { best = v; break; }
        if (v->track == track && e->voice_fire[i] < oldest) { oldest = e->voice_fire[i]; best = v; }
    }
    if (!best) {                            /* global oldest */
        oldest = ~0ull;
        for (int i = 0; i < MAX_VOICES; i++)
            if (e->voice_fire[i] < oldest) { oldest = e->voice_fire[i]; best = &e->voices[i]; }
    }
    return best;
}

static void fire(Engine *e, int track, int variant, float level_gain, int delay)
{
    int len;
    const float *data = sample_get(track, variant, &len);
    if (!data) return;
    Voice *v = alloc_voice(e, track);
    v->data = data; v->len = len; v->pos = 0;
    v->delay = delay;
    v->gain = level_gain * TRACKS[track].gain;
    v->track = track;
    e->voice_fire[v - e->voices] = e->total_pos;
}

/* Play one track now with a ratchet pattern (preview); consumed by audio callback. */
void engine_preview(Engine *e, int track, int variant, int rch, int lvl)
{
    if (track >= 0 && track < N_TRACKS) {
        e->preview_variant = variant;
        e->preview_rch = rch;
        e->preview_lvl = lvl;
        e->preview_track = track + 1;
    }
}

/* Schedule hit(s) for one voice: straight, ratchet roll (2/3/4 even subs) or
 * flam (quiet grace at the boundary, main ~14 ms later). Delays may exceed the
 * current buffer — voices carry their delay across callbacks. */
static void fire_sched(Engine *e, int track, int variant, float gain, int delay, int rch)
{
    double sps = samples_per_step(e);
    switch (rch) {
    case RCH_2:
        for (int k = 0; k < 2; k++) fire(e, track, variant, gain, delay + (int)(k * sps / 2.0));
        return;
    case RCH_3:
        for (int k = 0; k < 3; k++) fire(e, track, variant, gain, delay + (int)(k * sps / 3.0));
        return;
    case RCH_4:
        for (int k = 0; k < 4; k++) fire(e, track, variant, gain, delay + (int)(k * sps / 4.0));
        return;
    case RCH_FLAM:
        fire(e, track, variant, gain * 0.5f, delay);
        fire(e, track, variant, gain, delay + (int)(0.014 * e->rate));
        return;
    default:
        fire(e, track, variant, gain, delay);
        return;
    }
}

/* Swing: odd 16ths land late by swing_pct/100 * half a step (100 % = full shuffle). */
static int swing_delay(const Engine *e, int step, double sps)
{
    int pct = e->swing_pct;
    if (pct <= 0 || (step & 1) == 0) return 0;
    if (pct > 100) pct = 100;
    return (int)(sps * 0.5 * (pct / 100.0));
}

void engine_restart(Engine *e)
{
    e->step = -1;
    e->next_step = (double)e->total_pos;
}

void engine_fire_step(Engine *e, int step)
{
    for (int t = 0; t < N_TRACKS; t++) {
        unsigned char lv = e->lvl[t][step];
        if (lv > LVL_OFF)
            fire_sched(e, t, e->kit[t], LVL_GAIN[lv], 0, e->rch[t][step]);
    }
}

void engine_mix(Engine *e, float *dst, int frames)
{
    for (int f = 0; f < frames; f++) dst[2*f] = dst[2*f+1] = 0.0f;

    if (e->preview_track) {
        int t = e->preview_track - 1;
        int v = e->preview_variant;
        int r = e->preview_rch;
        int l = e->preview_lvl;
        e->preview_track = 0;
        if (t >= 0 && t < N_TRACKS)
            fire_sched(e, t, v, LVL_GAIN[l == LVL_OFF ? LVL_ACC : l], 0, r);
    }

    if (e->playing) {
        double sps = samples_per_step(e);
        /* fire any step boundaries falling inside this buffer */
        while (e->next_step <= (double)(e->total_pos + (unsigned long long)frames)) {
            int at = (int)(e->next_step - (double)e->total_pos);
            if (at < 0) at = 0;
            e->step = (e->step + 1) % N_STEPS;
            at += swing_delay(e, e->step, sps);
            for (int t = 0; t < N_TRACKS; t++) {
                unsigned char lv = e->lvl[t][e->step];
                if (lv > LVL_OFF) fire_sched(e, t, e->kit[t], LVL_GAIN[lv], at, e->rch[t][e->step]);
            }
            e->next_step += sps;
        }
        e->cur_step = e->step < 0 ? 0 : e->step;
    } else {
        e->cur_step = -1;
        e->next_step = (double)e->total_pos + samples_per_step(e);
    }

    /* render active voices into the buffer (mono sum duplicated to stereo) */
    for (int i = 0; i < MAX_VOICES; i++) {
        Voice *v = &e->voices[i];
        if (!v->data) continue;
        int f = 0;
        if (v->delay > 0) {
            int skip = v->delay < frames ? v->delay : frames;
            v->delay -= skip;
            f = skip;
        }
        for (; f < frames; f++) {
            if (v->pos >= v->len) break;
            float s = v->data[v->pos++] * v->gain;
            dst[2*f] += s;
            dst[2*f+1] += s;
        }
        if (v->pos >= v->len && v->delay <= 0) v->data = NULL;
    }

    /* master gain + cheap soft clip */
    float m = e->master;
    for (int f = 0; f < 2*frames; f++) {
        float x = dst[f] * m;
        if (x > 0.98f)  x = 0.98f + (x - 0.98f) * 0.25f;
        if (x > 1.0f)   x = 1.0f;
        if (x < -0.98f) x = -0.98f + (x + 0.98f) * 0.25f;
        if (x < -1.0f)  x = -1.0f;
        dst[f] = x;
    }

    e->total_pos += (unsigned long long)frames;
}
