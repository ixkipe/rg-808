/* RG-808 pattern library implementation. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "pattern.h"
#include "samples.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <io.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

/* --------------------------------------------------------------- fs helpers */

static int ensure_dir(const char *path)
{
    if (!path || !*path) return 0;
#ifdef _WIN32
    if (_mkdir(path) == 0 || errno == EEXIST) return 0;
#else
    if (mkdir(path, 0755) == 0 || errno == EEXIST) return 0;
#endif
    return -1;
}

/* mkdir -p for the simple paths we use (conf/ and conf/patterns/). */
static void ensure_dir_tree(char *path)
{
    for (char *p = path + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char save = *p;
            *p = 0;
            if (*path) ensure_dir(path);
            *p = save;
        }
    }
    ensure_dir(path);
}

static void ensure_parent(const char *path)
{
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    char *bslash = strrchr(dir, '\\');
    if (!slash || (bslash && bslash > slash)) slash = bslash;
    if (!slash) return;
    *slash = 0;
    ensure_dir_tree(dir);
}

/* Persist the directory entry after a rename (POSIX only; Windows uses
 * MOVEFILE_WRITE_THROUGH below). */
#ifndef _WIN32
static void sync_parent_dir(const char *path)
{
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    if (!slash) return;
    *slash = 0;
    int fd = open(dir, O_RDONLY);
    if (fd >= 0) {
        fsync(fd);
        close(fd);
    }
}
#endif

/* Write bytes to path.tmp, flush+fsync, then atomically replace path. On the
 * device (Linux/SD card) rename() is atomic, so a power cut leaves either the
 * old file or the new one — never a truncated pattern. */
static int write_atomic(const char *path, const void *data, size_t len)
{
    char tmp[1200];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    ensure_parent(path);

    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    int ok = fwrite(data, 1, len, f) == len;
    ok = (fflush(f) == 0) && ok;
#ifdef _WIN32
    if (ok) ok = (_commit(_fileno(f)) == 0);
#else
    if (ok) ok = (fsync(fileno(f)) == 0);
#endif
    if (fclose(f) != 0) ok = 0;
    if (!ok) { remove(tmp); return -1; }

#ifdef _WIN32
    if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        remove(tmp);
        return -1;
    }
#else
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return -1;
    }
    sync_parent_dir(path);
#endif
    return 0;
}

static int read_file(const char *path, char **out, size_t *out_len)
{
    *out = NULL;
    *out_len = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return -1; }
    rewind(f);
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return -1; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) { free(buf); return -1; }
    buf[got] = 0;
    *out = buf;
    *out_len = got;
    return 0;
}

static int copy_file_atomic(const char *src, const char *dst)
{
    char *data;
    size_t len;
    if (read_file(src, &data, &len) != 0) return -1;
    int rc = write_atomic(dst, data, len);
    free(data);
    return rc;
}

/* ------------------------------------------------------------------ names */

static void trim_eol(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = 0;
}

static void copy_name(const char *src, char *dst, size_t cap)
{
    size_t n = 0;
    for (; src && *src && n + 1 < cap; src++) {
        unsigned char ch = (unsigned char)*src;
        if (ch < 32 || ch == 127) ch = ' ';
        dst[n++] = (char)ch;
    }
    while (n && dst[n - 1] == ' ') n--;
    dst[n] = 0;
}

void pattern_default_name(int slot, char *dst, size_t cap)
{
    snprintf(dst, cap, "PATTERN %03d", slot);
}

static int slot_valid(int slot)
{
    return slot >= 1 && slot <= PAT_MAX_SLOTS;
}

/* Bounded string join (kept warning-free and truncation-safe). */
static void join_path(char *dst, size_t cap, const char *base, const char *suffix)
{
    size_t n = strlen(base);
    if (n >= cap) n = cap - 1;
    memcpy(dst, base, n);
    dst[n] = 0;
    size_t m = strlen(suffix);
    if (n + m >= cap) m = cap - n - 1;
    memcpy(dst + n, suffix, m);
    dst[n + m] = 0;
}

