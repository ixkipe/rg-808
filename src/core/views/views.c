/* Shared view layer: shortcut labels, page chrome and the export panel. */
#include "views.h"
#include "machine.h"

#include <stdio.h>
#include <string.h>

static const char *sh_name(int sh)
{
    switch (sh) {
    case RGSH_L1: return "L1";
    case RGSH_L2: return "L2";
    case RGSH_R1: return "R1";
    case RGSH_R2: return "R2";
    default:      return "?";
    }
}

const char *rg_shortcut_label(int sh, int dir)
{
    switch (sh) {
    case RGSH_L1:
        if (dir == RGV_LEFT) return "TEMPO";
        if (dir == RGV_RIGHT) return "SWING";
        if (dir == RGV_UP) return "PATTERN LENGTH";
        if (dir == RGV_DOWN) return "RATCHET";
        break;
    case RGSH_L2:
        if (dir == RGV_LEFT) return "PATTERN LIBRARY";
        if (dir == RGV_RIGHT) return "SONG MODE";
        if (dir == RGV_UP) return machine_shortcut_label(sh, dir);
        if (dir == RGV_DOWN) return "EXPORT WAV";
        break;
    case RGSH_R1:
        if (dir == RGV_LEFT) return "1/4";
        if (dir == RGV_RIGHT) return "1/8";
        if (dir == RGV_UP) return "1/16";
        if (dir == RGV_DOWN) return "1/32";
        break;
    case RGSH_R2:
        if (dir == RGV_UP) return "DRIVE FX";
        if (dir == RGV_LEFT) return "MOD FX";
        if (dir == RGV_DOWN) return "DELAY FX";
        if (dir == RGV_RIGHT) return "REVERB FX";
        break;
    default:
        break;
    }
    return NULL;
}

/* Shoulder + face-button combinations. One row per binding; the hint draws
 * whatever is here, so a new combination is a one-line change. A '\n' in the
 * label breaks it over two lines inside the cell. */
const char *rg_shortcut_face_label(int sh, int face)
{
    if (sh == RGSH_L1) {
        if (face == RGFACE_X) return "PITCH";
        /* P3 adds B ("TRIM") in the same cluster. */
    } else if (sh == RGSH_R1) {
        if (face == RGFACE_A) return "REWIND";
        if (face == RGFACE_B) return "GATE";
        if (face == RGFACE_X) return "TAPE\nSTOP";
        if (face == RGFACE_Y) return "PHONE";
    }
    return NULL;
}

/* Per-shoulder copy of the hint panel. */
static const char *hint_title(int sh)
{
    switch (sh) {
    case RGSH_R1: return "PUNCH-IN FX";
    default:      return "SHORTCUTS";
    }
}

static const char *hint_subtitle(int sh)
{
    if (sh == RGSH_R1) return "HOLD R1 AND PRESS A KEY - QUANTIZED TO 1/16";
    return "PRESS A DIRECTION OR FACE BUTTON";
}

static const char *hint_dir_caption(int sh)
{
    switch (sh) {
    case RGSH_L1: return "D-PAD: EDIT";
    case RGSH_L2: return "D-PAD: BANKS";
    case RGSH_R1: return "D-PAD: STUTTER";
    case RGSH_R2: return "D-PAD: EFFECTS";
    default:      return NULL;
    }
}

static const char *hint_face_caption(int sh)
{
    if (sh == RGSH_R1) return "FACE: FX";
    return "FACE BUTTONS";
}

static const char *hint_footnote(int sh)
{
    if (sh == RGSH_R1) return "ONE EFFECT AT A TIME - RELEASE TO RETURN TO LIVE";
    return "RELEASE THE SHOULDER TO CLOSE";
}

/* One key cell. A short label rides inside the cell under the key; a free cell
 * stays dim with the key centred. */
