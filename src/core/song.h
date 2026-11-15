/* RG-808 song mode: an ordered chain of pattern slots (one bar per entry),
 * persisted as conf/song.txt, plus helpers to build the immutable snapshot set
 * the audio thread plays. */
#ifndef RG808_SONG_H
#define RG808_SONG_H

#include <stddef.h>
#include <time.h>

#include "engine.h"
#include "pattern.h"

typedef struct Song {
    int count;
    int loop;
    int slot[SONG_MAX_BARS];   /* 1-based pattern slot per bar */
} Song;

void song_init(Song *s);
int song_load(Song *s, const char *path);
int song_save(const Song *s, const char *path);

/* Fill `set` from the chain. Returns the number of missing/empty bars, or -1
 * on bad arguments. Missing bars become silent default snapshots. */
int song_build_set(const Song *s, PatternLib *lib, SongSet *set);

/* Frames for one pass through the chain plus a tail for the last hit. */
long long song_total_frames(const SongSet *set, int rate, int tail_frames);

/* Build a unique export path:
 *   <dir>/song_<tempo>_<YYYYMMDD>_<HHMMSS>.wav
 * `bpm_x100` is the song's starting tempo (normally bar 1). The tempo tag uses
 * an underscore for the decimal part (90_5 for 90.5). When the name already
 * exists, _2, _3, ... is appended before ".wav" so exports never overwrite an
 * earlier file (important when the device clock is unset or frozen).
 * Returns 0 on success, -1 on bad arguments/overflow/failure. */
int song_export_path(char *out, size_t cap, const char *dir, int bpm_x100,
                     time_t now);

#endif
