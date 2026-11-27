#include "midi.h"
#include "engine.h"
#include "fx.h"
#include "pattern.h"
#include "perf_fx.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Sanity bounds for the gap between two timing clocks: 20..400 BPM. Outside
 * the range the stream stalled or glitched, so the lock is re-seeded instead
 * of trying to smooth across the jump. */
#define MIDI_CLK_MIN_FRAMES(rate) ((rate) * 60 / (400 * 24))
#define MIDI_CLK_MAX_FRAMES(rate) ((rate) * 60 / (20 * 24) + 1)

void midi_init(MidiClock *mc, int rate)
{
    memset(mc, 0, sizeof *mc);
    mc->rate = rate > 0 ? rate : 44100;
    mc->spp_pending = -1;
    atomic_store_explicit(&mc->qr, 0, memory_order_relaxed);
    atomic_store_explicit(&mc->qw, 0, memory_order_relaxed);
}

void midi_flush(MidiClock *mc)
{
    atomic_store_explicit(&mc->qr, 0, memory_order_relaxed);
    atomic_store_explicit(&mc->qw, 0, memory_order_relaxed);
}

/* One 16th from the measured clock interval: 6 clocks per 16th. */
double midi_step_frames(const MidiClock *mc, int rate)
{
    if (!mc || !mc->interval_valid) return 0.0;
    double r = rate > 0 ? (double)rate : (double)mc->rate;
    double scale = r / (double)mc->rate;    /* engine rate may differ */
    return mc->interval * 6.0 * scale;
}

/* Clock -> BPM lock. The smoothing keeps the last tempo dominant while
 * tracking the master within a couple of clocks (0.25 per new interval). */
static void midi_clock_measure(MidiClock *mc, unsigned long long now)
{
    if (mc->last_valid) {
        double dt = (double)now - (double)mc->last_at;
        if (dt <= 0.0) {
            /* duplicate timestamp: ignore, keep the current interval */
        } else if (dt < (double)MIDI_CLK_MIN_FRAMES(mc->rate)) {
            /* impossibly fast burst: ignore, keep the current interval */
        } else if (dt > (double)MIDI_CLK_MAX_FRAMES(mc->rate)) {
            /* stall or restart: re-seed on the next sane clock */
            mc->interval_valid = 0;
        } else if (!mc->interval_valid) {
            mc->interval = dt;
            mc->interval_valid = 1;
        } else {
            mc->interval += (dt - mc->interval) * 0.25;
        }
    }
    mc->last_at = now;
    mc->last_valid = 1;
    if (mc->interval_valid)
        mc->bpm_x100 = (int)(6000.0 * mc->rate / (mc->interval * 24.0) + 0.5);
}

static void midi_push(MidiClock *mc, unsigned char type, unsigned long long now)
{
    unsigned int r = atomic_load_explicit(&mc->qr, memory_order_acquire);
    unsigned int w = atomic_load_explicit(&mc->qw, memory_order_relaxed);
    unsigned int free_slots = MIDI_Q_MAX - (w - r);
    if (free_slots == 0) return;
    /* Keep the tail slots free for transport events: a stalled consumer must
     * not bury a START under stale timing clocks. */
    if (type == MIDI_EV_CLOCK && free_slots <= MIDI_CLOCK_RESERVE) return;
    MidiEvent *slot = &mc->q[w & (MIDI_Q_MAX - 1)];
    slot->at = now;
    slot->type = type;
    slot->value = 0;
    atomic_store_explicit(&mc->qw, w + 1, memory_order_release);
}

int midi_next_event(MidiClock *mc, unsigned long long before, MidiEvent *ev)
{
    unsigned int r = atomic_load_explicit(&mc->qr, memory_order_relaxed);
    unsigned int w = atomic_load_explicit(&mc->qw, memory_order_acquire);
    if (r == w) return 0;
    const MidiEvent *head = &mc->q[r & (MIDI_Q_MAX - 1)];
    if (head->at >= before) return 0;
    *ev = *head;
    atomic_store_explicit(&mc->qr, r + 1, memory_order_release);
    return 1;
}

/* Feed one byte. System real-time bytes (0xF8..0xFF) are valid anywhere and
 * never disturb running status or a sysex dump. */
