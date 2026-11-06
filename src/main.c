/* RG-808 — TR-808 clone main: app state, input, UI, persistence, CLI modes. */
#include <SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include <sys/stat.h>
#include <sys/types.h>

#include "engine.h"
#include "canvas.h"
#include "wav.h"
#include "samples.h"
#include "pattern.h"

#define SCR_W 640
#define SCR_H 480

/* pattern browser geometry */
#define PAT_ROWS   12
#define PAT_ROW_H  30
#define PAT_LIST_X 24
#define PAT_LIST_Y 92
#define PAT_LIST_W 592
#define PAT_ACT_N  6
#define MENU_N     8

/* grid geometry */
#define GRID_X0  76
#define GRID_Y0  58
#define CELL_W   30
#define CELL_H   30
#define COL_PITCH (CELL_W + 4)          /* 34 */
#define ROW_PITCH (CELL_H + 6)          /* 36 */

enum Mode { MODE_EDIT, MODE_MENU, MODE_TEMPO, MODE_KIT, MODE_RCH, MODE_SWING,
            MODE_PATTERN, MODE_PAT_ACT, MODE_RENAME };

enum Action {
    ACT_NONE, ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT,
    ACT_CONFIRM, ACT_BACK, ACT_CLEAR, ACT_HEAR, ACT_PLAY, ACT_MENU,
    ACT_TEMPO_UP, ACT_TEMPO_DOWN, ACT_QUIT, ACT_HEAR_ALT
};

/* RG35XX SP shoulder buttons. L1/R1 arrive as controller shoulder buttons;
 * L2/R2 usually arrive as the two trigger axes. */
enum { SH_L1, SH_L2, SH_R1, SH_R2, SH_COUNT };

typedef struct {
    Engine eng;
    PatternLib lib;
    int mode;
    int cur_track, cur_step;
    int menu_sel;
    int quit;
    int legacy_override;              /* RG808_CONF pointed at a pattern file */
    char conf_override[1200];         /* that file, for tooling/tests */
    int pat_sel;                      /* pattern browser cursor (1-based slot) */
    int pat_act_sel;
    int rename_slot;
    int rename_pos;
    char rename_buf[PAT_NAME_LEN + 1];
    int sh_held[SH_COUNT];            /* shoulder buttons currently down */
    int sh_last;                      /* most recent shoulder (overlay owner) */
    int sh_suppress;                  /* shortcut used: hide hint until release */
    Uint32 shortcut_until;            /* hint overlay deadline */
    char msg[48];
    Uint32 msg_until;
    int dirty;
    Uint32 last_save;
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    SDL_AudioDeviceID dev;
    Uint32 *pixels;
    Canvas cv;
} App;

static const char *DEMO[N_TRACKS] = {
    /*bd*/ "2000200020002000",
    /*sd*/ "0000100000001000",
    /*lt*/ "0000000000000000",
    /*mt*/ "0000000000000100",
    /*ht*/ "0000000000000010",
    /*ch*/ "1110111011101110",
    /*oh*/ "0001000100010001",
    /*cl*/ "0000000000000000",
    /*cp*/ "0000100000001000",
    /*cb*/ "0000001000000010",
    /*cy*/ "0000000000000000",
};

/* ------------------------------------------------------------------ util */

static void set_msg(App *a, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(a->msg, sizeof a->msg, fmt, ap);
    va_end(ap);
    a->msg_until = SDL_GetTicks() + 2500;
}

/* ------------------------------------------------------------ persistence */

static void demo_pattern(App *a)
{
    for (int t = 0; t < N_TRACKS; t++)
        for (int s = 0; s < N_STEPS; s++)
            a->eng.lvl[t][s] = (unsigned char)(DEMO[t][s] - '0');
}

