#include "engine.h"
#include <math.h>
#include <string.h>

/* Curated 808 kit, mapped from the sample pack. Colors evoke the TR-808 panel. */
const TrackInfo TRACKS[N_TRACKS] = {
    { "bd", "BD", "BASS DRUM",  0xD94F3B, 1.00f },
    { "sd", "SD", "SNARE",      0xE08A2E, 0.85f },
    { "lt", "LT", "LOW TOM",    0xD8B62C, 0.80f },
    { "mt", "MT", "MID TOM",    0x9CC832, 0.75f },
    { "ht", "HT", "HIGH TOM",   0x3FA93C, 0.70f },
    { "ch", "CH", "CLOSED HAT", 0x2FA3A0, 0.50f },
    { "oh", "OH", "OPEN HAT",   0x3579C8, 0.50f },
    { "cl", "CL", "CLAVE",      0x7A4FC0, 0.65f },
    { "cp", "CP", "HAND CLAP",  0xC24F8A, 0.60f },
    { "cb", "CB", "COWBELL",    0xB0A03C, 0.45f },
    { "cy", "CY", "CYMBAL",     0x8A8F98, 0.40f },
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

/* Externally provided by samples.c */
extern const float *sample_data(const char *id, int *len);

static void fire_one(Engine *e, int track, float level_gain, int delay)
{
    int len;
    const float *data = sample_data(TRACKS[track].id, &len);
    if (!data) return;
    Voice *v = alloc_voice(e, track);
    v->data = data; v->len = len; v->pos = 0;
    v->delay = delay;
    v->gain = level_gain * TRACKS[track].gain;
    v->track = track;
    e->voice_fire[v - e->voices] = e->total_pos;
}

/* Play a single track now (preview); consumed by audio callback. */
void engine_preview(Engine *e, int track)
{
    if (track >= 0 && track < N_TRACKS) e->preview_track = track + 1;
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
            fire_one(e, t, LVL_GAIN[lv], 0);
    }
}

void engine_mix(Engine *e, float *dst, int frames)
{
    for (int f = 0; f < frames; f++) dst[2*f] = dst[2*f+1] = 0.0f;

    if (e->preview_track) {
        int t = e->preview_track - 1;
        e->preview_track = 0;
        if (t >= 0 && t < N_TRACKS) fire_one(e, t, LVL_GAIN[LVL_ACC], 0);
    }

    if (e->playing) {
        double sps = samples_per_step(e);
        /* fire any step boundaries falling inside this buffer */
        while (e->next_step <= (double)(e->total_pos + (unsigned long long)frames)) {
            int at = (int)(e->next_step - (double)e->total_pos);
            if (at < 0) at = 0;
            e->step = (e->step + 1) % N_STEPS;
            for (int t = 0; t < N_TRACKS; t++) {
                unsigned char lv = e->lvl[t][e->step];
                if (lv > LVL_OFF) fire_one(e, t, LVL_GAIN[lv], at);
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
