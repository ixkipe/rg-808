/* RG-808 machine definition: the classic 11-track kit and its sample families.
 * The shared core owns sequencing, patterns, songs and FX. */
#include "engine.h"
#include "machine.h"
#include "pitch.h"
#include "samples.h"
#include "views/view_keys.h"

#include <stdio.h>
#include <string.h>

/* Curated 808 kit families, mapped from the sample pack. Colors evoke the
 * TR-808 panel. */
const TrackInfo TRACKS[N_TRACKS] = {
    { "bd", "BD", "BASS DRUM",  0xD94F3B, 1.00f, 7 },
    { "sd", "SD", "SNARE",      0xE08A2E, 0.85f, 1 },
    { "lt", "LT", "LOW TOM",    0xD8B62C, 0.80f, 1 },
    { "mt", "MT", "MID TOM",    0x9CC832, 0.75f, 4 },
    { "ht", "HT", "HIGH TOM",   0x3FA93C, 0.70f, 6 },
    { "ch", "CH", "CLOSED HAT", 0x2FA3A0, 0.50f, 1 },
    { "oh", "OH", "OPEN HAT",   0x3579C8, 0.50f, 1 },
    { "cl", "CL", "CLAVE",      0x7A4FC0, 0.65f, 1 },
    { "cp", "CP", "HAND CLAP",  0xC24F8A, 0.60f, 1 },
    { "cb", "CB", "COWBELL",    0xB0A03C, 0.45f, 1 },
    { "cy", "CY", "CYMBAL",     0x8A8F98, 0.40f, 6 },
};

const char *machine_id(void) { return "808"; }
const char *machine_name(void) { return "RG-808"; }
const char *machine_window_title(void) { return "RG-808"; }
const char *machine_pattern_magic(void) { return "RG808P1"; }

const char *machine_shortcut_label(int sh, int dir)
{
    if (sh == RGSH_L2 && dir == RGV_UP) return "SAMPLE KIT";
    return NULL;
}

/* Rendered transposes of the kit samples. The kit itself never changes after
 * startup, so the keys are stable and the cache needs no invalidation; the
 * live engine and the song snapshots share it, which keeps a song bar that
 * repeats an interval from rendering twice. */
static PitchCache kit_pitch;
static int kit_pitch_ready;

static void kit_pitch_init(void)
{
    if (kit_pitch_ready) return;
    pitch_cache_init(&kit_pitch, 44100, 64, 24u * 1024u * 1024u);
    kit_pitch_ready = 1;
}

/* Resolve one track, transposed when the pattern asks for it. */
static void resolve_track(Engine *e, int t, int variant)
{
    char key[64];
    snprintf(key, sizeof key, "%s:%d", TRACKS[t].id, variant);

    int len = 0;
    const float *data = sample_get(t, variant, &len);
    e->sample_data[t] = data;
    e->sample_len[t] = len;
    e->sample_missing[t] = data ? 0 : 1;

    int st = pitch_clamp(e->pitch[t]);
    if (!data || len <= 0 || st == 0) return;
    kit_pitch_init();
    int plen = 0;
    const float *p = pitch_cache_get(&kit_pitch, key, data, len, st, &plen);
    if (p && plen > 0) {
        e->sample_data[t] = p;
        e->sample_len[t] = plen;
    }
}

void machine_init_engine(Engine *e)
{
    e->track_voice_limit = 0;   /* global-oldest stealing, as before */
    for (int t = 0; t < N_TRACKS; t++) {
        e->kit[t] = TRACKS[t].dflt - 1;
        e->pitch[t] = 0;
        e->sample_path[t][0] = 0;
        e->sample_data[t] = NULL;
        e->sample_len[t] = 0;
        e->sample_missing[t] = 0;
    }
    /* If the kit is already loaded (tests, offline render), resolve it now.
     * The app calls this again after samples_load_all(). */
    machine_resolve_live(e);
}

void machine_resolve_live(Engine *e)
{
    for (int t = 0; t < N_TRACKS; t++)
        resolve_track(e, t, e->kit[t]);
}

void machine_after_load(Engine *e)
{
    machine_resolve_live(e);
}

const float *machine_sample_for(Engine *e, int track, int variant, int *len)
{
    (void)e;
    if (len) *len = 0;
    if (track < 0 || track >= N_TRACKS) return NULL;
    return sample_get(track, variant, len);
}

void machine_default_snapshot(PatternSnapshot *s)
{
    for (int t = 0; t < N_TRACKS; t++) {
        s->kit[t] = TRACKS[t].dflt - 1;
        s->pitch[t] = 0;
        s->sample_path[t][0] = 0;
    }
}

void machine_resolve_snapshot(PatternSnapshot *s)
{
    for (int t = 0; t < N_TRACKS; t++) {
        char key[64];
        snprintf(key, sizeof key, "%s:%d", TRACKS[t].id, s->kit[t]);

        int len = 0;
        const float *data = sample_get(t, s->kit[t], &len);
        s->sample[t] = data;
        s->sample_len[t] = len;

        /* A snapshot must not touch the engine, so resolve the transpose
         * through the cache directly. */
        int st = pitch_clamp(s->pitch[t]);
        if (!data || len <= 0 || st == 0) continue;
        kit_pitch_init();
        int plen = 0;
        const float *p = pitch_cache_get(&kit_pitch, key, data, len, st, &plen);
        if (p && plen > 0) {
            s->sample[t] = p;
            s->sample_len[t] = plen;
        }
    }
}

int machine_read_sample_line(PatternSnapshot *s, const char *line)
{
    if (strncmp(line, "KIT ", 4)) return 0;

    char lab[8] = { 0 };
    int idx = 0;
    if (sscanf(line + 4, "%7s %d", lab, &idx) != 2) return 1;

    for (int t = 0; t < N_TRACKS; t++) {
        if (!strcmp(lab, TRACKS[t].label)) {
            int cnt = variant_count(t);
            if (cnt && idx < 1) idx = 1;
            if (cnt && idx > cnt) idx = cnt;
            if (cnt) s->kit[t] = idx - 1;
            return 1;
        }
    }
    return 1;
}

void machine_write_sample_lines(FILE *f, const Engine *e)
{
    for (int t = 0; t < N_TRACKS; t++)
        fprintf(f, "KIT %s %d\n", TRACKS[t].label, e->kit[t] + 1);
}