static void midi_byte(MidiClock *mc, unsigned char b, unsigned long long now)
{
    if (b >= 0xF8) {
        switch (b) {
        case 0xF8:                          /* timing clock */
            mc->stat_clock++;
            mc->clocks++;
            midi_clock_measure(mc, now);
            midi_push(mc, MIDI_EV_CLOCK, now);
            break;
        case 0xFA:                          /* start */
            mc->stat_start++;
            mc->running = 1;
            midi_push(mc, MIDI_EV_START, now);
            break;
        case 0xFB:                          /* continue */
            mc->stat_cont++;
            mc->running = 1;
            midi_push(mc, MIDI_EV_CONTINUE, now);
            break;
        case 0xFC:                          /* stop */
            mc->stat_stop++;
            mc->running = 0;
            midi_push(mc, MIDI_EV_STOP, now);
            break;
        default:                            /* 0xFE sense, 0xFF reset: ignored */
            break;
        }
        return;
    }

    if (mc->escape) {                       /* sysex payload */
        if (b == 0xF7) mc->escape = 0;
        return;
    }

    if (b & 0x80) {                         /* status byte */
        mc->common_need = 0;
        mc->chan_need = 0;
        mc->d1 = 0;
        if (b == 0xF0) {                    /* sysex begin */
            mc->escape = 1;
            mc->status = 0;
            return;
        }
        if (b < 0xF0) {                     /* channel voice message */
            mc->status = b;                 /* new running status */
            mc->chan_need = (b == 0xC0 || b == 0xD0) ? 1 : 2;
        } else {                            /* system common */
            mc->status = 0;                 /* clears running status */
            if (b == 0xF2) mc->common_need = 2;   /* song position */
            else if (b == 0xF1 || b == 0xF3) mc->common_need = 1;
        }
        return;
    }

    /* data byte */
    if (mc->common_need > 0) {
        if (mc->common_need == 2) {
            mc->d1 = b;
            mc->common_need = 1;
            return;
        }
        mc->common_need = 0;
        int spp = mc->d1 | ((int)b << 7);   /* MIDI beat = one 16th */
        if (spp > 16383) spp = 16383;
        mc->spp_pending = spp;
        mc->stat_spp++;
        return;
    }
    if (mc->status) {
        unsigned char st = (unsigned char)(mc->status & 0xF0);
        int two = (st != 0xC0 && st != 0xD0);   /* 1- vs 2-data-byte messages */
        if (mc->chan_need == 0) {
            /* running status: the message restarts with the same status */
            mc->chan_need = two ? 2 : 1;
            mc->d1 = 0;
        }
        if (mc->chan_need == 2) {
            mc->d1 = b;
            mc->chan_need = 1;
            return;
        }
        mc->chan_need = 0;
        if (st == 0x90 && b > 0)        /* note-on */
            mc->stat_note++;
        return;
    }
    /* stray data byte: ignored */
}

void midi_feed(MidiClock *mc, const unsigned char *data, int len,
               unsigned long long now)
{
    if (!mc || !data) return;
    for (int i = 0; i < len; i++)
        midi_byte(mc, data[i], now);
}

/* ------------------------------------------------------------ self test */

static int check(int cond, const char *msg, int *fails)
{
    if (cond) {
        printf("PASS: %s\n", msg);
    } else {
        printf("FAIL: %s\n", msg);
        (*fails)++;
    }
    return cond;
}

static void feed_byte(MidiClock *mc, unsigned char b, unsigned long long at)
{
    midi_feed(mc, &b, 1, at);
}

/* Mix `e` forward in 512-frame chunks until `until`, feeding timing clocks
 * every `clk` frames along the way. The last chunk is capped at `until`, so
 * the engine never mixes past it. */
static void run_master(Engine *e, MidiClock *mc, float *buf,
                       unsigned long long *next_clock, double clk,
                       unsigned long long until)
{
    while (e->total_pos < until) {
        unsigned long long chunk = 512;
        if (until - e->total_pos < chunk) chunk = until - e->total_pos;
        unsigned long long end = e->total_pos + chunk;
        while (*next_clock < end) {
            feed_byte(mc, 0xF8, *next_clock);
            *next_clock += (unsigned long long)(clk + 0.5);
        }
        engine_mix(e, buf, (int)chunk);
    }
}

static void fill_synth(Engine *e, float *sample, int len)
{
    for (int i = 0; i < len; i++) sample[i] = 0.25f;
    for (int t = 0; t < N_TRACKS; t++) {
        e->sample_data[t] = sample;
        e->sample_len[t] = len;
    }
}

