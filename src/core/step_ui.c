#include "step_ui.h"

#include <stdio.h>
#include <string.h>

static const unsigned char LEVEL_FOR_SEL[4] = {
    LVL_GHOST, LVL_WEAK, LVL_ON, LVL_ACC
};

static unsigned char level_for_sel(int sel)
{
    if (sel < 0) sel = 0;
    if (sel > 3) sel = 3;
    return LEVEL_FOR_SEL[sel];
}

static int sel_for_level(unsigned char lv)
{
    switch (lv) {
    case LVL_GHOST: return 0;
    case LVL_WEAK:  return 1;
    case LVL_ACC:   return 3;
    default:        return 2;   /* off and regular both start at REGULAR */
    }
}

static void preview(StepUi *ui, Engine *e)
{
    if (ui->track < 0 || ui->track >= N_TRACKS) return;
    unsigned char lv = level_for_sel(ui->sel);
    engine_preview(e, ui->track, e->kit[ui->track],
                   e->rch[ui->track][ui->step], lv);
}

void step_ui_init(StepUi *ui)
{
    memset(ui, 0, sizeof *ui);
    ui->sel = 2;
}

void step_ui_a_down(StepUi *ui, Engine *e, int track, int step,
                    unsigned int now)
{
    (void)e;
    ui->a_held = 1;
    ui->long_fired = 0;
    ui->track = track;
    ui->step = step;
    ui->a_down_ms = now;
}

int step_ui_a_up(StepUi *ui, Engine *e)
{
    if (!ui->a_held) return 0;
    ui->a_held = 0;
    if (ui->long_fired) {
        ui->long_fired = 0;
        return 0;   /* the press opened the modal; do not cycle on release */
    }
    if (ui->track < 0 || ui->track >= N_TRACKS) return 0;

    unsigned char lv = e->lvl[ui->track][ui->step];
    if (lv >= LVL_GHOST) lv = LVL_OFF;
    else lv = (unsigned char)((lv + 1) % 3);
    e->lvl[ui->track][ui->step] = lv;
    if (lv)
        engine_preview(e, ui->track, e->kit[ui->track],
                       e->rch[ui->track][ui->step], lv);
    return 1;
}

int step_ui_tick(StepUi *ui, Engine *e, int track, int step,
                 unsigned int now)
{
    if (!ui->a_held || ui->long_fired) return 0;
    if (track != ui->track || step != ui->step) {
        ui->a_held = 0;   /* cursor moved: cancel the hold */
        return 0;
    }
    if ((unsigned int)(now - ui->a_down_ms) < 500) return 0;

    ui->active = 1;
    ui->track = track;
    ui->step = step;
    ui->sel = sel_for_level(e->lvl[track][step]);
    ui->long_fired = 1;
    return 1;
}

void step_ui_move(StepUi *ui, Engine *e, int dir)
{
    if (!ui->active) return;
    ui->sel = (ui->sel + dir + 4) % 4;
    preview(ui, e);
}

int step_ui_confirm(StepUi *ui, Engine *e)
{
    if (!ui->active) return 0;
    if (ui->track >= 0 && ui->track < N_TRACKS)
        e->lvl[ui->track][ui->step] = level_for_sel(ui->sel);
    preview(ui, e);
    ui->active = 0;
    return 1;
}

void step_ui_cancel(StepUi *ui)
{
    ui->active = 0;
}

void step_ui_draw(const StepUi *ui, const Engine *e, Canvas *c,
                  unsigned int panel, unsigned int text,
                  unsigned int dim, unsigned int cursor)
{
    if (!ui->active) return;
    if (ui->track < 0 || ui->track >= N_TRACKS) return;

    const int w = 320, h = 214;
    const int x = (640 - w) / 2, y = (480 - h) / 2;
    unsigned int col = TRACKS[ui->track].color;

    cv_rect(c, x, y, w, h, panel);
    cv_border(c, x, y, w, h, 2, text);
    cv_text(c, x + 14, y + 10, "STEP LEVEL", 1, text);

    char buf[64];
    snprintf(buf, sizeof buf, "TRACK %s  STEP %02d",
             TRACKS[ui->track].label, ui->step + 1);
    cv_text(c, x + 14, y + 28, buf, 1, dim);

    static const char *names[4] = { "GHOST", "WEAK", "REGULAR", "ACCENTED" };
    for (int i = 0; i < 4; i++) {
        int ry = y + 58 + i * 30;
        int sel = (i == ui->sel);
        if (sel) {
            cv_rect(c, x + 10, ry - 5, w - 20, 24, cv_dim(col, 0.28f));
            cv_border(c, x + 10, ry - 5, w - 20, 24, 1, cursor);
        }
        cv_text(c, x + 22, ry, names[i], 1, sel ? cursor : text);
        if (level_for_sel(i) == e->lvl[ui->track][ui->step])
            cv_text(c, x + w - 70, ry, "CURRENT", 1, dim);
    }

    cv_text(c, x + 14, y + h - 20, "DPAD:SELECT  A:SET  B:CANCEL", 1, dim);
}