/* Detect a directory (RG808_CONF may name a library dir or a legacy file). */
static int path_is_dir(const char *path)
{
#ifdef _WIN32
    struct _stat st;
    return _stat(path, &st) == 0 && (st.st_mode & _S_IFDIR) != 0;
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static void resolve_conf(App *a, const char *base)
{
    const char *env = getenv("RG808_CONF");
    char conf[1024];
    a->legacy_override = 0;
    a->conf_override[0] = 0;

    if (env && *env) {
        size_t elen = strlen(env);
        int as_dir = path_is_dir(env) ||
                     (elen > 0 && (env[elen - 1] == '/' || env[elen - 1] == '\\'));
        if (as_dir) {
            snprintf(conf, sizeof conf, "%s", env);
        } else {
            /* Back-compat for tooling/tests: RG808_CONF=<pattern file> keeps
             * using that exact file as the active pattern. Its parent folder
             * still hosts a normal library for the browser. */
            snprintf(a->conf_override, sizeof a->conf_override, "%s", env);
            a->legacy_override = 1;
            snprintf(conf, sizeof conf, "%s", env);
            char *slash = strrchr(conf, '/');
            char *bslash = strrchr(conf, '\\');
            if (!slash || (bslash && bslash > slash)) slash = bslash;
            if (slash) *slash = 0;
            else snprintf(conf, sizeof conf, ".");
        }
    } else {
        snprintf(conf, sizeof conf, "%sconf", (base && *base) ? base : "");
    }

    /* A legacy file override is an explicit single-pattern sandbox: never
     * migrate it and never treat its parent as a fresh v1 install. */
    pattern_init(&a->lib, conf, !a->legacy_override);

    /* Read-only card / failed migration: keep using the legacy file rather
     * than falling back to the demo. */
    if (!a->legacy_override) {
        int any = 0;
        for (int s = 0; s < PAT_MAX_SLOTS; s++)
            if (a->lib.exists[s]) { any = 1; break; }
        if (!any && !pattern_file_exists(a->lib.index_path)) {
            char legacy[1200];
            snprintf(legacy, sizeof legacy, "%s/pattern.txt", a->lib.conf_dir);
            if (pattern_file_exists(legacy)) {
                a->legacy_override = 1;
                snprintf(a->conf_override, sizeof a->conf_override, "%s", legacy);
            }
        }
    }
}

static int save_current(App *a)
{
    if (a->legacy_override)
        return pattern_save_file(&a->eng, a->conf_override);
    return pattern_save_slot(&a->lib, &a->eng, a->lib.active_slot);
}

static int load_current(App *a)
{
    if (a->legacy_override)
        return pattern_load_file(&a->eng, a->conf_override);
    return pattern_load_slot(&a->lib, &a->eng, a->lib.active_slot);
}

static int load_slot_into_editor(App *a, int slot)
{
    if (!pattern_slot_exists(&a->lib, slot)) return -1;
    /* Never drop the last few seconds of edits before autosave had a chance
     * to run, even when reloading the active slot. */
    if (a->dirty) {
        if (save_current(a) != 0) return -3;
        a->dirty = 0;
    }
    if (pattern_load_slot(&a->lib, &a->eng, slot) != 0) return -2;
    a->lib.active_slot = slot;
    pattern_set_active(&a->lib, slot);
    a->dirty = 0;
    return 0;
}

/* ------------------------------------------------------------------- ui */

static const Uint32 C_BG        = 0x17171f;
static const Uint32 C_PANEL     = 0x24242e;
static const Uint32 C_TEXT      = 0xe8d8a8;
static const Uint32 C_TEXT_DIM  = 0x8a8570;
static const Uint32 C_CURSOR    = 0xffffff;
static const Uint32 C_LED       = 0xff5040;
static const Uint32 C_ACCENT    = 0xffffff;

static const char RENAME_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_.";

static const char *PAT_ACT[PAT_ACT_N] = {
    "LOAD SLOT", "SAVE HERE", "COPY ACTIVE HERE", "RENAME", "DELETE", "CANCEL"
};

static const char *SH_NAMES[SH_COUNT] = { "L1", "L2", "R1", "R2" };

/* Shortcut shown for one shoulder + D-pad direction (NULL = reserved). */
static const char *shortcut_label(int sh, int ad)
{
    switch (sh) {
    case SH_L1:
        if (ad == ACT_LEFT) return "TEMPO";
        if (ad == ACT_RIGHT) return "SWING";
        break;
    case SH_L2:
        if (ad == ACT_LEFT) return "PATTERN LIBRARY";
        break;
    case SH_R1:
        if (ad == ACT_LEFT) return "SAMPLE KIT";
        if (ad == ACT_RIGHT) return "RATCHET";
        break;
    default:
        break;
    }
    return NULL;
}

static void draw_pad_cell(Canvas *c, int x, int y, int size, const char *sym, int active)
{
    cv_rect(c, x, y, size, size, active ? cv_dim(C_TEXT, 0.30f) : 0x1c1c24);
    cv_border(c, x, y, size, size, 2, active ? C_TEXT : C_TEXT_DIM);
    cv_text(c, x + (size - 8) / 2, y + (size - 8) / 2, sym, 1,
            active ? C_TEXT : C_TEXT_DIM);
}

/* Transient D-pad-shaped hint shown while a shoulder button is down (and for
 * a moment after release). */
static void draw_shortcut_overlay(App *a)
{
    Uint32 now = SDL_GetTicks();
    if (!a->shortcut_until || (int)(now - a->shortcut_until) >= 0) return;
    int sh = a->sh_last;
    if (sh < 0 || sh >= SH_COUNT) return;

    Canvas *c = &a->cv;
    int px = 20, py = 110, pw = 600, ph = 250;
    cv_rect(c, px, py, pw, ph, C_PANEL);
    cv_border(c, px, py, pw, ph, 2, C_TEXT);

    char buf[80];
    snprintf(buf, sizeof buf, "%s + D-PAD", SH_NAMES[sh]);
    cv_text(c, px + 16, py + 10, buf, 1, C_TEXT);
    cv_text(c, px + 16, py + 26,
            sh == SH_R2 ? "NO SHORTCUTS ASSIGNED YET"
                        : "PRESS A DIRECTION TO OPEN ITS MENU",
            1, C_TEXT_DIM);

    const int cell = 42, gap = 6;
    int cx = px + 210, cy = py + 140;
    int lx = cx - cell / 2 - gap - cell;
    int rx = cx + cell / 2 + gap;
    int uy = cy - cell / 2 - gap - cell;
    int dy = cy + cell / 2 + gap;

    const char *ll = shortcut_label(sh, ACT_LEFT);
    const char *rl = shortcut_label(sh, ACT_RIGHT);
    const char *ul = shortcut_label(sh, ACT_UP);
    const char *dl = shortcut_label(sh, ACT_DOWN);

    draw_pad_cell(c, cx - cell / 2, uy, cell, "^", ul != NULL);
    draw_pad_cell(c, lx, cy - cell / 2, cell, "<", ll != NULL);
    draw_pad_cell(c, rx, cy - cell / 2, cell, ">", rl != NULL);
    draw_pad_cell(c, cx - cell / 2, dy, cell, "v", dl != NULL);
    cv_rect(c, cx - cell / 2, cy - cell / 2, cell, cell, 0x1c1c24);
    cv_border(c, cx - cell / 2, cy - cell / 2, cell, cell, 2, C_TEXT_DIM);

    if (ll) {
        cv_text(c, lx - 12 - cv_text_w(ll, 1), cy - 4, ll, 1, C_TEXT);
    } else {
        cv_text(c, lx - 20, cy - 4, "-", 1, C_TEXT_DIM);
    }
    if (rl) {
        cv_text(c, rx + cell + 12, cy - 4, rl, 1, C_TEXT);
    } else {
        cv_text(c, rx + cell + 12, cy - 4, "-", 1, C_TEXT_DIM);
    }
    if (ul) {
        cv_text(c, cx - cv_text_w(ul, 1) / 2, uy - 14, ul, 1, C_TEXT);
    } else {
        cv_text(c, cx - 4, uy - 14, "-", 1, C_TEXT_DIM);
    }
    if (dl) {
        cv_text(c, cx - cv_text_w(dl, 1) / 2, dy + cell + 6, dl, 1, C_TEXT);
    } else {
        cv_text(c, cx - 4, dy + cell + 6, "-", 1, C_TEXT_DIM);
    }

    cv_text(c, px + 16, py + ph - 18,
            "OTHER COMBINATIONS ARE RESERVED FOR FUTURE FEATURES", 1, C_TEXT_DIM);
}

static void draw_pattern_list(App *a, int with_actions)
{
    Canvas *c = &a->cv;
    int px = 16, py = 26, pw = 608, ph = 430;
    cv_rect(c, px, py, pw, ph, C_PANEL);
    cv_border(c, px, py, pw, ph, 2, C_TEXT);
    cv_text(c, px + 12, py + 10, "PATTERN LIBRARY", 1, C_TEXT);

    int page = (a->pat_sel - 1) / PAT_ROWS;
    int first = page * PAT_ROWS + 1;
    int last = first + PAT_ROWS - 1;
    if (last > PAT_MAX_SLOTS) last = PAT_MAX_SLOTS;
    char buf[64];
    snprintf(buf, sizeof buf, "%03d-%03d / %d", first, last, PAT_MAX_SLOTS);
    cv_text(c, px + pw - cv_text_w(buf, 1) - 12, py + 10, buf, 1, C_TEXT_DIM);

    for (int i = 0; i < PAT_ROWS; i++) {
        int slot = first + i;
        if (slot > PAT_MAX_SLOTS) break;
        int ry = PAT_LIST_Y + i * PAT_ROW_H;
        int sel = (slot == a->pat_sel);
        int active = (slot == a->lib.active_slot);
        int has = pattern_slot_exists(&a->lib, slot);
        const char *nm = pattern_name(&a->lib, slot);

        if (sel)
            cv_rect(c, PAT_LIST_X - 8, ry - 4, PAT_LIST_W, 26, cv_dim(C_TEXT, 0.22f));
        if (active)
            cv_rect(c, PAT_LIST_X - 8, ry - 4, 4, 26, C_LED);

        snprintf(buf, sizeof buf, "P%03d", slot);
        cv_text(c, PAT_LIST_X, ry, buf, 1, has ? C_TEXT : C_TEXT_DIM);
        cv_text(c, PAT_LIST_X + 56, ry, nm, 1,
                has ? (sel ? C_CURSOR : C_TEXT) : C_TEXT_DIM);
        if (!has)
            cv_text(c, PAT_LIST_X + 56 + cv_text_w(nm, 1) + 8, ry, "- EMPTY", 1,
                    cv_dim(C_TEXT_DIM, 0.65f));
        if (active)
            cv_text(c, PAT_LIST_X + PAT_LIST_W - 64, ry, "ACTIVE", 1, C_LED);
        if (sel)
            cv_border(c, PAT_LIST_X - 8, ry - 4, PAT_LIST_W, 26, 1, C_CURSOR);
    }

    if (with_actions) {
        int mw = 280, mh = PAT_ACT_N * 18 + 30;
        int mx = (SCR_W - mw) / 2, my = (SCR_H - mh) / 2;
        cv_rect(c, mx, my, mw, mh, C_PANEL);
        cv_border(c, mx, my, mw, mh, 2, C_TEXT);
        snprintf(buf, sizeof buf, "P%03d %s", a->pat_sel,
                 pattern_name(&a->lib, a->pat_sel));
        cv_text(c, mx + 10, my + 8, buf, 1, C_TEXT);
        for (int i = 0; i < PAT_ACT_N; i++) {
            int iy = my + 26 + i * 18;
            if (i == a->pat_act_sel)
                cv_rect(c, mx + 4, iy - 2, mw - 8, 15, cv_dim(C_TEXT, 0.30f));
            cv_text(c, mx + 14, iy, PAT_ACT[i], 1,
                    i == a->pat_act_sel ? C_CURSOR : C_TEXT_DIM);
        }
    }
}

static void draw_rename(App *a)
{
    Canvas *c = &a->cv;
    int mw = 480, mh = 126;
    int mx = (SCR_W - mw) / 2, my = (SCR_H - mh) / 2;
    cv_rect(c, mx, my, mw, mh, C_PANEL);
    cv_border(c, mx, my, mw, mh, 2, C_TEXT);

    char buf[64];
    snprintf(buf, sizeof buf, "RENAME SLOT P%03d", a->rename_slot);
    cv_text(c, mx + 14, my + 10, buf, 1, C_TEXT);

    int cx = mx + 40, cy = my + 34, cw = 24;
    for (int i = 0; i < PAT_NAME_LEN; i++) {
        int x = cx + i * cw;
        cv_rect(c, x, cy, cw - 2, 28, i == a->rename_pos ? cv_dim(C_TEXT, 0.30f) : 0x1c1c24);
        char ch[2] = { a->rename_buf[i] ? a->rename_buf[i] : ' ', 0 };
        cv_text(c, x + 7, cy + 10, ch, 1, i == a->rename_pos ? C_CURSOR : C_TEXT);
    }
    cv_border(c, cx + a->rename_pos * cw, cy, cw - 2, 28, 1, C_CURSOR);
    cv_text(c, mx + 14, my + 88,
            "UD:CHAR  LR:MOVE  A:NEXT  X:DEL  SELECT:SAVE  B:CANCEL", 1, C_TEXT_DIM);
}

static void draw_ui(App *a)
{
    Canvas *c = &a->cv;
    Engine *e = &a->eng;
    cv_fill(c, C_BG);

    /* header */
    cv_text(c, 10, 8, "RG-808", 2, C_TEXT);
    char tbuf[40];
    const char *st = e->playing ? "PLAYING" : "STOPPED";
    cv_text(c, SCR_W - cv_text_w(st, 1) - 10, 10, st, 1, e->playing ? C_LED : C_TEXT_DIM);
    cv_text(c, SCR_W - 230, 10, "TEMPO", 1, C_TEXT_DIM);
    snprintf(tbuf, sizeof tbuf, "%6.2f", e->bpm_x100 / 100.0);
    cv_text(c, SCR_W - 170, 10, tbuf, 1, C_TEXT);
    if (e->swing_pct > 0) {
        snprintf(tbuf, sizeof tbuf, "%d%% SWING", e->swing_pct);
        cv_text(c, SCR_W - 360, 10, tbuf, 1, C_TEXT_DIM);
    }
    /* active pattern slot + name (M7) */
    snprintf(tbuf, sizeof tbuf, "P%03d", a->lib.active_slot);
    cv_text(c, 112, 10, tbuf, 1, C_TEXT);
    cv_text(c, 148, 10, pattern_name(&a->lib, a->lib.active_slot), 1, C_TEXT_DIM);

    /* step LED + beat numbers */
    int led = e->cur_step;
    for (int s = 0; s < N_STEPS; s++) {
        int x = GRID_X0 + s * COL_PITCH;
        if (s % 4 == 0) {
            char d[2] = { (char)('1' + s / 4), 0 };
            cv_text(c, x + CELL_W - 8, 40, d, 1, C_TEXT_DIM);
        }
        Uint32 col = (s == led) ? C_LED : ((s % 4 == 0) ? cv_dim(C_LED, 0.35f) : 0x33333b);
        cv_rect(c, x, 50, CELL_W, 4, col);
    }

    /* grid background bands */
    for (int t = 0; t < N_TRACKS; t++) {
        int y = GRID_Y0 + t * ROW_PITCH;
        cv_rect(c, 0, y - 3, GRID_X0 - 6, CELL_H + 6, (a->mode == MODE_EDIT && a->cur_track == t) ? C_PANEL : 0x1c1c24);
    }

    /* track labels */
    for (int t = 0; t < N_TRACKS; t++) {
        int y = GRID_Y0 + t * ROW_PITCH + (CELL_H - 8) / 2;
        int on_row = (a->mode == MODE_EDIT || a->mode == MODE_KIT || a->mode == MODE_RCH) && a->cur_track == t;
        cv_rect(c, 8, GRID_Y0 + t * ROW_PITCH, 4, CELL_H, TRACKS[t].color);
        cv_text(c, 22, y, TRACKS[t].label, 1, on_row ? TRACKS[t].color : C_TEXT_DIM);
        if (variant_count(t) > 1) {                  /* current variant number */
            snprintf(tbuf, sizeof tbuf, "%d", e->kit[t] + 1);
            cv_text(c, 46, y, tbuf, 1, C_TEXT_DIM);
        }
    }

    /* cells (edit) / variant picker (kit) / ratchet editor (rch) */
    static const char RCH_CH[RCH_COUNT] = { ' ', '2', '3', '4', 'F' };
    if (a->mode == MODE_KIT) {
        for (int t = 0; t < N_TRACKS; t++) {
            Uint32 col = TRACKS[t].color;
            int cnt = variant_count(t);
            for (int v = 0; v < cnt; v++) {
                int x = GRID_X0 + v * COL_PITCH;
                int y = GRID_Y0 + t * ROW_PITCH;
                int sel = (e->kit[t] == v);
                cv_rect(c, x, y, CELL_W, CELL_H, sel ? col : cv_dim(col, 0.20f));
                if (sel)
                    cv_border(c, x + 5, y + 5, CELL_W - 10, CELL_H - 10, 2, C_ACCENT);
                if (t == a->cur_track) {              /* 1-based position in box */
                    snprintf(tbuf, sizeof tbuf, "%d", v + 1);
                    cv_text(c, x + (CELL_W - 8) / 2, y + (CELL_H - 8) / 2, tbuf, 1,
                            sel ? 0x000000 : C_TEXT_DIM);
                }
            }
        }
    } else if (a->mode == MODE_RCH) {
        for (int t = 0; t < N_TRACKS; t++) {
            Uint32 col = TRACKS[t].color;
            for (int s = 0; s < N_STEPS; s++) {
                int x = GRID_X0 + s * COL_PITCH;
                int y = GRID_Y0 + t * ROW_PITCH;
                unsigned char lv = e->lvl[t][s];
                unsigned char r = e->rch[t][s];
                Uint32 fill = lv ? (r ? cv_dim(col, 0.55f) : cv_dim(col, 0.25f))
                                 : cv_dim(col, 0.10f);
                cv_rect(c, x, y, CELL_W, CELL_H, fill);
                if (r) {
                    char dch[2] = { RCH_CH[r], 0 };
                    cv_text(c, x + (CELL_W - 8) / 2, y + (CELL_H - 8) / 2, dch, 1,
                            lv ? col : C_TEXT_DIM);
                }
                if (s == led && e->playing)
                    cv_border(c, x, y, CELL_W, CELL_H, 2, C_LED);
            }
        }
    } else {
    for (int t = 0; t < N_TRACKS; t++) {
        Uint32 col = TRACKS[t].color;
        for (int s = 0; s < N_STEPS; s++) {
            int x = GRID_X0 + s * COL_PITCH;
            int y = GRID_Y0 + t * ROW_PITCH;
            unsigned char lv = e->lvl[t][s];
            unsigned char r = e->rch[t][s];
            Uint32 fill = lv == LVL_OFF ? cv_dim(col, 0.20f)
                        : lv == LVL_ON  ? cv_dim(col, 0.65f)
                                        : col;
            cv_rect(c, x, y, CELL_W, CELL_H, fill);
            if (lv == LVL_ACC)
                cv_border(c, x + 5, y + 5, CELL_W - 10, CELL_H - 10, 2, C_ACCENT);
            if (lv > LVL_OFF && r > RCH_OFF) {
                char dch[2] = { RCH_CH[r], 0 };
                cv_text(c, x + (CELL_W - 8) / 2, y + CELL_H - 10, dch, 1,
                        lv == LVL_ACC ? 0x000000u : C_TEXT);
            }
            /* playing column highlight */
            if (s == led && e->playing)
                cv_border(c, x, y, CELL_W, CELL_H, 2, C_LED);
        }
    }
    }

    /* cursor */
    if (a->mode == MODE_EDIT || a->mode == MODE_KIT || a->mode == MODE_RCH) {
        int x = GRID_X0 + a->cur_step * COL_PITCH;
        int y = GRID_Y0 + a->cur_track * ROW_PITCH;
        cv_border(c, x - 2, y - 2, CELL_W + 4, CELL_H + 4, 2, C_CURSOR);
    }

    /* pattern browser overlays (M7) */
    if (a->mode == MODE_PATTERN) {
        draw_pattern_list(a, 0);
    } else if (a->mode == MODE_PAT_ACT) {
        draw_pattern_list(a, 1);
    } else if (a->mode == MODE_RENAME) {
        draw_pattern_list(a, 0);
        draw_rename(a);
    }

    /* status / menu / hints */
    Uint32 now = SDL_GetTicks();
    if (a->mode == MODE_KIT) {
        snprintf(tbuf, sizeof tbuf, "KIT: %s = %02d/%02d  %s", TRACKS[a->cur_track].label,
                 e->kit[a->cur_track] + 1, variant_count(a->cur_track), TRACKS[a->cur_track].name);
        cv_text(c, 10, 10 + 18, tbuf, 1, C_TEXT_DIM);
    }
    if (a->msg[0] && (int)(now - a->msg_until) < 0)
        cv_text(c, 10, SCR_H - 14, a->msg, 1, C_TEXT);
    else if (a->mode == MODE_KIT)
        cv_text(c, 10, SCR_H - 14, "A:SELECT+HEAR  X:HEAR  B:BACK  START:PLAY", 1, C_TEXT_DIM);
    else if (a->mode == MODE_RCH)
        cv_text(c, 10, SCR_H - 14, "A:ROLL 2/3/4/F  X:HEAR  B:CLEAR ROLL  SELECT:BACK", 1, C_TEXT_DIM);
    else if (a->mode == MODE_PATTERN)
        cv_text(c, 10, SCR_H - 14, "A:LOAD  SELECT:ACTIONS  DPAD-LR:PAGE  B:BACK  START:PLAY", 1, C_TEXT_DIM);
    else if (a->mode == MODE_PAT_ACT)
        cv_text(c, 10, SCR_H - 14, "UD:ACTION  LR:SLOT  A:SELECT  B:CANCEL", 1, C_TEXT_DIM);
    else if (a->mode == MODE_RENAME)
        cv_text(c, 10, SCR_H - 14, "UD:CHAR  LR:MOVE  A:NEXT  X:DEL  SELECT:SAVE  B:CANCEL", 1, C_TEXT_DIM);
    else
        cv_text(c, 10, SCR_H - 14, "A:STEP  B:CLEAR  X:HEAR  START:PLAY  SELECT:MENU  HOLD A SHOULDER", 1, C_TEXT_DIM);

    if (a->mode == MODE_MENU) {
        static const char *items[MENU_N] = {
            "EDIT TEMPO", "EDIT SWING", "RATCHET NOTES", "SELECT KITS",
            "SAVE PATTERN", "PATTERN LIBRARY", "CLEAR ALL", "QUIT"
        };
        int n = MENU_N;
        int mw = 240, mh = 16 * n + 34;
        int mx = (SCR_W - mw) / 2, my = (SCR_H - mh) / 2;
        cv_rect(c, mx, my, mw, mh, C_PANEL);
        cv_border(c, mx, my, mw, mh, 2, C_TEXT);
        cv_text(c, mx + 8, my + 8, "MENU", 1, C_TEXT);
        for (int i = 0; i < n; i++) {
            int iy = my + 30 + i * 16;
            if (i == a->menu_sel)
                cv_rect(c, mx + 4, iy - 2, mw - 8, 14, cv_dim(C_TEXT, 0.30f));
            cv_text(c, mx + 12, iy, items[i], 1, i == a->menu_sel ? C_CURSOR : C_TEXT_DIM);
        }
    } else if (a->mode == MODE_TEMPO) {
        int mw = 300, mh = 80;
        int mx = (SCR_W - mw) / 2, my = (SCR_H - mh) / 2;
        cv_rect(c, mx, my, mw, mh, C_PANEL);
        cv_border(c, mx, my, mw, mh, 2, C_TEXT);
        cv_text(c, mx + 12, my + 10, "TEMPO", 1, C_TEXT);
        snprintf(tbuf, sizeof tbuf, "%.2f BPM", e->bpm_x100 / 100.0);
        cv_text(c, mx + 12, my + 34, tbuf, 2, C_TEXT);
        cv_text(c, mx + 12, my + 62, "UD:+-5  LR:+-0.5  A/B:OK", 1, C_TEXT_DIM);
    } else if (a->mode == MODE_SWING) {
        int mw = 300, mh = 80;
        int mx = (SCR_W - mw) / 2, my = (SCR_H - mh) / 2;
        cv_rect(c, mx, my, mw, mh, C_PANEL);
        cv_border(c, mx, my, mw, mh, 2, C_TEXT);
        cv_text(c, mx + 12, my + 10, "SWING / SHUFFLE", 1, C_TEXT);
        snprintf(tbuf, sizeof tbuf, "%d%%", e->swing_pct);
        cv_text(c, mx + 12, my + 34, tbuf, 2, C_TEXT);
        cv_text(c, mx + 12, my + 62, "UD:+-1  LR:+-5  A/B:OK", 1, C_TEXT_DIM);
    }

    draw_shortcut_overlay(a);
}

/* ---------------------------------------------------------------- render */

static int render_init(App *a)
{
    Uint32 flags = SDL_WINDOW_SHOWN;
    if (getenv("RG808_FULLSCREEN")) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    a->win = SDL_CreateWindow("RG-808", 0, 0, SCR_W, SCR_H, flags);
    if (!a->win) { SDL_Log("window: %s", SDL_GetError()); return -1; }
    a->ren = SDL_CreateRenderer(a->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!a->ren) a->ren = SDL_CreateRenderer(a->win, -1, SDL_RENDERER_SOFTWARE);
    if (!a->ren) { SDL_Log("renderer: %s", SDL_GetError()); return -1; }
    a->pixels = SDL_malloc((size_t)SCR_W * SCR_H * 4);
    a->tex = SDL_CreateTexture(a->ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCR_W, SCR_H);
    if (!a->tex) { SDL_Log("texture: %s", SDL_GetError()); return -1; }
    a->cv.px = a->pixels; a->cv.w = SCR_W; a->cv.h = SCR_H; a->cv.pitch = SCR_W;
    return 0;
}

static void present(App *a)
{
    SDL_UpdateTexture(a->tex, NULL, a->pixels, SCR_W * 4);
    SDL_RenderClear(a->ren);
    SDL_RenderCopy(a->ren, a->tex, NULL, NULL);
    SDL_RenderPresent(a->ren);
}

/* ----------------------------------------------------------------- audio */

static void SDLCALL audio_cb(void *ud, Uint8 *stream, int len)
{
    App *a = (App *)ud;
    static float buf[4096 * 2];
    Sint16 *out = (Sint16 *)stream;
    int remaining = len / 4;                        /* S16 stereo frames */
    while (remaining > 0) {
        int frames = remaining > 4096 ? 4096 : remaining;
        if (a->eng.restart) { a->eng.restart = 0; engine_restart(&a->eng); }
        engine_mix(&a->eng, buf, frames);
        for (int i = 0; i < 2 * frames; i++) {
            float x = buf[i];
            *out++ = (Sint16)(x * 32767.0f);
        }
        remaining -= frames;
    }
}

/* ----------------------------------------------------------------- input */

static void do_menu(App *a, int item)
{
    switch (item) {
    case 0: a->mode = MODE_TEMPO; break;
    case 1: a->mode = MODE_SWING; break;
    case 2: a->mode = MODE_RCH; break;
    case 3:
        a->mode = MODE_KIT;
        a->cur_step = a->eng.kit[a->cur_track];   /* land cursor on current pick */
        break;
    case 4:
        if (save_current(a) == 0) set_msg(a, "SAVED P%03d", a->lib.active_slot);
        else set_msg(a, "SAVE FAILED");
        a->mode = MODE_EDIT;
        break;
    case 5:
        a->mode = MODE_PATTERN;
        a->pat_sel = a->lib.active_slot;
        break;
    case 6:
        for (int t = 0; t < N_TRACKS; t++)
            for (int s = 0; s < N_STEPS; s++) {
                a->eng.lvl[t][s] = 0;
                a->eng.rch[t][s] = 0;
            }
        a->dirty = 1;
        set_msg(a, "PATTERN CLEARED");
        a->mode = MODE_EDIT;
        break;
    case 7: a->quit = 1; break;
    }
}

static void pattern_begin_rename(App *a, int slot)
{
    a->rename_slot = slot;
    a->rename_pos = 0;
    snprintf(a->rename_buf, sizeof a->rename_buf, "%-*s", PAT_NAME_LEN,
             pattern_name(&a->lib, slot));
}

static void pattern_rename_cycle(App *a, int dir)
{
    int n = (int)strlen(RENAME_CHARS);
    char ch = a->rename_buf[a->rename_pos];
    const char *p = ch ? strchr(RENAME_CHARS, ch) : NULL;
    int idx = p ? (int)(p - RENAME_CHARS) : 0;
    idx = (idx + dir + n) % n;
    a->rename_buf[a->rename_pos] = RENAME_CHARS[idx];
}

static void pattern_commit_rename(App *a)
{
    if (pattern_rename_slot(&a->lib, a->rename_slot, a->rename_buf) == 0)
        set_msg(a, "RENAMED P%03d", a->rename_slot);
    else
        set_msg(a, "RENAME FAILED");
    a->mode = MODE_PATTERN;
    a->pat_sel = a->rename_slot;
}

static void pattern_do_action(App *a, int item)
{
    int slot = a->pat_sel;
    switch (item) {
    case 0: {                                    /* load selected slot */
        int rc = load_slot_into_editor(a, slot);
        if (rc == 0)
            set_msg(a, "LOADED P%03d %s", slot, pattern_name(&a->lib, slot));
        else if (rc == -1)
            set_msg(a, "P%03d EMPTY", slot);
        else if (rc == -3)
            set_msg(a, "SAVE FAILED");
        else
            set_msg(a, "LOAD FAILED");
        a->mode = MODE_PATTERN;
        break;
    }
    case 1:                                      /* overwrite slot with editor */
        if (pattern_save_slot(&a->lib, &a->eng, slot) == 0) {
            a->lib.active_slot = slot;
            pattern_set_active(&a->lib, slot);
            a->dirty = 0;
            set_msg(a, "SAVED P%03d %s", slot, pattern_name(&a->lib, slot));
        } else {
            set_msg(a, "SAVE FAILED");
        }
        a->mode = MODE_PATTERN;
        break;
    case 2:                                      /* copy active slot here */
        if (a->dirty && save_current(a) != 0) {
            set_msg(a, "SAVE FAILED");
        } else if (pattern_copy_slot(&a->lib, a->lib.active_slot, slot) == 0) {
            a->dirty = 0;
            set_msg(a, "COPIED P%03d -> P%03d", a->lib.active_slot, slot);
        } else {
            set_msg(a, "COPY FAILED");
        }
        a->mode = MODE_PATTERN;
        break;
    case 3:                                      /* rename */
        pattern_begin_rename(a, slot);
        a->mode = MODE_RENAME;
        break;
    case 4:                                      /* delete */
        if (pattern_delete_slot(&a->lib, slot) == 0) {
            if (slot == a->lib.active_slot) a->dirty = 0;
            set_msg(a, "DELETED P%03d", slot);
        } else {
            set_msg(a, "P%03d EMPTY", slot);
        }
        a->mode = MODE_PATTERN;
        break;
    default:
        a->mode = MODE_PATTERN;
        break;
    }
}

static void act(App *a, int act)
{
    Engine *e = &a->eng;
    switch (a->mode) {
    case MODE_EDIT:
        switch (act) {
        case ACT_UP:    a->cur_track = (a->cur_track + N_TRACKS - 1) % N_TRACKS; break;
        case ACT_DOWN:  a->cur_track = (a->cur_track + 1) % N_TRACKS; break;
        case ACT_LEFT:  a->cur_step = (a->cur_step + N_STEPS - 1) % N_STEPS; break;
        case ACT_RIGHT: a->cur_step = (a->cur_step + 1) % N_STEPS; break;
        case ACT_CONFIRM: {
            unsigned char lv = (unsigned char)((e->lvl[a->cur_track][a->cur_step] + 1) % 3);
            e->lvl[a->cur_track][a->cur_step] = lv;
            a->dirty = 1;
            if (lv) engine_preview(e, a->cur_track, e->kit[a->cur_track],
                                   e->rch[a->cur_track][a->cur_step], lv);
            break;
        }
        case ACT_CLEAR: e->lvl[a->cur_track][a->cur_step] = LVL_OFF; a->dirty = 1; break;
        case ACT_HEAR:
        case ACT_HEAR_ALT:
            engine_preview(e, a->cur_track, e->kit[a->cur_track],
                           e->rch[a->cur_track][a->cur_step], LVL_ON);
            break;
        case ACT_PLAY:
            e->playing ^= 1;
            if (e->playing) e->restart = 1;   /* consumed in audio cb */
            break;
        case ACT_MENU: a->mode = MODE_MENU; a->menu_sel = 0; break;
        case ACT_TEMPO_UP:   e->bpm_x100 += 500; if (e->bpm_x100 > 25000) e->bpm_x100 = 25000; a->dirty = 1; break;
        case ACT_TEMPO_DOWN: e->bpm_x100 -= 500; if (e->bpm_x100 < 400)  e->bpm_x100 = 400;  a->dirty = 1; break;
        default: break;
        }
        break;
    case MODE_MENU:
        switch (act) {
        case ACT_UP:    a->menu_sel = (a->menu_sel + MENU_N - 1) % MENU_N; break;
        case ACT_DOWN:  a->menu_sel = (a->menu_sel + 1) % MENU_N; break;
        case ACT_CONFIRM: do_menu(a, a->menu_sel); break;
        case ACT_BACK:
        case ACT_MENU:  a->mode = MODE_EDIT; break;
        default: break;
        }
        break;
    case MODE_KIT: {
        int t = a->cur_track;
        int cnt = variant_count(t);
        switch (act) {
        case ACT_UP:
            a->cur_track = (t + N_TRACKS - 1) % N_TRACKS;
            a->cur_step = e->kit[a->cur_track];
            break;
        case ACT_DOWN:
            a->cur_track = (t + 1) % N_TRACKS;
            a->cur_step = e->kit[a->cur_track];
            break;
        case ACT_LEFT:
            if (cnt > 0) a->cur_step = (a->cur_step + cnt - 1) % cnt;
            break;
        case ACT_RIGHT:
            if (cnt > 0) a->cur_step = (a->cur_step + 1) % cnt;
            break;
        case ACT_CONFIRM:
            if (cnt > 0) { e->kit[t] = a->cur_step; a->dirty = 1; }
            engine_preview(e, t, a->cur_step, RCH_OFF, LVL_ACC);
            break;
        case ACT_HEAR:
        case ACT_HEAR_ALT:
            engine_preview(e, t, a->cur_step, RCH_OFF, LVL_ACC);
            break;
        case ACT_PLAY:
            e->playing ^= 1;
            if (e->playing) e->restart = 1;
            break;
        case ACT_BACK:
        case ACT_MENU: a->mode = MODE_EDIT; break;
        default: break;
        }
        break;
    }
    case MODE_TEMPO: {
        Engine *en = &a->eng;
        switch (act) {
        case ACT_TEMPO_UP:   en->bpm_x100 += 500; break;
        case ACT_TEMPO_DOWN: en->bpm_x100 -= 500; break;
        case ACT_RIGHT:      en->bpm_x100 += 50;  break;
        case ACT_LEFT:       en->bpm_x100 -= 50;  break;
        case ACT_UP:         en->bpm_x100 += 500; break;
        case ACT_DOWN:       en->bpm_x100 -= 500; break;
        default: break;
        }
        if (act != ACT_CONFIRM && act != ACT_BACK && act != ACT_MENU && act != ACT_HEAR) a->dirty = 1;
        if (en->bpm_x100 < 400) en->bpm_x100 = 400;
        if (en->bpm_x100 > 25000) en->bpm_x100 = 25000;
        if (act == ACT_CONFIRM || act == ACT_BACK || act == ACT_MENU) a->mode = MODE_EDIT;
        break;
    }
    case MODE_SWING: {
        Engine *en = &a->eng;
        switch (act) {
        case ACT_TEMPO_UP:   en->swing_pct += 10; break;
        case ACT_TEMPO_DOWN: en->swing_pct -= 10; break;
        case ACT_RIGHT:      en->swing_pct += 5;  break;
        case ACT_LEFT:       en->swing_pct -= 5;  break;
        case ACT_UP:         en->swing_pct += 1;  break;
        case ACT_DOWN:       en->swing_pct -= 1;  break;
        default: break;
        }
        if (act != ACT_CONFIRM && act != ACT_BACK && act != ACT_MENU && act != ACT_HEAR) {
            if (en->swing_pct < 0) en->swing_pct = 0;
            if (en->swing_pct > 100) en->swing_pct = 100;
            a->dirty = 1;
        }
        if (act == ACT_CONFIRM || act == ACT_BACK || act == ACT_MENU) a->mode = MODE_EDIT;
        break;
    }
    case MODE_RCH: {
        int t = a->cur_track;
        switch (act) {
        case ACT_UP:    a->cur_track = (t + N_TRACKS - 1) % N_TRACKS; break;
        case ACT_DOWN:  a->cur_track = (t + 1) % N_TRACKS; break;
        case ACT_LEFT:  a->cur_step = (a->cur_step + N_STEPS - 1) % N_STEPS; break;
        case ACT_RIGHT: a->cur_step = (a->cur_step + 1) % N_STEPS; break;
        case ACT_CONFIRM: {
            unsigned char r = (unsigned char)((e->rch[t][a->cur_step] + 1) % RCH_COUNT);
            e->rch[t][a->cur_step] = r;
            a->dirty = 1;
            engine_preview(e, t, e->kit[t], r,
                           e->lvl[t][a->cur_step] ? e->lvl[t][a->cur_step] : LVL_ON);
            break;
        }
        case ACT_CLEAR:
            if (e->rch[t][a->cur_step]) { e->rch[t][a->cur_step] = RCH_OFF; a->dirty = 1; }
            break;
        case ACT_HEAR:
        case ACT_HEAR_ALT:
            engine_preview(e, t, e->kit[t], e->rch[t][a->cur_step],
                           e->lvl[t][a->cur_step] ? e->lvl[t][a->cur_step] : LVL_ON);
            break;
        case ACT_PLAY:
            e->playing ^= 1;
            if (e->playing) e->restart = 1;
            break;
        case ACT_MENU: a->mode = MODE_EDIT; break;   /* SELECT backs out of the grid modes */
        case ACT_BACK: a->mode = MODE_EDIT; break;
        default: break;
        }
        break;
    }
    case MODE_PATTERN:
        switch (act) {
        case ACT_UP:
            a->pat_sel = (a->pat_sel <= 1) ? PAT_MAX_SLOTS : a->pat_sel - 1;
            break;
        case ACT_DOWN:
            a->pat_sel = (a->pat_sel >= PAT_MAX_SLOTS) ? 1 : a->pat_sel + 1;
            break;
        case ACT_LEFT:
        case ACT_TEMPO_DOWN:
            a->pat_sel -= PAT_ROWS;
            if (a->pat_sel < 1) a->pat_sel = 1;
            break;
        case ACT_RIGHT:
        case ACT_TEMPO_UP:
            a->pat_sel += PAT_ROWS;
            if (a->pat_sel > PAT_MAX_SLOTS) a->pat_sel = PAT_MAX_SLOTS;
            break;
        case ACT_CONFIRM: pattern_do_action(a, 0); break;
        case ACT_MENU:
            a->mode = MODE_PAT_ACT;
            a->pat_act_sel = 0;
            break;
        case ACT_BACK: a->mode = MODE_EDIT; break;
        case ACT_PLAY:
            e->playing ^= 1;
            if (e->playing) e->restart = 1;
            break;
        default: break;
        }
        break;
    case MODE_PAT_ACT:
        switch (act) {
        case ACT_UP:
            a->pat_act_sel = (a->pat_act_sel + PAT_ACT_N - 1) % PAT_ACT_N;
            break;
        case ACT_DOWN:
            a->pat_act_sel = (a->pat_act_sel + 1) % PAT_ACT_N;
            break;
        case ACT_LEFT:
            a->pat_sel = (a->pat_sel <= 1) ? PAT_MAX_SLOTS : a->pat_sel - 1;
            break;
        case ACT_RIGHT:
            a->pat_sel = (a->pat_sel >= PAT_MAX_SLOTS) ? 1 : a->pat_sel + 1;
            break;
        case ACT_CONFIRM: pattern_do_action(a, a->pat_act_sel); break;
        case ACT_BACK:
        case ACT_MENU: a->mode = MODE_PATTERN; break;
        default: break;
        }
        break;
    case MODE_RENAME:
        switch (act) {
        case ACT_LEFT:
            a->rename_pos = (a->rename_pos + PAT_NAME_LEN - 1) % PAT_NAME_LEN;
            break;
        case ACT_RIGHT:
            a->rename_pos = (a->rename_pos + 1) % PAT_NAME_LEN;
            break;
        case ACT_UP:   pattern_rename_cycle(a, 1); break;
        case ACT_DOWN: pattern_rename_cycle(a, -1); break;
        case ACT_CONFIRM:
            a->rename_pos = (a->rename_pos + 1) % PAT_NAME_LEN;
            break;
        case ACT_HEAR:
        case ACT_CLEAR:
            if (a->rename_pos > 0) a->rename_pos--;
            a->rename_buf[a->rename_pos] = ' ';
            break;
        case ACT_MENU: pattern_commit_rename(a); break;
        case ACT_BACK: a->mode = MODE_PATTERN; a->pat_sel = a->rename_slot; break;
        case ACT_PLAY:
            e->playing ^= 1;
            if (e->playing) e->restart = 1;
            break;
        default: break;
        }
        break;
    default: break;
    }
}

/* map SDL events -> action */
static int held_dir = 0;          /* dpad direction currently held (action code) */
static Uint32 held_since = 0;
static Uint32 last_auto = 0;

static int dir_action(int button)
{
    switch (button) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return ACT_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return ACT_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return ACT_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return ACT_RIGHT;
    default: return ACT_NONE;
    }
}

