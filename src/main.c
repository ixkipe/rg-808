/* RG-808 — TR-808 clone main: app state, input, UI, persistence, CLI modes. */
#include <SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#include "engine.h"
#include "canvas.h"
#include "wav.h"
#include "samples.h"

#define SCR_W 640
#define SCR_H 480

/* grid geometry */
#define GRID_X0  76
#define GRID_Y0  58
#define CELL_W   30
#define CELL_H   30
#define COL_PITCH (CELL_W + 4)          /* 34 */
#define ROW_PITCH (CELL_H + 6)          /* 36 */

enum Mode { MODE_EDIT, MODE_MENU, MODE_TEMPO };

enum Action {
    ACT_NONE, ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT,
    ACT_CONFIRM, ACT_BACK, ACT_CLEAR, ACT_HEAR, ACT_PLAY, ACT_MENU,
    ACT_TEMPO_UP, ACT_TEMPO_DOWN, ACT_QUIT, ACT_HEAR_ALT
};

typedef struct {
    Engine eng;
    int mode;
    int cur_track, cur_step;
    int menu_sel;
    int quit;
    char conf_path[1024];
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

static void ensure_parent_dir(const char *path)
{
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    if (!slash) slash = strrchr(dir, '\\');
    if (!slash) return;
    *slash = 0;
#ifdef _WIN32
    _mkdir(dir);
#else
    mkdir(dir, 0755);
#endif
}

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

static void resolve_conf_path(App *a, const char *base)
{
    const char *env = getenv("RG808_CONF");
    if (env && *env) {
        snprintf(a->conf_path, sizeof a->conf_path, "%s", env);
        return;
    }
    snprintf(a->conf_path, sizeof a->conf_path, "%sconf/pattern.txt", base);
}

static int save_pattern(App *a)
{
    ensure_parent_dir(a->conf_path);
    FILE *f = fopen(a->conf_path, "w");
    if (!f) return -1;
    fprintf(f, "RG808P1\n");
    fprintf(f, "BPM %.2f\n", a->eng.bpm_x100 / 100.0);
    for (int t = 0; t < N_TRACKS; t++) {
        fprintf(f, "%s ", TRACKS[t].label);
        for (int s = 0; s < N_STEPS; s++) fprintf(f, "%d", a->eng.lvl[t][s]);
        fprintf(f, "\n");
    }
    fclose(f);
    return 0;
}

static int load_pattern(App *a)
{
    FILE *f = fopen(a->conf_path, "r");
    if (!f) return -1;
    char line[128];
    int got_any = 0;
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "BPM ", 4)) {
            double bpm = atof(line + 4);
            if (bpm < 4) bpm = 4;
            if (bpm > 300) bpm = 300;
            a->eng.bpm_x100 = (int)(bpm * 100);
            got_any = 1;
        } else {
            char lab[8] = {0};
            if (sscanf(line, "%4s", lab) == 1) {
                for (int t = 0; t < N_TRACKS; t++) {
                    if (!strcmp(lab, TRACKS[t].label)) {
                        char *p = strchr(line, ' ');
                        int n = 0;
                        for (; p && *p; p++)
                            if (*p >= '0' && *p <= '2' && n < N_STEPS)
                                a->eng.lvl[t][n++] = (unsigned char)(*p - '0');
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

/* ------------------------------------------------------------------- ui */

static const Uint32 C_BG        = 0x17171f;
static const Uint32 C_PANEL     = 0x24242e;
static const Uint32 C_TEXT      = 0xe8d8a8;
static const Uint32 C_TEXT_DIM  = 0x8a8570;
static const Uint32 C_CURSOR    = 0xffffff;
static const Uint32 C_LED       = 0xff5040;
static const Uint32 C_ACCENT    = 0xffffff;

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
        cv_rect(c, 8, GRID_Y0 + t * ROW_PITCH, 4, CELL_H, TRACKS[t].color);
        cv_text(c, 22, y, TRACKS[t].label, 1,
                (a->mode == MODE_EDIT && a->cur_track == t) ? TRACKS[t].color : C_TEXT_DIM);
    }

    /* cells */
    for (int t = 0; t < N_TRACKS; t++) {
        Uint32 col = TRACKS[t].color;
        for (int s = 0; s < N_STEPS; s++) {
            int x = GRID_X0 + s * COL_PITCH;
            int y = GRID_Y0 + t * ROW_PITCH;
            unsigned char lv = e->lvl[t][s];
            Uint32 fill = lv == LVL_OFF ? cv_dim(col, 0.20f)
                        : lv == LVL_ON  ? cv_dim(col, 0.65f)
                                        : col;
            cv_rect(c, x, y, CELL_W, CELL_H, fill);
            if (lv == LVL_ACC)
                cv_border(c, x + 5, y + 5, CELL_W - 10, CELL_H - 10, 2, C_ACCENT);
            /* playing column highlight */
            if (s == led && e->playing)
                cv_border(c, x, y, CELL_W, CELL_H, 2, C_LED);
        }
    }

    /* cursor */
    if (a->mode == MODE_EDIT) {
        int x = GRID_X0 + a->cur_step * COL_PITCH;
        int y = GRID_Y0 + a->cur_track * ROW_PITCH;
        cv_border(c, x - 2, y - 2, CELL_W + 4, CELL_H + 4, 2, C_CURSOR);
    }

    /* status / menu / hints */
    Uint32 now = SDL_GetTicks();
    if (a->msg[0] && (int)(now - a->msg_until) < 0)
        cv_text(c, 10, SCR_H - 14, a->msg, 1, C_TEXT);
    else
        cv_text(c, 10, SCR_H - 14, "A:STEP  B:CLEAR  X:HEAR  START:PLAY  SELECT:MENU  L/R:TEMPO", 1, C_TEXT_DIM);

    if (a->mode == MODE_MENU) {
        static const char *items[] = { "EDIT TEMPO", "SAVE PATTERN", "LOAD PATTERN", "CLEAR ALL", "QUIT" };
        int n = 5;
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
        cv_text(c, mx + 12, my + 62, "L/R:-5  R/L:+5  UD:-5/+5  A/B:BACK", 1, C_TEXT_DIM);
    }
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
    case 1: set_msg(a, save_pattern(a) == 0 ? "PATTERN SAVED" : "SAVE FAILED"); break;
    case 2: set_msg(a, load_pattern(a) == 0 ? "PATTERN LOADED" : "NO SAVED PATTERN"); break;
    case 3:
        for (int t = 0; t < N_TRACKS; t++)
            for (int s = 0; s < N_STEPS; s++) a->eng.lvl[t][s] = 0;
        a->dirty = 1;
        set_msg(a, "PATTERN CLEARED");
        break;
    case 4: a->quit = 1; break;
    }
    if (item != 0) a->mode = MODE_EDIT;
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
            if (lv) engine_preview(e, a->cur_track);
            break;
        }
        case ACT_CLEAR: e->lvl[a->cur_track][a->cur_step] = LVL_OFF; a->dirty = 1; break;
        case ACT_HEAR:
        case ACT_HEAR_ALT: engine_preview(e, a->cur_track); break;
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
        case ACT_UP:    a->menu_sel = (a->menu_sel + 4) % 5; break;
        case ACT_DOWN:  a->menu_sel = (a->menu_sel + 1) % 5; break;
        case ACT_CONFIRM: do_menu(a, a->menu_sel); break;
        case ACT_BACK:
        case ACT_MENU:  a->mode = MODE_EDIT; break;
        default: break;
        }
        break;
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
    default: break;
    }
}

