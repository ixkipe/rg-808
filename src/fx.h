/* RG-808 effects settings, defaults and persistence.
 * The audio engine owns the runtime state; these helpers move parameters
 * between plain structs, the engine and files. */
#ifndef RG808_FX_H
#define RG808_FX_H

#include <stdio.h>
#include "engine.h"

/* Drive slot algorithms. */
enum {
    FX_DRIVE_CRUSH = 0,
    FX_DRIVE_OVERDRIVE,
    FX_DRIVE_COMPRESS,
    FX_DRIVE_COUNT
};

/* Modulation slot algorithms. */
enum {
    FX_MOD_FLANGER = 0,
    FX_MOD_PHASER,
    FX_MOD_COUNT
};

/* Tempo-synced delay divisions. */
enum {
    FX_DIV_4 = 0,
    FX_DIV_8,
    FX_DIV_16,
    FX_DIV_4T,
    FX_DIV_8T,
    FX_DIV_16T,
    FX_DIV_4D,
    FX_DIV_8D,
    FX_DIV_16D,
    FX_DIV_COUNT
};

extern const char *const FX_DIV_NAMES[FX_DIV_COUNT];

void fx_defaults(FxSettings *f);
void fx_clamp(FxSettings *f);

/* Defaults used by patterns that predate per-pattern effects. */
void fx_set_fallback(const FxSettings *f);
void fx_get_fallback(FxSettings *f);

/* Copy parameters (not effect buffers) between an Engine and a plain struct. */
void fx_apply(Engine *e, const FxSettings *f);
void fx_capture(FxSettings *f, const Engine *e);

/* Resolve the delay time in milliseconds, honouring tempo sync. */
int fx_delay_ms_for(int sync, int delay_ms, int div, int bpm_x100);

/* One "FX ..." line. Returns 1 when the line was recognised. New and old M9
 * formats are both accepted. */
int fx_parse_line(FxSettings *f, const char *line);

/* Write the five "FX ..." lines for a settings struct. */
void fx_write_stream(FILE *fp, const FxSettings *f);

/* fallback file helpers. fx_load_file() fills defaults first and returns -1
 * when the file does not exist; a malformed line is ignored, not fatal. */
int fx_load_file(FxSettings *f, const char *path);
int fx_save_file(const FxSettings *f, const char *path);

#endif
