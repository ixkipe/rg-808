/* Machine definition seam shared by the RG-808 and RG-SMP builds.
 *
 * The core owns the sequencer, patterns, songs, FX and punch-in FX.  A machine
 * provides only the things that differ between the drum machine and the
 * sampler: its track table, sample loading/resolution, and the sample lines
 * written to pattern files.
 */
#ifndef RG808_MACHINE_H
#define RG808_MACHINE_H

#include <stdio.h>

#define N_TRACKS 11
#define SAMPLE_PATH_MAX 256

typedef struct TrackInfo {
    const char *id;        /* sample family prefix for the 808, or slot id */
    const char *label;     /* short grid label / pattern-file key */
    const char *name;      /* long display name */
    unsigned int color;    /* 0xRRGGBB */
    float gain;            /* nominal level */
    int dflt;              /* default variant (1-based; 0 when unused) */
} TrackInfo;

extern const TrackInfo TRACKS[N_TRACKS];

struct Engine;
struct PatternSnapshot;
struct SongSet;

/* Identity used by the app shell. */
const char *machine_id(void);
const char *machine_name(void);
const char *machine_window_title(void);

/* Engine lifecycle and sample resolution. */
void machine_init_engine(struct Engine *e);
void machine_after_load(struct Engine *e);
void machine_resolve_live(struct Engine *e);
const float *machine_sample_for(struct Engine *e, int track, int variant,
                                int *len);

/* Pattern/song sample metadata. */
const char *machine_pattern_magic(void);
void machine_default_snapshot(struct PatternSnapshot *s);
void machine_resolve_snapshot(struct PatternSnapshot *s);
int machine_read_sample_line(struct PatternSnapshot *s, const char *line);
void machine_write_sample_lines(FILE *f, const struct Engine *e);

#endif