static void hint_cell(Canvas *c, int x, int y, int size, const char *key,
                      const char *inside, int engaged,
                      Uint32 text, Uint32 dim, Uint32 lit)
{
    int on = inside != NULL;
    /* Calm at rest - only the engaged cell takes the LED colour. */
    cv_rect(c, x, y, size, size, engaged ? cv_dim(lit, 0.45f) : 0x1c1c24);
    cv_border(c, x, y, size, size, 2, engaged ? lit : dim);
    if (!on) {
        cv_text(c, x + (size - cv_text_w(key, 1)) / 2, y + (size - 8) / 2,
                key, 1, dim);
        return;
    }
    cv_text(c, x + (size - cv_text_w(key, 1)) / 2, y + 5, key, 1, text);
    const char *nl = strchr(inside, '\n');
    if (nl) {
        char top[24];
        size_t n = (size_t)(nl - inside);
        if (n >= sizeof top) n = sizeof top - 1;
        memcpy(top, inside, n);
        top[n] = 0;
        cv_text(c, x + (size - cv_text_w(top, 1)) / 2, y + 18, top, 1, text);
        cv_text(c, x + (size - cv_text_w(nl + 1, 1)) / 2, y + 30, nl + 1, 1, text);
    } else {
        cv_text(c, x + (size - cv_text_w(inside, 1)) / 2, y + 22, inside, 1, text);
    }
}

/* Direction names go outside the diamond when they are too long to sit in a
 * cell - the choice is per shoulder, so all four always agree. */
static int dir_labels_fit(int sh)
{
    static const int dirs[4] = { RGV_LEFT, RGV_RIGHT, RGV_UP, RGV_DOWN };
    for (int i = 0; i < 4; i++) {
        const char *l = rg_shortcut_label(sh, dirs[i]);
        if (!l || strlen(l) > 5) return 0;
    }
    return 1;
}

void rg_draw_shoulder_hint(const ViewCtx *v, const ShoulderHint *h)
{
    if (!v || !v->cv || !h) return;
    Canvas *c = v->cv;
    int sh = h->shoulder;

    /* One frame and one rhythm for every shoulder. */
    int px = 20, py = 88, pw = 600, ph = 300;
    rg_view_panel(c, px, py, pw, ph, v->panel, v->text);

    char buf[80];
    snprintf(buf, sizeof buf, "%s + %s", sh_name(sh), hint_title(sh));
    cv_text(c, px + 16, py + 10, buf, 1, v->text);
    cv_text(c, px + 16, py + 26, hint_subtitle(sh), 1, v->dim);

    const int dcell = 46, fcell = 54, gap = 6;
    const int cy = py + 142;
    const int cx = px + 214;             /* D-pad centre */
    const int fx = px + 485;             /* face centre */
    const int lx = cx - dcell / 2 - gap - dcell;
    const int rx = cx + dcell / 2 + gap;
    const int uy = cy - dcell / 2 - gap - dcell;
    const int dy = cy + dcell / 2 + gap;

    int inside = dir_labels_fit(sh);
    const char *lu = rg_shortcut_label(sh, RGV_UP);
    const char *ll = rg_shortcut_label(sh, RGV_LEFT);
    const char *lr = rg_shortcut_label(sh, RGV_RIGHT);
    const char *ld = rg_shortcut_label(sh, RGV_DOWN);

    hint_cell(c, cx - dcell / 2, uy, dcell, "^", inside ? lu : NULL,
              h->dir_engaged[RGH_UP], v->text, v->dim, v->text);
    hint_cell(c, lx, cy - dcell / 2, dcell, "<", inside ? ll : NULL,
              h->dir_engaged[RGH_LEFT], v->text, v->dim, v->text);
    hint_cell(c, rx, cy - dcell / 2, dcell, ">", inside ? lr : NULL,
              h->dir_engaged[RGH_RIGHT], v->text, v->dim, v->text);
    hint_cell(c, cx - dcell / 2, dy, dcell, "v", inside ? ld : NULL,
              h->dir_engaged[RGH_DOWN], v->text, v->dim, v->text);
    cv_rect(c, cx - dcell / 2, cy - dcell / 2, dcell, dcell, 0x1c1c24);
    cv_border(c, cx - dcell / 2, cy - dcell / 2, dcell, dcell, 2, v->dim);

    if (!inside) {
        /* Long names sit around the pad, as plain text. */
        if (ll) cv_text(c, lx - 12 - cv_text_w(ll, 1), cy - 4, ll, 1, v->text);
        if (lr) cv_text(c, rx + dcell + 12, cy - 4, lr, 1, v->text);
        if (lu) cv_text(c, cx - cv_text_w(lu, 1) / 2, uy - 12, lu, 1, v->text);
        if (ld) cv_text(c, cx - cv_text_w(ld, 1) / 2, dy + dcell + 4, ld, 1, v->text);
    }

    /* SP face layout: X top, A right, B bottom, Y left. */
    const int fuy = cy - fcell / 2 - gap - fcell;
    const int fdy = cy + fcell / 2 + gap;
    hint_cell(c, fx - fcell / 2, fuy, fcell, "X",
              rg_shortcut_face_label(sh, RGFACE_X), h->face_engaged[RGFACE_X],
              v->text, v->dim, v->led);
    hint_cell(c, fx - fcell / 2 - gap - fcell, cy - fcell / 2, fcell, "Y",
              rg_shortcut_face_label(sh, RGFACE_Y), h->face_engaged[RGFACE_Y],
              v->text, v->dim, v->led);
    hint_cell(c, fx + fcell / 2 + gap, cy - fcell / 2, fcell, "A",
              rg_shortcut_face_label(sh, RGFACE_A), h->face_engaged[RGFACE_A],
              v->text, v->dim, v->led);
    hint_cell(c, fx - fcell / 2, fdy, fcell, "B",
              rg_shortcut_face_label(sh, RGFACE_B), h->face_engaged[RGFACE_B],
              v->text, v->dim, v->led);
    cv_rect(c, fx - fcell / 2, cy - fcell / 2, fcell, fcell, 0x1c1c24);
    cv_border(c, fx - fcell / 2, cy - fcell / 2, fcell, fcell, 2, v->dim);

    const char *dc = hint_dir_caption(sh);
    const char *fc = hint_face_caption(sh);
    if (dc) cv_text(c, cx - cv_text_w(dc, 1) / 2, py + ph - 46, dc, 1, v->dim);
    if (fc) cv_text(c, fx - cv_text_w(fc, 1) / 2, py + ph - 46, fc, 1, v->dim);
    cv_text(c, px + 16, py + ph - 16, hint_footnote(sh), 1, v->dim);
}

