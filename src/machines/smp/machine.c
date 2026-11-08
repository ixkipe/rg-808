/* RG-SMP machine definition: 11 free sample slots and a path-based sample
 * model.  The shared core owns sequencing, patterns, songs and FX. */
#include "engine.h"
#include "machine.h"
#include "samplebank.h"
#include "smp_machine.h"
#include "song.h"

#include <stdio.h>
#include <string.h>

/* Every slot is identical apart from its shade; the assigned sample is shown
 * in the browser, not in the grid header. */
const TrackInfo TRACKS[N_TRACKS] = {
    { "01", "01", "TRACK 01", 0xE8E8E8, 0.85f, 0 },
    { "02", "02", "TRACK 02", 0xC8C8C8, 0.85f, 0 },
    { "03", "03", "TRACK 03", 0xB0B0B0, 0.85f, 0 },
    { "04", "04", "TRACK 04", 0xE0E0E0, 0.85f, 0 },
    { "05", "05", "TRACK 05", 0xA8A8A8, 0.85f, 0 },
    { "06", "06", "TRACK 06", 0xD0D0D0, 0.85f, 0 },
    { "07", "07", "TRACK 07", 0x989898, 0.85f, 0 },
    { "08", "08", "TRACK 08", 0xF0F0F0, 0.85f, 0 },
    { "09", "09", "TRACK 09", 0xC0C0C0, 0.85f, 0 },
    { "10", "10", "TRACK 10", 0xA0A0A0, 0.85f, 0 },
    { "11", "11", "TRACK 11", 0xD8D8D8, 0.85f, 0 },
};

const char *machine_id(void) { return "smp"; }
const char *machine_name(void) { return "RG-SMP"; }
const char *machine_window_title(void) { return "RG-SMP"; }
const char *machine_pattern_magic(void) { return "RGSMP1"; }

void machine_init_engine(Engine *e)
{
    e->track_voice_limit = 2;
    for (int t = 0; t < N_TRACKS; t++) {
        e->kit[t] = 0;
        e->sample_path[t][0] = 0;
        e->sample_data[t] = NULL;
        e->sample_len[t] = 0;
        e->sample_missing[t] = 0;
    }
}

void machine_after_load(Engine *e)
{
    (void)e;   /* the SMP app reloads its live sample bank explicitly */
}

void machine_resolve_live(Engine *e)
{
    (void)e;
}

const float *machine_sample_for(Engine *e, int track, int variant, int *len)
{
    (void)variant;
    if (!e || track < 0 || track >= N_TRACKS) {
        if (len) *len = 0;
        return NULL;
    }
    if (len) *len = e->sample_len[track];
    return (const float *)e->sample_data[track];
}

void machine_default_snapshot(PatternSnapshot *s)
{
    (void)s;
}

void machine_resolve_snapshot(PatternSnapshot *s)
{
    (void)s;   /* song_load_samples() fills the pointers for the SMP */
}

static void trim_eol(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = 0;
}

int machine_read_sample_line(PatternSnapshot *s, const char *line)
{
    if (strncmp(line, "SMP ", 4)) return 0;

    char lab[8] = { 0 };
    int pos = 0;
    if (sscanf(line, "SMP %7s %n", lab, &pos) != 1 || pos <= 0) return 1;

    char path[SAMPLE_PATH_MAX];
    snprintf(path, sizeof path, "%s", line + pos);
    trim_eol(path);
    char *p = path;
    while (*p == ' ') p++;

    for (int t = 0; t < N_TRACKS; t++) {
        if (!strcmp(lab, TRACKS[t].label)) {
            snprintf(s->sample_path[t], sizeof s->sample_path[t], "%s", p);
            return 1;
        }
    }
    return 1;
}

void machine_write_sample_lines(FILE *f, const Engine *e)
{
    for (int t = 0; t < N_TRACKS; t++)
        if (e->sample_path[t][0])
            fprintf(f, "SMP %s %s\n", TRACKS[t].label, e->sample_path[t]);
}

/* Resolve every sample path in the set against one bank. Returns the number of
 * missing/failed samples, or -2 when the set needs more slots than the bank. */
int song_load_samples(SongSet *set, SampleBank *bank, const char *root)
{
    if (!set || !bank || !root) return -1;

    int unique = 0;
    for (int i = 0; i < set->count; i++) {
        for (int t = 0; t < N_TRACKS; t++) {
            const char *p = set->pat[i].sample_path[t];
            if (!*p) continue;
            int found = 0;
            for (int j = 0; j <= i && !found; j++) {
                int last = (j == i) ? t : N_TRACKS;
                for (int k = 0; k < last; k++)
                    if (!strcmp(p, set->pat[j].sample_path[k])) { found = 1; break; }
            }
            if (!found) unique++;
        }
    }
    if (unique > bank->max_entries) return -2;

    int missing = 0;
    for (int i = 0; i < set->count; i++) {
        for (int t = 0; t < N_TRACKS; t++) {
            const char *p = set->pat[i].sample_path[t];
            set->pat[i].sample[t] = NULL;
            set->pat[i].sample_len[t] = 0;
            if (!*p) continue;
            int idx = samplebank_load(bank, root, p);
            if (idx < 0) {
                missing++;
                continue;
            }
            set->pat[i].sample[t] = samplebank_data(bank, idx, &set->pat[i].sample_len[t]);
        }
    }
    if (bank->overflow) return -2;
    return missing;
}
