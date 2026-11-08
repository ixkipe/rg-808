/* Performance punch-in effects: held, quantized, one-at-a-time treatments
 * applied to the full post-FX mix. State is owned by the Engine and driven by
 * the audio callback; the UI only requests/releases an effect. */
#ifndef RG808_PERF_FX_H
#define RG808_PERF_FX_H

enum {
    PUNCH_LOOP_4 = 0,
    PUNCH_LOOP_8,
    PUNCH_LOOP_16,
    PUNCH_LOOP_32,
    PUNCH_REWIND,
    PUNCH_GATE,
    PUNCH_TAPESTOP,
    PUNCH_PHONE,
    PUNCH_COUNT
};

extern const char *const PUNCH_NAMES[PUNCH_COUNT];

typedef struct PerfFxState PerfFxState;

PerfFxState *perf_fx_create(int rate);
void perf_fx_destroy(PerfFxState *st);

/* Thread-safe one-way requests from the UI to the audio thread. */
void perf_fx_request(PerfFxState *st, int effect);
void perf_fx_release(PerfFxState *st);
int perf_fx_active(const PerfFxState *st);
int perf_fx_pending(const PerfFxState *st);

/* Capture the dry post-FX mix and replace it with the active performance
 * effect. `start_at` is the sample offset of the next 16th boundary in this
 * buffer, or -1 when the effect must keep waiting. `dst` is stereo interleaved
 * and is modified in place. */
void perf_fx_process(PerfFxState *st, float *dst, int frames, int bpm_x100,
                     int start_at);

/* Headless synthetic tests for `--test-punch`; prints PASS/FAIL lines and
 * returns the number of failures. */
int perf_fx_selftest(int rate);

#endif
