/* RG-SMP sample bank and directory listing. */
#include "samplebank.h"
#include "wav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#endif

static int ci_cmp(const char *a, const char *b)
{
#ifdef _WIN32
    return _stricmp(a, b);
#else
    return strcasecmp(a, b);
#endif
}

int sampler_wav_name(const char *name)
{
    size_t n = name ? strlen(name) : 0;
    if (n < 4) return 0;
    const char *e = name + n - 4;
    return e[0] == '.' &&
           (e[1] == 'w' || e[1] == 'W') &&
           (e[2] == 'a' || e[2] == 'A') &&
           (e[3] == 'v' || e[3] == 'V');
}

/* Normalise a relative path to forward slashes and reject escapes. */
static int normalize_rel(const char *src, char *dst, size_t cap)
{
    if (!src || !*src) return -1;
    size_t n = 0;
    int component_start = 1;
    for (const char *p = src; *p; p++) {
        char c = *p;
        if (c == '\\') c = '/';
        if (c == '/') {
            if (component_start) continue;    /* collapse leading / double / */
            component_start = 1;
        } else {
            component_start = 0;
        }
        if (n + 1 >= cap) return -1;
        dst[n++] = c;
    }
    while (n && dst[n - 1] == '/') n--;
    dst[n] = 0;
    if (!n) return -1;

    /* Reject "." and ".." components. */
    const char *p = dst;
    while (*p) {
        const char *e = p;
        while (*e && *e != '/') e++;
        size_t len = (size_t)(e - p);
        if ((len == 1 && p[0] == '.') ||
            (len == 2 && p[0] == '.' && p[1] == '.'))
            return -1;
        p = (*e) ? e + 1 : e;
    }
    return 0;
}

void samplebank_init(SampleBank *b, int rate, int max_entries, size_t max_bytes)
{
    memset(b, 0, sizeof *b);
    b->rate = rate;
    b->max_entries = max_entries;
    b->max_bytes = max_bytes;
}

void samplebank_clear(SampleBank *b)
{
    if (!b) return;
    for (int i = 0; i < b->count; i++) {
        free(b->entry[i].data);
        b->entry[i].data = NULL;
        b->entry[i].frames = 0;
        b->entry[i].path[0] = 0;
    }
    b->count = 0;
    b->bytes = 0;
    b->overflow = 0;
}

int samplebank_find(const SampleBank *b, const char *path)
{
    if (!b || !path || !*path) return -1;
    for (int i = 0; i < b->count; i++)
        if (!strcmp(b->entry[i].path, path)) return i;
    return -1;
}

int samplebank_load(SampleBank *b, const char *root, const char *path)
{
    if (!b || !root || !path || !*path) return -1;

    char rel[SAMPLE_PATH_MAX];
    if (normalize_rel(path, rel, sizeof rel) != 0) return -1;

    int found = samplebank_find(b, rel);
    if (found >= 0) return found;

    if (b->count >= b->max_entries || b->count >= SAMPLE_BANK_MAX) {
        b->overflow = 1;
        return -1;
    }

    char full[1600];
    if (sampler_path_join(root, rel, full, sizeof full) != 0) return -1;

    WavAudio w;
    if (wav_load(full, b->rate, &w) != 0) return -1;
    if (w.frames <= 0 || w.frames > b->rate * SAMPLE_MAX_SECONDS) {
        free(w.data);
        b->overflow = 1;
        return -1;
    }
    size_t bytes = (size_t)w.frames * sizeof(float);
    if (b->bytes + bytes > b->max_bytes) {
        free(w.data);
        b->overflow = 1;
        return -1;
    }

    int idx = b->count++;
    snprintf(b->entry[idx].path, sizeof b->entry[idx].path, "%s", rel);
    b->entry[idx].data = w.data;
    b->entry[idx].frames = w.frames;
    b->bytes += bytes;
    return idx;
}

const float *samplebank_data(const SampleBank *b, int idx, int *len)
{
    *len = 0;
    if (!b || idx < 0 || idx >= b->count) return NULL;
    *len = b->entry[idx].frames;
    return b->entry[idx].data;
}

static int copy_bounded(char *dst, size_t cap, const char *src, size_t len)
{
    if (len + 1 > cap) return -1;
    memcpy(dst, src, len);
    dst[len] = 0;
    return 0;
}