static int any_shoulder_held(const App *a)
{
    for (int i = 0; i < SH_COUNT; i++)
        if (a->sh_held[i]) return 1;
    return 0;
}

/* The shoulder whose hints/shortcuts are active: the last pressed one, or
 * any still-held shoulder after a release. */
static int active_shoulder(const App *a)
{
    if (a->sh_last >= 0 && a->sh_last < SH_COUNT && a->sh_held[a->sh_last])
        return a->sh_last;
    for (int i = 0; i < SH_COUNT; i++)
        if (a->sh_held[i]) return i;
    return -1;
}

static void handle_shoulder(App *a, int sh, int down)
{
    if (sh < 0 || sh >= SH_COUNT) return;
    Uint32 now = SDL_GetTicks();

    if (down) {
        a->sh_held[sh] = 1;
        a->sh_last = sh;
        a->sh_suppress = 0;
        a->shortcut_until = now + 1500;
        held_dir = 0;                 /* stop any cursor auto-repeat */
        return;
    }

    a->sh_held[sh] = 0;
    if (a->sh_last == sh) {
        a->sh_last = -1;
        for (int i = SH_COUNT - 1; i >= 0; i--)
            if (a->sh_held[i]) { a->sh_last = i; break; }
    }
    /* Released: hide immediately. If another shoulder is still held, the main
     * loop refreshes the hint for that one on the next frame. */
    if (a->sh_last < 0) a->shortcut_until = 0;
    if (!any_shoulder_held(a)) a->sh_suppress = 0;
}