int midi_selftest(int rate)
{
    int fails = 0;
    MidiClock mc;
    unsigned char stream[64];

    /* ---- parser: running status, sysex, real-time interleaving, SPP ---- */
    midi_init(&mc, rate);
    memcpy(stream, (unsigned char[]){0x90, 0x3C, 0x40, 0x3E, 0x40}, 5);
    midi_feed(&mc, stream, 5, 0);            /* note-on + running status */
    memcpy(stream, (unsigned char[]){0x92, 0x40, 0xF8, 0x50}, 4);
    midi_feed(&mc, stream, 4, 0);            /* clock inside a channel message */
    memcpy(stream, (unsigned char[]){0x80, 0x3C, 0x00}, 3);
    midi_feed(&mc, stream, 3, 0);            /* note-off: new running status */
    memcpy(stream, (unsigned char[]){0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7}, 6);
    midi_feed(&mc, stream, 6, 0);            /* sysex identity reply */
    memcpy(stream, (unsigned char[]){0xB0, 0x07, 0x64}, 3);
    midi_feed(&mc, stream, 3, 0);            /* CC7 */
    memcpy(stream, (unsigned char[]){0xF2, 0x0A, 0x00}, 3);
    midi_feed(&mc, stream, 3, 0);            /* song position: 10 sixteenths */
    memcpy(stream, (unsigned char[]){0xFE, 0xFE}, 2);
    midi_feed(&mc, stream, 2, 0);            /* active sensing */
    memcpy(stream, (unsigned char[]){0x91, 0x30, 0x7F}, 3);
    midi_feed(&mc, stream, 3, 0);            /* note-on on another channel */

    check(mc.stat_note == 4, "parser counts note-ons across running status",
          &fails);
    check(mc.stat_clock == 1, "a real-time clock inside a note is counted",
          &fails);
    check(mc.spp_pending == 10, "song position parses (10 sixteenths)", &fails);
    check(mc.stat_clock == 1 && mc.stat_note == 4,
          "sysex and active sensing do not disturb the parser", &fails);

    /* ---- transport bytes ---- */
    midi_init(&mc, rate);
    {
        unsigned char b = 0xFA;
        midi_feed(&mc, &b, 1, 0);
        check(mc.running == 1, "START runs the transport", &fails);
        b = 0xFC;
        midi_feed(&mc, &b, 1, 0);
        check(mc.running == 0, "STOP halts the transport", &fails);
        b = 0xFB;
        midi_feed(&mc, &b, 1, 0);
        check(mc.running == 1, "CONTINUE resumes the transport", &fails);
    }

    /* ---- clock -> BPM lock ---- */
    midi_init(&mc, rate);
    {
        double clk = (double)rate * 60.0 / (120.0 * 24.0);
        unsigned long long at = 0;
        for (int k = 0; k < 100; k++) {
            at += (unsigned long long)(clk + 0.5);
            feed_byte(&mc, 0xF8, at);
        }
        int bpm = mc.bpm_x100;
        check(bpm >= 12000 - 12 && bpm <= 12000 + 12,
              "the clock locks to 120 BPM", &fails);
        for (int k = 0; k < 100; k++) {
            at += (unsigned long long)(clk * 2.0 + 0.5);
            feed_byte(&mc, 0xF8, at);
        }
        bpm = mc.bpm_x100;
        check(bpm >= 6000 - 12 && bpm <= 6000 + 12,
              "the lock follows the master to 60 BPM", &fails);
        double sps = midi_step_frames(&mc, rate);
        double want = (double)rate * 60.0 / (60.0 * 4.0);
        check(fabs(sps - want) < want * 0.005,
              "the locked 16th length matches the tempo", &fails);
    }

    /* ---- engine external clock: live pattern ---- */
    {
        static float synth[2000];
        Engine e;
        engine_init(&e, rate);
        engine_midi_attach(&e, &mc);
        e.ext_clock = 1;                    /* the app toggles this with the device */
        midi_init(&mc, rate);
        fill_synth(&e, synth, 2000);
        e.length = 16;
        e.lvl[0][0] = LVL_ON;
        e.lvl[0][5] = LVL_ON;
        e.lvl[0][8] = LVL_ACC;
        e.lvl[0][13] = LVL_ON;

        float *buf = malloc(sizeof(float) * 2 * 512);
        double clk = (double)rate * 60.0 / (120.0 * 24.0);
        double sps16 = clk * 6.0;
        unsigned long long next_clock = 100;

        /* START, then the first clock marks the start of step 0 */
        feed_byte(&mc, 0xFA, 100);
        engine_mix(&e, buf, 512);
        check(e.playing == 1, "the master's START starts our transport", &fails);
        check(e.cur_step == 0, "the playhead arms at step 0 on START", &fails);
        check(e.step_fire_at == 100,
              "the first clocked step fires at the clock's frame", &fails);

        run_master(&e, &mc, buf, &next_clock, clk,
                   (unsigned long long)(100 + sps16 * 4));
        check(e.step == 3 && mc.pos == 4,
              "one quarter drives four of our 16th steps", &fails);
        check(llabs((long long)e.step_fire_at - (long long)(100 + sps16 * 3))
                  <= (long long)clk + 1,
              "step boundaries keep the clock's cadence", &fails);

        run_master(&e, &mc, buf, &next_clock, clk,
                   (unsigned long long)(100 + sps16 * 8));
        check(e.step == 7 && mc.pos == 8,
              "the second quarter keeps the lock", &fails);
        run_master(&e, &mc, buf, &next_clock, clk,
                   (unsigned long long)(100 + sps16 * 16));
        check(e.step == 15 && mc.pos == 16,
              "sixteen clocked sixteenths play a full bar", &fails);
        check(e.bpm_x100 >= 12000 - 12 && e.bpm_x100 <= 12000 + 12,
              "the engine tempo follows the master", &fails);

        /* our pattern length stays ours: LEN 12 wraps inside the master's grid */
        e.length = 12;
        run_master(&e, &mc, buf, &next_clock, clk,
                   next_clock + (unsigned long long)(clk * 6.0 * 13.0));
        check(e.step == 0 && mc.pos == 29,
              "a 12-step pattern wraps inside the master's clock", &fails);

        /* punch-in quantization still hits the next clocked 16th */
        engine_punch_trigger(&e, PUNCH_LOOP_4);
        run_master(&e, &mc, buf, &next_clock, clk,
                   next_clock + (unsigned long long)(clk * 6.0 + 1));
        check(engine_punch_active(&e) == PUNCH_LOOP_4,
              "a punch-in engages on the next clocked 16th", &fails);
        engine_punch_release(&e);

        /* STOP stops us; SPP + CONTINUE resumes at the song position */
        feed_byte(&mc, 0xFC, e.total_pos + 16);
        engine_mix(&e, buf, 512);
        check(e.playing == 0 && e.cur_step == -1,
              "the master's STOP stops us", &fails);
        next_clock = e.total_pos + 100;
        {
            unsigned char b[] = {0xF2, 0x0A, 0x00, 0xFB};   /* SPP 10 + continue */
            midi_feed(&mc, b, 4, e.total_pos);
        }
        engine_mix(&e, buf, 512);
        run_master(&e, &mc, buf, &next_clock, clk, next_clock + 528);
        check(e.playing == 1 && e.step == 10,
              "CONTINUE plays from the song position", &fails);

        free(buf);
        engine_free(&e);
    }

    /* ---- a synced delay bypasses while the clock drives us ---- */
    {
        static float synth[2000];
        Engine ea, eb;
        MidiClock ma, mb;
        engine_init(&ea, rate);
        engine_init(&eb, rate);
        midi_init(&ma, rate);
        midi_init(&mb, rate);
        engine_midi_attach(&ea, &ma);
        ea.ext_clock = 1;
        engine_midi_attach(&eb, &mb);
        eb.ext_clock = 1;
        fill_synth(&ea, synth, 2000);
        fill_synth(&eb, synth, 2000);
        ea.lvl[0][0] = ea.lvl[0][4] = LVL_ON;
        eb.lvl[0][0] = eb.lvl[0][4] = LVL_ON;

        /* identical chains; ea's delay is tempo-synced (bypassed under the
           clock), eb's is off - the reference render */
        FxSettings fx;
        fx_defaults(&fx);
        fx.on = 1;
        fx.delay_on = 1;
        fx.delay_sync = 1;
        fx.delay_div = FX_DIV_4;
        fx.delay_fb = 40;
        fx.delay_mix = 50;
        fx_apply(&ea, &fx);
        fx.delay_on = 0;
        fx_apply(&eb, &fx);

        float *buf = malloc(sizeof(float) * 2 * 512);
        float *ref = malloc(sizeof(float) * 2 * 512);
        double clk = (double)rate * 60.0 / (120.0 * 24.0);
        unsigned long long na = 100, nb = 100;
        feed_byte(&ma, 0xFA, 100);
        feed_byte(&mb, 0xFA, 100);

        int diff = 0;
        unsigned long long until = (unsigned long long)(100 + clk * 24.0);
        while (ea.total_pos < until) {
            unsigned long long end = ea.total_pos + 512;
            while (na < end) {
                feed_byte(&ma, 0xF8, na);
                na += (unsigned long long)(clk + 0.5);
            }
            while (nb < end) {
                feed_byte(&mb, 0xF8, nb);
                nb += (unsigned long long)(clk + 0.5);
            }
            engine_mix(&ea, buf, 512);
            engine_mix(&eb, ref, 512);
            if (memcmp(buf, ref, sizeof(float) * 2 * 512) != 0) diff = 1;
        }
        check(ea.delay_bypassed == 1,
              "a synced delay bypasses under the clock", &fails);
        check(!diff, "the bypassed synced delay is bit-identical to delay off",
              &fails);

        /* the delay returns (with a cleared line) when the clock detaches */
        ea.ext_clock = 0;
        engine_mix(&ea, buf, 512);
        check(ea.delay_bypassed == 0,
              "the synced delay returns when the device detaches", &fails);

        free(buf);
        free(ref);
        engine_free(&ea);
        engine_free(&eb);
    }

    /* ---- engine external clock: our song chain stays ours ---- */
    {
        static float synth[2000];
        Engine e;
        engine_init(&e, rate);
        engine_midi_attach(&e, &mc);
        e.ext_clock = 1;
        midi_init(&mc, rate);
        fill_synth(&e, synth, 2000);

        SongSet *set = calloc(1, sizeof *set);
        float *buf = malloc(sizeof(float) * 2 * 512);
        if (set && buf) {
            pattern_default_snapshot(&set->pat[0]);
            pattern_default_snapshot(&set->pat[1]);
            set->count = 2;
            set->pat[0].bpm_x100 = 12000;
            set->pat[0].length = 8;
            set->pat[0].lvl[0][0] = LVL_ON;
            set->pat[1].bpm_x100 = 9600;
            set->pat[1].length = 16;
            set->pat[1].lvl[1][0] = LVL_ON;
            machine_resolve_snapshot(&set->pat[0]);
            machine_resolve_snapshot(&set->pat[1]);

            engine_song_start(&e, set, 1, 0);
            engine_restart(&e);

            double clk = (double)rate * 60.0 / (120.0 * 24.0);
            double sps16 = clk * 6.0;
            unsigned long long next_clock = 100;
            feed_byte(&mc, 0xFA, 100);

            /* bar 0 is LEN 8: its eight steps ride eight master sixteenths,
             * then the chain moves on to bar 2 */
            run_master(&e, &mc, buf, &next_clock, clk,
                       (unsigned long long)(100 + sps16 * 8));
            check(e.song_bar == 0 && e.step == 7,
                  "a LEN 8 bar plays eight master sixteenths", &fails);
            run_master(&e, &mc, buf, &next_clock, clk,
                       (unsigned long long)(100 + sps16 * 9));
            check(e.song_bar == 1 && e.step == 0,
                  "the ninth 16th starts bar 2 with our own length", &fails);

            /* STOP, then SPP into the middle of the chain + CONTINUE */
            feed_byte(&mc, 0xFC, e.total_pos + 16);
            engine_mix(&e, buf, 512);
            next_clock = e.total_pos + 100;
            {
                unsigned char b[] = {0xF2, 0x09, 0x00, 0xFB};   /* SPP 9 + continue */
                midi_feed(&mc, b, 4, e.total_pos);
            }
            engine_mix(&e, buf, 512);
            run_master(&e, &mc, buf, &next_clock, clk, next_clock + 528);
            check(e.playing == 1 && e.song_bar == 1 && e.step == 1,
                  "SPP + CONTINUE jumps into our song chain", &fails);
        } else {
            check(0, "song-mode midi test buffers", &fails);
        }
        free(set);
        free(buf);
        engine_free(&e);
    }

    if (fails == 0) printf("PASS: midi parser, tempo lock and transport\n");
    return fails;
}
