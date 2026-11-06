#include "samples.h"
#include "engine.h"
#include "wav.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct { char id[8]; float *data; int len; } tbl[N_TRACKS];

const float *sample_data(const char *id, int *len)
{
    for (int i = 0; i < N_TRACKS; i++) {
        if (!strcmp(tbl[i].id, id)) { *len = tbl[i].len; return tbl[i].data; }
    }
    *len = 0;
    return NULL;
}

int samples_load_all(const char *dir, int rate)
{
    int missing = 0;
    for (int i = 0; i < N_TRACKS; i++) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%s.wav", dir, TRACKS[i].id);
        WavAudio w;
        if (wav_load(path, rate, &w) == 0) {
            strncpy(tbl[i].id, TRACKS[i].id, 7); tbl[i].id[7] = 0;
            tbl[i].data = w.data; tbl[i].len = w.frames;
        } else {
            tbl[i].id[0] = 0; tbl[i].data = NULL; tbl[i].len = 0;
            missing++;
            fprintf(stderr, "rg808: missing sample %s\n", path);
        }
    }
    return missing;
}