const char *pattern_name(const PatternLib *lib, int slot)
{
    if (!slot_valid(slot)) return "";
    return lib->name[slot - 1];
}

int pattern_slot_exists(const PatternLib *lib, int slot)
{
    return slot_valid(slot) && lib->exists[slot - 1] != 0;
}

static void slot_path(const PatternLib *lib, int slot, char *dst, size_t cap)
{
    join_path(dst, cap, lib->pat_dir, "/");
    size_t n = strlen(dst);
    if (n + 8 < cap) snprintf(dst + n, cap - n, "%03d.txt", slot);
}

/* ------------------------------------------------------------------ index */

static int save_index(const PatternLib *lib)
{
    char buf[8192];
    int w = snprintf(buf, sizeof buf, "ACTIVE %d\n", lib->active_slot);
    if (w < 0) return -1;
    size_t n = (size_t)w;
    for (int s = 1; s <= PAT_MAX_SLOTS && n + 32 < sizeof buf; s++) {
        w = snprintf(buf + n, sizeof buf - n, "%03d %s\n", s, lib->name[s - 1]);
        if (w < 0) return -1;
        n += (size_t)w;
        if (n >= sizeof buf) { n = sizeof buf - 1; break; }
    }
    return write_atomic(lib->index_path, buf, n);
}

static void load_index(PatternLib *lib)
{
    FILE *f = fopen(lib->index_path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        trim_eol(line);
        if (!strncmp(line, "ACTIVE ", 7)) {
            int n = atoi(line + 7);
            if (slot_valid(n)) lib->active_slot = n;
        } else if (line[0] >= '0' && line[0] <= '9') {
            int n = atoi(line);
            if (!slot_valid(n)) continue;
            const char *p = strchr(line, ' ');
            while (p && *p == ' ') p++;
            if (p && *p) copy_name(p, lib->name[n - 1], PAT_NAME_LEN + 1);
        }
    }
    fclose(f);
}

static void scan_slots(PatternLib *lib)
{
    for (int s = 1; s <= PAT_MAX_SLOTS; s++) {
        char path[1200];
        slot_path(lib, s, path, sizeof path);
        FILE *f = fopen(path, "rb");
        lib->exists[s - 1] = f != NULL;
        if (f) fclose(f);
    }
}

int pattern_init(PatternLib *lib, const char *conf_dir, int migrate)
{
    memset(lib, 0, sizeof *lib);
    lib->active_slot = 1;

    snprintf(lib->conf_dir, sizeof lib->conf_dir, "%s", conf_dir ? conf_dir : "conf");
    size_t n = strlen(lib->conf_dir);
    while (n > 1 && (lib->conf_dir[n - 1] == '/' || lib->conf_dir[n - 1] == '\\'))
        lib->conf_dir[--n] = 0;

    join_path(lib->pat_dir, sizeof lib->pat_dir, lib->conf_dir, "/patterns");
    join_path(lib->index_path, sizeof lib->index_path, lib->pat_dir, "/index.txt");

    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", lib->conf_dir);
    ensure_dir_tree(tmp);
    snprintf(tmp, sizeof tmp, "%s", lib->pat_dir);
    ensure_dir_tree(tmp);

    for (int s = 1; s <= PAT_MAX_SLOTS; s++)
        pattern_default_name(s, lib->name[s - 1], PAT_NAME_LEN + 1);

    int had_index = pattern_file_exists(lib->index_path);
    load_index(lib);
    scan_slots(lib);

    if (migrate && !had_index) {
        int have_any = 0;
        for (int s = 0; s < PAT_MAX_SLOTS; s++)
            if (lib->exists[s]) { have_any = 1; break; }
        if (!have_any) {
            char legacy[1200];
            snprintf(legacy, sizeof legacy, "%s/pattern.txt", lib->conf_dir);
            char dst[1200];
            slot_path(lib, 1, dst, sizeof dst);
            if (pattern_file_exists(legacy) && copy_file_atomic(legacy, dst) == 0) {
                char def[PAT_NAME_LEN + 1];
                pattern_default_name(1, def, sizeof def);
                if (!strcmp(lib->name[0], def))
                    snprintf(lib->name[0], PAT_NAME_LEN + 1, "IMPORTED");
                lib->exists[0] = 1;
                lib->active_slot = 1;
                save_index(lib);
            }
        }
    }
    return 0;
}