/* map SDL events -> action */
static int g_l_held = 0, g_r_held = 0;
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

static void handle_key(App *a, SDL_Keycode k)
{
    switch (k) {
    case SDLK_UP: case SDLK_w:     act(a, ACT_UP); break;
    case SDLK_DOWN: case SDLK_s:   act(a, ACT_DOWN); break;
    case SDLK_LEFT: case SDLK_a:   act(a, ACT_LEFT); break;
    case SDLK_RIGHT: case SDLK_d:  act(a, ACT_RIGHT); break;
    case SDLK_z: case SDLK_RETURN: act(a, ACT_CONFIRM); break;
    case SDLK_x:                   act(a, a->mode == MODE_EDIT ? ACT_CLEAR : ACT_BACK); break;
    case SDLK_c:                   act(a, ACT_HEAR); break;
    case SDLK_SPACE:               act(a, ACT_PLAY); break;
    case SDLK_m: case SDLK_ESCAPE: act(a, ACT_MENU); break;
    case SDLK_BACKSPACE:           act(a, ACT_BACK); break;
    case SDLK_LEFTBRACKET:         act(a, ACT_TEMPO_DOWN); break;
    case SDLK_RIGHTBRACKET:        act(a, ACT_TEMPO_UP); break;
    case SDLK_q:                   a->quit = 1; break;
    default: break;
    }
}

static void row_clear(App *a)
{
    for (int s = 0; s < N_STEPS; s++) a->eng.lvl[a->cur_track][s] = 0;
    a->dirty = 1;
    set_msg(a, "ROW %s CLEARED", TRACKS[a->cur_track].label);
}

