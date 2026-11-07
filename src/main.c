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
#include "song.h"
#include "fx.h"
#include "perf_fx.h"

#define SCR_W 640
#define SCR_H 480

/* pattern browser geometry */
#define PAT_ROWS   12
#define PAT_ROW_H  30
#define PAT_LIST_X 24
#define PAT_LIST_Y 92
#define PAT_LIST_W 592
#define PAT_ACT_N  6
#define MENU_N     11

/* song page geometry */
#define SONG_ROWS   12
#define SONG_ROW_H  30
#define SONG_ACT_N  10

/* grid geometry */
#define GRID_X0  76
#define GRID_Y0  58
#define CELL_W   30
#define CELL_H   30
#define COL_PITCH (CELL_W + 4)          /* 34 */
#define ROW_PITCH (CELL_H + 6)          /* 36 */

enum Mode { MODE_EDIT, MODE_MENU, MODE_TEMPO, MODE_KIT, MODE_RCH, MODE_SWING,
            MODE_LENGTH, MODE_FX, MODE_PATTERN, MODE_PAT_ACT, MODE_RENAME,
            MODE_SONG, MODE_SONG_ACT, MODE_EXPORT };

enum Action {
    ACT_NONE, ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT,
    ACT_CONFIRM, ACT_BACK, ACT_CLEAR, ACT_HEAR, ACT_PLAY, ACT_MENU,
    ACT_TEMPO_UP, ACT_TEMPO_DOWN, ACT_QUIT, ACT_MUTE
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
    int pat_pick_target;              /* song bar+1 while picking a pattern */
    int rename_slot;
    int rename_pos;
    char rename_buf[PAT_NAME_LEN + 1];
    Song song;
    SongSet *song_sets[2];            /* double-buffered immutable snapshot sets */
    int song_set_active;              /* buffer index the engine last used */
    FxSettings edit_fx;               /* live-pattern chain while a song plays */
    int edit_fx_valid;
    int song_sel;                     /* song editor cursor (0-based bar) */
    int song_act_sel;
    int song_dirty;
    Uint32 song_last_save;
    char song_path[1200];
    char song_wav_path[1200];
    char fx_path[1200];
    int fx_sel;                       /* FX page cursor */
    int export_progress;              /* 0..1000 */
    char export_display[64];
    int sh_held[SH_COUNT];            /* shoulder buttons currently down */
    int sh_last;                      /* most recent shoulder (overlay owner) */
    int sh_suppress;                  /* shortcut used: hide hint until release */
    int punch_held;                   /* punch effect currently held, -1 = none */
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

static int pattern_len(const App *a)
{
    return engine_length_clamp(a->eng.length);
}

static void clamp_cursor(App *a)
{
    int len = pattern_len(a);
    if (a->cur_step < 0) a->cur_step = 0;
    if (a->cur_step >= len) a->cur_step = len - 1;
}

/* Move the step cursor within the pattern length (wraps at the last step). */
static void move_step(App *a, int dir)
{
    int len = pattern_len(a);
    if (a->cur_step < 0 || a->cur_step >= len)
        a->cur_step = dir > 0 ? len - 1 : 0;
    a->cur_step = (a->cur_step + dir + len) % len;
}

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

    snprintf(a->song_path, sizeof a->song_path, "%s/song.txt", a->lib.conf_dir);
    snprintf(a->song_wav_path, sizeof a->song_wav_path, "%s/song.wav", a->lib.conf_dir);
    snprintf(a->fx_path, sizeof a->fx_path, "%s/fx.txt", a->lib.conf_dir);
}

static int load_fx_settings(App *a)
{
    FxSettings f;
    int rc = fx_load_file(&f, a->fx_path);
    fx_set_fallback(&f);
    fx_apply(&a->eng, &f);
    return rc;
}

static int save_current(App *a)
{
    if (a->legacy_override)
        return pattern_save_file(&a->eng, a->conf_override);
    return pattern_save_slot(&a->lib, &a->eng, a->lib.active_slot);
}

static int load_current(App *a)
{
    int rc;
    if (a->legacy_override)
        rc = pattern_load_file(&a->eng, a->conf_override);
    else
        rc = pattern_load_slot(&a->lib, &a->eng, a->lib.active_slot);
    if (rc == 0) clamp_cursor(a);
    return rc;
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
    clamp_cursor(a);
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
        if (ad == ACT_UP) return "PATTERN LENGTH";
        if (ad == ACT_DOWN) return "RATCHET";
        break;
    case SH_L2:
        if (ad == ACT_LEFT) return "PATTERN LIBRARY";
        if (ad == ACT_RIGHT) return "SONG MODE";
        if (ad == ACT_UP) return "SAMPLE KIT";
        break;
    case SH_R2:
        if (ad == ACT_UP) return "DRIVE FX";
        if (ad == ACT_LEFT) return "MOD FX";
        if (ad == ACT_DOWN) return "DELAY FX";
        if (ad == ACT_RIGHT) return "REVERB FX";
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

/* One cell in the punch-in cluster: key symbol on top, effect name below. */
static void draw_punch_cell(Canvas *c, int x, int y, int size,
                            const char *key, const char *l1, const char *l2,
                            int on, int playing)
{
    Uint32 text = playing ? C_LED : (on ? C_CURSOR : C_TEXT);
    cv_rect(c, x, y, size, size, on ? cv_dim(C_LED, 0.25f) : 0x1c1c24);
    cv_border(c, x, y, size, size, 2, on ? C_LED : C_TEXT_DIM);
    cv_text(c, x + (size - cv_text_w(key, 1)) / 2, y + 5, key, 1,
            on ? C_TEXT : C_TEXT_DIM);
    if (l1)
        cv_text(c, x + (size - cv_text_w(l1, 1)) / 2, y + 21, l1, 1, text);
    if (l2)
        cv_text(c, x + (size - cv_text_w(l2, 1)) / 2, y + 33, l2, 1, text);
}

/* R1 punch-in layer: D-pad on the left, face-button diamond on the right.
 * SP face layout: X top, A right, B bottom, Y left. */
static void draw_punch_overlay(App *a)
{
    Canvas *c = &a->cv;
    int px = 20, py = 96, pw = 600, ph = 282;
    cv_rect(c, px, py, pw, ph, C_PANEL);
    cv_border(c, px, py, pw, ph, 2, C_TEXT);

    cv_text(c, px + 16, py + 10, "R1 + PUNCH-IN FX", 1, C_TEXT);
    cv_text(c, px + 16, py + 26,
            "HOLD R1 AND PRESS A KEY - QUANTIZED TO 1/16", 1, C_TEXT_DIM);

    const int size = 52, gap = 8;
    const int cy = 228;
    const int lx = 144, ly = cy - size / 2;
    const int rx = 424;
    int active = engine_punch_active(&a->eng);

    draw_punch_cell(c, lx, ly - size - gap, size, "^", "1/16", NULL,
                    a->punch_held == PUNCH_LOOP_16, active == PUNCH_LOOP_16);
    draw_punch_cell(c, lx - size - gap, ly, size, "<", "1/4", NULL,
                    a->punch_held == PUNCH_LOOP_4, active == PUNCH_LOOP_4);
    draw_punch_cell(c, lx + size + gap, ly, size, ">", "1/8", NULL,
                    a->punch_held == PUNCH_LOOP_8, active == PUNCH_LOOP_8);
    draw_punch_cell(c, lx, ly + size + gap, size, "v", "1/32", NULL,
                    a->punch_held == PUNCH_LOOP_32, active == PUNCH_LOOP_32);

    draw_punch_cell(c, rx, ly - size - gap, size, "X", "TAPE", "STOP",
                    a->punch_held == PUNCH_TAPESTOP, active == PUNCH_TAPESTOP);
    draw_punch_cell(c, rx - size - gap, ly, size, "Y", "PHONE", NULL,
                    a->punch_held == PUNCH_PHONE, active == PUNCH_PHONE);
    draw_punch_cell(c, rx + size + gap, ly, size, "A", "REWIND", NULL,
                    a->punch_held == PUNCH_REWIND, active == PUNCH_REWIND);
    draw_punch_cell(c, rx, ly + size + gap, size, "B", "GATE", NULL,
                    a->punch_held == PUNCH_GATE, active == PUNCH_GATE);

    const char *left = "D-PAD: STUTTER LOOPS";
    const char *right = "FACE BUTTONS: FX";
    cv_text(c, lx + size / 2 - cv_text_w(left, 1) / 2, py + ph - 52,
            left, 1, C_TEXT_DIM);
    cv_text(c, rx + size / 2 - cv_text_w(right, 1) / 2, py + ph - 52,
            right, 1, C_TEXT_DIM);
    cv_text(c, px + 16, py + ph - 18,
            "ONE EFFECT AT A TIME - RELEASE TO RETURN TO LIVE", 1, C_TEXT_DIM);
}

/* Transient D-pad-shaped hint shown while a shoulder button is down (and for
 * a moment after release). */
static void draw_shortcut_overlay(App *a)
{
    Uint32 now = SDL_GetTicks();
    if (!a->shortcut_until || (int)(now - a->shortcut_until) >= 0) return;
    if (a->pat_pick_target > 0 || a->mode == MODE_EXPORT) return;
    int sh = a->sh_last;
    if (sh < 0 || sh >= SH_COUNT) return;
    if (sh == SH_R1) { draw_punch_overlay(a); return; }

    Canvas *c = &a->cv;
    int px = 20, py = 110, pw = 600, ph = 250;
    cv_rect(c, px, py, pw, ph, C_PANEL);
    cv_border(c, px, py, pw, ph, 2, C_TEXT);

    char buf[80];
    snprintf(buf, sizeof buf, "%s + D-PAD", SH_NAMES[sh]);
    cv_text(c, px + 16, py + 10, buf, 1, C_TEXT);
    cv_text(c, px + 16, py + 26,
            "PRESS A DIRECTION TO OPEN ITS MENU", 1, C_TEXT_DIM);

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
            "RELEASE THE SHOULDER TO CLOSE", 1, C_TEXT_DIM);
}