int sampler_path_join(const char *dir, const char *name, char *out, size_t cap)
{
    if (!name || !*name) return -1;
    size_t nl = strlen(name);
    if (!dir || !*dir)
        return copy_bounded(out, cap, name, nl);
    size_t dl = strlen(dir);
    if (dl + 1 + nl + 1 > cap) return -1;
    memcpy(out, dir, dl);
    out[dl] = '/';
    memcpy(out + dl + 1, name, nl + 1);
    return 0;
}

int sampler_parent_dir(const char *dir, char *out, size_t cap)
{
    if (!dir || !*dir) {
        if (cap) out[0] = 0;
        return 0;
    }
    char tmp[SAMPLE_PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s", dir);
    size_t n = strlen(tmp);
    while (n && tmp[n - 1] == '/') tmp[--n] = 0;
    char *slash = strrchr(tmp, '/');
    if (slash) *slash = 0;
    else tmp[0] = 0;
    snprintf(out, cap, "%s", tmp);
    return 0;
}

static int dir_entry_cmp(const void *va, const void *vb)
{
    const SampleDirEntry *a = va;
    const SampleDirEntry *b = vb;
    if (a->is_dir != b->is_dir) return a->is_dir ? -1 : 1;
    return ci_cmp(a->name, b->name);
}

int sampler_list_dir(const char *root, const char *dir,
                     SampleDirEntry *out, int max)
{
    if (!root || !out || max <= 0) return -1;

    char full[1600];
    if (dir && *dir) {
        if (sampler_path_join(root, dir, full, sizeof full) != 0) return -1;
    } else {
        size_t rl = strlen(root);
        if (rl >= sizeof full) return -1;
        memcpy(full, root, rl + 1);
    }

    int count = 0;
    if (dir && *dir) {
        snprintf(out[count].name, sizeof out[count].name, "..");
        out[count].is_dir = 1;
        count++;
    }

#ifdef _WIN32
    char pattern[1300];
    snprintf(pattern, sizeof pattern, "%s/*", full);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return count ? count : -1;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        int is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (!is_dir && !sampler_wav_name(fd.cFileName)) continue;
        if (count >= max) break;
        size_t nl = strlen(fd.cFileName);
        if (nl >= sizeof out[count].name) nl = sizeof out[count].name - 1;
        memcpy(out[count].name, fd.cFileName, nl);
        out[count].name[nl] = 0;
        out[count].is_dir = is_dir;
        count++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(full);
    if (!d) return count ? count : -1;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        char p[1600];
        if (sampler_path_join(full, de->d_name, p, sizeof p) != 0) continue;
        struct stat st;
        if (stat(p, &st) != 0) continue;
        int is_dir = S_ISDIR(st.st_mode) != 0;
        if (!is_dir && !sampler_wav_name(de->d_name)) continue;
        if (count >= max) break;
        size_t nl = strlen(de->d_name);
        if (nl >= sizeof out[count].name) nl = sizeof out[count].name - 1;
        memcpy(out[count].name, de->d_name, nl);
        out[count].name[nl] = 0;
        out[count].is_dir = is_dir;
        count++;
    }
    closedir(d);
#endif

    int first = (dir && *dir) ? 1 : 0;
    if (count - first > 1)
        qsort(out + first, (size_t)(count - first), sizeof *out, dir_entry_cmp);
    return count;
}

static void collect_rec(const char *root, const char *dir, int depth,
                        char paths[][SAMPLE_PATH_MAX], int max, int *count)
{
    if (depth > 8 || *count >= max) return;
    SampleDirEntry entries[SAMPLE_LIST_MAX];
    int n = sampler_list_dir(root, dir, entries, SAMPLE_LIST_MAX);
    if (n < 0) return;
    for (int i = 0; i < n && *count < max; i++) {
        if (!strcmp(entries[i].name, "..")) continue;
        char rel[SAMPLE_PATH_MAX];
        if (sampler_path_join(dir, entries[i].name, rel, sizeof rel) != 0) continue;
        if (entries[i].is_dir) {
            collect_rec(root, rel, depth + 1, paths, max, count);
        } else {
            snprintf(paths[*count], SAMPLE_PATH_MAX, "%s", rel);
            (*count)++;
        }
    }
}

int sampler_collect_wavs(const char *root, const char *dir,
                         char paths[][SAMPLE_PATH_MAX], int max, int *count)
{
    if (!root || !paths || !count || max <= 0) return -1;
    *count = 0;
    collect_rec(root, dir ? dir : "", 0, paths, max, count);
    return 0;
}
