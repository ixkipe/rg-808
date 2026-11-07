#include "engine.h"
#include "fx.h"
#include "perf_fx.h"
#include "samples.h"
#include <math.h>
#include <stdlib.h>
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

/* Master-effect delay lines. The delay is allocated on the heap so an Engine
 * stays small enough to live on the stack (the app also builds temporary
 * engines for WAV export and the self-tests). */
#define FX_DELAY_MAX        (48000 * 8)  /* 8 s at 48 kHz */
#define FX_FLANGER_MAX      2048
#define FX_REVERB_COMBS     4
#define FX_REVERB_APS       2
#define FX_REVERB_MAX       2048
#define FX_PHASER_STAGES    6
#define FX_TWO_PI           6.28318530717958647692

typedef struct FxState {
    /* drive slot */
    float crush_hold[2];
    int crush_count[2];
    float comp_env;

    /* modulation slot (flanger delay line or phaser allpasses) */
    float flanger_buf[2][FX_FLANGER_MAX];
    int flanger_pos;
    float phaser_z[2][FX_PHASER_STAGES];
    float phaser_last[2];
    double mod_phase;

    /* delay */
    float delay_buf[2][FX_DELAY_MAX];
    int delay_pos;

    /* Schroeder-style reverb: 4 parallel damped combs + 2 allpasses */
    float comb_buf[FX_REVERB_COMBS][2][FX_REVERB_MAX];
    float comb_lp[FX_REVERB_COMBS][2];
    float ap_buf[FX_REVERB_APS][2][FX_REVERB_MAX];
    int rev_pos;
} FxState;

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

    FxSettings fx;
    fx_defaults(&fx);
    fx_apply(e, &fx);
    e->fx_state = calloc(1, sizeof(FxState));
    e->perf_fx = perf_fx_create(rate);
}

void engine_free(Engine *e)
{
    if (!e) return;
    free(e->fx_state);
    e->fx_state = NULL;
    perf_fx_destroy(e->perf_fx);
    e->perf_fx = NULL;
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

void engine_punch_trigger(Engine *e, int effect)
{
    if (e) perf_fx_request(e->perf_fx, effect);
}

void engine_punch_release(Engine *e)
{
    if (e) perf_fx_release(e->perf_fx);
}

int engine_punch_active(const Engine *e)
{
    return e ? perf_fx_active(e->perf_fx) : -1;
}

int engine_punch_pending(const Engine *e)
{
    return e ? perf_fx_pending(e->perf_fx) : -1;
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
        const SongSet *set = (const SongSet *)e->song_set;
        int count = set ? set->count : 0;
        int bar = e->song_start_bar;
        if (bar < 0) bar = 0;
        if (bar >= count) bar = count > 0 ? count - 1 : 0;
        e->song_bar = bar;
        e->song_done = 0;
        if (count > 0) fx_apply(e, &set->pat[bar].fx);
    }
}

