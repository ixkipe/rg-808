/* RG-808 engine: pattern model, sample-accurate sequencer clock, voice mixer.
 * Runs inside the SDL audio callback (and offline for --bounce). */
#ifndef RG808_ENGINE_H
#define RG808_ENGINE_H

#include "machine.h"

#define N_STEPS  16          /* maximum pattern length (and grid width) */
#define MAX_VOICES 64
#define MAX_TRACK_VOICES 8   /* ceiling for per-track voice limits */
#define SONG_MAX_BARS 512

/* Pattern length is 1..N_STEPS; clamp every value read from the UI or disk. */
static inline int engine_length_clamp(int n)
{
    return n < 1 ? 1 : (n > N_STEPS ? N_STEPS : n);
}

enum { LVL_OFF = 0, LVL_ON = 1, LVL_ACC = 2, LVL_GHOST = 3, LVL_WEAK = 4, LVL_COUNT = 5 };
enum { RCH_OFF = 0, RCH_2 = 1, RCH_3 = 2, RCH_4 = 3, RCH_FLAM = 4, RCH_COUNT = 5 };

/* Master effects settings. The chain order is fixed:
 * drive slot -> modulation slot -> delay -> reverb -> master. */
typedef struct FxSettings {
    int on;                    /* master chain: 1 = process, 0 = bypass */

    /* drive slot: bitcrusher, overdrive or compression */
    int drive_on;
    int drive_type;            /* FX_DRIVE_* */
    int crush_bits;            /* 4..16 */
    int crush_down;            /* 1..32 */
    int drive_amount;          /* 0..100 overdrive */
    int drive_mix;             /* 0..100 overdrive */
    int comp_threshold;        /* 0..100 */
    int comp_ratio;            /* 1..20 */
    int comp_makeup;           /* 0..100 */

    /* modulation slot: flanger or phaser */
    int mod_on;
    int mod_type;              /* FX_MOD_* */
    int mod_rate_x100;         /* 0.05..5.00 Hz, stored * 100 */
    int mod_depth;             /* 0..100 */
    int mod_fb;                /* 0..90 */
    int mod_mix;               /* 0..100 */

    /* delay, free-running or tempo-synced */
    int delay_on;
    int delay_sync;            /* 0 = milliseconds, 1 = note division */
    int delay_ms;              /* 20..4000 ms */
    int delay_div;             /* FX_DIV_* */
    int delay_fb;              /* 0..90 */
    int delay_mix;             /* 0..100 */

    int reverb_on;
    int reverb_room;           /* 0..100 */
    int reverb_damp;           /* 0..100 */
    int reverb_mix;            /* 0..100 */
} FxSettings;

/* One immutable pattern, prepared by the UI thread for song playback. */
typedef struct PatternSnapshot {
    int bpm_x100;
    int swing_pct;
    int length;                /* 1..N_STEPS: steps played per bar */
    FxSettings fx;             /* per-pattern effects chain */
    unsigned char lvl[N_TRACKS][N_STEPS];
    unsigned char rch[N_TRACKS][N_STEPS];
    int kit[N_TRACKS];               /* 808 variant index; unused by the SMP */
    char sample_path[N_TRACKS][SAMPLE_PATH_MAX];
    const float *sample[N_TRACKS];   /* resolved by the song sample bank */
    int sample_len[N_TRACKS];
    int slot;                  /* source slot for display (0 = none) */
} PatternSnapshot;

/* Immutable chain of per-bar snapshots. The audio thread only reads it. */
typedef struct SongSet {
    int count;
    PatternSnapshot pat[SONG_MAX_BARS];
} SongSet;

typedef struct Voice {
    const float *data;   /* mono sample, owned by sample table */
    int len;
    int pos;             /* frame index into data */
    int delay;           /* frames to wait before starting (sample-accurate triggers) */
    float gain;
    int track;
} Voice;

struct PerfFxState;

