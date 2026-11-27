/* USB-MIDI device input (v1.1 P5): a background reader for Linux usbfs.
 *
 * The device kernel has no snd-usb-audio, so a MIDI interface never becomes
 * an ALSA rawmidi node.  This module does what tools/device/usbmidi_probe.py
 * proved on hardware: scan /sys/bus/usb/devices for a MIDI streaming
 * interface (generic class 01 or Roland vendor class - both carry sub 03),
 * claim it with USBDEVFS_CLAIMINTERFACE and drain its bulk IN endpoint from
 * the moment the device enumerates.  A master whose IN pipe nobody drains
 * fills its buffer and gives up (confirmed on the T-8), so the reader polls
 * for the device and attaches within a fraction of a second.
 *
 * Expanded MIDI bytes feed the P4 MidiClock; the engine consumes it in
 * external-clock mode.  On non-Linux builds the reader is a clean no-op and
 * the app stays on its internal clock.
 */
#ifndef RG808_USBMIDI_H
#define RG808_USBMIDI_H

#include "midi.h"

/* ---- portable pure parts (covered by --test-usbmidi) ---- */

#define USBMIDI_MAX_IF 16
#define USBMIDI_MAX_EP 6

typedef struct UsbMidiEp {
    unsigned char addr;         /* endpoint address, direction in bit 7 */
    unsigned char attr;         /* transfer type: 1 iso, 2 bulk, 3 interrupt */
    int maxpkt;
} UsbMidiEp;

typedef struct UsbMidiIface {
    unsigned char num, alt;
    unsigned char cls, sub;     /* MIDI streaming shows up as sub 0x03 */
    int nep;
    UsbMidiEp eps[USBMIDI_MAX_EP];
} UsbMidiIface;

/* Parse a raw configuration descriptor (sysfs `descriptors`).  Returns the
 * number of interface altsettings written to `out` (<= cap). */
int usbmidi_parse_config(const unsigned char *blob, int len,
                         UsbMidiIface *out, int cap);

/* Pick the best MIDI streaming interface: sub 0x03 with an IN endpoint,
 * bulk preferred over interrupt, alt 0 preferred over others.  Returns the
 * interface index into `ifs` (or -1); the endpoint details come back through
 * the out parameters. */
int usbmidi_pick(const UsbMidiIface *ifs, int n,
                 int *alt, int *ep, int *maxpkt);

/* Expand 4-byte USB-MIDI event packets into one MIDI byte stream.  Returns
 * the number of MIDI bytes written (<= cap). */
int usbmidi_expand(const unsigned char *pkt, int len,
                   unsigned char *out, int cap);

/* ---- background reader ---- */

typedef struct UsbMidi {
    MidiClock *midi;                /* byte stream target */
    const unsigned long long *now;  /* frame clock source (engine total_pos) */
    volatile int attached;          /* device claimed and being drained */
    volatile int stop;              /* set by usbmidi_stop() */
    char node[48];                  /* current /dev/bus/usb/BBB/DDD, "" = none */
    void *thread;                   /* pthread_t on Linux */
    int started;
} UsbMidi;

/* Start the reader thread. `now` is the engine's sample position, used to
 * timestamp incoming events on the engine's clock. */
int usbmidi_start(UsbMidi *u, MidiClock *midi, const unsigned long long *now);

/* Stop the thread, release the interface, close the device. */
void usbmidi_stop(UsbMidi *u);

/* 1 while a device is claimed and being drained. */
int usbmidi_attached(const UsbMidi *u);

#endif