static void draw_pattern_list(App *a, int with_actions)
{
    Canvas *c = &a->cv;
    int px = 16, py = 26, pw = 608, ph = 430;
    cv_rect(c, px, py, pw, ph, C_PANEL);
    cv_border(c, px, py, pw, ph, 2, C_TEXT);
    cv_text(c, px + 12, py + 10,
            a->pat_pick_target > 0 ? "PICK PATTERN" : "PATTERN LIBRARY", 1, C_TEXT);
    if (a->pat_pick_target > 0) {
        char sbuf[40];
        snprintf(sbuf, sizeof sbuf, "FOR SONG BAR %03d", a->pat_pick_target);
        cv_text(c, px + 12, py + 26, sbuf, 1, C_TEXT_DIM);
    }

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

/* -------------------------------------------------------------- effects UI */

enum {
    FXR_MASTER,
    FXR_DRIVE_ON, FXR_DRIVE_TYPE,
    FXR_CRUSH_BITS, FXR_CRUSH_DOWN,
    FXR_OD_AMOUNT, FXR_OD_MIX,
    FXR_COMP_THRESH, FXR_COMP_RATIO, FXR_COMP_MAKEUP,
    FXR_MOD_ON, FXR_MOD_TYPE, FXR_MOD_RATE, FXR_MOD_DEPTH,
    FXR_MOD_FB, FXR_MOD_MIX,
    FXR_DELAY_ON, FXR_DELAY_SYNC, FXR_DELAY_TIME,
    FXR_DELAY_FB, FXR_DELAY_MIX,
    FXR_REVERB_ON, FXR_REVERB_ROOM, FXR_REVERB_DAMP, FXR_REVERB_MIX,
    FXR_COUNT
};

static const char *const DRIVE_NAMES[FX_DRIVE_COUNT] = {
    "BITCRUSH", "OVERDRIVE", "COMPRESS"
};
static const char *const MOD_NAMES[FX_MOD_COUNT] = {
    "FLANGER", "PHASER"
};

static int fx_clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Rows visible for the selected drive algorithm. */
static int fx_visible_rows(const Engine *e, int *rows)
{
    int n = 0;
    int type = fx_clampi(e->fx.drive_type, 0, FX_DRIVE_COUNT - 1);
    rows[n++] = FXR_MASTER;
    rows[n++] = FXR_DRIVE_ON;
    rows[n++] = FXR_DRIVE_TYPE;
    if (type == FX_DRIVE_CRUSH) {
        rows[n++] = FXR_CRUSH_BITS;
        rows[n++] = FXR_CRUSH_DOWN;
    } else if (type == FX_DRIVE_OVERDRIVE) {
        rows[n++] = FXR_OD_AMOUNT;
        rows[n++] = FXR_OD_MIX;
    } else {
        rows[n++] = FXR_COMP_THRESH;
        rows[n++] = FXR_COMP_RATIO;
        rows[n++] = FXR_COMP_MAKEUP;
    }
    rows[n++] = FXR_MOD_ON;
    rows[n++] = FXR_MOD_TYPE;
    rows[n++] = FXR_MOD_RATE;
    rows[n++] = FXR_MOD_DEPTH;
    rows[n++] = FXR_MOD_FB;
    rows[n++] = FXR_MOD_MIX;
    rows[n++] = FXR_DELAY_ON;
    rows[n++] = FXR_DELAY_SYNC;
    rows[n++] = FXR_DELAY_TIME;
    rows[n++] = FXR_DELAY_FB;
    rows[n++] = FXR_DELAY_MIX;
    rows[n++] = FXR_REVERB_ON;
    rows[n++] = FXR_REVERB_ROOM;
    rows[n++] = FXR_REVERB_DAMP;
    rows[n++] = FXR_REVERB_MIX;
    return n;
}

/* Open the FX page with the cursor on one section's first row. */
static void fx_open_section(App *a, int target)
{
    int rows[24];
    int n = fx_visible_rows(&a->eng, rows);
    a->fx_sel = 0;
    for (int i = 0; i < n; i++) {
        if (rows[i] == target) {
            a->fx_sel = i;
            break;
        }
    }
    a->mode = MODE_FX;
}

static int fx_row_is_toggle(int row)
{
    return row == FXR_MASTER || row == FXR_DRIVE_ON || row == FXR_MOD_ON ||
           row == FXR_DELAY_ON || row == FXR_DELAY_SYNC ||
           row == FXR_REVERB_ON;
}

static int fx_row_stage_on(const Engine *e, int row)
{
    if (row == FXR_MASTER) return 1;
    if (row <= FXR_COMP_MAKEUP) return e->fx.drive_on;
    if (row <= FXR_MOD_MIX) return e->fx.mod_on;
    if (row <= FXR_DELAY_MIX) return e->fx.delay_on;
    return e->fx.reverb_on;
}

static const char *fx_row_label(const Engine *e, int row)
{
    switch (row) {
    case FXR_MASTER:        return "FX MASTER";
    case FXR_DRIVE_ON:      return "DRIVE";
    case FXR_DRIVE_TYPE:    return "  TYPE";
    case FXR_CRUSH_BITS:    return "  BITS";
    case FXR_CRUSH_DOWN:    return "  DOWN";
    case FXR_OD_AMOUNT:     return "  DRIVE";
    case FXR_OD_MIX:        return "  MIX";
    case FXR_COMP_THRESH:   return "  THRESH";
    case FXR_COMP_RATIO:    return "  RATIO";
    case FXR_COMP_MAKEUP:   return "  MAKEUP";
    case FXR_MOD_ON:        return "MODULATION";
    case FXR_MOD_TYPE:      return "  TYPE";
    case FXR_MOD_RATE:      return "  RATE";
    case FXR_MOD_DEPTH:     return "  DEPTH";
    case FXR_MOD_FB:        return "  FEEDBACK";
    case FXR_MOD_MIX:       return "  MIX";
    case FXR_DELAY_ON:      return "DELAY";
    case FXR_DELAY_SYNC:    return "  SYNC";
    case FXR_DELAY_TIME:    return e->fx.delay_sync ? "  DIV" : "  TIME";
    case FXR_DELAY_FB:      return "  FEEDBACK";
    case FXR_DELAY_MIX:     return "  MIX";
    case FXR_REVERB_ON:     return "REVERB";
    case FXR_REVERB_ROOM:   return "  ROOM";
    case FXR_REVERB_DAMP:   return "  DAMP";
    case FXR_REVERB_MIX:    return "  MIX";
    default:                return "";
    }
}

static void fx_row_text(const Engine *e, int row, char *buf, size_t cap)
{
    switch (row) {
    case FXR_MASTER:        snprintf(buf, cap, "%s", e->fx.on ? "ON" : "BYPASS"); break;
    case FXR_DRIVE_ON:      snprintf(buf, cap, "%s", e->fx.drive_on ? "ON" : "OFF"); break;
    case FXR_DRIVE_TYPE:    snprintf(buf, cap, "%s",
                                     DRIVE_NAMES[fx_clampi(e->fx.drive_type, 0, FX_DRIVE_COUNT - 1)]); break;
    case FXR_CRUSH_BITS:    snprintf(buf, cap, "%d BIT", e->fx.crush_bits); break;
    case FXR_CRUSH_DOWN:    snprintf(buf, cap, "x%d", e->fx.crush_down); break;
    case FXR_OD_AMOUNT:     snprintf(buf, cap, "%d%%", e->fx.drive_amount); break;
    case FXR_OD_MIX:        snprintf(buf, cap, "%d%%", e->fx.drive_mix); break;
    case FXR_COMP_THRESH:   snprintf(buf, cap, "%d%%", e->fx.comp_threshold); break;
    case FXR_COMP_RATIO:    snprintf(buf, cap, "%d:1", e->fx.comp_ratio); break;
    case FXR_COMP_MAKEUP:   snprintf(buf, cap, "%d%%", e->fx.comp_makeup); break;
    case FXR_MOD_ON:        snprintf(buf, cap, "%s", e->fx.mod_on ? "ON" : "OFF"); break;
    case FXR_MOD_TYPE:      snprintf(buf, cap, "%s",
                                     MOD_NAMES[fx_clampi(e->fx.mod_type, 0, FX_MOD_COUNT - 1)]); break;
    case FXR_MOD_RATE:      snprintf(buf, cap, "%.2f HZ", e->fx.mod_rate_x100 / 100.0); break;
    case FXR_MOD_DEPTH:     snprintf(buf, cap, "%d%%", e->fx.mod_depth); break;
    case FXR_MOD_FB:        snprintf(buf, cap, "%d%%", e->fx.mod_fb); break;
    case FXR_MOD_MIX:       snprintf(buf, cap, "%d%%", e->fx.mod_mix); break;
    case FXR_DELAY_ON:      snprintf(buf, cap, "%s", e->fx.delay_on ? "ON" : "OFF"); break;
    case FXR_DELAY_SYNC:    snprintf(buf, cap, "%s", e->fx.delay_sync ? "ON" : "OFF"); break;
    case FXR_DELAY_TIME:
        if (e->fx.delay_sync)
            snprintf(buf, cap, "%s", FX_DIV_NAMES[fx_clampi(e->fx.delay_div, 0, FX_DIV_COUNT - 1)]);
        else
            snprintf(buf, cap, "%d MS", e->fx.delay_ms);
        break;
    case FXR_DELAY_FB:      snprintf(buf, cap, "%d%%", e->fx.delay_fb); break;
    case FXR_DELAY_MIX:     snprintf(buf, cap, "%d%%", e->fx.delay_mix); break;
    case FXR_REVERB_ON:     snprintf(buf, cap, "%s", e->fx.reverb_on ? "ON" : "OFF"); break;
    case FXR_REVERB_ROOM:   snprintf(buf, cap, "%d%%", e->fx.reverb_room); break;
    case FXR_REVERB_DAMP:   snprintf(buf, cap, "%d%%", e->fx.reverb_damp); break;
    case FXR_REVERB_MIX:    snprintf(buf, cap, "%d%%", e->fx.reverb_mix); break;
    default:                buf[0] = 0; break;
    }
}

/* Left/right adjust one row. Percentages move in 5s, delay time in 50 ms. */
static void fx_row_adjust(App *a, int row, int dir)
{
    Engine *e = &a->eng;
    switch (row) {
    case FXR_MASTER:        e->fx.on = dir > 0; break;
    case FXR_DRIVE_ON:      e->fx.drive_on = dir > 0; break;
    case FXR_DRIVE_TYPE:
        e->fx.drive_type = (e->fx.drive_type + dir + FX_DRIVE_COUNT) % FX_DRIVE_COUNT;
        break;
    case FXR_CRUSH_BITS:
        e->fx.crush_bits = fx_clampi(e->fx.crush_bits + dir, 4, 16); break;
    case FXR_CRUSH_DOWN:
        e->fx.crush_down = fx_clampi(e->fx.crush_down + dir, 1, 32); break;
    case FXR_OD_AMOUNT:
        e->fx.drive_amount = fx_clampi(e->fx.drive_amount + dir * 5, 0, 100); break;
    case FXR_OD_MIX:
        e->fx.drive_mix = fx_clampi(e->fx.drive_mix + dir * 5, 0, 100); break;
    case FXR_COMP_THRESH:
        e->fx.comp_threshold = fx_clampi(e->fx.comp_threshold + dir * 5, 0, 100); break;
    case FXR_COMP_RATIO:
        e->fx.comp_ratio = fx_clampi(e->fx.comp_ratio + dir, 1, 20); break;
    case FXR_COMP_MAKEUP:
        e->fx.comp_makeup = fx_clampi(e->fx.comp_makeup + dir * 5, 0, 100); break;
    case FXR_MOD_ON:        e->fx.mod_on = dir > 0; break;
    case FXR_MOD_TYPE:
        e->fx.mod_type = (e->fx.mod_type + dir + FX_MOD_COUNT) % FX_MOD_COUNT;
        break;
    case FXR_MOD_RATE:
        e->fx.mod_rate_x100 = fx_clampi(e->fx.mod_rate_x100 + dir * 5, 5, 500); break;
    case FXR_MOD_DEPTH:
        e->fx.mod_depth = fx_clampi(e->fx.mod_depth + dir * 5, 0, 100); break;
    case FXR_MOD_FB:
        e->fx.mod_fb = fx_clampi(e->fx.mod_fb + dir * 5, 0, 90); break;
    case FXR_MOD_MIX:
        e->fx.mod_mix = fx_clampi(e->fx.mod_mix + dir * 5, 0, 100); break;
    case FXR_DELAY_ON:      e->fx.delay_on = dir > 0; break;
    case FXR_DELAY_SYNC:    e->fx.delay_sync = dir > 0; break;
    case FXR_DELAY_TIME:
        if (e->fx.delay_sync)
            e->fx.delay_div = (e->fx.delay_div + dir + FX_DIV_COUNT) % FX_DIV_COUNT;
        else
            e->fx.delay_ms = fx_clampi(e->fx.delay_ms + dir * 50, 20, 4000);
        break;
    case FXR_DELAY_FB:
        e->fx.delay_fb = fx_clampi(e->fx.delay_fb + dir * 5, 0, 90); break;
    case FXR_DELAY_MIX:
        e->fx.delay_mix = fx_clampi(e->fx.delay_mix + dir * 5, 0, 100); break;
    case FXR_REVERB_ON:     e->fx.reverb_on = dir > 0; break;
    case FXR_REVERB_ROOM:
        e->fx.reverb_room = fx_clampi(e->fx.reverb_room + dir * 5, 0, 100); break;
    case FXR_REVERB_DAMP:
        e->fx.reverb_damp = fx_clampi(e->fx.reverb_damp + dir * 5, 0, 100); break;
    case FXR_REVERB_MIX:
        e->fx.reverb_mix = fx_clampi(e->fx.reverb_mix + dir * 5, 0, 100); break;
    default: return;
    }
    a->dirty = 1;
}

static void fx_row_toggle(App *a, int row)
{
    Engine *e = &a->eng;
    switch (row) {
    case FXR_MASTER:    e->fx.on ^= 1; break;
    case FXR_DRIVE_ON:  e->fx.drive_on ^= 1; break;
    case FXR_MOD_ON:    e->fx.mod_on ^= 1; break;
    case FXR_DELAY_ON:  e->fx.delay_on ^= 1; break;
    case FXR_DELAY_SYNC:e->fx.delay_sync ^= 1; break;
    case FXR_REVERB_ON: e->fx.reverb_on ^= 1; break;
    default: return;
    }
    a->dirty = 1;
}

static void draw_fx_page(App *a)
{
    Canvas *c = &a->cv;
    Engine *e = &a->eng;
    int px = 44, py = 14, pw = 552, ph = 448;
    cv_rect(c, px, py, pw, ph, C_PANEL);
    cv_border(c, px, py, pw, ph, 2, C_TEXT);
    cv_text(c, px + 14, py + 10, "PATTERN EFFECTS", 1, C_TEXT);
    char head[80];
    snprintf(head, sizeof head, "P%03d  DRIVE > MOD > DELAY > REVERB", a->lib.active_slot);
    cv_text(c, px + 14, py + 26, head, 1, C_TEXT_DIM);

    int rows[24];
    int n = fx_visible_rows(e, rows);
    if (a->fx_sel >= n) a->fx_sel = n - 1;
    if (a->fx_sel < 0) a->fx_sel = 0;

    int y0 = py + 44, rh = 18;
    char buf[40];
    for (int i = 0; i < n; i++) {
        int row = rows[i];
        int y = y0 + i * rh;
        int sel = (i == a->fx_sel);
        int stage_on = fx_row_stage_on(e, row);
        int toggle = fx_row_is_toggle(row);
        if (sel)
            cv_rect(c, px + 8, y - 3, pw - 16, rh - 2, cv_dim(C_TEXT, 0.26f));

        Uint32 lc = sel ? C_CURSOR
                   : toggle ? C_TEXT
                   : (stage_on ? C_TEXT_DIM : cv_dim(C_TEXT_DIM, 0.55f));
        cv_text(c, px + 22, y, fx_row_label(e, row), 1, lc);

        fx_row_text(e, row, buf, sizeof buf);
        Uint32 vc = sel ? C_CURSOR
                   : toggle ? (e->fx.on || row == FXR_MASTER ? C_TEXT : C_TEXT_DIM)
                   : (stage_on ? C_TEXT : C_TEXT_DIM);
        cv_text(c, px + pw - 22 - cv_text_w(buf, 1), y, buf, 1, vc);
        if (sel)
            cv_border(c, px + 8, y - 3, pw - 16, rh - 2, 1, C_CURSOR);
    }

    cv_text(c, px + 14, py + ph - 18,
            "FX ARE SAVED WITH THIS PATTERN", 1, C_TEXT_DIM);
}

static void draw_rename(App *a)
{    Canvas *c = &a->cv;
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

static const char *SONG_ACT[SONG_ACT_N] = {
    "INSERT BAR", "DELETE BAR", "MOVE BAR UP", "MOVE BAR DOWN",
    "PLAY FROM HERE", "TOGGLE LOOP", "EXPORT WAV", "SAVE SONG",
    "CLEAR SONG", "CANCEL"
};

static void draw_song_list(App *a, int with_actions)
{
    Canvas *c = &a->cv;
    int px = 16, py = 26, pw = 608, ph = 430;
    cv_rect(c, px, py, pw, ph, C_PANEL);
    cv_border(c, px, py, pw, ph, 2, C_TEXT);
    cv_text(c, px + 12, py + 10, "SONG", 1, C_TEXT);

    char buf[80];
    int count = a->song.count;
    int page = count > 0 ? a->song_sel / SONG_ROWS : 0;
    int first = page * SONG_ROWS;
    int last = first + SONG_ROWS - 1;
    if (last >= count) last = count - 1;
    if (count > 0)
        snprintf(buf, sizeof buf, "%03d-%03d / %03d", first + 1, last + 1, count);
    else
        snprintf(buf, sizeof buf, "000-000 / 000");
    cv_text(c, px + pw - cv_text_w(buf, 1) - 12, py + 10, buf, 1, C_TEXT_DIM);

    if (count == 0) {
        cv_text(c, px + 24, py + 60, "EMPTY SONG", 2, C_TEXT_DIM);
        cv_text(c, px + 24, py + 90, "SELECT:ACTIONS -> INSERT BAR", 1, C_TEXT_DIM);
    } else {
        for (int i = 0; i < SONG_ROWS; i++) {
            int bar = first + i;
            if (bar >= count) break;
            int ry = PAT_LIST_Y + i * SONG_ROW_H;
            int sel = (bar == a->song_sel);
            int playing = (a->eng.song_playing && bar == a->eng.song_bar);
            int slot = a->song.slot[bar];
            int has = (slot >= 1 && pattern_slot_exists(&a->lib, slot));
            const char *nm = has ? pattern_name(&a->lib, slot) : "MISSING";

            if (sel)
                cv_rect(c, PAT_LIST_X - 8, ry - 4, PAT_LIST_W, 26, cv_dim(C_TEXT, 0.22f));
            if (playing)
                cv_rect(c, PAT_LIST_X - 8, ry - 4, 4, 26, C_LED);
            snprintf(buf, sizeof buf, "BAR %03d", bar + 1);
            cv_text(c, PAT_LIST_X, ry, buf, 1, C_TEXT);
            snprintf(buf, sizeof buf, "P%03d", slot);
            cv_text(c, PAT_LIST_X + 78, ry, buf, 1, has ? C_TEXT : C_TEXT_DIM);
            cv_text(c, PAT_LIST_X + 126, ry, nm, 1,
                    has ? (sel ? C_CURSOR : C_TEXT) : C_TEXT_DIM);
            if (playing)
                cv_text(c, PAT_LIST_X + PAT_LIST_W - 44, ry, "PLAY", 1, C_LED);
            if (sel)
                cv_border(c, PAT_LIST_X - 8, ry - 4, PAT_LIST_W, 26, 1, C_CURSOR);
        }
    }

    if (count > 0) {
        snprintf(buf, sizeof buf, "%d BARS   LOOP %s", count, a->song.loop ? "ON" : "OFF");
        cv_text(c, px + 12, py + 28, buf, 1, C_TEXT_DIM);
    }

    if (with_actions) {
        int mw = 300, mh = SONG_ACT_N * 18 + 30;
        int mx = (SCR_W - mw) / 2, my = (SCR_H - mh) / 2;
        cv_rect(c, mx, my, mw, mh, C_PANEL);
        cv_border(c, mx, my, mw, mh, 2, C_TEXT);
        if (count > 0)
            snprintf(buf, sizeof buf, "BAR %03d  P%03d", a->song_sel + 1, a->song.slot[a->song_sel]);
        else
            snprintf(buf, sizeof buf, "EMPTY SONG");
        cv_text(c, mx + 10, my + 8, buf, 1, C_TEXT);
        for (int i = 0; i < SONG_ACT_N; i++) {
            int iy = my + 26 + i * 18;
            if (i == a->song_act_sel)
                cv_rect(c, mx + 4, iy - 2, mw - 8, 15, cv_dim(C_TEXT, 0.30f));
            const char *label = SONG_ACT[i];
            char lbuf[32];
            if (i == 5) {
                snprintf(lbuf, sizeof lbuf, "LOOP: %s", a->song.loop ? "ON" : "OFF");
                label = lbuf;
            }
            cv_text(c, mx + 14, iy, label, 1,
                    i == a->song_act_sel ? C_CURSOR : C_TEXT_DIM);
        }
    }
}

static void draw_export_progress(App *a)
{
    Canvas *c = &a->cv;
    int mw = 460, mh = 140;
    int mx = (SCR_W - mw) / 2, my = (SCR_H - mh) / 2;
    cv_rect(c, mx, my, mw, mh, C_PANEL);
    cv_border(c, mx, my, mw, mh, 2, C_TEXT);
    cv_text(c, mx + 16, my + 12, "EXPORTING SONG", 1, C_TEXT);
    cv_text(c, mx + 16, my + 32, a->export_display, 1, C_TEXT_DIM);

    int bx = mx + 20, by = my + 64, bw = mw - 40, bh = 20;
    cv_rect(c, bx, by, bw, bh, 0x1c1c24);
    int fill = bw * a->export_progress / 1000;
    if (fill > 0) cv_rect(c, bx, by, fill, bh, C_LED);
    cv_border(c, bx, by, bw, bh, 2, C_TEXT_DIM);

    char buf[32];
    snprintf(buf, sizeof buf, "%d%%", a->export_progress / 10);
    cv_text(c, mx + (mw - cv_text_w(buf, 1)) / 2, my + 92, buf, 1, C_TEXT);
    cv_text(c, mx + 16, my + 116, "PLEASE WAIT", 1, C_TEXT_DIM);
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

    int header_bpm = e->bpm_x100;
    int header_swing = e->swing_pct;
    int header_len = engine_length_clamp(e->length);
    const PatternSnapshot *hsp = NULL;
    const SongSet *hset = (const SongSet *)e->song_set;
    if (e->song_playing && hset &&
        e->song_bar >= 0 && e->song_bar < hset->count)
        hsp = &hset->pat[e->song_bar];
    if (hsp) {
        header_bpm = hsp->bpm_x100;
        header_swing = hsp->swing_pct;
        header_len = engine_length_clamp(hsp->length);
    }

    cv_text(c, SCR_W - 230, 10, "TEMPO", 1, C_TEXT_DIM);
    snprintf(tbuf, sizeof tbuf, "%6.2f", header_bpm / 100.0);
    cv_text(c, SCR_W - 170, 10, tbuf, 1, C_TEXT);
    if (header_swing > 0)
        snprintf(tbuf, sizeof tbuf, "LEN %02d  SW %d%%", header_len, header_swing);
    else
        snprintf(tbuf, sizeof tbuf, "LEN %02d", header_len);
    cv_text(c, 288, 10, tbuf, 1, C_TEXT_DIM);

    /* active pattern slot + name, or the song bar in song mode */
    if (a->mode == MODE_SONG || a->mode == MODE_SONG_ACT || a->mode == MODE_EXPORT) {
        if (a->song.count > 0) {
            int bar = (e->song_playing && e->song_bar >= 0 && e->song_bar < a->song.count)
                      ? e->song_bar : a->song_sel;
            snprintf(tbuf, sizeof tbuf, "SONG %03d/%03d", bar + 1, a->song.count);
        } else {
            snprintf(tbuf, sizeof tbuf, "SONG 000/000");
        }
        cv_text(c, 112, 10, tbuf, 1, e->song_playing ? C_LED : C_TEXT);
    } else {
        snprintf(tbuf, sizeof tbuf, "P%03d", a->lib.active_slot);
        cv_text(c, 112, 10, tbuf, 1, C_TEXT);
        cv_text(c, 148, 10, pattern_name(&a->lib, a->lib.active_slot), 1, C_TEXT_DIM);
    }

    /* step LED + beat numbers */
    int led = e->cur_step;
    int pat_len = engine_length_clamp(e->length);
    for (int s = 0; s < N_STEPS; s++) {
        int x = GRID_X0 + s * COL_PITCH;
        int in = (s < pat_len);
        if (in && s % 4 == 0) {
            char d[2] = { (char)('1' + s / 4), 0 };
            cv_text(c, x + CELL_W - 8, 40, d, 1, C_TEXT_DIM);
        }
        Uint32 col = !in ? 0x222228
                    : (s == led) ? C_LED
                    : ((s % 4 == 0) ? cv_dim(C_LED, 0.35f) : 0x33333b);
        cv_rect(c, x, 50, CELL_W, 4, col);
    }

    /* grid background bands */
    for (int t = 0; t < N_TRACKS; t++) {
        int y = GRID_Y0 + t * ROW_PITCH;
        int sel = (a->mode == MODE_EDIT && a->cur_track == t);
        Uint32 bg = e->mute[t] ? (sel ? 0x3a2226 : 0x281518)
                               : (sel ? C_PANEL : 0x1c1c24);
        cv_rect(c, 0, y - 3, GRID_X0 - 6, CELL_H + 6, bg);
    }

    /* track labels */
    for (int t = 0; t < N_TRACKS; t++) {
        int y = GRID_Y0 + t * ROW_PITCH + (CELL_H - 8) / 2;
        int on_row = (a->mode == MODE_EDIT || a->mode == MODE_KIT || a->mode == MODE_RCH) && a->cur_track == t;
        int muted = e->mute[t];
        cv_rect(c, 8, GRID_Y0 + t * ROW_PITCH, 4, CELL_H,
                muted ? cv_dim(TRACKS[t].color, 0.30f) : TRACKS[t].color);
        cv_text(c, 22, y, TRACKS[t].label, 1,
                muted ? cv_dim(TRACKS[t].color, 0.40f)
                      : (on_row ? TRACKS[t].color : C_TEXT_DIM));
        if (variant_count(t) > 1) {                  /* current variant number */
            snprintf(tbuf, sizeof tbuf, "%d", e->kit[t] + 1);
            cv_text(c, 46, y, tbuf, 1, muted ? cv_dim(C_TEXT_DIM, 0.55f) : C_TEXT_DIM);
        }
        if (muted)                                   /* performance mute marker */
            cv_text(c, 64, y, "M", 1, C_LED);
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
                int in = (s < pat_len);
                unsigned char lv = e->lvl[t][s];
                unsigned char r = e->rch[t][s];
                Uint32 fill = !in ? cv_dim(col, 0.06f)
                            : lv ? (r ? cv_dim(col, 0.55f) : cv_dim(col, 0.25f))
                                 : cv_dim(col, 0.10f);
                cv_rect(c, x, y, CELL_W, CELL_H, fill);
                if (r) {
                    char dch[2] = { RCH_CH[r], 0 };
                    cv_text(c, x + (CELL_W - 8) / 2, y + (CELL_H - 8) / 2, dch, 1,
                            !in ? cv_dim(C_TEXT_DIM, 0.55f) : (lv ? col : C_TEXT_DIM));
                }
                if (in && s == led && e->playing)
                    cv_border(c, x, y, CELL_W, CELL_H, 2, C_LED);
            }
        }
    } else {
    for (int t = 0; t < N_TRACKS; t++) {
        Uint32 col = TRACKS[t].color;
        for (int s = 0; s < N_STEPS; s++) {
            int x = GRID_X0 + s * COL_PITCH;
            int y = GRID_Y0 + t * ROW_PITCH;
            int in = (s < pat_len);
            unsigned char lv = e->lvl[t][s];
            unsigned char r = e->rch[t][s];
            Uint32 fill = !in ? cv_dim(col, 0.06f)
                        : lv == LVL_OFF ? cv_dim(col, 0.20f)
                        : lv == LVL_ON  ? cv_dim(col, 0.65f)
                                        : col;
            cv_rect(c, x, y, CELL_W, CELL_H, fill);
            if (in && lv == LVL_ACC)
                cv_border(c, x + 5, y + 5, CELL_W - 10, CELL_H - 10, 2, C_ACCENT);
            if (in && lv > LVL_OFF && r > RCH_OFF) {
                char dch[2] = { RCH_CH[r], 0 };
                cv_text(c, x + (CELL_W - 8) / 2, y + CELL_H - 10, dch, 1,
                        lv == LVL_ACC ? 0x000000u : C_TEXT);
            }
            /* playing column highlight */
            if (in && s == led && e->playing)
                cv_border(c, x, y, CELL_W, CELL_H, 2, C_LED);
        }
    }
    }

    /* end-of-pattern marker: steps past the pattern length stay visible but
     * dimmed, so shortening a pattern never destroys hidden notes */
    if (a->mode != MODE_KIT && pat_len < N_STEPS) {
        int gx = GRID_X0 + pat_len * COL_PITCH - 3;
        int gy = GRID_Y0 + (N_TRACKS - 1) * ROW_PITCH + CELL_H;
        cv_rect(c, gx, 50, 2, gy - 50, C_TEXT_DIM);
    }

    /* cursor */
    if (a->mode == MODE_KIT ||
        ((a->mode == MODE_EDIT || a->mode == MODE_RCH) && a->cur_step < pat_len)) {
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
    } else if (a->mode == MODE_SONG) {
        draw_song_list(a, 0);
    } else if (a->mode == MODE_SONG_ACT) {
        draw_song_list(a, 1);
    } else if (a->mode == MODE_FX) {
        draw_fx_page(a);
    } else if (a->mode == MODE_EXPORT) {
        draw_export_progress(a);
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
    else if (a->mode == MODE_PATTERN) {
        if (a->pat_pick_target > 0)
            cv_text(c, 10, SCR_H - 14, "A:PICK  B:CANCEL  DPAD-LR:PAGE", 1, C_TEXT_DIM);
        else
            cv_text(c, 10, SCR_H - 14, "A:LOAD  SELECT:ACTIONS  DPAD-LR:PAGE  B:BACK  START:PLAY", 1, C_TEXT_DIM);
    }
    else if (a->mode == MODE_PAT_ACT)
        cv_text(c, 10, SCR_H - 14, "UD:ACTION  LR:SLOT  A:SELECT  B:CANCEL", 1, C_TEXT_DIM);
    else if (a->mode == MODE_RENAME)
        cv_text(c, 10, SCR_H - 14, "UD:CHAR  LR:MOVE  A:NEXT  X:DEL  SELECT:SAVE  B:CANCEL", 1, C_TEXT_DIM);
    else if (a->mode == MODE_SONG) {
        if (a->song.count == 0)
            cv_text(c, 10, SCR_H - 14, "A:ADD BAR  SELECT:ACTIONS  B:BACK  START:PLAY", 1, C_TEXT_DIM);
        else
            cv_text(c, 10, SCR_H - 14, "A:PICK  SELECT:ACTIONS  DPAD-LR:PAGE  X:JUMP  B:BACK  START:PLAY", 1, C_TEXT_DIM);
    }
    else if (a->mode == MODE_SONG_ACT)
        cv_text(c, 10, SCR_H - 14, "UD:ACTION  LR:BAR  A:SELECT  B:CANCEL", 1, C_TEXT_DIM);
    else if (a->mode == MODE_FX)
        cv_text(c, 10, SCR_H - 14, "UD:SELECT  LR:ADJUST  A:TOGGLE  B:BACK", 1, C_TEXT_DIM);
    else if (a->mode == MODE_EXPORT)
        cv_text(c, 10, SCR_H - 14, "EXPORTING WAV - PLEASE WAIT", 1, C_TEXT_DIM);
    else
        cv_text(c, 10, SCR_H - 14, "A:STEP  B:CLEAR  X:HEAR  Y:MUTE  START:PLAY  SELECT:MENU  HOLD A SHOULDER", 1, C_TEXT_DIM);

    if (a->mode == MODE_MENU) {
        static const char *items[MENU_N] = {
            "EDIT TEMPO", "EDIT SWING", "PATTERN LENGTH", "RATCHET NOTES",
            "SELECT KITS", "SAVE PATTERN", "PATTERN LIBRARY", "SONG MODE",
            "EFFECTS", "CLEAR ALL", "QUIT"
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
    } else if (a->mode == MODE_LENGTH) {
        int mw = 300, mh = 80;
        int mx = (SCR_W - mw) / 2, my = (SCR_H - mh) / 2;
        cv_rect(c, mx, my, mw, mh, C_PANEL);
        cv_border(c, mx, my, mw, mh, 2, C_TEXT);
        cv_text(c, mx + 12, my + 10, "PATTERN LENGTH", 1, C_TEXT);
        snprintf(tbuf, sizeof tbuf, "%d STEPS", engine_length_clamp(e->length));
        cv_text(c, mx + 12, my + 34, tbuf, 2, C_TEXT);
        cv_text(c, mx + 12, my + 62, "UD:+-1  LR:+-4  A/B:OK", 1, C_TEXT_DIM);
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

/* -------------------------------------------------------------- song mode */

static void stop_playback(App *a)
{
    if (a->eng.song_playing) engine_song_stop(&a->eng);
    a->eng.playing = 0;
    if (a->edit_fx_valid) {
        fx_apply(&a->eng, &a->edit_fx);
        a->edit_fx_valid = 0;
    }
}

static void toggle_edit_play(App *a)
{
    if (a->eng.song_playing) stop_playback(a);
    a->eng.playing ^= 1;
    if (a->eng.playing) a->eng.restart = 1;
}

/* Build a fresh immutable snapshot set in the inactive buffer. Stops playback
 * first; the old set stays valid for any callback still using it. */
static SongSet *song_prepare(App *a, int *missing_out)
{
    if (missing_out) *missing_out = 0;
    if (!a->song_sets[0] || !a->song_sets[1]) return NULL;
    stop_playback(a);

    /* Wait for one audio callback to finish after stopping, so the buffer we
     * are about to overwrite is no longer being read by the audio thread. */
    if (a->dev) {
        unsigned int before = a->eng.mix_count;
        Uint32 deadline = SDL_GetTicks() + 100;
        while (a->eng.mix_count == before && (int)(SDL_GetTicks() - deadline) < 0)
            SDL_Delay(1);
    }

    int idx = 1 - a->song_set_active;
    SongSet *set = a->song_sets[idx];
    int missing = song_build_set(&a->song, &a->lib, set);
    if (missing < 0) return NULL;
    a->song_set_active = idx;
    a->eng.song_set = set;
    if (missing_out) *missing_out = missing;
    return set;
}

static void song_play(App *a, int from_bar)
{
    if (a->song.count <= 0) { set_msg(a, "SONG EMPTY"); return; }
    int missing = 0;
    SongSet *set = song_prepare(a, &missing);
    if (!set || set->count <= 0) { set_msg(a, "SONG BUILD FAILED"); return; }
    if (from_bar < 0) from_bar = 0;
    if (from_bar >= set->count) from_bar = set->count - 1;
    a->song_sel = from_bar;
    fx_capture(&a->edit_fx, &a->eng);   /* restore after the song stops */
    a->edit_fx_valid = 1;
    engine_song_start(&a->eng, set, a->song.loop, from_bar);
    if (missing > 0) set_msg(a, "PLAYING (%d EMPTY BARS)", missing);
    else set_msg(a, "PLAYING SONG");
}

static void song_toggle_play(App *a)
{
    if (a->eng.song_playing) {
        stop_playback(a);
        set_msg(a, "SONG STOPPED");
    } else {
        song_play(a, 0);
    }
}

static void song_insert(App *a)
{
    if (a->song.count >= SONG_MAX_BARS) { set_msg(a, "SONG FULL"); return; }
    int at, slot;
    if (a->song.count == 0) {
        at = 0;
        slot = a->lib.active_slot;
        if (slot < 1 || !pattern_slot_exists(&a->lib, slot)) {
            set_msg(a, "SAVE A PATTERN FIRST");
            return;
        }
    } else {
        at = a->song_sel + 1;
        slot = a->song.slot[a->song_sel];
    }
    memmove(&a->song.slot[at + 1], &a->song.slot[at],
            (size_t)(a->song.count - at) * sizeof(int));
    a->song.slot[at] = slot;
    a->song.count++;
    a->song_sel = at;
    a->song_dirty = 1;
    set_msg(a, "BAR %03d ADDED", at + 1);
}

static void song_delete(App *a)
{
    if (a->song.count <= 0) { set_msg(a, "SONG EMPTY"); return; }
    int at = a->song_sel;
    memmove(&a->song.slot[at], &a->song.slot[at + 1],
            (size_t)(a->song.count - at - 1) * sizeof(int));
    a->song.count--;
    if (a->song.count == 0) a->song_sel = 0;
    else if (a->song_sel >= a->song.count) a->song_sel = a->song.count - 1;
    a->song_dirty = 1;
    set_msg(a, "BAR %03d DELETED", at + 1);
}

static void song_move(App *a, int dir)
{
    if (a->song.count < 2) return;
    int at = a->song_sel;
    int to = at + dir;
    if (to < 0 || to >= a->song.count) return;
    int tmp = a->song.slot[at];
    a->song.slot[at] = a->song.slot[to];
    a->song.slot[to] = tmp;
    a->song_sel = to;
    a->song_dirty = 1;
}

static void song_begin_pick(App *a)
{
    if (a->song.count == 0) {
        song_insert(a);
        return;
    }
    stop_playback(a);
    a->pat_pick_target = a->song_sel + 1;
    a->pat_sel = a->song.slot[a->song_sel];
    if (a->pat_sel < 1 || a->pat_sel > PAT_MAX_SLOTS)
        a->pat_sel = a->lib.active_slot;
    if (a->pat_sel < 1 || a->pat_sel > PAT_MAX_SLOTS)
        a->pat_sel = 1;
    a->mode = MODE_PATTERN;
}

static void song_assign_pick(App *a)
{
    int bar = a->pat_pick_target - 1;
    if (bar < 0 || bar >= a->song.count) {
        a->pat_pick_target = 0;
        a->mode = MODE_SONG;
        return;
    }
    if (!pattern_slot_exists(&a->lib, a->pat_sel)) {
        set_msg(a, "P%03d EMPTY", a->pat_sel);
        return;
    }
    a->song.slot[bar] = a->pat_sel;
    a->song_sel = bar;
    a->pat_pick_target = 0;
    a->mode = MODE_SONG;
    a->song_dirty = 1;
    if (song_save(&a->song, a->song_path) == 0) {
        a->song_dirty = 0;
        a->song_last_save = SDL_GetTicks();
    }
    set_msg(a, "BAR %03d = P%03d", bar + 1, a->pat_sel);
}

static int song_save_now(App *a)
{
    int rc = song_save(&a->song, a->song_path);
    if (rc == 0) {
        a->song_dirty = 0;
        a->song_last_save = SDL_GetTicks();
    }
    return rc;
}

static void open_song_mode(App *a)
{
    stop_playback(a);
    a->mode = MODE_SONG;
    a->song_sel = 0;
    if (a->song.count == 0) set_msg(a, "SONG EMPTY - SELECT:ACTIONS");
}

/* Stream the song to a PCM16 WAV, writing chunks straight to disk. */
static int render_song_wav(App *a, SongSet *set, const char *path, int show_progress)
{
    const int rate = 44100;
    const int tail = rate * 2;                 /* let the last hits ring */
    long long total = song_total_frames(set, rate, tail);
    if (total <= 0) return -1;
    if (total > (long long)(0xFFFFFFFFu / 4u)) return -1;

    WavWriter w;
    if (wav_writer_open(&w, path, rate) != 0) return -1;

    Engine ex;
    engine_init(&ex, rate);
    FxSettings fx;
    fx_capture(&fx, &a->eng);
    fx_apply(&ex, &fx);
    ex.song_set = set;
    ex.song_loop = 0;
    ex.song_start_bar = 0;
    ex.song_playing = 1;
    ex.playing = 1;
    engine_restart(&ex);

    float buf[1024 * 2];
    long long done = 0;
    Uint32 last = 0;
    if (show_progress) {
        a->export_progress = 0;
        draw_ui(a);
        present(a);
        last = SDL_GetTicks();
    }

    while (done < total) {
        int n = (int)((total - done) > 1024 ? 1024 : (total - done));
        engine_mix(&ex, buf, n);
        if (wav_writer_write(&w, buf, n) != 0) {
            engine_free(&ex);
            wav_writer_close(&w);
            return -1;
        }
        done += n;
        if (show_progress) {
            Uint32 now = SDL_GetTicks();
            if (now - last > 100 || done >= total) {
                a->export_progress = (int)(done * 1000 / total);
                draw_ui(a);
                present(a);
                last = now;
            }
        }
    }

    if (show_progress) {
        a->export_progress = 1000;
        draw_ui(a);
        present(a);
    }
    int rc = wav_writer_close(&w);
    engine_free(&ex);
    return rc;
}

static void song_export_ui(App *a)
{
    if (a->song.count <= 0) { set_msg(a, "SONG EMPTY"); a->mode = MODE_SONG; return; }
    int missing = 0;
    SongSet *set = song_prepare(a, &missing);
    if (!set) { set_msg(a, "EXPORT FAILED"); a->mode = MODE_SONG; return; }

    a->mode = MODE_EXPORT;
    a->export_progress = 0;
    snprintf(a->export_display, sizeof a->export_display, "conf/song.wav");
    int rc = render_song_wav(a, set, a->song_wav_path, 1);
    a->mode = MODE_SONG;
    if (rc != 0) set_msg(a, "EXPORT FAILED");
    else if (missing > 0) set_msg(a, "EXPORTED (%d EMPTY BARS)", missing);
    else set_msg(a, "EXPORTED conf/song.wav");
}

static void song_do_action(App *a, int item)
{
    switch (item) {
    case 0: song_insert(a); a->mode = MODE_SONG; break;
    case 1: song_delete(a); a->mode = MODE_SONG; break;
    case 2: song_move(a, -1); a->mode = MODE_SONG; break;
    case 3: song_move(a, 1); a->mode = MODE_SONG; break;
    case 4:
        a->mode = MODE_SONG;
        song_play(a, a->song_sel);
        break;
    case 5:
        a->song.loop ^= 1;
        a->song_dirty = 1;
        engine_song_set_loop(&a->eng, a->song.loop);
        set_msg(a, "LOOP %s", a->song.loop ? "ON" : "OFF");
        break;
    case 6:
        song_export_ui(a);
        break;
    case 7:
        if (song_save_now(a) == 0) set_msg(a, "SONG SAVED");
        else set_msg(a, "SONG SAVE FAILED");
        a->mode = MODE_SONG;
        break;
    case 8:
        a->song.count = 0;
        a->song_sel = 0;
        a->song_dirty = 1;
        set_msg(a, "SONG CLEARED");
        a->mode = MODE_SONG;
        break;
    default:
        a->mode = MODE_SONG;
        break;
    }
}

/* Headless song persistence/snapshot checks (`--test-song`). */
static void act(App *a, int act);

static int test_song_run(App *a)
{
    int fails = 0;
    SDL_Init(SDL_INIT_TIMER);
#define CHECK(cond, msg) do { \
    if (cond) printf("PASS: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); fails++; } \
} while (0)

    Song s;
    song_init(&s);
    int rc = song_load(&s, a->song_path);
    CHECK(rc == 0, "song file loads");
    CHECK(s.count == 4, "song has 4 bars");
    CHECK(s.loop == 0, "loop flag loaded");
    CHECK(s.slot[0] == 1 && s.slot[1] == 2 && s.slot[2] == 3 && s.slot[3] == 1,
          "bar slots loaded");

    SongSet *set = calloc(1, sizeof(SongSet));
    if (!set) return 1;
    int missing = song_build_set(&s, &a->lib, set);
    CHECK(missing == 0, "all song bars load");
    CHECK(set->count == 4, "snapshot set has 4 bars");
    CHECK(set->pat[0].bpm_x100 == 12000, "bar 1 uses 120 BPM");
    CHECK(set->pat[1].bpm_x100 == 25000, "bar 2 uses 250 BPM");
    CHECK(set->pat[2].bpm_x100 == 9050, "bar 3 uses 90.5 BPM");
    long long frames = song_total_frames(set, 44100, 44100 * 2);
    CHECK(frames == 423886, "song length math");

    /* Engine clock: bar advance, non-loop end, loop wrap. */
    long long song_frames = song_total_frames(set, 44100, 0);
    int bar1_steps = engine_length_clamp(set->pat[0].length);
    long long one_bar = (long long)(44100.0 * 60.0 / (120.0 * 4.0) * bar1_steps);
    float buf[2048];
    Engine ex;
    engine_init(&ex, 44100);
    engine_song_start(&ex, set, 0, 0);
    engine_restart(&ex);
    long long done = 0;
    while (done < one_bar + 1024) {
        int n = (int)((one_bar + 1024 - done) > 1024 ? 1024 : (one_bar + 1024 - done));
        engine_mix(&ex, buf, n);
        done += n;
    }
    CHECK(ex.song_bar == 1, "song advances to the next bar");
    CHECK(ex.song_playing == 1, "song keeps playing after a bar");

    while (done < song_frames + 2048) {
        int n = (int)((song_frames + 2048 - done) > 1024 ? 1024 : (song_frames + 2048 - done));
        engine_mix(&ex, buf, n);
        done += n;
    }
    CHECK(ex.song_playing == 0 && ex.song_done == 1, "non-loop song ends");
    CHECK(ex.song_bar == 3, "song ends on the last bar");

    engine_free(&ex);
    engine_init(&ex, 44100);
    engine_song_start(&ex, set, 1, 0);
    engine_restart(&ex);
    done = 0;
    while (done < song_frames + 1024) {
        int n = (int)((song_frames + 1024 - done) > 1024 ? 1024 : (song_frames + 1024 - done));
        engine_mix(&ex, buf, n);
        done += n;
    }
    CHECK(ex.song_playing == 1 && ex.song_done == 0, "looping song wraps");
    CHECK(ex.song_bar == 0, "looping song returns to bar 1");
    engine_free(&ex);

    /* Live callback chunking (512) vs export chunking (1024) must be identical. */
    long long nframes = song_frames;
    float *la = malloc(sizeof(float) * 2 * (size_t)nframes);
    float *lb = malloc(sizeof(float) * 2 * (size_t)nframes);
    if (la && lb) {
        Engine ea, eb;
        engine_init(&ea, 44100);
        engine_song_start(&ea, set, 0, 0);
        engine_restart(&ea);
        done = 0;
        while (done < nframes) {
            int n = (int)((nframes - done) > 512 ? 512 : (nframes - done));
            engine_mix(&ea, la + done * 2, n);
            done += n;
        }
        engine_init(&eb, 44100);
        engine_song_start(&eb, set, 0, 0);
        engine_restart(&eb);
        done = 0;
        while (done < nframes) {
            int n = (int)((nframes - done) > 1024 ? 1024 : (nframes - done));
            engine_mix(&eb, lb + done * 2, n);
            done += n;
        }
        CHECK(memcmp(la, lb, sizeof(float) * 2 * (size_t)nframes) == 0,
              "live/export chunk sizes render identically");
        engine_free(&ea);
        engine_free(&eb);
    } else {
        CHECK(0, "chunk-test buffers");
    }
    free(la);
    free(lb);

    free(set);

    Song t = s;
    t.loop = 1;
    t.slot[1] = 3;
    char tmp[1210];
    snprintf(tmp, sizeof tmp, "%s.test", a->song_path);
    CHECK(song_save(&t, tmp) == 0, "song saves");
    Song r;
    song_init(&r);
    CHECK(song_load(&r, tmp) == 0, "song reloads");
    CHECK(r.count == 4 && r.loop == 1 && r.slot[1] == 3,
          "song save/load round-trip");
    remove(tmp);

    /* UI/action logic. */
    a->song = s;
    a->mode = MODE_SONG;
    a->song_sel = 0;
    act(a, ACT_DOWN);
    CHECK(a->song_sel == 1, "song cursor moves");

    a->mode = MODE_SONG_ACT;
    a->song_sel = 0;
    a->song_act_sel = 0;
    act(a, ACT_DOWN);
    CHECK(a->song_act_sel == 1, "song action cursor moves");
    act(a, ACT_RIGHT);
    CHECK(a->song_sel == 1, "song action menu moves the target bar");

    a->song = s;
    a->song_sel = 0;
    a->song_dirty = 0;
    song_do_action(a, 0);
    CHECK(a->song.count == 5 && a->song.slot[1] == 1,
          "insert bar duplicates the selection");
    song_do_action(a, 1);
    CHECK(a->song.count == 4, "delete bar");
    a->song_sel = 1;
    song_do_action(a, 2);
    CHECK(a->song.slot[0] == 2 && a->song_sel == 0, "move bar up");
    song_do_action(a, 3);
    CHECK(a->song.slot[0] == 1 && a->song_sel == 1, "move bar down");
    song_do_action(a, 5);
    CHECK(a->song.loop == 1, "loop toggle");
    song_do_action(a, 8);
    CHECK(a->song.count == 0, "clear song");

    a->song = s;
    a->song_sets[0] = calloc(1, sizeof(SongSet));
    a->song_sets[1] = calloc(1, sizeof(SongSet));
    if (a->song_sets[0] && a->song_sets[1]) {
        a->mode = MODE_SONG;
        a->song_sel = 0;
        a->eng.playing = 0;
        a->eng.song_playing = 0;
        act(a, ACT_PLAY);
        CHECK(a->eng.song_playing == 1, "song START begins playback");
        act(a, ACT_PLAY);
        CHECK(a->eng.song_playing == 0, "song START stops playback");
    } else {
        CHECK(0, "song test buffers");
    }
    free(a->song_sets[0]);
    free(a->song_sets[1]);
    a->song_sets[0] = a->song_sets[1] = NULL;

    SDL_Quit();
    return fails ? 1 : 0;
#undef CHECK
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
    case 2: a->mode = MODE_LENGTH; clamp_cursor(a); break;
    case 3: a->mode = MODE_RCH; break;
    case 4:
        a->mode = MODE_KIT;
        a->cur_step = a->eng.kit[a->cur_track];   /* land cursor on current pick */
        break;
    case 5:
        if (save_current(a) == 0) set_msg(a, "SAVED P%03d", a->lib.active_slot);
        else set_msg(a, "SAVE FAILED");
        a->mode = MODE_EDIT;
        break;
    case 6:
        a->mode = MODE_PATTERN;
        a->pat_sel = a->lib.active_slot;
        break;
    case 7:
        open_song_mode(a);
        break;
    case 8:
        a->mode = MODE_FX;
        a->fx_sel = 0;
        break;
    case 9:
        for (int t = 0; t < N_TRACKS; t++)
            for (int s = 0; s < N_STEPS; s++) {
                a->eng.lvl[t][s] = 0;
                a->eng.rch[t][s] = 0;
            }
        a->dirty = 1;
        set_msg(a, "PATTERN CLEARED");
        a->mode = MODE_EDIT;
        break;
    case 10: a->quit = 1; break;
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
        case ACT_LEFT:  move_step(a, -1); break;
        case ACT_RIGHT: move_step(a, 1); break;
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
            engine_preview(e, a->cur_track, e->kit[a->cur_track],
                           e->rch[a->cur_track][a->cur_step], LVL_ON);
            break;
        case ACT_MUTE: {
            int t = a->cur_track;
            int on = !e->mute[t];
            e->mute[t] = (unsigned char)on;
            if (on) e->mute_kill_mask |= 1u << t;
            else    e->mute_kill_mask &= ~(1u << t);
            set_msg(a, on ? "MUTE %s" : "UNMUTE %s", TRACKS[t].label);
            break;
        }
        case ACT_PLAY:
            toggle_edit_play(a);   /* consumed in audio cb */
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
        case ACT_MUTE:
            engine_preview(e, t, a->cur_step, RCH_OFF, LVL_ACC);
            break;
        case ACT_PLAY:
            toggle_edit_play(a);
            break;
        case ACT_BACK:
        case ACT_MENU:
            a->mode = MODE_EDIT;
            clamp_cursor(a);          /* variant index may exceed the new length */
            break;
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
        if (act != ACT_CONFIRM && act != ACT_BACK && act != ACT_MENU &&
            act != ACT_HEAR && act != ACT_MUTE) a->dirty = 1;
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
        if (act != ACT_CONFIRM && act != ACT_BACK && act != ACT_MENU &&
            act != ACT_HEAR && act != ACT_MUTE) {
            if (en->swing_pct < 0) en->swing_pct = 0;
            if (en->swing_pct > 100) en->swing_pct = 100;
            a->dirty = 1;
        }
        if (act == ACT_CONFIRM || act == ACT_BACK || act == ACT_MENU) a->mode = MODE_EDIT;
        break;
    }
    case MODE_LENGTH: {
        Engine *en = &a->eng;
        switch (act) {
        case ACT_TEMPO_UP:   en->length += 4; break;
        case ACT_TEMPO_DOWN: en->length -= 4; break;
        case ACT_RIGHT:      en->length += 4; break;
        case ACT_LEFT:       en->length -= 4; break;
        case ACT_UP:         en->length += 1; break;
        case ACT_DOWN:       en->length -= 1; break;
        default: break;
        }
        if (act != ACT_CONFIRM && act != ACT_BACK && act != ACT_MENU &&
            act != ACT_HEAR && act != ACT_MUTE) {
            en->length = engine_length_clamp(en->length);
            if (a->cur_step >= en->length) a->cur_step = en->length - 1;
            a->dirty = 1;
        }
        if (act == ACT_CONFIRM || act == ACT_BACK || act == ACT_MENU) {
            clamp_cursor(a);
            a->mode = MODE_EDIT;
        }
        break;
    }
    case MODE_FX: {
        int rows[24];
        int n = fx_visible_rows(e, rows);
        if (n < 1) break;
        if (a->fx_sel >= n) a->fx_sel = n - 1;
        if (a->fx_sel < 0) a->fx_sel = 0;
        switch (act) {
        case ACT_UP:
            a->fx_sel = (a->fx_sel + n - 1) % n;
            break;
        case ACT_DOWN:
            a->fx_sel = (a->fx_sel + 1) % n;
            break;
        case ACT_LEFT:
            fx_row_adjust(a, rows[a->fx_sel], -1);
            break;
        case ACT_RIGHT:
            fx_row_adjust(a, rows[a->fx_sel], 1);
            break;
        case ACT_CONFIRM:
            fx_row_toggle(a, rows[a->fx_sel]);
            break;
        case ACT_PLAY:
            toggle_edit_play(a);
            break;
        case ACT_BACK:
        case ACT_MENU:
            a->mode = MODE_EDIT;
            break;
        default: break;
        }
        break;
    }
    case MODE_RCH: {
        int t = a->cur_track;
        switch (act) {
        case ACT_UP:    a->cur_track = (t + N_TRACKS - 1) % N_TRACKS; break;
        case ACT_DOWN:  a->cur_track = (t + 1) % N_TRACKS; break;
        case ACT_LEFT:  move_step(a, -1); break;
        case ACT_RIGHT: move_step(a, 1); break;
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
        case ACT_MUTE:
            engine_preview(e, t, e->kit[t], e->rch[t][a->cur_step],
                           e->lvl[t][a->cur_step] ? e->lvl[t][a->cur_step] : LVL_ON);
            break;
        case ACT_PLAY:
            toggle_edit_play(a);
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
        case ACT_CONFIRM:
            if (a->pat_pick_target > 0) song_assign_pick(a);
            else pattern_do_action(a, 0);
            break;
        case ACT_MENU:
            if (a->pat_pick_target == 0) {
                a->mode = MODE_PAT_ACT;
                a->pat_act_sel = 0;
            }
            break;
        case ACT_BACK:
            if (a->pat_pick_target > 0) {
                a->pat_pick_target = 0;
                a->mode = MODE_SONG;
            } else {
                a->mode = MODE_EDIT;
            }
            break;
        case ACT_PLAY:
            if (a->pat_pick_target == 0) toggle_edit_play(a);
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
        case ACT_MUTE:
        case ACT_CLEAR:
            if (a->rename_pos > 0) a->rename_pos--;
            a->rename_buf[a->rename_pos] = ' ';
            break;
        case ACT_MENU: pattern_commit_rename(a); break;
        case ACT_BACK: a->mode = MODE_PATTERN; a->pat_sel = a->rename_slot; break;
        case ACT_PLAY:
            toggle_edit_play(a);
            break;
        default: break;
        }
        break;
    case MODE_SONG:
        switch (act) {
        case ACT_UP:
            if (a->song.count > 0)
                a->song_sel = (a->song_sel + a->song.count - 1) % a->song.count;
            break;
        case ACT_DOWN:
            if (a->song.count > 0)
                a->song_sel = (a->song_sel + 1) % a->song.count;
            break;
        case ACT_LEFT:
            a->song_sel -= SONG_ROWS;
            if (a->song_sel < 0) a->song_sel = 0;
            break;
        case ACT_RIGHT:
            if (a->song.count > 0) {
                a->song_sel += SONG_ROWS;
                if (a->song_sel >= a->song.count) a->song_sel = a->song.count - 1;
            }
            break;
        case ACT_CONFIRM: song_begin_pick(a); break;
        case ACT_HEAR:
        case ACT_MUTE:
            if (a->eng.song_playing && a->song.count > 0) {
                a->song_sel = a->eng.song_bar;
                if (a->song_sel < 0) a->song_sel = 0;
                if (a->song_sel >= a->song.count) a->song_sel = a->song.count - 1;
                set_msg(a, "BAR %03d", a->song_sel + 1);
            }
            break;
        case ACT_PLAY: song_toggle_play(a); break;
        case ACT_MENU:
            stop_playback(a);
            a->mode = MODE_SONG_ACT;
            a->song_act_sel = 0;
            break;
        case ACT_BACK:
            stop_playback(a);
            a->mode = MODE_EDIT;
            break;
        default: break;
        }
        break;
    case MODE_SONG_ACT:
        switch (act) {
        case ACT_UP:
            a->song_act_sel = (a->song_act_sel + SONG_ACT_N - 1) % SONG_ACT_N;
            break;
        case ACT_DOWN:
            a->song_act_sel = (a->song_act_sel + 1) % SONG_ACT_N;
            break;
        case ACT_LEFT:
            if (a->song.count > 0)
                a->song_sel = (a->song_sel + a->song.count - 1) % a->song.count;
            break;
        case ACT_RIGHT:
            if (a->song.count > 0)
                a->song_sel = (a->song_sel + 1) % a->song.count;
            break;
        case ACT_CONFIRM: song_do_action(a, a->song_act_sel); break;
        case ACT_BACK:
        case ACT_MENU: a->mode = MODE_SONG; break;
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

/* ---------------------------------------------------------- punch-in FX */
/* R1 is a held performance layer: R1 + direction / face button selects one of
 * eight effects. The engine quantizes the start to the next 16th, and the UI
 * tracks which effect is held so release always returns to live audio. */
static int punch_enabled(const App *a)
{
    return a->sh_held[SH_R1] &&
           a->mode != MODE_RENAME && a->mode != MODE_EXPORT &&
           a->pat_pick_target == 0;
}

static int punch_for_dir(int ad)
{
    switch (ad) {
    case ACT_LEFT:  return PUNCH_LOOP_4;
    case ACT_RIGHT: return PUNCH_LOOP_8;
    case ACT_UP:    return PUNCH_LOOP_16;
    case ACT_DOWN:  return PUNCH_LOOP_32;
    default:        return -1;
    }
}

static int punch_for_gc_button(int button)
{
    switch (button) {
    case SDL_CONTROLLER_BUTTON_A: return PUNCH_REWIND;
    case SDL_CONTROLLER_BUTTON_B: return PUNCH_GATE;
    case SDL_CONTROLLER_BUTTON_X: return PUNCH_TAPESTOP;
    case SDL_CONTROLLER_BUTTON_Y: return PUNCH_PHONE;
    default:                      return -1;
    }
}

static int punch_for_raw_button(int button)
{
    switch (button) {
    case 0: return PUNCH_REWIND;     /* A */
    case 1: return PUNCH_GATE;       /* B */
    case 2: return PUNCH_TAPESTOP;   /* X */
    case 3: return PUNCH_PHONE;      /* Y */
    default: return -1;
    }
}

static int punch_for_key(SDL_Keycode k)
{
    switch (k) {
    case SDLK_z: case SDLK_RETURN: return PUNCH_REWIND;
    case SDLK_x:                   return PUNCH_GATE;
    case SDLK_c:                   return PUNCH_TAPESTOP;
    case SDLK_y:                   return PUNCH_PHONE;
    default:                       return -1;
    }
}

static int punch_press(App *a, int effect)
{
    if (!punch_enabled(a) || effect < 0 || effect >= PUNCH_COUNT) return 0;
    if (a->punch_held >= 0) return 0;          /* one at a time */
    if (engine_punch_active(&a->eng) >= 0 ||
        engine_punch_pending(&a->eng) >= 0) return 0;
    a->punch_held = effect;
    engine_punch_trigger(&a->eng, effect);
    return 1;
}

static void punch_release_effect(App *a, int effect)
{
    if (a->punch_held < 0) return;
    if (effect >= 0 && a->punch_held != effect) return;
    a->punch_held = -1;
    engine_punch_release(&a->eng);
}

static void punch_release_all(App *a)
{
    punch_release_effect(a, -1);
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
        if (sh == SH_R1 && !a->sh_held[sh]) punch_release_all(a);
        a->sh_held[sh] = 1;
        a->sh_last = sh;
        a->sh_suppress = 0;
        a->shortcut_until = now + 1500;
        held_dir = 0;                 /* stop any cursor auto-repeat */
        return;
    }

    if (sh == SH_R1) punch_release_all(a);
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
    if (a->eng.song_playing) stop_playback(a);
    switch (sh) {
    case SH_L1:
        if (ad == ACT_LEFT)  { a->mode = MODE_TEMPO; return 1; }
        if (ad == ACT_RIGHT) { a->mode = MODE_SWING; return 1; }
        if (ad == ACT_UP)    { a->mode = MODE_LENGTH; return 1; }
        if (ad == ACT_DOWN)  { a->mode = MODE_RCH; return 1; }
        break;
    case SH_L2:
        if (ad == ACT_LEFT) {
            a->mode = MODE_PATTERN;
            a->pat_sel = a->lib.active_slot;
            return 1;
        }
        if (ad == ACT_RIGHT) {
            open_song_mode(a);
            return 1;
        }
        if (ad == ACT_UP) {
            a->mode = MODE_KIT;
            a->cur_step = a->eng.kit[a->cur_track];
            return 1;
        }
        break;
    case SH_R2:
        if (ad == ACT_UP)    { fx_open_section(a, FXR_DRIVE_ON); return 1; }
        if (ad == ACT_LEFT)  { fx_open_section(a, FXR_MOD_ON); return 1; }
        if (ad == ACT_DOWN)  { fx_open_section(a, FXR_DELAY_ON); return 1; }
        if (ad == ACT_RIGHT) { fx_open_section(a, FXR_REVERB_ON); return 1; }
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
    if (a->pat_pick_target > 0) return 0;
    return a->mode != MODE_RENAME;
}

/* Intercept a D-pad direction while a shoulder is held. Returns 1 if the
 * direction was consumed by the shortcut layer. */
static int shortcut_dir(App *a, int ad)
{
    if (a->sh_suppress || !shortcuts_enabled(a)) return 0;
    int sh = active_shoulder(a);
    if (sh < 0) return 0;

    if (sh == SH_R1) {
        if (!punch_enabled(a)) return 0;
        int eff = punch_for_dir(ad);
        if (eff >= 0) punch_press(a, eff);
        return 1;                  /* always consume; never move the cursor */
    }

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
    handle_shoulder(a, SH_L2, 1);
    input_dir(a, ACT_RIGHT);
    CHECK(a->mode == MODE_SONG, "L2+Right opens song mode");
    handle_shoulder(a, SH_L2, 0);
    a->sh_suppress = 0;

    a->mode = MODE_EDIT;
    handle_shoulder(a, SH_L1, 1);
    input_dir(a, ACT_DOWN);
    CHECK(a->mode == MODE_RCH, "L1+Down opens ratchet notes");
    handle_shoulder(a, SH_L1, 0);
    a->sh_suppress = 0;

    a->mode = MODE_EDIT;
    handle_shoulder(a, SH_L2, 1);
    input_dir(a, ACT_UP);
    CHECK(a->mode == MODE_KIT, "L2+Up opens sample selection");
    handle_shoulder(a, SH_L2, 0);
    a->sh_suppress = 0;

    a->mode = MODE_EDIT;
    a->punch_held = -1;
    handle_shoulder(a, SH_R1, 1);
    input_dir(a, ACT_LEFT);
    CHECK(a->mode == MODE_EDIT && engine_punch_pending(&a->eng) == PUNCH_LOOP_4,
          "R1+Left triggers the 1/4 punch loop");
    handle_shoulder(a, SH_R1, 0);
    a->sh_suppress = 0;

    a->mode = MODE_EDIT;
    handle_shoulder(a, SH_R2, 1);
    input_dir(a, ACT_UP);
    int rows[24];
    int n = fx_visible_rows(&a->eng, rows);
    CHECK(a->mode == MODE_FX && a->fx_sel < n && rows[a->fx_sel] == FXR_DRIVE_ON,
          "R2+Up opens drive FX");
    handle_shoulder(a, SH_R2, 0);
    a->sh_suppress = 0;

    a->mode = MODE_EDIT;
    handle_shoulder(a, SH_R2, 1);
    input_dir(a, ACT_LEFT);
    n = fx_visible_rows(&a->eng, rows);
    CHECK(a->mode == MODE_FX && a->fx_sel < n && rows[a->fx_sel] == FXR_MOD_ON,
          "R2+Left opens modulation FX");
    handle_shoulder(a, SH_R2, 0);
    a->sh_suppress = 0;

    a->mode = MODE_EDIT;
    handle_shoulder(a, SH_R2, 1);
    input_dir(a, ACT_DOWN);
    n = fx_visible_rows(&a->eng, rows);
    CHECK(a->mode == MODE_FX && a->fx_sel < n && rows[a->fx_sel] == FXR_DELAY_ON,
          "R2+Down opens delay FX");
    handle_shoulder(a, SH_R2, 0);
    a->sh_suppress = 0;

    a->mode = MODE_EDIT;
    handle_shoulder(a, SH_R2, 1);
    input_dir(a, ACT_RIGHT);
    n = fx_visible_rows(&a->eng, rows);
    CHECK(a->mode == MODE_FX && a->fx_sel < n && rows[a->fx_sel] == FXR_REVERB_ON,
          "R2+Right opens reverb FX");
    handle_shoulder(a, SH_R2, 0);
    a->sh_suppress = 0;

    a->mode = MODE_EDIT;
    handle_shoulder(a, SH_L1, 1);
    input_dir(a, ACT_UP);
    CHECK(a->mode == MODE_LENGTH, "L1+Up opens pattern length");
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
    a->sh_held[SH_L2] = 0;
    a->sh_suppress = 0;
    handle_trigger_axis(a, SH_L2, 20000);
    input_dir(a, ACT_RIGHT);
    CHECK(a->mode == MODE_SONG, "L2 trigger axis + Right opens song mode");
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

/* Headless pattern-length checks (`--test-length`). */
static int test_length_run(App *a)
{
    int fails = 0;
    SDL_Init(SDL_INIT_TIMER);

#define CHECK(cond, msg) do { \
    if (cond) printf("PASS: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); fails++; } \
} while (0)

    char path[1200];
    snprintf(path, sizeof path, "%s/test-length.txt", a->lib.conf_dir);

    Engine e;
    engine_init(&e, 44100);
    CHECK(e.length == N_STEPS, "new pattern plays all 16 steps");

    e.length = 5;
    e.bpm_x100 = 12500;
    CHECK(pattern_save_file(&e, path) == 0, "pattern with length saves");

    int file_len = 0;
    FILE *f = fopen(path, "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof line, f))
            if (!strncmp(line, "LEN ", 4)) file_len = atoi(line + 4);
        fclose(f);
    }
    CHECK(file_len == 5, "LEN line is written");

    Engine r;
    engine_init(&r, 44100);
    CHECK(pattern_load_file(&r, path) == 0 && r.length == 5,
          "length round-trips through the pattern file");

    /* Old pattern files have no LEN line and must keep working as 16 steps. */
    f = fopen(path, "w");
    if (f) { fputs("RG808P1\nBPM 120.00\n", f); fclose(f); }
    engine_free(&r);
    engine_init(&r, 44100);
    CHECK(pattern_load_file(&r, path) == 0 && r.length == N_STEPS,
          "legacy pattern without LEN defaults to 16");

    /* Values outside 1..16 are clamped on the way to disk. */
    e.length = 0;
    pattern_save_file(&e, path);
    engine_free(&r);
    engine_init(&r, 44100);
    pattern_load_file(&r, path);
    CHECK(r.length == 1, "length below 1 clamps to 1");
    e.length = 99;
    pattern_save_file(&e, path);
    engine_free(&r);
    engine_init(&r, 44100);
    pattern_load_file(&r, path);
    CHECK(r.length == N_STEPS, "length above 16 clamps to 16");
    engine_free(&r);
    remove(path);

    /* Live clock: 125 BPM -> exactly 5292 frames per 16th. */
    const int sps = 5292;
    float buf[1024 * 2];
    Engine ex;
    engine_init(&ex, 44100);
    ex.length = 5;
    ex.bpm_x100 = 12500;
    ex.playing = 1;
    engine_restart(&ex);
    int done = 0;
    while (done < sps * 5 - 1) {
        int n = (sps * 5 - 1 - done) > 1024 ? 1024 : (sps * 5 - 1 - done);
        engine_mix(&ex, buf, n);
        done += n;
    }
    CHECK(ex.cur_step == 4, "clock stops the bar at the pattern length");
    engine_mix(&ex, buf, 1);
    CHECK(ex.cur_step == 0, "clock wraps to step 1 after the pattern length");
    engine_free(&ex);

    /* Song snapshots: each bar uses its own length for both timing and end. */
    SongSet *set = calloc(1, sizeof(SongSet));
    if (set) {
        set->count = 2;
        for (int i = 0; i < 2; i++) {
            pattern_default_snapshot(&set->pat[i]);
            set->pat[i].bpm_x100 = 12500;
            set->pat[i].length = 5;
        }
        CHECK(song_total_frames(set, 44100, 0) == (long long)sps * 5 * 2,
              "song length math follows per-pattern lengths");

        Engine sx;
        engine_init(&sx, 44100);
        engine_song_start(&sx, set, 0, 0);
        engine_restart(&sx);
        done = 0;
        while (done < sps * 5 - 1) {
            int n = (sps * 5 - 1 - done) > 1024 ? 1024 : (sps * 5 - 1 - done);
            engine_mix(&sx, buf, n);
            done += n;
        }
        CHECK(sx.song_bar == 0 && sx.cur_step == 4,
              "song stays in a short bar until its last step");
        engine_mix(&sx, buf, 1);
        CHECK(sx.song_bar == 1 && sx.cur_step == 0,
              "song advances on the pattern's length");
        done = 0;
        while (done < sps * 5 - 1) {
            int n = (sps * 5 - 1 - done) > 1024 ? 1024 : (sps * 5 - 1 - done);
            engine_mix(&sx, buf, n);
            done += n;
        }
        engine_mix(&sx, buf, 1);
        CHECK(sx.song_playing == 0 && sx.song_done == 1,
              "song ends after the last short bar");
        engine_free(&sx);
    } else {
        CHECK(0, "length test snapshot allocation");
    }
    free(set);
    engine_free(&e);

    SDL_Quit();
    return fails ? 1 : 0;
#undef CHECK
}
/* Headless effects checks (`--test-fx`). */
static void fx_test_pattern(Engine *e)
{
    for (int t = 0; t < N_TRACKS; t++)
        for (int s = 0; s < N_STEPS; s++)
            e->lvl[t][s] = 0;
    e->lvl[0][0] = LVL_ACC;
    e->lvl[1][4] = LVL_ON;
    e->bpm_x100 = 12000;
    e->length = N_STEPS;
    e->playing = 1;
    engine_restart(e);
}

static void fx_test_render(Engine *e, float *out, int frames)
{
    int done = 0;
    while (done < frames) {
        int n = (frames - done) > 512 ? 512 : (frames - done);
        engine_mix(e, out + done * 2, n);
        done += n;
    }
}

static int test_fx_run(App *a)
{
    int fails = 0;
    SDL_Init(SDL_INIT_TIMER);

#define CHECK(cond, msg) do { \
    if (cond) printf("PASS: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); fails++; } \
} while (0)

    FxSettings def;
    fx_defaults(&def);
    fx_set_fallback(&def);
    fx_apply(&a->eng, &def);

    const int frames = 44100;
    float *dry = malloc(sizeof(float) * 2 * (size_t)frames);
    float *wet = malloc(sizeof(float) * 2 * (size_t)frames);
    if (!dry || !wet) {
        CHECK(0, "fx test buffers");
        free(dry);
        free(wet);
        SDL_Quit();
        return 1;
    }

    Engine e;
    engine_init(&e, 44100);
    fx_test_pattern(&e);
    fx_test_render(&e, dry, frames);
    engine_free(&e);

    /* Master bypass is bit-identical to the dry render. */
    engine_init(&e, 44100);
    fx_test_pattern(&e);
    e.fx.on = 0;
    e.fx.drive_on = e.fx.mod_on = e.fx.delay_on = e.fx.reverb_on = 1;
    fx_test_render(&e, wet, frames);
    CHECK(memcmp(dry, wet, sizeof(float) * 2 * (size_t)frames) == 0,
          "master bypass is bit-identical");
    engine_free(&e);

    engine_init(&e, 44100);
    fx_test_pattern(&e);
    fx_test_render(&e, wet, frames);
    CHECK(memcmp(dry, wet, sizeof(float) * 2 * (size_t)frames) == 0,
          "all stages off is bit-identical");
    engine_free(&e);

    /* Every drive algorithm must alter the signal. */
    for (int type = 0; type < FX_DRIVE_COUNT; type++) {
        engine_init(&e, 44100);
        fx_test_pattern(&e);
        e.fx.drive_on = 1;
        e.fx.drive_type = type;
        if (type == FX_DRIVE_CRUSH) {
            e.fx.crush_bits = 4;
            e.fx.crush_down = 4;
        } else if (type == FX_DRIVE_OVERDRIVE) {
            e.fx.drive_amount = 90;
            e.fx.drive_mix = 100;
        } else {
            e.fx.comp_threshold = 10;
            e.fx.comp_ratio = 20;
            e.fx.comp_makeup = 100;
        }
        fx_test_render(&e, wet, frames);
        char msg[64];
        snprintf(msg, sizeof msg, "%s drive changes audio", DRIVE_NAMES[type]);
        CHECK(memcmp(dry, wet, sizeof(float) * 2 * (size_t)frames) != 0, msg);
        engine_free(&e);
    }

    /* Flanger and phaser are both selectable. */
    for (int type = 0; type < FX_MOD_COUNT; type++) {
        engine_init(&e, 44100);
        fx_test_pattern(&e);
        e.fx.mod_on = 1;
        e.fx.mod_type = type;
        e.fx.mod_depth = 50;
        e.fx.mod_mix = 50;
        fx_test_render(&e, wet, frames);
        char msg[64];
        snprintf(msg, sizeof msg, "%s modulation changes audio", MOD_NAMES[type]);
        CHECK(memcmp(dry, wet, sizeof(float) * 2 * (size_t)frames) != 0, msg);
        engine_free(&e);
    }

    /* The phaser at 50 % must be clearly audible, and its max setting must not
     * turn into a level boost / clipped overdrive. */
    double dry_e = 0.0, diff_e = 0.0;
    for (int i = 0; i < 2 * frames; i++) {
        double d = (double)dry[i] - (double)wet[i];
        dry_e += (double)dry[i] * (double)dry[i];
        diff_e += d * d;
    }
    CHECK(diff_e > dry_e * 0.004, "phaser at 50% is clearly audible");

    engine_init(&e, 44100);
    fx_test_pattern(&e);
    e.fx.mod_on = 1;
    e.fx.mod_type = FX_MOD_PHASER;
    e.fx.mod_rate_x100 = 500;
    e.fx.mod_depth = 100;
    e.fx.mod_fb = 90;
    e.fx.mod_mix = 100;
    fx_test_render(&e, wet, frames);
    double wet_e = 0.0;
    for (int i = 0; i < 2 * frames; i++)
        wet_e += (double)wet[i] * (double)wet[i];
    CHECK(wet_e < dry_e * 2.0, "phaser max level stays under control");
    engine_free(&e);

    /* Delay works free-running and tempo-synced. */
    engine_init(&e, 44100);
    fx_test_pattern(&e);
    e.fx.delay_on = 1;
    e.fx.delay_sync = 0;
    e.fx.delay_ms = 120;
    e.fx.delay_mix = 50;
    fx_test_render(&e, wet, frames);
    CHECK(memcmp(dry, wet, sizeof(float) * 2 * (size_t)frames) != 0,
          "free-running delay changes audio");
    engine_free(&e);

    engine_init(&e, 44100);
    fx_test_pattern(&e);
    e.fx.delay_on = 1;
    e.fx.delay_sync = 1;
    e.fx.delay_div = FX_DIV_8;
    e.fx.delay_mix = 50;
    fx_test_render(&e, wet, frames);
    CHECK(memcmp(dry, wet, sizeof(float) * 2 * (size_t)frames) != 0,
          "tempo-synced delay changes audio");
    engine_free(&e);

    engine_init(&e, 44100);
    fx_test_pattern(&e);
    e.fx.reverb_on = 1;
    e.fx.reverb_mix = 50;
    fx_test_render(&e, wet, frames);
    CHECK(memcmp(dry, wet, sizeof(float) * 2 * (size_t)frames) != 0,
          "reverb changes audio");
    engine_free(&e);

    free(dry);
    free(wet);

    /* Settings defaults, clamping and persistence. */
    char path[1200];
    snprintf(path, sizeof path, "%s/test-fx.txt", a->lib.conf_dir);
    FxSettings f, g;
    fx_defaults(&f);
    CHECK(f.on == 1 && f.crush_bits == 8 && f.delay_ms == 250 &&
          f.delay_div == FX_DIV_8 && f.drive_type == FX_DRIVE_CRUSH &&
          f.mod_type == FX_MOD_FLANGER,
          "effects defaults");
    CHECK(fx_delay_ms_for(1, 250, FX_DIV_4, 12000) == 500 &&
          fx_delay_ms_for(1, 250, FX_DIV_8, 12000) == 250 &&
          fx_delay_ms_for(1, 250, FX_DIV_16, 12000) == 125 &&
          fx_delay_ms_for(1, 250, FX_DIV_4T, 12000) == 333 &&
          fx_delay_ms_for(1, 250, FX_DIV_8T, 12000) == 167 &&
          fx_delay_ms_for(1, 250, FX_DIV_16T, 12000) == 83 &&
          fx_delay_ms_for(1, 250, FX_DIV_4D, 12000) == 750 &&
          fx_delay_ms_for(1, 250, FX_DIV_8D, 12000) == 375 &&
          fx_delay_ms_for(1, 250, FX_DIV_16D, 12000) == 188 &&
          fx_delay_ms_for(0, 250, FX_DIV_4, 12000) == 250,
          "delay divisions resolve at 120 BPM");
    f.drive_type = 99;
    f.mod_type = -1;
    f.crush_bits = 2;
    f.crush_down = 99;
    f.comp_ratio = 99;
    f.mod_rate_x100 = 999;
    f.delay_ms = 5;
    f.delay_div = 99;
    f.reverb_mix = 200;
    fx_clamp(&f);
    CHECK(f.drive_type == FX_DRIVE_COMPRESS && f.mod_type == FX_MOD_FLANGER &&
          f.crush_bits == 4 && f.crush_down == 32 && f.comp_ratio == 20 &&
          f.mod_rate_x100 == 500 && f.delay_ms == 20 &&
          f.delay_div == FX_DIV_16D && f.reverb_mix == 100,
          "effects values clamp");
    CHECK(fx_save_file(&f, path) == 0, "effects settings save");
    CHECK(fx_load_file(&g, path) == 0, "effects settings reload");
    CHECK(g.drive_type == f.drive_type && g.crush_bits == f.crush_bits &&
          g.comp_ratio == f.comp_ratio && g.mod_type == f.mod_type &&
          g.mod_rate_x100 == f.mod_rate_x100 && g.delay_ms == f.delay_ms &&
          g.delay_div == f.delay_div && g.reverb_mix == f.reverb_mix,
          "effects settings round-trip");
    remove(path);

    /* Old M9 FX files still load. */
    FILE *old = fopen(path, "w");
    if (old) {
        fputs("RG808FX1\nFX ON 1\nFX CRUSH 1 5 3\n"
              "FX FLANGER 1 60 40 20 70\n"
              "FX DELAY 1 300 25 60\n"
              "FX REVERB 1 40 60 35\n", old);
        fclose(old);
    }
    CHECK(fx_load_file(&g, path) == 0 && g.drive_type == FX_DRIVE_CRUSH &&
          g.crush_bits == 5 && g.crush_down == 3 &&
          g.mod_type == FX_MOD_FLANGER && g.mod_rate_x100 == 60 &&
          g.delay_sync == 0 && g.delay_ms == 300 &&
          g.reverb_room == 40,
          "old M9 effects file still loads");
    remove(path);
    CHECK(fx_load_file(&g, path) == -1 && g.crush_bits == 8,
          "missing effects file keeps defaults");

    /* Per-pattern persistence: save a pattern with an unusual chain. */
    char ppath[1200];
    snprintf(ppath, sizeof ppath, "%s/test-fx-pattern.txt", a->lib.conf_dir);
    Engine pe;
    engine_init(&pe, 44100);
    pe.fx.on = 1;
    pe.fx.drive_on = 1;
    pe.fx.drive_type = FX_DRIVE_OVERDRIVE;
    pe.fx.drive_amount = 77;
    pe.fx.mod_on = 1;
    pe.fx.mod_type = FX_MOD_PHASER;
    pe.fx.mod_depth = 65;
    pe.fx.delay_on = 1;
    pe.fx.delay_sync = 1;
    pe.fx.delay_div = FX_DIV_16D;
    pe.fx.reverb_on = 1;
    pe.fx.reverb_room = 81;
    CHECK(pattern_save_file(&pe, ppath) == 0, "pattern saves its effects");
    Engine pl;
    engine_init(&pl, 44100);
    CHECK(pattern_load_file(&pl, ppath) == 0, "pattern with effects loads");
    FxSettings cap;
    fx_capture(&cap, &pl);
    CHECK(cap.drive_type == FX_DRIVE_OVERDRIVE && cap.drive_amount == 77 &&
          cap.mod_type == FX_MOD_PHASER && cap.mod_depth == 65 &&
          cap.delay_sync == 1 && cap.delay_div == FX_DIV_16D &&
          cap.reverb_room == 81,
          "pattern effects round-trip");
    engine_free(&pl);
    engine_free(&pe);
    remove(ppath);

    /* A legacy pattern without FX lines gets the global fallback. */
    FxSettings fb;
    fx_defaults(&fb);
    fb.drive_on = 1;
    fb.drive_type = FX_DRIVE_COMPRESS;
    fb.comp_ratio = 12;
    fb.mod_on = 1;
    fb.mod_type = FX_MOD_PHASER;
    fb.delay_sync = 1;
    fb.delay_div = FX_DIV_4D;
    fx_set_fallback(&fb);
    FILE *lf = fopen(ppath, "w");
    if (lf) { fputs("RG808P1\nBPM 120.00\n", lf); fclose(lf); }
    engine_init(&pl, 44100);
    CHECK(pattern_load_file(&pl, ppath) == 0, "legacy pattern loads");
    fx_capture(&cap, &pl);
    CHECK(cap.drive_on == 1 && cap.drive_type == FX_DRIVE_COMPRESS &&
          cap.comp_ratio == 12 && cap.mod_type == FX_MOD_PHASER &&
          cap.delay_sync == 1 && cap.delay_div == FX_DIV_4D,
          "legacy pattern uses fallback effects");
    engine_free(&pl);
    remove(ppath);
    fx_set_fallback(&def);

    /* FX page actions: A toggles headers, left/right adjusts and clamps. */
    fx_apply(&a->eng, &def);
    a->mode = MODE_FX;
    int rows[24];
    int n = fx_visible_rows(&a->eng, rows);
    int idx = 0;
    for (int i = 0; i < n; i++) if (rows[i] == FXR_DRIVE_ON) idx = i;
    a->fx_sel = idx;
    act(a, ACT_CONFIRM);
    CHECK(a->eng.fx.drive_on == 1, "A enables the drive stage");
    a->eng.fx.crush_bits = 16;
    n = fx_visible_rows(&a->eng, rows);
    idx = 0;
    for (int i = 0; i < n; i++) if (rows[i] == FXR_CRUSH_BITS) idx = i;
    a->fx_sel = idx;
    act(a, ACT_RIGHT);
    CHECK(a->eng.fx.crush_bits == 16, "bit depth clamps at 16");
    act(a, ACT_LEFT);
    CHECK(a->eng.fx.crush_bits == 15, "left/right adjusts a value");
    a->eng.fx.delay_sync = 0;
    a->eng.fx.delay_ms = 4000;
    n = fx_visible_rows(&a->eng, rows);
    idx = 0;
    for (int i = 0; i < n; i++) if (rows[i] == FXR_DELAY_TIME) idx = i;
    a->fx_sel = idx;
    act(a, ACT_RIGHT);
    CHECK(a->eng.fx.delay_ms == 4000, "delay time clamps at 4000 ms");
    a->eng.fx.delay_sync = 1;
    a->eng.fx.delay_div = FX_DIV_16D;
    n = fx_visible_rows(&a->eng, rows);
    idx = 0;
    for (int i = 0; i < n; i++) if (rows[i] == FXR_DELAY_TIME) idx = i;
    a->fx_sel = idx;
    act(a, ACT_RIGHT);
    CHECK(a->eng.fx.delay_div == FX_DIV_4, "delay divisions wrap");
    a->mode = MODE_EDIT;

    /* Leaving song playback restores the live pattern's chain. */
    FxSettings live_fx, song_fx;
    fx_capture(&live_fx, &a->eng);
    a->edit_fx = live_fx;
    a->edit_fx_valid = 1;
    song_fx = live_fx;
    song_fx.crush_bits = 4;
    fx_apply(&a->eng, &song_fx);
    stop_playback(a);
    fx_capture(&cap, &a->eng);
    CHECK(a->edit_fx_valid == 0 && cap.crush_bits == live_fx.crush_bits,
          "song stop restores the live effects chain");

    SDL_Quit();
    return fails ? 1 : 0;
#undef CHECK
}

static float test_peak(const float *buf, int frames)
{
    float peak = 0.0f;
    for (int i = 0; i < frames * 2; i++) {
        float v = buf[i] < 0.0f ? -buf[i] : buf[i];
        if (v > peak) peak = v;
    }
    return peak;
}

/* Headless performance-mute checks (`--test-mute`). */
static int test_mute_run(App *a)
{
    int fails = 0;
    SDL_Init(SDL_INIT_TIMER);

#define CHECK(cond, msg) do { \
    if (cond) printf("PASS: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); fails++; } \
} while (0)

    float buf[2048 * 2];
    Engine e;
    engine_init(&e, 44100);
    for (int t = 0; t < N_TRACKS; t++)
        for (int s = 0; s < N_STEPS; s++)
            e.lvl[t][s] = e.rch[t][s] = 0;
    e.bpm_x100 = 12000;
    e.lvl[0][0] = LVL_ACC;
    e.playing = 1;
    engine_restart(&e);
    engine_mix(&e, buf, 2048);
    CHECK(test_peak(buf, 2048) > 0.001f, "unmuted track produces audio");

    e.playing = 0;
    e.mute[0] = 1;
    e.mute_kill_mask |= 1u << 0;      /* same as the UI: cut the ringing hit */
    engine_restart(&e);
    e.playing = 1;
    engine_mix(&e, buf, 2048);
    CHECK(test_peak(buf, 2048) < 0.0001f, "muted track is silent");

    /* Auditioning must still work while the track is muted. */
    e.preview_track = 0;
    engine_preview(&e, 0, e.kit[0], RCH_OFF, LVL_ON);
    engine_mix(&e, buf, 2048);
    CHECK(test_peak(buf, 2048) > 0.001f, "preview sounds while muted");

    /* Mute-on must cut a voice already ringing, not just skip new hits. */
    e.playing = 0;
    e.mute[0] = 0;
    engine_restart(&e);
    e.playing = 1;
    engine_mix(&e, buf, 512);
    e.mute[0] = 1;
    e.mute_kill_mask |= 1u << 0;
    engine_mix(&e, buf, 256);
    CHECK(test_peak(buf, 256) < 0.0001f, "mute cuts ringing voices");

    e.playing = 0;
    e.mute[0] = 0;
    e.mute_kill_mask = 0;
    engine_restart(&e);
    e.playing = 1;
    engine_mix(&e, buf, 2048);
    CHECK(test_peak(buf, 2048) > 0.001f, "unmute restores audio");
    e.playing = 0;
    engine_free(&e);

    /* The same flag must gate song playback, not only the live pattern. */
    SongSet *set = calloc(1, sizeof *set);
    if (set) {
        Engine s;
        engine_init(&s, 44100);
        pattern_default_snapshot(&set->pat[0]);
        set->count = 1;
        set->pat[0].bpm_x100 = 12000;
        set->pat[0].length = N_STEPS;
        set->pat[0].lvl[0][0] = LVL_ACC;
        s.mute[0] = 1;
        engine_song_start(&s, set, 0, 0);
        engine_restart(&s);
        engine_mix(&s, buf, 2048);
        CHECK(test_peak(buf, 2048) < 0.0001f, "muted track is silent in a song");
        s.mute[0] = 0;
        engine_restart(&s);
        engine_mix(&s, buf, 2048);
        CHECK(test_peak(buf, 2048) > 0.001f, "unmute restores song audio");
        engine_song_stop(&s);
        engine_free(&s);
        free(set);
    } else {
        CHECK(0, "song test buffer");
    }

    /* UI/action wiring: Y mutes in the sequencer but still auditions in KIT. */
    Engine *ue = &a->eng;
    a->mode = MODE_EDIT;
    a->cur_track = 5;
    ue->mute[5] = 0;
    ue->mute_kill_mask = 0;
    act(a, ACT_MUTE);
    CHECK(ue->mute[5] == 1, "Y mutes the selected track");
    CHECK((ue->mute_kill_mask & (1u << 5)) != 0, "mute requests a voice cut");
    act(a, ACT_MUTE);
    CHECK(ue->mute[5] == 0, "Y unmutes the selected track");

    a->mode = MODE_KIT;
    a->cur_track = 0;
    a->cur_step = 0;
    ue->mute[0] = 0;
    ue->preview_track = 0;
    act(a, ACT_MUTE);
    CHECK(ue->mute[0] == 0, "Y does not mute outside the sequencer");
    CHECK(ue->preview_track == 1, "Y still auditions in KIT mode");
    ue->preview_track = 0;

    SDL_Quit();
    return fails ? 1 : 0;
#undef CHECK
}

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
    if (punch_enabled(a)) {
        int eff = punch_for_key(k);
        if (eff >= 0) { punch_press(a, eff); return; }
    }
    switch (k) {
    case SDLK_UP: case SDLK_w:     input_dir(a, ACT_UP); break;
    case SDLK_DOWN: case SDLK_s:   input_dir(a, ACT_DOWN); break;
    case SDLK_LEFT: case SDLK_a:   input_dir(a, ACT_LEFT); break;
    case SDLK_RIGHT: case SDLK_d:  input_dir(a, ACT_RIGHT); break;
    case SDLK_z: case SDLK_RETURN: act(a, ACT_CONFIRM); break;
    case SDLK_x:                   act(a, (a->mode == MODE_EDIT || a->mode == MODE_RCH) ? ACT_CLEAR : ACT_BACK); break;
    case SDLK_c:                   act(a, ACT_HEAR); break;
    case SDLK_y:                   act(a, ACT_MUTE); break;
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
    int eff = punch_for_key(k);
    if (eff >= 0) { punch_release_effect(a, eff); return; }

    switch (k) {
    case SDLK_UP: case SDLK_w:
        punch_release_effect(a, punch_for_dir(ACT_UP)); break;
    case SDLK_DOWN: case SDLK_s:
        punch_release_effect(a, punch_for_dir(ACT_DOWN)); break;
    case SDLK_LEFT: case SDLK_a:
        punch_release_effect(a, punch_for_dir(ACT_LEFT)); break;
    case SDLK_RIGHT: case SDLK_d:
        punch_release_effect(a, punch_for_dir(ACT_RIGHT)); break;
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

    if (punch_enabled(a)) {
        int eff = punch_for_gc_button(button);
        if (eff >= 0) {
            if (down) punch_press(a, eff);
            else      punch_release_effect(a, eff);
            return;
        }
    }

    if (down) {
        switch (button) {
        case SDL_CONTROLLER_BUTTON_A: act(a, ACT_CONFIRM); break;
        case SDL_CONTROLLER_BUTTON_B: act(a, (a->mode == MODE_EDIT || a->mode == MODE_RCH) ? ACT_CLEAR : ACT_BACK); break;
        case SDL_CONTROLLER_BUTTON_X: act(a, ACT_HEAR); break;
        case SDL_CONTROLLER_BUTTON_Y: act(a, ACT_MUTE); break;
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
            punch_release_effect(a, punch_for_dir(dir_action(button)));
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

    if (punch_enabled(a)) {
        int eff = punch_for_raw_button(button);
        if (eff >= 0) {
            if (down) punch_press(a, eff);
            else      punch_release_effect(a, eff);
            return;
        }
    }
    if (!down) return;

    switch (button) {
    case 0: act(a, ACT_CONFIRM); break;       /* A */
    case 1: act(a, (a->mode == MODE_EDIT || a->mode == MODE_RCH) ? ACT_CLEAR : ACT_BACK); break;  /* B */
    case 2: act(a, ACT_HEAR); break;          /* X */
    case 3: act(a, ACT_MUTE); break;          /* Y */
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
    if (hat != 0) return;
    if (saw_dpad_buttons) return;

    if (punch_enabled(a)) {
        if (dir == SDL_HAT_CENTERED) { punch_release_all(a); return; }
        int eff = (dir & SDL_HAT_UP)    ? PUNCH_LOOP_16
                : (dir & SDL_HAT_DOWN)  ? PUNCH_LOOP_32
                : (dir & SDL_HAT_LEFT)  ? PUNCH_LOOP_4
                : (dir & SDL_HAT_RIGHT) ? PUNCH_LOOP_8 : -1;
        if (eff >= 0) {
            if (a->punch_held >= 0 && a->punch_held != eff) punch_release_all(a);
            punch_press(a, eff);
        }
        return;
    }

    if (dir == SDL_HAT_CENTERED) return;
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

    if (punch_enabled(a)) {
        int e1 = punch_for_dir(axis == 0 ? ACT_LEFT : ACT_UP);
        int e2 = punch_for_dir(axis == 0 ? ACT_RIGHT : ACT_DOWN);
        if (nd == 0) {
            if (a->punch_held == e1 || a->punch_held == e2) punch_release_all(a);
            return;
        }
        int eff = punch_for_dir(axis == 0
                                ? (nd > 0 ? ACT_RIGHT : ACT_LEFT)
                                : (nd > 0 ? ACT_DOWN : ACT_UP));
        if (eff >= 0) {
            if (a->punch_held >= 0 && a->punch_held != eff) punch_release_all(a);
            punch_press(a, eff);
        }
        return;
    }

    if (nd == dir[axis]) return;
    dir[axis] = nd;
    if (!nd) return;
    if (axis == 0) input_dir(a, nd > 0 ? ACT_RIGHT : ACT_LEFT);
    else           input_dir(a, nd > 0 ? ACT_DOWN : ACT_UP);
}

/* Headless punch-in checks (`--test-punch`). */
static int test_punch_run(App *a)
{
    int fails = 0;
    SDL_Init(SDL_INIT_TIMER);

#define CHECK(cond, msg) do { \
    if (cond) printf("PASS: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); fails++; } \
} while (0)

    CHECK(perf_fx_selftest(44100) == 0, "punch FX DSP self-test");

    float *buf = malloc(sizeof(float) * 2 * 512);
    if (!buf) { CHECK(0, "punch test buffer"); SDL_Quit(); return 1; }

    Engine e;
    engine_init(&e, 44100);
    e.bpm_x100 = 12000;
    for (int t = 0; t < N_TRACKS; t++)
        for (int s = 0; s < N_STEPS; s++)
            e.lvl[t][s] = e.rch[t][s] = 0;
    e.lvl[0][0] = LVL_ON;
    e.playing = 1;
    engine_restart(&e);
    engine_mix(&e, buf, 256);                 /* step 0 fires */
    engine_punch_trigger(&e, PUNCH_LOOP_4);
    engine_mix(&e, buf, 256);
    CHECK(engine_punch_active(&e) == -1 &&
          engine_punch_pending(&e) == PUNCH_LOOP_4,
          "punch waits for the next 16th boundary");
    for (int n = 0; n < 13; n++) engine_mix(&e, buf, 512);
    CHECK(engine_punch_active(&e) == PUNCH_LOOP_4,
          "punch starts on the next 16th");
    engine_punch_release(&e);
    for (int n = 0; n < 8; n++) engine_mix(&e, buf, 512);
    CHECK(engine_punch_active(&e) == -1,
          "punch release returns to live audio");
    engine_free(&e);

    /* Song playback uses the same step-boundary quantization. */
    SongSet *set = calloc(1, sizeof *set);
    if (set) {
        Engine s;
        engine_init(&s, 44100);
        pattern_default_snapshot(&set->pat[0]);
        set->count = 1;
        set->pat[0].bpm_x100 = 12000;
        set->pat[0].lvl[0][0] = LVL_ON;
        engine_song_start(&s, set, 0, 0);
        engine_restart(&s);
        engine_mix(&s, buf, 256);
        engine_punch_trigger(&s, PUNCH_LOOP_8);
        engine_mix(&s, buf, 256);
        CHECK(engine_punch_pending(&s) == PUNCH_LOOP_8,
              "song punch waits for the next 16th");
        for (int n = 0; n < 13; n++) engine_mix(&s, buf, 512);
        CHECK(engine_punch_active(&s) == PUNCH_LOOP_8,
              "song punch starts on the next 16th");
        engine_punch_release(&s);
        engine_song_stop(&s);
        engine_free(&s);
        free(set);
    } else {
        CHECK(0, "song punch test buffer");
    }

    /* Input layer: R1 + face button, one at a time, release, no mute leak. */
    a->mode = MODE_EDIT;
    a->punch_held = -1;
    a->eng.mute[0] = 0;
    handle_shoulder(a, SH_R1, 1);
    handle_gc_button(a, SDL_CONTROLLER_BUTTON_A, 1);
    CHECK(a->punch_held == PUNCH_REWIND &&
          engine_punch_pending(&a->eng) == PUNCH_REWIND,
          "R1+A triggers rewind");
    handle_gc_button(a, SDL_CONTROLLER_BUTTON_B, 1);
    CHECK(a->punch_held == PUNCH_REWIND,
          "second punch press is ignored while one is held");
    handle_gc_button(a, SDL_CONTROLLER_BUTTON_A, 0);
    CHECK(a->punch_held < 0, "releasing the face button releases the punch");
    handle_shoulder(a, SH_R1, 0);

    a->punch_held = -1;
    a->eng.mute[0] = 0;
    handle_shoulder(a, SH_R1, 1);
    handle_gc_button(a, SDL_CONTROLLER_BUTTON_Y, 1);
    CHECK(a->eng.mute[0] == 0 &&
          engine_punch_pending(&a->eng) == PUNCH_PHONE,
          "R1+Y triggers phone without muting");
    handle_gc_button(a, SDL_CONTROLLER_BUTTON_Y, 0);
    handle_shoulder(a, SH_R1, 0);

    a->mode = MODE_EDIT;
    a->punch_held = -1;
    handle_shoulder(a, SH_R1, 1);
    input_dir(a, ACT_LEFT);
    CHECK(engine_punch_pending(&a->eng) == PUNCH_LOOP_4,
          "R1+Left triggers the 1/4 punch loop");
    handle_shoulder(a, SH_R1, 0);
    CHECK(engine_punch_pending(&a->eng) < 0,
          "releasing R1 cancels a pending punch");

    free(buf);
    SDL_Quit();
    return fails ? 1 : 0;
#undef CHECK
}

/* ---------------------------------------------------------------- bounce */

static int run_bounce(App *a, const char *outpath, int loops)
{
    int rate = 44100;
    FxSettings fx;
    fx_capture(&fx, &a->eng);
    engine_free(&a->eng);
    engine_init(&a->eng, rate);
    fx_apply(&a->eng, &fx);
    if (load_current(a) != 0) demo_pattern(a);
    a->eng.playing = 1;
    engine_restart(&a->eng);
    int len = engine_length_clamp(a->eng.length);
    double sps = 60.0 * rate / (a->eng.bpm_x100 / 100.0 * 4.0);
    long total = (long)(sps * len * loops) + 8192;   /* tail so last hits finish */
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
    a.punch_held = -1;

    const char *assets_arg = NULL;
    const char *bounce = NULL;
    const char *shot = NULL;
    const char *mode_arg = NULL;
    int bounce_loops = 4;
    int pattern_arg = 0;
    int save_slot_arg = 0;
    int save_slot_set = 0;
    int song_bounce = 0;
    int test_shortcuts = 0;
    int test_song = 0;
    int test_length = 0;
    int test_fx = 0;
    int test_mute = 0;
    int test_punch = 0;

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
        else if (!strcmp(argv[i], "--song")) song_bounce = 1;
        else if (!strcmp(argv[i], "--test-shortcuts")) test_shortcuts = 1;
        else if (!strcmp(argv[i], "--test-song")) test_song = 1;
        else if (!strcmp(argv[i], "--test-length")) test_length = 1;
        else if (!strcmp(argv[i], "--test-fx")) test_fx = 1;
        else if (!strcmp(argv[i], "--test-mute")) test_mute = 1;
        else if (!strcmp(argv[i], "--test-punch")) test_punch = 1;
        else if (!strcmp(argv[i], "--help")) {
            printf("usage: rg808 [--assets DIR] [--pattern N] [--save-slot N]"
                   " [--song [--bounce out.wav]] [--bounce out.wav --loops N]"
                   " [--test-shortcuts] [--test-song] [--test-length] [--test-fx] [--test-mute] [--test-punch]"
                   " [--screenshot out.bmp [--mode edit|mute|punch|kit|rch|swing|length|fx|menu|tempo|pattern|song|songact|export|l1|l2|r1|r2]]\n");
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
        int rc = test_shortcuts_run(&a);
        engine_free(&a.eng);
        return rc;
    }

    if (bounce || save_slot_set || song_bounce || test_song || test_length || test_fx || test_mute || test_punch) {
        samples_load_all(assets, 44100);
        resolve_conf(&a, base ? base : ".");
        load_fx_settings(&a);
        if (pattern_arg >= 1 && pattern_arg <= PAT_MAX_SLOTS)
            pattern_set_active(&a.lib, pattern_arg);
        SDL_free(base);

        if (test_song) return test_song_run(&a);
        if (test_length) return test_length_run(&a);
        if (test_fx) return test_fx_run(&a);
        if (test_mute) return test_mute_run(&a);
        if (test_punch) return test_punch_run(&a);

        if (save_slot_set) {
            engine_free(&a.eng);
            engine_init(&a.eng, 44100);
            FxSettings fb;
            fx_get_fallback(&fb);
            fx_apply(&a.eng, &fb);
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

        if (song_bounce) {
            song_init(&a.song);
            song_load(&a.song, a.song_path);
            if (a.song.count <= 0) {
                fprintf(stderr, "rg808: song is empty (%s)\n", a.song_path);
                return 1;
            }
            SongSet *set = calloc(1, sizeof(SongSet));
            if (!set) return 1;
            int missing = song_build_set(&a.song, &a.lib, set);
            const char *out = bounce ? bounce : a.song_wav_path;
            int rc = render_song_wav(&a, set, out, 0);
            if (rc == 0) {
                long long frames = song_total_frames(set, 44100, 44100 * 2);
                printf("rg808: song exported: %d bars, %lld frames (%.2fs)%s -> %s\n",
                       set->count, frames, (double)frames / 44100.0,
                       missing ? " (some bars empty)" : "", out);
            } else {
                fprintf(stderr, "rg808: song export failed\n");
            }
            free(set);
            return rc;
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
    load_fx_settings(&a);
    if (pattern_arg >= 1 && pattern_arg <= PAT_MAX_SLOTS)
        pattern_set_active(&a.lib, pattern_arg);
    SDL_free(base);

    a.song_sets[0] = calloc(1, sizeof(SongSet));
    a.song_sets[1] = calloc(1, sizeof(SongSet));
    if (!a.song_sets[0] || !a.song_sets[1]) {
        fprintf(stderr, "rg808: out of memory for song snapshots\n");
        if (a.dev) SDL_CloseAudioDevice(a.dev);
        SDL_Quit();
        return 1;
    }
    song_init(&a.song);
    song_load(&a.song, a.song_path);
    a.song_sel = 0;
    a.song_last_save = SDL_GetTicks();

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
            if (!strcmp(mode_arg, "mute")) {
                a.mode = MODE_EDIT;
                a.eng.mute[0] = 1;
                a.eng.mute[5] = 1;
                a.cur_track = 0;
            }
            else if (!strcmp(mode_arg, "punch")) {
                a.mode = MODE_EDIT;
                a.sh_held[SH_R1] = 1;
                a.sh_last = SH_R1;
                a.punch_held = PUNCH_GATE;
                a.shortcut_until = SDL_GetTicks() + 60000;
            }
            else if (!strcmp(mode_arg, "kit"))   { a.mode = MODE_KIT; a.cur_step = a.eng.kit[a.cur_track]; }
            else if (!strcmp(mode_arg, "rch"))   a.mode = MODE_RCH;
            else if (!strcmp(mode_arg, "swing")) a.mode = MODE_SWING;
            else if (!strcmp(mode_arg, "length")) a.mode = MODE_LENGTH;
            else if (!strcmp(mode_arg, "fx"))    a.mode = MODE_FX;
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
            } else if (!strcmp(mode_arg, "song")) {
                a.mode = MODE_SONG;
                a.song_sel = 0;
            } else if (!strcmp(mode_arg, "songact")) {
                a.mode = MODE_SONG_ACT;
                a.song_sel = 0;
                a.song_act_sel = 0;
            } else if (!strcmp(mode_arg, "export")) {
                a.mode = MODE_EXPORT;
                a.export_progress = 500;
                snprintf(a.export_display, sizeof a.export_display, "conf/song.wav");
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
        if (a.eng.song_done) {
            a.eng.song_done = 0;
            stop_playback(&a);
            set_msg(&a, "SONG ENDED");
        }
        /* periodic autosave: kills/HOME-exit shouldn't lose the groove */
        if (a.dirty && (int)(now - a.last_save) > 5000) {
            if (save_current(&a) == 0) a.dirty = 0;
            else set_msg(&a, "AUTOSAVE FAILED");
            a.last_save = now;
        }
        if (a.song_dirty && (int)(now - a.song_last_save) > 5000) {
            if (song_save(&a.song, a.song_path) == 0) a.song_dirty = 0;
            else set_msg(&a, "SONG SAVE FAILED");
            a.song_last_save = now;
        }
        draw_ui(&a);
        present(&a);
    }

    if (a.dirty) save_current(&a);
    if (a.song_dirty) song_save(&a.song, a.song_path);
    if (a.dev) SDL_CloseAudioDevice(a.dev);
    free(a.song_sets[0]);
    free(a.song_sets[1]);
    engine_free(&a.eng);
    SDL_Quit();
    return 0;
}
