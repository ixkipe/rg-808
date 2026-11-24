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

/* Face buttons in the shells' raw order (0=A, 1=B, 2=X, 3=Y). */
enum { RGFACE_A = 0, RGFACE_B, RGFACE_X, RGFACE_Y, RGFACE_COUNT };

/* Label of a shoulder + face-button shortcut, or NULL when it is free.
 * Both tables are data: a new shoulder + face combination only needs a row
 * here, and the hint below picks it up. */
const char *rg_shortcut_face_label(int sh, int face);

/* Direction slots of the hint overlay. */
enum { RGH_LEFT = 0, RGH_RIGHT, RGH_UP, RGH_DOWN };

/* One modal hint, shared by every shoulder so they all look alike: the D-pad
 * on the left, the face diamond on the right (SP layout, X top). A shoulder
 * without face bindings still shows the diamond, dimmed, which says "nothing
 * here" instead of leaving the reader guessing.
 *
 * `engaged` marks cells that are currently doing something - the punch-in
 * layer uses it to light the effect it is holding or playing. */
typedef struct ShoulderHint {
    int shoulder;                       /* RGSH_* */
    unsigned char dir_engaged[4];       /* by RGH_* */
    unsigned char face_engaged[RGFACE_COUNT];
} ShoulderHint;

void rg_draw_shoulder_hint(const ViewCtx *v, const ShoulderHint *h);

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