/* Open the menu bound to shoulder + direction. Returns 1 when a shortcut fired. */
static int shortcut_activate(App *a, int sh, int ad)
{
    switch (sh) {
    case SH_L1:
        if (ad == ACT_LEFT)  { a->mode = MODE_TEMPO; return 1; }
        if (ad == ACT_RIGHT) { a->mode = MODE_SWING; return 1; }
        break;
    case SH_L2:
        if (ad == ACT_LEFT) {
            a->mode = MODE_PATTERN;
            a->pat_sel = a->lib.active_slot;
            return 1;
        }
        break;
    case SH_R1:
        if (ad == ACT_LEFT) {
            a->mode = MODE_KIT;
            a->cur_step = a->eng.kit[a->cur_track];
            return 1;
        }
        if (ad == ACT_RIGHT) { a->mode = MODE_RCH; return 1; }
        break;
    default:
        break;
    }
    return 0;
}

/* Shortcuts apply everywhere except the name editor, where the D-pad must
 * keep moving the text cursor. */
static int shortcuts_enabled(const App *a)
{
    return a->mode != MODE_RENAME;
}

/* Intercept a D-pad direction while a shoulder is held. Returns 1 if the
 * direction was consumed by the shortcut layer. */
static int shortcut_dir(App *a, int ad)
{
    if (a->sh_suppress || !shortcuts_enabled(a)) return 0;
    int sh = active_shoulder(a);
    if (sh < 0) return 0;

    if (shortcut_activate(a, sh, ad)) {
        a->sh_suppress = 1;
        a->shortcut_until = 0;
    }
    return 1;
}