static void handle_gc_button(App *a, int button, int down)
{
    if (down) {
        switch (button) {
        case SDL_CONTROLLER_BUTTON_A: act(a, ACT_CONFIRM); break;
        case SDL_CONTROLLER_BUTTON_B: act(a, a->mode == MODE_EDIT ? ACT_CLEAR : ACT_BACK); break;
        case SDL_CONTROLLER_BUTTON_X: case SDL_CONTROLLER_BUTTON_Y: act(a, ACT_HEAR); break;
        case SDL_CONTROLLER_BUTTON_START: act(a, ACT_PLAY); break;
        case SDL_CONTROLLER_BUTTON_BACK: act(a, ACT_MENU); break;
        case SDL_CONTROLLER_BUTTON_DPAD_UP: case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: {
            int ad = dir_action(button);
            act(a, ad);
            held_dir = ad; held_since = SDL_GetTicks(); last_auto = held_since;
            break;
        }
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
            if (g_r_held) { row_clear(a); }
            else { g_l_held = 1; act(a, ACT_TEMPO_DOWN); }
            break;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
            if (g_l_held) { row_clear(a); }
            else { g_r_held = 1; act(a, ACT_TEMPO_UP); }
            break;
        default: break;
        }
    } else {
        switch (button) {
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: g_l_held = 0; break;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: g_r_held = 0; break;
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
    if (!down) return;
    switch (button) {
    case 0: act(a, ACT_CONFIRM); break;       /* A */
    case 1: act(a, a->mode == MODE_EDIT ? ACT_CLEAR : ACT_BACK); break;  /* B */
    case 2: act(a, ACT_HEAR); break;          /* X */
    case 3: act(a, ACT_HEAR_ALT); break;      /* Y */
    case 6: act(a, ACT_MENU); break;          /* SELECT */
    case 7: act(a, ACT_PLAY); break;          /* START */
    case 4: act(a, ACT_TEMPO_DOWN); break;    /* L */
    case 5: act(a, ACT_TEMPO_UP); break;      /* R */
    case 8: case 9: a->quit = 1; break;       /* HOLD etc. -> quit */
    case 11: act(a, ACT_UP); break;           /* dpad via buttons */
    case 12: act(a, ACT_DOWN); break;
    case 13: act(a, ACT_LEFT); break;
    case 14: act(a, ACT_RIGHT); break;
    default: break;
    }
}

static void handle_joy_hat(App *a, int hat, int dir)
{
    if (hat != 0 || dir == SDL_HAT_CENTERED) return;
    if (dir & SDL_HAT_UP) act(a, ACT_UP);
    if (dir & SDL_HAT_DOWN) act(a, ACT_DOWN);
    if (dir & SDL_HAT_LEFT) act(a, ACT_LEFT);
    if (dir & SDL_HAT_RIGHT) act(a, ACT_RIGHT);
}

static void handle_joy_axis(App *a, int axis, int val)
{
    static int dir[2] = { 0, 0 };
    if (axis > 1) return;
    int nd = 0;
    if (val > 12000) nd = 1;
    else if (val < -12000) nd = -1;
    if (nd == dir[axis]) return;
    dir[axis] = nd;
    if (!nd) return;
    if (axis == 0) act(a, nd > 0 ? ACT_RIGHT : ACT_LEFT);
    else           act(a, nd > 0 ? ACT_DOWN : ACT_UP);
}

/* ---------------------------------------------------------------- bounce */

static int run_bounce(App *a, const char *outpath, int loops)
{
    int rate = 44100;
    a->eng.rate = rate;
    engine_init(&a->eng, rate);
    if (load_pattern(a) != 0) demo_pattern(a);
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
    a.cur_track = 0; a.cur_step = 0; a.mode = MODE_EDIT;

    const char *assets_arg = NULL;
    const char *bounce = NULL;
    const char *shot = NULL;
    int bounce_loops = 4;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--assets") && i + 1 < argc) assets_arg = argv[++i];
        else if (!strcmp(argv[i], "--bounce") && i + 1 < argc) bounce = argv[++i];
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--loops") && i + 1 < argc) bounce_loops = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--help")) {
            printf("usage: rg808 [--assets DIR] [--bounce out.wav --loops N] [--screenshot out.bmp]\n");
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

    if (bounce) {
        samples_load_all(assets, 44100);
        resolve_conf_path(&a, base ? base : ".");
        SDL_free(base);
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

    resolve_conf_path(&a, base ? base : ".");
    SDL_free(base);

    int missing = samples_load_all(assets, 44100);
    if (missing > 0) fprintf(stderr, "rg808: %d sample(s) missing from %s\n", missing, assets);

    if (load_pattern(&a) != 0) demo_pattern(&a);

    if (shot) {
        /* one UI frame -> BMP */
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
        if (SDL_IsGameController(i)) SDL_GameControllerOpen(i);
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
            case SDL_CONTROLLERBUTTONDOWN: handle_gc_button(&a, ev.cbutton.button, 1); break;
            case SDL_CONTROLLERBUTTONUP:   handle_gc_button(&a, ev.cbutton.button, 0); break;
            case SDL_CONTROLLERAXISMOTION: handle_joy_axis(&a, ev.caxis.axis, ev.caxis.value); break;
            case SDL_JOYHATMOTION: handle_joy_hat(&a, ev.jhat.hat, ev.jhat.value); break;
            case SDL_JOYBUTTONDOWN:
                if (!SDL_IsGameController(ev.jbutton.which))
                    handle_joy_button(&a, ev.jbutton.button, 1);
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
            act(&a, held_dir);
        }
        /* periodic autosave: kills/HOME-exit shouldn't lose the groove */
        if (a.dirty && (int)(now - a.last_save) > 5000) {
            save_pattern(&a);
            a.dirty = 0;
            a.last_save = now;
        }
        draw_ui(&a);
        present(&a);
    }

    save_pattern(&a);
    if (a.dev) SDL_CloseAudioDevice(a.dev);
    SDL_Quit();
    return 0;
}
