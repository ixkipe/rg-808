/* Loads the kit families: <id>NN.wav (NN=01..MAX_VARIANTS) into float mono
 * buffers at the active sample rate. First missing number ends the family. */
#ifndef RG808_SAMPLES_H
#define RG808_SAMPLES_H

#include "engine.h"

#define MAX_VARIANTS 16

int samples_load_all(const char *dir, int rate);        /* returns #tracks w/o samples */
int variant_count(int track);                            /* 0..MAX_VARIANTS */
const float *sample_get(int track, int variant, int *len); /* clamps variant; NULL if none */

#endif
