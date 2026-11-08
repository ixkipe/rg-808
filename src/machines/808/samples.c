#include "samples.h"
#include "wav.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    float *data[MAX_VARIANTS];
    int len[MAX_VARIANTS];
    int count;
} Kit;

static Kit kits[N_TRACKS];

int variant_count(int track)
{
    if (track < 0 || track >= N_TRACKS) return 0;
    return kits[track].count;
}

const float *sample_get(int track, int variant, int *len)
{
    *len = 0;
    if (track < 0 || track >= N_TRACKS) return NULL;
    Kit *k = &kits[track];
    if (k->count == 0) return NULL;
    if (variant < 0) variant = 0;
    if (variant >= k->count) variant = k->count - 1;
    *len = k->len[variant];
    return k->data[variant];
}

int samples_load_all(const char *dir, int rate)
{
    int empty_tracks = 0;
    for (int t = 0; t < N_TRACKS; t++) {
        Kit *k = &kits[t];
        k->count = 0;
        for (int v = 0; v < MAX_VARIANTS; v++) {
            char path[1024];
            snprintf(path, sizeof path, "%s/%s%02d.wav", dir, TRACKS[t].id, v + 1);
            WavAudio w;
            if (wav_load(path, rate, &w) != 0) break;   /* family ends here */
            k->data[v] = w.data;
            k->len[v] = w.frames;
            k->count++;
        }
        if (k->count == 0) {
            empty_tracks++;
            fprintf(stderr, "rg808: no samples for %s in %s\n", TRACKS[t].id, dir);
        }
    }
    return empty_tracks;
}
