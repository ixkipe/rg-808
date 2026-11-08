/* Shared step-volume modal: ghost / weak / regular / accented.
 *
 * The sequencer's A button is a press/release pair here: a quick tap keeps the
 * old off -> regular -> accented cycle, while holding A for 500 ms opens the
 * modal. The modal is machine-agnostic; only the track table and engine sample
 * preview come from the core.
 */
#ifndef RG808_STEP_UI_H
#define RG808_STEP_UI_H

#include "canvas.h"
#include "engine.h"

typedef struct StepUi {
    int active;                 /* modal open */
    int track, step;            /* step being edited */
    int sel;                    /* 0=ghost 1=weak 2=regular 3=accented */
    int a_held;                 /* A is down in the sequencer */
    int long_fired;             /* this press already opened the modal */
    unsigned int a_down_ms;
} StepUi;

void step_ui_init(StepUi *ui);

/* Sequencer A press/release. a_down records the step; a_up returns 1 when it
 * changed the level (quick tap), 0 when the press belonged to the modal. */
void step_ui_a_down(StepUi *ui, Engine *e, int track, int step,
                    unsigned int now);
int step_ui_a_up(StepUi *ui, Engine *e);

/* Call once per frame while in the sequencer. Returns 1 when the 500 ms hold
 * just opened the modal. */
int step_ui_tick(StepUi *ui, Engine *e, int track, int step,
                 unsigned int now);

/* Modal actions. */
void step_ui_move(StepUi *ui, Engine *e, int dir);
int step_ui_confirm(StepUi *ui, Engine *e);   /* returns 1 on confirm */
void step_ui_cancel(StepUi *ui);

void step_ui_draw(const StepUi *ui, const Engine *e, Canvas *c,
                  unsigned int panel, unsigned int text,
                  unsigned int dim, unsigned int cursor);

#endif
