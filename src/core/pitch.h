/* Pitch shifting with the duration preserved, plus a small cache of the
 * rendered buffers.
 *
 * The engine plays raw sample pointers, so a pitched track simply points at a
 * pre-rendered buffer: the shift is done once, on the UI thread, when the
 * value changes - never in the audio callback. A phase vocoder (with the
 * identity phase locking that keeps transients intact) stretches the sample,
 * then a cubic resampler restores the original length. */
#ifndef RG808_PITCH_H
#define RG808_PITCH_H

#include <stddef.h>

#define PITCH_MIN     (-24)   /* semitones */
#define PITCH_MAX     24
#define PITCH_KEY_MAX 300

/* Clamp a semitone value into the supported range. */
static inline int pitch_clamp(int semitones)
{
    return semitones < PITCH_MIN ? PITCH_MIN
         : (semitones > PITCH_MAX ? PITCH_MAX : semitones);
}

/* Render `in` (mono, `len` frames) shifted by `semitones`, keeping the frame
 * count. Returns 0 on success and fills a malloc'd buffer plus its length
 * (always `len`). The caller owns *out. */
int pitch_shift(const float *in, int len, int rate, int semitones,
                float **out, int *out_len);

/* Cache of rendered pitches, keyed by a caller string plus the interval.
 * Entries are never evicted: the audio thread may still be reading them, so
 * the cache is cleared only where the owner knows playback has stopped (a
 * sample-bank reload). When the budget is exhausted the lookup returns NULL
 * and the caller falls back to the unpitched sample. */
typedef struct PitchEntry {
    char key[PITCH_KEY_MAX];
    int semitones;
    float *data;
    int len;
} PitchEntry;

typedef struct PitchCache {
    PitchEntry *entry;
    int count;
    int cap;
    int rate;
    size_t bytes;
    size_t max_bytes;
    int overflow;                 /* budget was hit at least once */
} PitchCache;

void pitch_cache_init(PitchCache *c, int rate, int max_entries, size_t max_bytes);
void pitch_cache_free(PitchCache *c);
void pitch_cache_clear(PitchCache *c);

/* Returns the rendered buffer for (key, semitones), rendering it from
 * `src`/`len` on first use. Returns NULL when the interval is 0 (use the
 * original sample), when the source is empty, or when the budget is hit. */
const float *pitch_cache_get(PitchCache *c, const char *key, const float *src,
                             int len, int semitones, int *out_len);

/* Headless DSP check (used by --test-pitch). */
int pitch_selftest(int rate);

#endif