int pattern_set_active(PatternLib *lib, int slot)
{
    if (!slot_valid(slot)) return -1;
    lib->active_slot = slot;
    return save_index(lib);
}

/* --------------------------------------------------------------- pattern io */

static void reset_engine_pattern(Engine *e)
{
    e->bpm_x100 = 12000;
    e->swing_pct = 0;
    for (int t = 0; t < N_TRACKS; t++) {
        for (int s = 0; s < N_STEPS; s++) {
            e->lvl[t][s] = LVL_OFF;
            e->rch[t][s] = RCH_OFF;
        }
        e->kit[t] = TRACKS[t].dflt - 1;
    }
}

static int write_pattern_stream(FILE *f, Engine *e)
{
    fprintf(f, "RG808P1\n");
    fprintf(f, "BPM %.2f\n", e->bpm_x100 / 100.0);
    for (int t = 0; t < N_TRACKS; t++) {
        fprintf(f, "%s ", TRACKS[t].label);
        for (int s = 0; s < N_STEPS; s++) fprintf(f, "%d", e->lvl[t][s]);
        fprintf(f, "\n");
    }
    for (int t = 0; t < N_TRACKS; t++) {
        fprintf(f, "RCH %s ", TRACKS[t].label);
        for (int s = 0; s < N_STEPS; s++) fprintf(f, "%d", e->rch[t][s]);
        fprintf(f, "\n");
    }
    for (int t = 0; t < N_TRACKS; t++)
        fprintf(f, "KIT %s %d\n", TRACKS[t].label, e->kit[t] + 1);
    fprintf(f, "SWING %d\n", e->swing_pct);
    return ferror(f) ? -1 : 0;
}

int pattern_save_file(Engine *e, const char *path)
{
    char tmp[1200];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    ensure_parent(path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    int ok = write_pattern_stream(f, e) == 0;
    ok = (fflush(f) == 0) && ok;
#ifdef _WIN32
    if (ok) ok = (_commit(_fileno(f)) == 0);
#else
    if (ok) ok = (fsync(fileno(f)) == 0);
#endif
    if (fclose(f) != 0) ok = 0;
    if (!ok) { remove(tmp); return -1; }

#ifdef _WIN32
    if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        remove(tmp);
        return -1;
    }
#else
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return -1;
    }
    sync_parent_dir(path);
#endif
    return 0;
}

int pattern_load_file(Engine *e, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    reset_engine_pattern(e);
    char line[256];
    int got_any = 0;
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "BPM ", 4)) {
            double bpm = atof(line + 4);
            if (bpm < 4) bpm = 4;
            if (bpm > 300) bpm = 300;
            e->bpm_x100 = (int)(bpm * 100);
            got_any = 1;
        } else {
            char lab[8] = { 0 };
            if (!strncmp(line, "SWING ", 6)) {
                int sw = atoi(line + 6);
                if (sw < 0) sw = 0;
                if (sw > 100) sw = 100;
                e->swing_pct = sw;
                got_any = 1;
            } else if (!strncmp(line, "KIT ", 4)) {
                int idx = 0;
                if (sscanf(line + 4, "%4s %d", lab, &idx) == 2) {
                    for (int t = 0; t < N_TRACKS; t++) {
                        if (!strcmp(lab, TRACKS[t].label)) {
                            int cnt = variant_count(t);
                            if (cnt && idx < 1) idx = 1;
                            if (cnt && idx > cnt) idx = cnt;
                            if (cnt) e->kit[t] = idx - 1;
                            break;
                        }
                    }
                }
            } else if (!strncmp(line, "RCH ", 4)) {
                if (sscanf(line + 4, "%4s", lab) == 1) {
                    for (int t = 0; t < N_TRACKS; t++) {
                        if (!strcmp(lab, TRACKS[t].label)) {
                            char *p = strchr(line + 4, ' ');
                            int n = 0;
                            for (; p && *p; p++)
                                if (*p >= '0' && *p <= '4' && n < N_STEPS)
                                    e->rch[t][n++] = (unsigned char)(*p - '0');
                            break;
                        }
                    }
                }
            } else if (sscanf(line, "%4s", lab) == 1) {
                for (int t = 0; t < N_TRACKS; t++) {
                    if (!strcmp(lab, TRACKS[t].label)) {
                        char *p = strchr(line, ' ');
                        int n = 0;
                        for (; p && *p; p++)
                            if (*p >= '0' && *p <= '2' && n < N_STEPS)
                                e->lvl[t][n++] = (unsigned char)(*p - '0');
                        if (n == N_STEPS) got_any = 1;
                        break;
                    }
                }
            }
        }
    }
    fclose(f);
    return got_any ? 0 : -2;
}

