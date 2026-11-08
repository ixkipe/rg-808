/* RG-SMP-specific helpers shared by the sampler app shell. */
#ifndef RG808_SMP_MACHINE_H
#define RG808_SMP_MACHINE_H

#include "engine.h"
#include "samplebank.h"

/* Resolve every sample path in a song set against one bank. Returns the number
 * of missing samples, or -2 when the set needs more slots than the bank. */
int song_load_samples(SongSet *set, SampleBank *bank, const char *root);

#endif