void engine_fire_step(Engine *e, int step)
{
    double sps = samples_per_step(e);
    for (int t = 0; t < N_TRACKS; t++) {
        unsigned char lv = e->lvl[t][step];
        if (lv > LVL_OFF && !e->mute[t])
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
 * immutable snapshot, so pattern changes are gapless at the bar boundary.
 * Returns the sample offset of the first step boundary when a punch-in is
 * waiting, or -1. */
static int mix_song_steps(Engine *e, int frames, int want_punch)
{
    int punch_at = -1;
    const SongSet *set = (const SongSet *)e->song_set;
    int count = set ? set->count : 0;
    if (count <= 0) {
        e->song_playing = 0;
        e->playing = 0;
        return -1;
    }
    if (e->song_bar < 0 || e->song_bar >= count) e->song_bar = 0;

    double end = (double)(e->total_pos + (unsigned long long)frames);
    while (e->next_step <= end) {
        int at = (int)(e->next_step - (double)e->total_pos);
        if (at < 0) at = 0;
        if (at > frames) at = frames;
        if (want_punch && punch_at < 0) punch_at = at;

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
            fx_apply(e, &sp->fx);
        }

        double sps = samples_per_step_bpm(e->rate, sp->bpm_x100);
        int delay = at + swing_delay_pct(sp->swing_pct, e->step, sps);
        for (int t = 0; t < N_TRACKS; t++) {
            unsigned char lv = sp->lvl[t][e->step];
            if (lv > LVL_OFF && !e->mute[t])
                fire_sched(e, t, sp->kit[t], LVL_GAIN[lv], delay,
                           sp->rch[t][e->step], sps);
        }
        e->next_step += sps;
    }
    return punch_at;
}

/* --------------------------------------------------------------- effects */

static int fx_clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void fx_process(Engine *e, float *dst, int frames, int bpm_x100)
{
    FxState *st = e->fx_state;
    if (!st) return;

    /* 1. drive slot: bitcrusher, overdrive or compression */
    if (e->fx.drive_on) {
        int type = fx_clampi(e->fx.drive_type, 0, FX_DRIVE_COUNT - 1);
        if (type == FX_DRIVE_CRUSH) {
            int bits = fx_clampi(e->fx.crush_bits, 4, 16);
            int down = fx_clampi(e->fx.crush_down, 1, 32);
            float levels = (float)(1 << (bits - 1));
            for (int f = 0; f < frames; f++) {
                for (int ch = 0; ch < 2; ch++) {
                    if (st->crush_count[ch] > 0) {
                        st->crush_count[ch]--;
                    } else {
                        float x = dst[2*f + ch];
                        float q = floorf(x * levels + 0.5f) / levels;
                        if (q > 1.0f) q = 1.0f;
                        if (q < -1.0f) q = -1.0f;
                        st->crush_hold[ch] = q;
                        st->crush_count[ch] = down - 1;
                    }
                    dst[2*f + ch] = st->crush_hold[ch];
                }
            }
        } else if (type == FX_DRIVE_OVERDRIVE) {
            float amount = fx_clampi(e->fx.drive_amount, 0, 100) / 100.0f;
            float gain = 1.0f + 15.0f * amount;
            float mix = fx_clampi(e->fx.drive_mix, 0, 100) / 100.0f;
            for (int i = 0; i < 2 * frames; i++) {
                float dry = dst[i];
                float wet = tanhf(dry * gain);
                dst[i] = dry * (1.0f - mix) + wet * mix;
            }
        } else {
            /* stereo-linked peak compressor with a simple static curve */
            float thr = 0.05f + 0.95f * (fx_clampi(e->fx.comp_threshold, 0, 100) / 100.0f);
            float ratio = (float)fx_clampi(e->fx.comp_ratio, 1, 20);
            float makeup = 1.0f + 3.0f * (fx_clampi(e->fx.comp_makeup, 0, 100) / 100.0f);
            const float attack = 0.02f;
            const float release = 0.0005f;
            for (int f = 0; f < frames; f++) {
                float l = dst[2*f], r = dst[2*f + 1];
                float al = fabsf(l), ar = fabsf(r);
                float peak = al > ar ? al : ar;
                float coeff = (peak > st->comp_env) ? attack : release;
                st->comp_env += (peak - st->comp_env) * coeff;
                float gain = 1.0f;
                if (st->comp_env > thr && st->comp_env > 0.0001f) {
                    float target = thr + (st->comp_env - thr) / ratio;
                    gain = target / st->comp_env;
                }
                gain *= makeup;
                dst[2*f] = l * gain;
                dst[2*f + 1] = r * gain;
            }
        }
    }

    /* 2. modulation slot: flanger or phaser, 90 deg stereo offset */
    if (e->fx.mod_on) {
        int type = fx_clampi(e->fx.mod_type, 0, FX_MOD_COUNT - 1);
        int rate_x100 = fx_clampi(e->fx.mod_rate_x100, 5, 500);
        float depth = fx_clampi(e->fx.mod_depth, 0, 100) / 100.0f;
        float fb = fx_clampi(e->fx.mod_fb, 0, 90) / 100.0f;
        float mix = fx_clampi(e->fx.mod_mix, 0, 100) / 100.0f;
        double inc = (rate_x100 / 100.0) / e->rate;

        if (type == FX_MOD_FLANGER) {
            float base = 0.004f * e->rate;              /* 4 ms */
            float mod = 0.003f * e->rate * depth;       /* up to 3 ms */
            for (int f = 0; f < frames; f++) {
                int pos = st->flanger_pos;
                for (int ch = 0; ch < 2; ch++) {
                    float phase = (float)st->mod_phase + (ch ? 0.25f : 0.0f);
                    if (phase >= 1.0f) phase -= 1.0f;
                    float lfo = sinf((float)FX_TWO_PI * phase);
                    float rpos = (float)pos - (base + mod * (0.5f + 0.5f * lfo));
                    while (rpos < 0.0f) rpos += FX_FLANGER_MAX;
                    int i0 = (int)rpos;
                    float frac = rpos - (float)i0;
                    int i1 = (i0 + 1) & (FX_FLANGER_MAX - 1);
                    float wet = st->flanger_buf[ch][i0] * (1.0f - frac) +
                                st->flanger_buf[ch][i1] * frac;
                    float dry = dst[2*f + ch];
                    float write = dry + wet * fb;
                    if (write > 4.0f) write = 4.0f;
                    if (write < -4.0f) write = -4.0f;
                    st->flanger_buf[ch][pos] = write;
                    dst[2*f + ch] = dry * (1.0f - mix) + wet * mix;
                }
                st->flanger_pos = (pos + 1) & (FX_FLANGER_MAX - 1);
                st->mod_phase += inc;
                if (st->mod_phase >= 1.0) st->mod_phase -= 1.0;
            }
        } else {
            float pfb = fb * 0.65f;              /* keep phaser resonance stable */
            float wet_gain = 1.0f - pfb;          /* compensate feedback boost */
            for (int f = 0; f < frames; f++) {
                for (int ch = 0; ch < 2; ch++) {
                    float phase = (float)st->mod_phase + (ch ? 0.25f : 0.0f);
                    if (phase >= 1.0f) phase -= 1.0f;
                    float lfo = sinf((float)FX_TWO_PI * phase);
                    /* Sweep the allpass coefficient through the low/mid range.
                     * sqrt() makes the middle of the DEPTH control useful.
                     * At full depth the notches travel from ~6.4 kHz down to
                     * under 1 kHz. */
                    float a = 0.10f - 0.80f * sqrtf(depth) * (0.5f + 0.5f * lfo);
                    float dry = dst[2*f + ch];
                    float x = dry + st->phaser_last[ch] * pfb;
                    for (int s = 0; s < FX_PHASER_STAGES; s++) {
                        float y = a * x + st->phaser_z[ch][s];
                        st->phaser_z[ch][s] = x - a * y;
                        x = y;
                    }
                    if (x > 2.0f) x = 2.0f;
                    if (x < -2.0f) x = -2.0f;
                    st->phaser_last[ch] = x;
                    dst[2*f + ch] = dry * (1.0f - mix) + x * wet_gain * mix;
                }
                st->mod_phase += inc;
                if (st->mod_phase >= 1.0) st->mod_phase -= 1.0;
            }
        }
    }

    /* 3. delay: stereo circular buffer, free-running or tempo-synced */
    if (e->fx.delay_on) {
        int ms = fx_delay_ms_for(e->fx.delay_sync, e->fx.delay_ms,
                                 e->fx.delay_div, bpm_x100);
        float fb = fx_clampi(e->fx.delay_fb, 0, 90) / 100.0f;
        float mix = fx_clampi(e->fx.delay_mix, 0, 100) / 100.0f;
        int ds = (int)((long long)ms * e->rate / 1000);
        if (ds < 1) ds = 1;
        if (ds > FX_DELAY_MAX - 2) ds = FX_DELAY_MAX - 2;
        for (int f = 0; f < frames; f++) {
            int pos = st->delay_pos;
            int rp = pos - ds;
            if (rp < 0) rp += FX_DELAY_MAX;
            for (int ch = 0; ch < 2; ch++) {
                float dry = dst[2*f + ch];
                float wet = st->delay_buf[ch][rp];
                float write = dry + wet * fb;
                if (write > 4.0f) write = 4.0f;
                if (write < -4.0f) write = -4.0f;
                st->delay_buf[ch][pos] = write;
                dst[2*f + ch] = dry * (1.0f - mix) + wet * mix;
            }
            st->delay_pos = (pos + 1) % FX_DELAY_MAX;
        }
    }

    /* 4. reverb: 4 damped parallel combs into 2 series allpasses */
    if (e->fx.reverb_on) {
        static const int comb_base[FX_REVERB_COMBS] = { 1557, 1617, 1491, 1422 };
        static const int ap_base[FX_REVERB_APS] = { 225, 556 };
        int room = fx_clampi(e->fx.reverb_room, 0, 100);
        int damp_pct = fx_clampi(e->fx.reverb_damp, 0, 100);
        float mix = fx_clampi(e->fx.reverb_mix, 0, 100) / 100.0f;
        float scale = 0.45f + 0.55f * (room / 100.0f);
        float fb = 0.72f + 0.20f * (room / 100.0f);
        float damp = 0.15f + 0.75f * (damp_pct / 100.0f);
        int len_l[FX_REVERB_COMBS], len_r[FX_REVERB_COMBS];
        int ap_len[FX_REVERB_APS];
        for (int i = 0; i < FX_REVERB_COMBS; i++) {
            len_l[i] = (int)(comb_base[i] * scale);
            if (len_l[i] < 64) len_l[i] = 64;
            if (len_l[i] > FX_REVERB_MAX - 32) len_l[i] = FX_REVERB_MAX - 32;
            len_r[i] = len_l[i] + 23;
            if (len_r[i] > FX_REVERB_MAX - 1) len_r[i] = FX_REVERB_MAX - 1;
        }
        for (int j = 0; j < FX_REVERB_APS; j++) {
            ap_len[j] = ap_base[j];
            if (ap_len[j] > FX_REVERB_MAX - 1) ap_len[j] = FX_REVERB_MAX - 1;
        }

        for (int f = 0; f < frames; f++) {
            int pos = st->rev_pos;
            float dry_l = dst[2*f], dry_r = dst[2*f + 1];
            float wet_l = 0.0f, wet_r = 0.0f;

            for (int i = 0; i < FX_REVERB_COMBS; i++) {
                int rl = (pos - len_l[i] + FX_REVERB_MAX) % FX_REVERB_MAX;
                int rr = (pos - len_r[i] + FX_REVERB_MAX) % FX_REVERB_MAX;
                float dl = st->comb_buf[i][0][rl];
                float dr = st->comb_buf[i][1][rr];
                st->comb_lp[i][0] = dl * (1.0f - damp) + st->comb_lp[i][0] * damp;
                st->comb_lp[i][1] = dr * (1.0f - damp) + st->comb_lp[i][1] * damp;
                st->comb_buf[i][0][pos] = dry_l * 0.25f + st->comb_lp[i][0] * fb;
                st->comb_buf[i][1][pos] = dry_r * 0.25f + st->comb_lp[i][1] * fb;
                wet_l += dl;
                wet_r += dr;
            }
            wet_l *= 0.25f;
            wet_r *= 0.25f;

            for (int j = 0; j < FX_REVERB_APS; j++) {
                int rl = (pos - ap_len[j] + FX_REVERB_MAX) % FX_REVERB_MAX;
                float bl = st->ap_buf[j][0][rl];
                float br = st->ap_buf[j][1][rl];
                float yl = -0.5f * wet_l + bl;
                float yr = -0.5f * wet_r + br;
                st->ap_buf[j][0][pos] = wet_l + 0.5f * bl;
                st->ap_buf[j][1][pos] = wet_r + 0.5f * br;
                wet_l = yl;
                wet_r = yr;
            }

            dst[2*f] = dry_l * (1.0f - mix) + wet_l * mix;
            dst[2*f + 1] = dry_r * (1.0f - mix) + wet_r * mix;
            st->rev_pos = (pos + 1) & (FX_REVERB_MAX - 1);
        }
    }
}

void engine_mix(Engine *e, float *dst, int frames)
{
    for (int f = 0; f < frames; f++) dst[2*f] = dst[2*f+1] = 0.0f;

    /* Muting a track should cut what is already ringing. The UI only sets
     * the mask; voice writes stay on the audio thread. */
    unsigned int kill = e->mute_kill_mask;
    if (kill) {
        e->mute_kill_mask = 0;
        for (int i = 0; i < MAX_VOICES; i++) {
            Voice *v = &e->voices[i];
            if (v->data && (kill & (1u << v->track))) {
                v->data = NULL;
                v->delay = 0;
            }
        }
    }

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
    int want_punch = e->perf_fx && perf_fx_pending(e->perf_fx) >= 0;
    int punch_at = -1;

    if (playing) {
        if (song) {
            punch_at = mix_song_steps(e, frames, want_punch);
            e->cur_step = e->playing ? (e->step < 0 ? 0 : e->step) : -1;
        } else {
            double sps = samples_per_step(e);
            int len = engine_length_clamp(e->length);
            /* fire any step boundaries falling inside this buffer */
            while (e->next_step <= (double)(e->total_pos + (unsigned long long)frames)) {
                int at = (int)(e->next_step - (double)e->total_pos);
                if (at < 0) at = 0;
                if (at > frames) at = frames;
                if (want_punch && punch_at < 0) punch_at = at;
                e->step++;
                if (e->step >= len) e->step = 0;
                at += swing_delay_pct(e->swing_pct, e->step, sps);
                for (int t = 0; t < N_TRACKS; t++) {
                    unsigned char lv = e->lvl[t][e->step];
                    if (lv > LVL_OFF && !e->mute[t])
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
        if (want_punch) punch_at = 0;
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

    /* master effects, before master gain + soft clip. The delay's tempo sync
     * follows the live tempo or the current song bar's tempo. */
    int fx_bpm = e->bpm_x100;
    const SongSet *fx_set = (const SongSet *)e->song_set;
    if (song && fx_set && e->song_bar >= 0 && e->song_bar < fx_set->count)
        fx_bpm = fx_set->pat[e->song_bar].bpm_x100;
    if (e->fx.on)
        fx_process(e, dst, frames, fx_bpm);

    /* performance punch-in FX sit after the master chain so stutters and
     * rewinds capture its tails; they are bypassed by the offline renderers
     * because those engines never request an effect. */
    if (e->perf_fx)
        perf_fx_process(e->perf_fx, dst, frames, fx_bpm, punch_at);

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