typedef struct Engine {
    /* UI thread -> audio thread */
    volatile int playing;
    volatile int bpm_x100;                 /* tempo * 100, e.g. 12000 = 120.00 */
    volatile int swing_pct;                /* 0..100: odd 16ths pushed late by pct/2 % of a step */
    volatile int length;                   /* 1..N_STEPS: live pattern length */
    volatile unsigned char lvl[N_TRACKS][N_STEPS];
    volatile unsigned char rch[N_TRACKS][N_STEPS];  /* per-step ratchet mode (RCH_*) */
    volatile int kit[N_TRACKS];            /* 808 selected variant (unused by SMP) */
    char sample_path[N_TRACKS][SAMPLE_PATH_MAX];    /* assigned sample, UI thread */
    volatile const float *sample_data[N_TRACKS];    /* decoded sample, audio thread */
    volatile int sample_len[N_TRACKS];
    volatile unsigned char sample_missing[N_TRACKS];
    volatile int preview_track;            /* 1-based track to fire, 0 = none (consumed by audio) */
    volatile const float *preview_data;    /* sample for a browser audition */
    volatile int preview_len;
    volatile int preview_rch;              /* ratchet mode to audition */
    volatile int preview_lvl;              /* level (gain) to audition */
    volatile int restart;                  /* set by UI on play to reset the step counter */
    volatile int kill_all_voices;          /* consumed by audio: cut every voice */

    /* Performance mutes: UI -> audio. Not stored with patterns or song bars;
     * they persist across pattern changes and reset when the app starts.
     * mute_kill_mask asks the audio thread to cut voices that are already
     * ringing when a track is muted. */
    volatile unsigned char mute[N_TRACKS];
    volatile unsigned int mute_kill_mask;

    /* master effects parameters (per-pattern; UI -> audio) */
    volatile FxSettings fx;

    /* song mode: the UI builds an immutable snapshot set, then swaps it in */
    volatile int song_playing;             /* play the snapshot set instead of the live pattern */
    volatile int song_loop;                /* loop at the end of the chain */
    volatile int song_start_bar;           /* first bar after restart (0-based) */
    volatile const SongSet *song_set;      /* prepared snapshot set */

    /* audio thread -> UI thread */
    volatile int cur_step;                 /* -1 when stopped */
    volatile int play_step;                /* grid LED; follows swing delay */
    volatile int song_bar;                 /* current bar (0-based) */
    volatile int song_done;                /* non-looping song reached the end */
    volatile unsigned int mix_count;       /* completed engine_mix calls (buffer-swap ack) */

    /* audio-thread private state */
    unsigned long long total_pos;          /* frames mixed so far */
    double next_step;                      /* frame of next step boundary */
    unsigned long long step_fire_at;       /* frame of the current audible step */
    int step_prev;                         /* step shown before the swung hit */
    int step;
    Voice voices[MAX_VOICES];
    unsigned long long voice_fire[MAX_VOICES]; /* for stealing */
    struct FxState *fx_state;              /* heap-allocated effect buffers/state */
    struct PerfFxState *perf_fx;           /* heap-allocated punch-in FX state */
    int track_voice_limit;                 /* 0 = global stealing; >0 = per-track cap */
    int rate;
    float master;
} Engine;

void engine_init(Engine *e, int rate);
void engine_free(Engine *e);                      /* release effect buffers */
void engine_fire_step(Engine *e, int step);       /* fire all notes at this step */
void engine_preview(Engine *e, int track, int variant, int rch, int lvl); /* audition track sample (X) */
void engine_preview_data(Engine *e, int track, const float *data, int len,
                         int rch, int lvl);       /* audition an arbitrary sample */
void engine_select_kit(Engine *e, int track, int variant); /* 808: set + resolve variant */
void engine_kill_all_voices(Engine *e);           /* cut all voices at the next callback */
void engine_restart(Engine *e);                   /* restart clock from step 0 / start bar */
void engine_mix(Engine *e, float *dst, int frames); /* advance clock + fill dst (stereo interleaved) */
int engine_selftest_swing(int rate);              /* headless playhead/swing check */
int engine_selftest_levels(int rate);             /* headless ghost/weak gain check */

/* Performance punch-in FX (R1 layer). Requests are consumed by the audio
 * thread at the next 16th-step boundary; release is immediate. */
void engine_punch_trigger(Engine *e, int effect);
void engine_punch_release(Engine *e);
int engine_punch_active(const Engine *e);         /* playing effect or -1 */
int engine_punch_pending(const Engine *e);        /* waiting effect or -1 */

/* Song playback: the UI prepares an immutable SongSet, then starts/stops it. */
void engine_song_start(Engine *e, const SongSet *set, int loop, int start_bar);
void engine_song_stop(Engine *e);
void engine_song_set_loop(Engine *e, int loop);

#endif
