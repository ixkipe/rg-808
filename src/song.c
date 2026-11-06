/* RG-808 song persistence and snapshot building. */
#include "song.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void trim_eol(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = 0;
}

void song_init(Song *s)
{
    memset(s, 0, sizeof *s);
    s->loop = 1;
}

int song_load(Song *s, const char *path)
{
    song_init(s);
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[256];
    while (fgets(line, sizeof line, f)) {
        trim_eol(line);
        if (!strncmp(line, "LOOP ", 5)) {
            s->loop = atoi(line + 5) ? 1 : 0;
        } else if (!strncmp(line, "BAR ", 4)) {
            int slot = atoi(line + 4);
            if (slot >= 1 && slot <= PAT_MAX_SLOTS && s->count < SONG_MAX_BARS)
                s->slot[s->count++] = slot;
        }
    }
    fclose(f);
    return 0;
}

int song_save(const Song *s, const char *path)
{
    char buf[8192];
    int n = snprintf(buf, sizeof buf, "RG808SONG1\nLOOP %d\n", s->loop ? 1 : 0);
    if (n < 0) return -1;
    for (int i = 0; i < s->count && i < SONG_MAX_BARS; i++) {
        if (n + 16 >= (int)sizeof buf) break;
        int w = snprintf(buf + n, sizeof buf - (size_t)n, "BAR %03d\n", s->slot[i]);
        if (w < 0) return -1;
        n += w;
    }
    return pattern_write_atomic(path, buf, (size_t)n);
}

int song_build_set(const Song *s, PatternLib *lib, SongSet *set)
{
    if (!s || !lib || !set) return -1;
    int count = s->count;
    if (count < 0) count = 0;
    if (count > SONG_MAX_BARS) count = SONG_MAX_BARS;
    set->count = count;

    int missing = 0;
    for (int i = 0; i < count; i++) {
        int slot = s->slot[i];
        if (pattern_load_snapshot(lib, slot, &set->pat[i]) != 0) {
            pattern_default_snapshot(&set->pat[i]);
            set->pat[i].slot = slot;
            missing++;
        }
    }
    return missing;
}

long long song_total_frames(const SongSet *set, int rate, int tail_frames)
{
    if (!set || set->count <= 0) return tail_frames;
    double total = 0;
    for (int i = 0; i < set->count; i++) {
        int bx = set->pat[i].bpm_x100;
        if (bx < 100) bx = 100;
        double bpm = bx / 100.0;
        int len = engine_length_clamp(set->pat[i].length);
        total += (double)rate * 60.0 / (bpm * 4.0) * len;
    }
    return (long long)total + tail_frames;
}