static int input_dir(App *a, int ad)
{
    if (shortcut_dir(a, ad)) return 1;
    act(a, ad);
    return 0;
}

/* Headless check of the shoulder shortcut map (`--test-shortcuts`). */
static void handle_trigger_axis(App *a, int sh, int val);

static int test_shortcuts_run(App *a)
{
    int fails = 0;
    SDL_Init(SDL_INIT_TIMER);
    a->lib.active_slot = 1;

#define CHECK(cond, msg) do { \
    if (cond) printf("PASS: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); fails++; } \
} while (0)

    a->mode = MODE_EDIT;
    handle_shoulder(a, SH_L1, 1);
    CHECK(a->shortcut_until != 0 && a->sh_last == SH_L1,
          "shoulder press shows the hint");
    handle_shoulder(a, SH_L1, 0);
    CHECK(a->shortcut_until == 0 && a->sh_last < 0,
          "shoulder release hides the hint instantly");

    a->mode = MODE_EDIT;
    handle_shoulder(a, SH_L1, 1);
    input_dir(a, ACT_LEFT);
    CHECK(a->mode == MODE_TEMPO, "L1+Left opens tempo");
    handle_shoulder(a, SH_L1, 0);
    a->sh_suppress = 0;

    a->mode = MODE_EDIT;
    handle_shoulder(a, SH_L1, 1);
    input_dir(a, ACT_RIGHT);
    CHECK(a->mode == MODE_SWING, "L1+Right opens swing");
    handle_shoulder(a, SH_L1, 0);
    a->sh_suppress = 0;

    a->mode = MODE_EDIT;
    handle_shoulder(a, SH_L2, 1);
    input_dir(a, ACT_LEFT);
    CHECK(a->mode == MODE_PATTERN, "L2+Left opens the pattern library");
    handle_shoulder(a, SH_L2, 0);
    a->sh_suppress = 0;

    a->mode = MODE_EDIT;
    handle_shoulder(a, SH_R1, 1);
    input_dir(a, ACT_LEFT);
    CHECK(a->mode == MODE_KIT, "R1+Left opens sample selection");
    handle_shoulder(a, SH_R1, 0);
    a->sh_suppress = 0;

    a->mode = MODE_EDIT;
    handle_shoulder(a, SH_R1, 1);
    input_dir(a, ACT_RIGHT);
    CHECK(a->mode == MODE_RCH, "R1+Right opens ratchet notes");
    handle_shoulder(a, SH_R1, 0);
    a->sh_suppress = 0;

    a->mode = MODE_EDIT;
    handle_shoulder(a, SH_R2, 1);
    input_dir(a, ACT_LEFT);
    CHECK(a->mode == MODE_EDIT, "R2+Left is reserved and consumed");
    handle_shoulder(a, SH_R2, 0);
    a->sh_suppress = 0;

    a->mode = MODE_EDIT;
    handle_shoulder(a, SH_L1, 1);
    input_dir(a, ACT_UP);
    CHECK(a->mode == MODE_EDIT, "L1+Up is reserved and consumed");
    handle_shoulder(a, SH_L1, 0);
    a->sh_suppress = 0;

    a->mode = MODE_EDIT;
    a->sh_held[SH_L2] = 0;
    a->sh_suppress = 0;
    handle_trigger_axis(a, SH_L2, 20000);
    CHECK(a->sh_held[SH_L2], "L2 trigger axis registers as held");
    input_dir(a, ACT_LEFT);
    CHECK(a->mode == MODE_PATTERN, "L2 trigger axis + Left opens pattern library");
    handle_trigger_axis(a, SH_L2, 0);
    a->sh_suppress = 0;

    a->mode = MODE_EDIT;
    a->cur_step = 0;
    input_dir(a, ACT_RIGHT);
    CHECK(a->cur_step == 1, "plain D-pad still moves the cursor");

    SDL_Quit();
    return fails ? 1 : 0;
#undef CHECK
}

/* Same physical dpad often arrives as BOTH a controller dpad-button and a
 * raw joystick hat (SDL mirrors events for mapped controllers). If both are
 * handled the cursor jumps twice — suppress raw events for mapped controllers. */
static int joy_covered_by_controller(SDL_JoystickID which)
{
    return SDL_GameControllerFromInstanceID(which) != NULL;
}

static int saw_dpad_buttons = 0;   /* controller exposes dpad buttons -> ignore dpad-as-axis */

/* If the controller mapping binds dpad buttons, axis-based dpad echoes
 * (some sunxi mappings carry leftx/lefty AND dpup from one hat) must be
 * ignored up front, not just after the first press. */
static void gc_note_mapping(SDL_GameController *gc)
{
    if (!gc) return;
    char *map = SDL_GameControllerMapping(gc);
    if (map) {
        if (strstr(map, "dpup:") || strstr(map, "dpleft:")) saw_dpad_buttons = 1;
        SDL_free(map);
    }
}

static void handle_key(App *a, SDL_Keycode k)
{
    switch (k) {
    case SDLK_UP: case SDLK_w:     input_dir(a, ACT_UP); break;
    case SDLK_DOWN: case SDLK_s:   input_dir(a, ACT_DOWN); break;
    case SDLK_LEFT: case SDLK_a:   input_dir(a, ACT_LEFT); break;
    case SDLK_RIGHT: case SDLK_d:  input_dir(a, ACT_RIGHT); break;
    case SDLK_z: case SDLK_RETURN: act(a, ACT_CONFIRM); break;
    case SDLK_x:                   act(a, (a->mode == MODE_EDIT || a->mode == MODE_RCH) ? ACT_CLEAR : ACT_BACK); break;
    case SDLK_c:                   act(a, ACT_HEAR); break;
    case SDLK_SPACE:               act(a, ACT_PLAY); break;
    case SDLK_m: case SDLK_ESCAPE: act(a, ACT_MENU); break;
    case SDLK_BACKSPACE:           act(a, a->mode == MODE_RENAME ? ACT_HEAR : ACT_BACK); break;
    case SDLK_LEFTBRACKET:         act(a, ACT_TEMPO_DOWN); break;
    case SDLK_RIGHTBRACKET:        act(a, ACT_TEMPO_UP); break;
    case SDLK_1:                   handle_shoulder(a, SH_L1, 1); break;
    case SDLK_2:                   handle_shoulder(a, SH_L2, 1); break;
    case SDLK_3:                   handle_shoulder(a, SH_R1, 1); break;
    case SDLK_4:                   handle_shoulder(a, SH_R2, 1); break;
    case SDLK_q:                   a->quit = 1; break;
    default: break;
    }
}

static void handle_key_up(App *a, SDL_Keycode k)
{
    switch (k) {
    case SDLK_1: handle_shoulder(a, SH_L1, 0); break;
    case SDLK_2: handle_shoulder(a, SH_L2, 0); break;
    case SDLK_3: handle_shoulder(a, SH_R1, 0); break;
    case SDLK_4: handle_shoulder(a, SH_R2, 0); break;
    default: break;
    }
}

static void handle_gc_button(App *a, int button, int down)
{
    if (button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) {
        handle_shoulder(a, SH_L1, down);
        return;
    }
    if (button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
        handle_shoulder(a, SH_R1, down);
        return;
    }

    if (down) {
        switch (button) {
        case SDL_CONTROLLER_BUTTON_A: act(a, ACT_CONFIRM); break;
        case SDL_CONTROLLER_BUTTON_B: act(a, (a->mode == MODE_EDIT || a->mode == MODE_RCH) ? ACT_CLEAR : ACT_BACK); break;
        case SDL_CONTROLLER_BUTTON_X: case SDL_CONTROLLER_BUTTON_Y: act(a, ACT_HEAR); break;
        case SDL_CONTROLLER_BUTTON_START: act(a, ACT_PLAY); break;
        case SDL_CONTROLLER_BUTTON_BACK: act(a, ACT_MENU); break;
        case SDL_CONTROLLER_BUTTON_DPAD_UP: case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: {
            int ad = dir_action(button);
            saw_dpad_buttons = 1;
            if (!input_dir(a, ad)) {
                held_dir = ad; held_since = SDL_GetTicks(); last_auto = held_since;
            } else {
                held_dir = 0;         /* shortcut consumed: no cursor repeat */
            }
            break;
        }
        default: break;
        }
    } else {
        switch (button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            if (held_dir == dir_action(button)) held_dir = 0;
            break;
        default: break;
        }
    }
}

static void handle_joy_button(App *a, int button, int down)
{
    /* raw fallback: buttons 4/5 are the L1/R1 shoulders on the SP mapping */
    if (button == 4) { handle_shoulder(a, SH_L1, down); return; }
    if (button == 5) { handle_shoulder(a, SH_R1, down); return; }
    if (!down) return;

    switch (button) {
    case 0: act(a, ACT_CONFIRM); break;       /* A */
    case 1: act(a, (a->mode == MODE_EDIT || a->mode == MODE_RCH) ? ACT_CLEAR : ACT_BACK); break;  /* B */
    case 2: act(a, ACT_HEAR); break;          /* X */
    case 3: act(a, ACT_HEAR_ALT); break;      /* Y */
    case 6: act(a, ACT_MENU); break;          /* SELECT */
    case 7: act(a, ACT_PLAY); break;          /* START */
    case 8: case 9: a->quit = 1; break;       /* HOLD etc. -> quit */
    case 11: input_dir(a, ACT_UP); break;     /* dpad via buttons */
    case 12: input_dir(a, ACT_DOWN); break;
    case 13: input_dir(a, ACT_LEFT); break;
    case 14: input_dir(a, ACT_RIGHT); break;
    default: break;
    }
}

static void handle_joy_hat(App *a, int hat, int dir)
{
    if (hat != 0 || dir == SDL_HAT_CENTERED) return;
    if (saw_dpad_buttons) return;
    if (dir & SDL_HAT_UP) input_dir(a, ACT_UP);
    if (dir & SDL_HAT_DOWN) input_dir(a, ACT_DOWN);
    if (dir & SDL_HAT_LEFT) input_dir(a, ACT_LEFT);
    if (dir & SDL_HAT_RIGHT) input_dir(a, ACT_RIGHT);
}

static void handle_trigger_axis(App *a, int sh, int val)
{
    if (!a->sh_held[sh] && val > 6000)
        handle_shoulder(a, sh, 1);
    else if (a->sh_held[sh] && val < 3000)
        handle_shoulder(a, sh, 0);
}

static void handle_joy_axis(App *a, int axis, int val)
{
    static int dir[2] = { 0, 0 };

    /* L2/R2 are the trigger axes on SDL's game-controller API and on most
     * raw Linux mappings; handle them before the stick/dpad filter. */
    if (axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
        handle_trigger_axis(a, SH_L2, val);
        return;
    }
    if (axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
        handle_trigger_axis(a, SH_R2, val);
        return;
    }

    if (axis > 1) return;
    if (saw_dpad_buttons) return;
    int nd = 0;
    if (val > 12000) nd = 1;
    else if (val < -12000) nd = -1;
    if (nd == dir[axis]) return;
    dir[axis] = nd;
    if (!nd) return;
    if (axis == 0) input_dir(a, nd > 0 ? ACT_RIGHT : ACT_LEFT);
    else           input_dir(a, nd > 0 ? ACT_DOWN : ACT_UP);
}

/* ---------------------------------------------------------------- bounce */

static int run_bounce(App *a, const char *outpath, int loops)
{
    int rate = 44100;
    a->eng.rate = rate;
    engine_init(&a->eng, rate);
    if (load_current(a) != 0) demo_pattern(a);
    a->eng.playing = 1;
    engine_restart(&a->eng);
    double sps = 60.0 * rate / (a->eng.bpm_x100 / 100.0 * 4.0);
    long total = (long)(sps * N_STEPS * loops) + 8192;   /* tail so last hits finish */
    float *buf = malloc(sizeof(float) * 2 * 1024);
    /* render in chunks into one big buffer (loops<=8 stays small) */
    float *all = malloc(sizeof(float) * 2 * ((size_t)total + 64));
    long done = 0;
    while (done < total) {
        int chunk = (int)((total - done) > 1024 ? 1024 : (total - done));
        engine_mix(&a->eng, buf, chunk);
        memcpy(all + done * 2, buf, sizeof(float) * 2 * chunk);
        done += chunk;
    }
    int rc = wav_write_stereo16(outpath, all, (int)total, rate);
    printf("rg808: bounced %ld frames (%.2fs) -> %s\n", total, (double)total / rate, outpath);
    free(all); free(buf);
    return rc;
}

/* ----------------------------------------------------------------- main */

int main(int argc, char **argv)
{
    App a;
    memset(&a, 0, sizeof a);
    a.cur_track = 0; a.cur_step = 0; a.mode = MODE_EDIT; a.pat_sel = 1;
    a.sh_last = -1;

    const char *assets_arg = NULL;
    const char *bounce = NULL;
    const char *shot = NULL;
    const char *mode_arg = NULL;
    int bounce_loops = 4;
    int pattern_arg = 0;
    int save_slot_arg = 0;
    int save_slot_set = 0;
    int test_shortcuts = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--assets") && i + 1 < argc) assets_arg = argv[++i];
        else if (!strcmp(argv[i], "--bounce") && i + 1 < argc) bounce = argv[++i];
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--mode") && i + 1 < argc) mode_arg = argv[++i];
        else if (!strcmp(argv[i], "--loops") && i + 1 < argc) bounce_loops = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--pattern") && i + 1 < argc) pattern_arg = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--save-slot") && i + 1 < argc) {
            save_slot_arg = atoi(argv[++i]);
            save_slot_set = 1;
        }
        else if (!strcmp(argv[i], "--test-shortcuts")) test_shortcuts = 1;
        else if (!strcmp(argv[i], "--help")) {
            printf("usage: rg808 [--assets DIR] [--pattern N] [--save-slot N]"
                   " [--bounce out.wav --loops N] [--test-shortcuts]"
                   " [--screenshot out.bmp [--mode edit|kit|rch|swing|menu|tempo|pattern|l1|l2|r1|r2]]\n");
            return 0;
        }
    }

    char *base = SDL_GetBasePath();
    char assets[1024];
    const char *env = getenv("RG808_ASSETS");
    if (assets_arg) snprintf(assets, sizeof assets, "%s", assets_arg);
    else if (env && *env) snprintf(assets, sizeof assets, "%s", env);
    else if (base && *base) snprintf(assets, sizeof assets, "%sassets/wav", base);
    else snprintf(assets, sizeof assets, "assets/wav");

    engine_init(&a.eng, 44100);

    if (test_shortcuts) {
        SDL_free(base);
        return test_shortcuts_run(&a);
    }

    if (bounce || save_slot_set) {
        samples_load_all(assets, 44100);
        resolve_conf(&a, base ? base : ".");
        if (pattern_arg >= 1 && pattern_arg <= PAT_MAX_SLOTS)
            pattern_set_active(&a.lib, pattern_arg);
        SDL_free(base);

        if (save_slot_set) {
            engine_init(&a.eng, 44100);
            if (load_current(&a) != 0) demo_pattern(&a);
            if (save_slot_arg < 1 || save_slot_arg > PAT_MAX_SLOTS) {
                fprintf(stderr, "rg808: --save-slot must be 1..%d\n", PAT_MAX_SLOTS);
                return 1;
            }
            if (pattern_save_slot(&a.lib, &a.eng, save_slot_arg) != 0) {
                fprintf(stderr, "rg808: could not save slot %d\n", save_slot_arg);
                return 1;
            }
            printf("rg808: saved P%03d -> P%03d (%s)\n", a.lib.active_slot,
                   save_slot_arg, pattern_name(&a.lib, save_slot_arg));
            return 0;
        }

        int rc = run_bounce(&a, bounce, bounce_loops);
        return rc;
    }

    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "sdl: %s\n", SDL_GetError());
        return 1;
    }

    if (render_init(&a) != 0) { SDL_Quit(); return 1; }

    SDL_AudioSpec desired;
    memset(&desired, 0, sizeof desired);
    desired.freq = 44100;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = 512;
    desired.callback = audio_cb;
    desired.userdata = &a;
    a.dev = SDL_OpenAudioDevice(NULL, 0, &desired, NULL, 0);
    if (!a.dev) fprintf(stderr, "rg808: no audio device: %s\n", SDL_GetError());
    else SDL_PauseAudioDevice(a.dev, 0);

    resolve_conf(&a, base ? base : ".");
    if (pattern_arg >= 1 && pattern_arg <= PAT_MAX_SLOTS)
        pattern_set_active(&a.lib, pattern_arg);
    SDL_free(base);

    int missing = samples_load_all(assets, 44100);
    if (missing > 0) fprintf(stderr, "rg808: %d track(s) have no samples in %s\n", missing, assets);

    if (load_current(&a) != 0) demo_pattern(&a);

    /* clamp selected variants to what actually loaded */
    for (int t = 0; t < N_TRACKS; t++) {
        int cnt = variant_count(t);
        if (!cnt) { a.eng.kit[t] = 0; continue; }
        if (a.eng.kit[t] < 0) a.eng.kit[t] = 0;
        if (a.eng.kit[t] >= cnt) a.eng.kit[t] = cnt - 1;
    }

    if (shot) {
        /* one UI frame -> BMP */
        if (mode_arg) {
            if (!strcmp(mode_arg, "kit"))   { a.mode = MODE_KIT; a.cur_step = a.eng.kit[a.cur_track]; }
            else if (!strcmp(mode_arg, "rch"))   a.mode = MODE_RCH;
            else if (!strcmp(mode_arg, "swing")) a.mode = MODE_SWING;
            else if (!strcmp(mode_arg, "menu"))  a.mode = MODE_MENU;
            else if (!strcmp(mode_arg, "tempo")) a.mode = MODE_TEMPO;
            else if (!strcmp(mode_arg, "pattern")) {
                a.mode = MODE_PATTERN;
                a.pat_sel = a.lib.active_slot;
            } else if (!strcmp(mode_arg, "patact")) {
                a.mode = MODE_PAT_ACT;
                a.pat_sel = a.lib.active_slot;
            } else if (!strcmp(mode_arg, "rename")) {
                a.mode = MODE_RENAME;
                a.pat_sel = a.lib.active_slot;
                pattern_begin_rename(&a, a.pat_sel);
            } else if (!strcmp(mode_arg, "l1")) {
                a.sh_last = SH_L1; a.shortcut_until = SDL_GetTicks() + 60000;
            } else if (!strcmp(mode_arg, "l2")) {
                a.sh_last = SH_L2; a.shortcut_until = SDL_GetTicks() + 60000;
            } else if (!strcmp(mode_arg, "r1")) {
                a.sh_last = SH_R1; a.shortcut_until = SDL_GetTicks() + 60000;
            } else if (!strcmp(mode_arg, "r2")) {
                a.sh_last = SH_R2; a.shortcut_until = SDL_GetTicks() + 60000;
            }
        }
        draw_ui(&a);
        SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormatFrom(a.pixels, SCR_W, SCR_H, 32, SCR_W * 4, SDL_PIXELFORMAT_ARGB8888);
        SDL_SaveBMP(sf, shot);
        SDL_FreeSurface(sf);
        printf("rg808: screenshot -> %s\n", shot);
        SDL_Quit();
        return 0;
    }

    /* open any gamepads: prefer mapped controllers, else raw joystick */
    int raw_opened = 0;
    a.last_save = SDL_GetTicks();
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) gc_note_mapping(SDL_GameControllerOpen(i));
        else if (!raw_opened && SDL_JoystickOpen(i)) raw_opened = 1;
    }

    /* main loop */
    while (!a.quit) {
        SDL_Event ev;
        while (SDL_WaitEventTimeout(&ev, 16)) {
            switch (ev.type) {
            case SDL_QUIT: a.quit = 1; break;
            case SDL_KEYDOWN:
                if (!ev.key.repeat || ev.key.keysym.sym == SDLK_UP || ev.key.keysym.sym == SDLK_DOWN ||
                    ev.key.keysym.sym == SDLK_LEFT || ev.key.keysym.sym == SDLK_RIGHT)
                    handle_key(&a, ev.key.keysym.sym);
                break;
            case SDL_KEYUP:
                handle_key_up(&a, ev.key.keysym.sym);
                break;
            case SDL_CONTROLLERBUTTONDOWN: handle_gc_button(&a, ev.cbutton.button, 1); break;
            case SDL_CONTROLLERBUTTONUP:   handle_gc_button(&a, ev.cbutton.button, 0); break;
            case SDL_CONTROLLERAXISMOTION: handle_joy_axis(&a, ev.caxis.axis, ev.caxis.value); break;
            case SDL_CONTROLLERDEVICEADDED:
                gc_note_mapping(SDL_GameControllerOpen(ev.cdevice.which));
                break;
            case SDL_JOYAXISMOTION:
                if (!joy_covered_by_controller(ev.jaxis.which))
                    handle_joy_axis(&a, ev.jaxis.axis, ev.jaxis.value);
                break;
            case SDL_JOYHATMOTION:
                if (!joy_covered_by_controller(ev.jhat.which))
                    handle_joy_hat(&a, ev.jhat.hat, ev.jhat.value);
                break;
            case SDL_JOYBUTTONDOWN:
                if (!joy_covered_by_controller(ev.jbutton.which))
                    handle_joy_button(&a, ev.jbutton.button, 1);
                break;
            case SDL_JOYBUTTONUP:
                if (!joy_covered_by_controller(ev.jbutton.which))
                    handle_joy_button(&a, ev.jbutton.button, 0);
                break;
            case SDL_JOYDEVICEADDED:
                if (!SDL_IsGameController(ev.jdevice.which))
                    SDL_JoystickOpen(ev.jdevice.which);
                break;
            case SDL_AUDIODEVICEADDED: break;
            default: break;
            }
        }
        /* d-pad hold repeat */
        Uint32 now = SDL_GetTicks();
        if (held_dir && (int)(now - held_since) > 350 && (int)(now - last_auto) > 120) {
            last_auto = now;
            if (input_dir(&a, held_dir)) held_dir = 0;
        }
        /* keep the shoulder-shortcut hint visible while a shoulder is held;
         * after a shortcut fired, stay hidden until all shoulders are up */
        if (any_shoulder_held(&a)) {
            if (!a.sh_suppress) a.shortcut_until = now + 1200;
        } else if (a.sh_suppress) {
            a.sh_suppress = 0;
        }
        /* periodic autosave: kills/HOME-exit shouldn't lose the groove */
        if (a.dirty && (int)(now - a.last_save) > 5000) {
            if (save_current(&a) == 0) a.dirty = 0;
            else set_msg(&a, "AUTOSAVE FAILED");
            a.last_save = now;
        }
        draw_ui(&a);
        present(&a);
    }

    if (a.dirty) save_current(&a);
    if (a.dev) SDL_CloseAudioDevice(a.dev);
    SDL_Quit();
    return 0;
}