int pattern_file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/* -------------------------------------------------------------- slot ops */

int pattern_save_slot(PatternLib *lib, Engine *e, int slot)
{
    if (!slot_valid(slot)) return -1;
    char path[1200];
    slot_path(lib, slot, path, sizeof path);
    if (pattern_save_file(e, path) != 0) return -1;

    char def[PAT_NAME_LEN + 1];
    pattern_default_name(slot, def, sizeof def);
    if (!lib->name[slot - 1][0])
        snprintf(lib->name[slot - 1], PAT_NAME_LEN + 1, "%s", def);
    lib->exists[slot - 1] = 1;
    return save_index(lib);
}

int pattern_load_slot(PatternLib *lib, Engine *e, int slot)
{
    if (!pattern_slot_exists(lib, slot)) return -1;
    char path[1200];
    slot_path(lib, slot, path, sizeof path);
    return pattern_load_file(e, path);
}

int pattern_delete_slot(PatternLib *lib, int slot)
{
    if (!slot_valid(slot)) return -1;
    char path[1200];
    slot_path(lib, slot, path, sizeof path);
    if (!lib->exists[slot - 1] && !pattern_file_exists(path)) return -1;
    if (remove(path) != 0 && pattern_file_exists(path)) return -1;
    lib->exists[slot - 1] = 0;
    pattern_default_name(slot, lib->name[slot - 1], PAT_NAME_LEN + 1);
    return save_index(lib);
}

int pattern_copy_slot(PatternLib *lib, int from, int to)
{
    if (!slot_valid(from) || !slot_valid(to)) return -1;
    if (from == to) return 0;
    if (!pattern_slot_exists(lib, from)) return -1;

    char src[1200], dst[1200];
    slot_path(lib, from, src, sizeof src);
    slot_path(lib, to, dst, sizeof dst);
    if (copy_file_atomic(src, dst) != 0) return -1;

    char src_name[PAT_NAME_LEN + 1];
    char src_default[PAT_NAME_LEN + 1];
    snprintf(src_name, sizeof src_name, "%s", lib->name[from - 1]);
    pattern_default_name(from, src_default, sizeof src_default);
    if (!strcmp(src_name, src_default))
        pattern_default_name(to, lib->name[to - 1], PAT_NAME_LEN + 1);
    else
        snprintf(lib->name[to - 1], PAT_NAME_LEN + 1, "%s", src_name);
    lib->exists[to - 1] = 1;
    return save_index(lib);
}

int pattern_rename_slot(PatternLib *lib, int slot, const char *name)
{
    if (!slot_valid(slot)) return -1;
    char clean[PAT_NAME_LEN + 1];
    copy_name(name, clean, sizeof clean);
    if (!clean[0]) pattern_default_name(slot, clean, sizeof clean);
    snprintf(lib->name[slot - 1], PAT_NAME_LEN + 1, "%s", clean);
    return save_index(lib);
}
