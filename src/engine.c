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

static double samples_per_step_bpm(int rate, int bpm_x100)
{
    if (bpm_x100 < 100) bpm_x100 = 100;   /* 1.00 bpm floor */
    double bpm = bpm_x100 / 100.0;
    return (double)rate * 60.0 / (bpm * 4.0);   /* 16th note */
}

static double samples_per_step(const Engine *e)
{
    return samples_per_step_bpm(e->rate, e->bpm_x100);
}

void engine_init(Engine *e, int rate)
{
    memset(e, 0, sizeof *e);
    e->rate = rate;
    e->master = 0.72f;
    e->cur_step = -1;
    e->bpm_x100 = 12000;
    e->length = N_STEPS;
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
static void fire_sched(Engine *e, int track, int variant, float gain, int delay, int rch, double sps)
{
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
static int swing_delay_pct(int pct, int step, double sps)
{
    if (pct <= 0 || (step & 1) == 0) return 0;
    if (pct > 100) pct = 100;
    return (int)(sps * 0.5 * (pct / 100.0));
}

void engine_restart(Engine *e)
{
    e->step = -1;
    e->next_step = (double)e->total_pos;
    if (e->song_playing) {
        int count = e->song_set ? e->song_set->count : 0;
        int bar = e->song_start_bar;
        if (bar < 0) bar = 0;
        if (bar >= count) bar = count > 0 ? count - 1 : 0;
        e->song_bar = bar;
        e->song_done = 0;
    }
}

void engine_fire_step(Engine *e, int step)
{
    double sps = samples_per_step(e);
    for (int t = 0; t < N_TRACKS; t++) {
        unsigned char lv = e->lvl[t][step];
        if (lv > LVL_OFF)
            fire_sched(e, t, e->kit[t], LVL_GAIN[lv], 0, e->rch[t][step], sps);
    }
}

void engine_song_start(Engine *e, const SongSet *set, int loop, int start_bar)
{
    if (!set || set->count <= 0) return;
    e->song_set = set;
    e->song_loop = loop ? 1 : 0;
    e->song_start_bar = start_bar;
    e->song_playing = 1;
    e->song_done = 0;
    e->playing = 1;
    e->restart = 1;
}

void engine_song_stop(Engine *e)
{
    e->song_playing = 0;
    e->song_done = 0;
}

void engine_song_set_loop(Engine *e, int loop)
{
    e->song_loop = loop ? 1 : 0;
}

/* Advance the song clock through one buffer. Per-bar BPM/swing come from the
 * immutable snapshot, so pattern changes are gapless at the bar boundary. */
static void mix_song_steps(Engine *e, int frames)
{
    const SongSet *set = (const SongSet *)e->song_set;
    int count = set ? set->count : 0;
    if (count <= 0) {
        e->song_playing = 0;
        e->playing = 0;
        return;
    }
    if (e->song_bar < 0 || e->song_bar >= count) e->song_bar = 0;

    double end = (double)(e->total_pos + (unsigned long long)frames);
    while (e->next_step <= end) {
        int at = (int)(e->next_step - (double)e->total_pos);
        if (at < 0) at = 0;

        /* Advance within the current bar. The bar length comes from that
         * bar's snapshot, so patterns of different lengths chain gaplessly. */
        const PatternSnapshot *sp = &set->pat[e->song_bar];
        int len = engine_length_clamp(sp->length);
        e->step++;
        if (e->step >= len) {
            e->step = 0;
            e->song_bar++;
            if (e->song_bar >= count) {
                if (e->song_loop) {
                    e->song_bar = 0;
                } else {
                    e->song_bar = count - 1;
                    e->song_playing = 0;
                    e->playing = 0;
                    e->song_done = 1;
                    e->cur_step = -1;
                    break;
                }
            }
            sp = &set->pat[e->song_bar];
        }

        double sps = samples_per_step_bpm(e->rate, sp->bpm_x100);
        int delay = at + swing_delay_pct(sp->swing_pct, e->step, sps);
        for (int t = 0; t < N_TRACKS; t++) {
            unsigned char lv = sp->lvl[t][e->step];
            if (lv > LVL_OFF)
                fire_sched(e, t, sp->kit[t], LVL_GAIN[lv], delay,
                           sp->rch[t][e->step], sps);
        }
        e->next_step += sps;
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
            fire_sched(e, t, v, LVL_GAIN[l == LVL_OFF ? LVL_ACC : l], 0, r, samples_per_step(e));
    }

    /* Cache the mode at buffer start: the UI may stop/start between callbacks,
     * but this buffer should stay on one path. */
    int playing = e->playing;
    int song = e->song_playing;

    if (playing) {
        if (song) {
            mix_song_steps(e, frames);
            e->cur_step = e->playing ? (e->step < 0 ? 0 : e->step) : -1;
        } else {
            double sps = samples_per_step(e);
            int len = engine_length_clamp(e->length);
            /* fire any step boundaries falling inside this buffer */
            while (e->next_step <= (double)(e->total_pos + (unsigned long long)frames)) {
                int at = (int)(e->next_step - (double)e->total_pos);
                if (at < 0) at = 0;
                e->step++;
                if (e->step >= len) e->step = 0;
                at += swing_delay_pct(e->swing_pct, e->step, sps);
                for (int t = 0; t < N_TRACKS; t++) {
                    unsigned char lv = e->lvl[t][e->step];
                    if (lv > LVL_OFF)
                        fire_sched(e, t, e->kit[t], LVL_GAIN[lv], at,
                                   e->rch[t][e->step], sps);
                }
                e->next_step += sps;
            }
            e->cur_step = e->step < 0 ? 0 : e->step;
        }
    } else {
        e->cur_step = -1;
        e->next_step = (double)e->total_pos + samples_per_step(e);
        if (e->song_playing) e->song_playing = 0;
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
    e->mix_count++;
}
