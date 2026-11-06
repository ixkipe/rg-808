/* Loads the curated kit into float mono buffers at the active sample rate. */
#ifndef RG808_SAMPLES_H
#define RG808_SAMPLES_H

int samples_load_all(const char *dir, int rate);   /* returns missing count */
const float *sample_data(const char *id, int *len);

#endif
