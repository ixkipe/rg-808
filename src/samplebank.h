/* RG-SMP sample bank and directory browser helpers.
 *
 * Samples are decoded to mono float at the engine rate and cached by their
 * path relative to the assets/wav root. Banks are plain structs owned by the
 * UI thread; the audio thread only receives data pointers that stay valid
 * until the bank is cleared while playback is stopped. */
#ifndef RG808_SAMPLEBANK_H
#define RG808_SAMPLEBANK_H

#include <stddef.h>

#define SAMPLE_PATH_MAX   256
#define SAMPLE_BANK_MAX   128
#define SAMPLE_MAX_SECONDS 10
#define SAMPLE_LIST_MAX   256

typedef struct {
    char path[SAMPLE_PATH_MAX];
    float *data;
    int frames;
} SampleEntry;

typedef struct {
    int rate;
    int count;
    size_t bytes;
    size_t max_bytes;
    int max_entries;
    int overflow;              /* a sample was rejected by the count/byte/length cap */
    SampleEntry entry[SAMPLE_BANK_MAX];
} SampleBank;

typedef struct {
    char name[128];
    int is_dir;
} SampleDirEntry;

void samplebank_init(SampleBank *b, int rate, int max_entries, size_t max_bytes);
void samplebank_clear(SampleBank *b);
int samplebank_find(const SampleBank *b, const char *path);
int samplebank_load(SampleBank *b, const char *root, const char *path);
const float *samplebank_data(const SampleBank *b, int idx, int *len);

/* Relative path helpers; all use '/' as the separator. */
int sampler_path_join(const char *dir, const char *name, char *out, size_t cap);
int sampler_parent_dir(const char *dir, char *out, size_t cap);
int sampler_wav_name(const char *name);

/* List one directory (non-recursive). ".." is returned first when dir is not
 * empty. Returns the number of entries or -1 on error. */
int sampler_list_dir(const char *root, const char *dir,
                     SampleDirEntry *out, int max);

/* Collect .wav paths recursively (used for the starter 909 kit only). */
int sampler_collect_wavs(const char *root, const char *dir,
                         char paths[][SAMPLE_PATH_MAX], int max, int *count);

#endif
