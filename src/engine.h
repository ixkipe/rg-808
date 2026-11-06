/* RG-808 engine: pattern model, sample-accurate sequencer clock, voice mixer.
 * Runs inside the SDL audio callback (and offline for --bounce). */
#ifndef RG808_ENGINE_H
#define RG808_ENGINE_H

#define N_TRACKS 11
#define N_STEPS  16
#define MAX_VOICES 64
#define MAX_VARIANTS 16

enum { LVL_OFF = 0, LVL_ON = 1, LVL_ACC = 2 };
enum { RCH_OFF = 0, RCH_2 = 1, RCH_3 = 2, RCH_4 = 3, RCH_FLAM = 4, RCH_COUNT = 5 };

typedef struct Voice {
    const float *data;   /* mono sample, owned by sample table */
    int len;
    int pos;             /* frame index into data */
    int delay;           /* frames to wait before starting (sample-accurate triggers) */
    float gain;
    int track;
} Voice;

typedef struct Engine {
    /* UI thread -> audio thread */
    volatile int playing;
    volatile int bpm_x100;                 /* tempo * 100, e.g. 12000 = 120.00 */
    volatile int swing_pct;                /* 0..100: odd 16ths pushed late by pct/2 % of a step */
    volatile unsigned char lvl[N_TRACKS][N_STEPS];
    volatile unsigned char rch[N_TRACKS][N_STEPS];  /* per-step ratchet mode (RCH_*) */
    volatile int kit[N_TRACKS];            /* selected sample variant per track (0-based) */
    volatile int preview_track;            /* 1-based track to fire, 0 = none (consumed by audio) */
    volatile int preview_variant;          /* variant to audition */
    volatile int preview_rch;              /* ratchet mode to audition */
    volatile int preview_lvl;              /* level (gain) to audition */
    volatile int restart;                  /* set by UI on play to reset the step counter */

    /* audio thread -> UI thread */
    volatile int cur_step;                 /* -1 when stopped */

    /* audio-thread private state */
    unsigned long long total_pos;          /* frames mixed so far */
    double next_step;                      /* frame of next step boundary */
    int step;
    Voice voices[MAX_VOICES];
    unsigned long long voice_fire[MAX_VOICES]; /* for stealing */
    int rate;
    float master;
} Engine;

typedef struct TrackInfo {
    const char *id;        /* sample family prefix, e.g. "bd" -> bd01.wav */
    const char *label;     /* short grid label */
    const char *name;      /* long name */
    unsigned int color;    /* 0xRRGGBB */
    float gain;            /* nominal level */
    int dflt;              /* default variant (1-based) */
} TrackInfo;

extern const TrackInfo TRACKS[N_TRACKS];

void engine_init(Engine *e, int rate);
void engine_fire_step(Engine *e, int step);       /* fire all notes at this step */
void engine_preview(Engine *e, int track, int variant, int rch, int lvl); /* audition ASAP (X) */
void engine_restart(Engine *e);                   /* restart clock from step 0 */
void engine_mix(Engine *e, float *dst, int frames); /* advance clock + fill dst (stereo interleaved) */

#endif
