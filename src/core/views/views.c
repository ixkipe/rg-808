/* Shared view layer: shortcut labels, page chrome and the export panel. */
#include "views.h"
#include "machine.h"

#include <stdio.h>

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