void rg_view_panel(Canvas *c, int x, int y, int w, int h,
                   Uint32 panel, Uint32 border)
{
    cv_rect(c, x, y, w, h, panel);
    cv_border(c, x, y, w, h, 2, border);
}

void rg_view_value(Canvas *c, int x, int y, int w, const char *label,
                   const char *value, int selected,
                   Uint32 text, Uint32 dim, Uint32 cursor)
{
    if (selected)
        cv_rect(c, x - 6, y - 3, w + 12, 13, cv_dim(cursor, 0.25f));
    if (label)
        cv_text(c, x, y, label, 1, selected ? cursor : dim);
    if (value) {
        int vx = x + w - cv_text_w(value, 1);
        cv_text(c, vx, y, value, 1, selected ? cursor : text);
    }
}

void rg_draw_export_confirm(const ViewCtx *v, const ExportView *ex)
{
    Canvas *c = v->cv;
    int mw = 480, mh = 176;
    int mx = (640 - mw) / 2, my = (480 - mh) / 2;
    rg_view_panel(c, mx, my, mw, mh, v->panel, v->text);

    cv_text(c, mx + 16, my + 12, "EXPORT SONG?", 1, v->text);
    cv_text(c, mx + 16, my + 34, ex->filename, 1, v->dim);

    char buf[64];
    snprintf(buf, sizeof buf, "%d BARS  -  44.1 KHZ / 16-BIT STEREO", ex->bars);
    cv_text(c, mx + 16, my + 56, buf, 1, v->dim);

    /* Muted tracks stay out of the export unless the user asks for them. */
    if (ex->muted > 0) {
        snprintf(buf, sizeof buf, ex->include_muted
                 ? "%d MUTED TRACKS INCLUDED"
                 : "%d MUTED TRACKS EXCLUDED", ex->muted);
        cv_text(c, mx + 16, my + 78, buf, 1, v->warn);
        cv_text(c, mx + 16, my + 100,
                "A:EXPORT   B:CANCEL   Y:TOGGLE MUTES", 1, v->text);
    } else {
        cv_text(c, mx + 16, my + 78, "NO MUTED TRACKS", 1, v->dim);
        cv_text(c, mx + 16, my + 100, "A:EXPORT   B:CANCEL", 1, v->text);
    }
    cv_text(c, mx + 16, my + 130,
            "THE EXPORT KEEPS EVERY EARLIER WAV", 1, v->dim);
}
