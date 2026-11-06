/* Minimal WAV reader: RIFF walk, PCM 8/16/24/32 + IEEE float 32/64,
 * any channel count -> downmixed mono float, any rate -> resampled to target. */
#ifndef RG808_WAV_H
#define RG808_WAV_H

typedef struct {
    float *data;    /* malloc'd mono float, -1..1 */
    int frames;
    int rate;
} WavAudio;

/* Returns 0 on success. */
int wav_load(const char *path, int target_rate, WavAudio *out);

/* Writes a PCM16 stereo WAV. Returns 0 on success. */
int wav_write_stereo16(const char *path, const float *interleaved, int frames, int rate);

#endif
