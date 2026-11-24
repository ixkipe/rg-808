/* Shared view layer for the RG-808 / RG-SMP app shells.
 *
 * The two machine shells are ~87 % identical, so a page that both of them
 * show is written once here: the shell keeps the state and the input
 * mapping, the core draws the page. The v1.1 pages (pitch, sample slice,
 * MIDI sync) are built on this seam, so they ship in both machines without
 * being written twice.
 */
#ifndef RG808_VIEWS_H
#define RG808_VIEWS_H

#include "canvas.h"
#include "engine.h"
#include "view_keys.h"

/* Everything a shared page needs: the canvas, the engine it edits, the
 * calling machine's palette and the track the page acts on. */
typedef struct ViewCtx {
    Canvas *cv;
    Engine *eng;
    Uint32 text, dim, cursor, led, panel, warn;
    int track;                 /* 0-based track the page edits */
} ViewCtx;

/* Label of a shoulder + direction shortcut, or NULL when the pair is free.
 * The one machine-specific label (sample browser vs kit page) comes from the
 * machine definition. */
const char *rg_shortcut_label(int sh, int dir);

/* Shared chrome. */
void rg_view_panel(Canvas *c, int x, int y, int w, int h,
                   Uint32 panel, Uint32 border);
void rg_view_value(Canvas *c, int x, int y, int w, const char *label,
                   const char *value, int selected,
                   Uint32 text, Uint32 dim, Uint32 cursor);

/* Export confirmation panel. `muted` counts the tracks that are muted, and
 * `include_muted` says whether they are part of the export. */
typedef struct ExportView {
    const char *filename;
    int bars;
    int muted;
    int include_muted;
} ExportView;

void rg_draw_export_confirm(const ViewCtx *v, const ExportView *ex);

#endif
