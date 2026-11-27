/* MIDI engine: byte-stream parser, clock -> BPM lock, transport, event queue.
 *
 * The app (or a test) feeds raw MIDI bytes as they arrive; the parser keeps
 * running status, survives interleaved system real-time bytes and sysex dumps,
 * and measures the master's tempo from the timing clocks. Transport events are
 * timestamped on the engine's sample clock and queued for the audio callback,
 * which runs the external-clock mode of the Engine (see engine_midi_attach).
 *
 * v1.1 P4 ships this engine-first with a synthetic-stream test; the device
 * input (usbfs reader) lands with P5 and feeds midi_feed() from there. */
#ifndef RG808_MIDI_H
#define RG808_MIDI_H

#include <stdatomic.h>
#include <stdint.h>

/* SPSC event ring: the input thread pushes, the audio callback pops. */
#define MIDI_Q_MAX 64           /* power of two */
#define MIDI_CLOCK_RESERVE 8    /* never let clocks fill the last slots */

enum {
    MIDI_EV_CLOCK = 0,          /* 0xF8: one of 24 per quarter note */
    MIDI_EV_START,              /* 0xFA: play from the song position */
    MIDI_EV_STOP,               /* 0xFC: stop, keep the position */
    MIDI_EV_CONTINUE            /* 0xFB: play from the song position / resume */
};

typedef struct MidiEvent {
    unsigned long long at;      /* engine frame the event happened at */
    unsigned char type;         /* MIDI_EV_* */
    int value;                  /* unused (SPP travels in spp_pending) */
} MidiEvent;

typedef struct MidiClock {
    /* parser state (input thread only) */
    unsigned char status;       /* running status for channel messages, 0 = none */
    int chan_need;              /* data bytes still expected by `status` */
    int common_need;            /* data bytes still expected by a system common */
    int d1;                     /* first data byte seen so far */
    int escape;                 /* inside a sysex dump (until 0xF7) */

    /* transport + tempo lock (input thread writes, UI/audio read) */
    volatile int running;       /* master transport is running */
    volatile int spp_pending;   /* -1 = none, else song position in 16ths */
    volatile int pos;           /* 16ths since the last transport start
                                   (advanced by the audio thread) */
    volatile int bpm_x100;      /* measured tempo, 0 = not locked yet */
    int rate;                   /* engine rate, for frame <-> time math */

    /* clock-interval measurement (input thread only) */
    double interval;            /* smoothed frames between clocks */
    int interval_valid;
    unsigned long long clocks;  /* total clocks seen */
    unsigned long long last_at; /* frame of the previous clock */
    int last_valid;

    /* event queue (SPSC; indices are C11 atomics, slots are plain) */
    MidiEvent q[MIDI_Q_MAX];
    _Atomic unsigned int qr;    /* consumer (audio thread) */
    _Atomic unsigned int qw;    /* producer (input thread) */

    /* counters for tests and diagnostics */
    unsigned long long stat_clock, stat_start, stat_stop, stat_cont, stat_spp;
    unsigned long long stat_note;
} MidiClock;

void midi_init(MidiClock *mc, int rate);

/* Parse raw MIDI bytes. `now` is the current engine frame, used to timestamp
 * the queued transport/clock events. */
void midi_feed(MidiClock *mc, const unsigned char *data, int len,
               unsigned long long now);

/* Pop the next event whose timestamp is older than `before` (the end of the
 * buffer being mixed), or return 0. Events in the future stay queued. */
int midi_next_event(MidiClock *mc, unsigned long long before, MidiEvent *ev);

void midi_flush(MidiClock *mc);      /* drop queued events (quiescent only) */

/* Length of one 16th in frames from the measured tempo, or 0 while unlocked. */
double midi_step_frames(const MidiClock *mc, int rate);

/* Headless synthetic-stream test for `--test-midi`: parser/running status,
 * clock -> BPM lock, transport + song position, and the engine's external
 * clock mode (live pattern, own length wrap, song chain, punch quantization).
 * Prints PASS/FAIL lines and returns the number of failures. */
int midi_selftest(int rate);

#endif
