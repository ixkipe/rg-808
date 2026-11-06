/* RG-808 pattern library: up to 200 numbered slots under conf/patterns/,
 * an index.txt (slot -> name) and crash-safe atomic writes.
 *
 * The on-disk format of each slot is exactly the legacy pattern.txt format, so
 * files stay readable by older builds and the first-run migration is a copy. */
#ifndef RG808_PATTERN_H
#define RG808_PATTERN_H

#include <stddef.h>
#include "engine.h"

#define PAT_MAX_SLOTS 200
#define PAT_NAME_LEN  16

typedef struct PatternLib {
    char conf_dir[1024];                  /* conf/ */
    char pat_dir[1024];                   /* conf/patterns/ */
    char index_path[1024];                /* conf/patterns/index.txt */
    int  active_slot;                     /* 1..PAT_MAX_SLOTS */
    unsigned char exists[PAT_MAX_SLOTS];  /* slot file present on disk */
    char name[PAT_MAX_SLOTS][PAT_NAME_LEN + 1];
} PatternLib;

/* Create the library directories and load index.txt. When `migrate` is set,
 * a legacy <conf_dir>/pattern.txt is imported into slot 001 if the library has
 * no pattern files yet (first run after upgrading from v1). */
int pattern_init(PatternLib *lib, const char *conf_dir, int migrate);

/* Slot operations. Slots are 1-based; out-of-range values are rejected. */
int pattern_save_slot(PatternLib *lib, Engine *e, int slot);
int pattern_load_slot(PatternLib *lib, Engine *e, int slot);
int pattern_delete_slot(PatternLib *lib, int slot);
int pattern_copy_slot(PatternLib *lib, int from, int to);
int pattern_rename_slot(PatternLib *lib, int slot, const char *name);
int pattern_set_active(PatternLib *lib, int slot);

int pattern_slot_exists(const PatternLib *lib, int slot);
const char *pattern_name(const PatternLib *lib, int slot);
void pattern_default_name(int slot, char *dst, size_t cap);

/* File-level helpers: the legacy RG808_CONF override and automated tests. */
int pattern_save_file(Engine *e, const char *path);
int pattern_load_file(Engine *e, const char *path);
int pattern_file_exists(const char *path);

#endif
