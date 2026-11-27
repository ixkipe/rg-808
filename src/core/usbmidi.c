#include "usbmidi.h"

#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------- descriptors */

int usbmidi_parse_config(const unsigned char *blob, int len,
                         UsbMidiIface *out, int cap)
{
    int n = 0, i = 0;
    while (i + 2 <= len) {
        int blen = blob[i];
        int btype = blob[i + 1];
        if (blen < 2 || i + blen > len) break;
        if (btype == 4 && blen >= 9) {                  /* INTERFACE */
            if (n < cap) {
                UsbMidiIface *f = &out[n];
                memset(f, 0, sizeof *f);
                f->num = blob[i + 2];
                f->alt = blob[i + 3];
                f->nep = blob[i + 4];
                f->cls = blob[i + 5];
                f->sub = blob[i + 6];
                if (f->nep > USBMIDI_MAX_EP) f->nep = USBMIDI_MAX_EP;
                n++;
            }
        } else if (btype == 5 && blen >= 7 && n > 0) {  /* ENDPOINT */
            UsbMidiIface *f = &out[n - 1];
            for (int k = 0; k < f->nep && k < USBMIDI_MAX_EP; k++) {
                if (f->eps[k].addr == 0) {
                    f->eps[k].addr = blob[i + 2];
                    f->eps[k].attr = blob[i + 3];
                    f->eps[k].maxpkt = blob[i + 4] | (blob[i + 5] << 8);
                    break;
                }
            }
        }
        i += blen;
    }
    return n;
}

static int ep_is_in(const UsbMidiEp *ep)
{
    return (ep->addr & 0x80) && ((ep->attr & 3) == 2 || (ep->attr & 3) == 3);
}

int usbmidi_pick(const UsbMidiIface *ifs, int n,
                 int *alt, int *ep, int *maxpkt)
{
    int best = -1, best_alt = 0, best_ep = 0, best_mp = 0, best_bulk = 0;
    for (int i = 0; i < n; i++) {
        if (ifs[i].sub != 3) continue;
        for (int k = 0; k < ifs[i].nep && k < USBMIDI_MAX_EP; k++) {
            const UsbMidiEp *e = &ifs[i].eps[k];
            if (!e->maxpkt || !ep_is_in(e)) continue;
            int bulk = ((e->attr & 3) == 2);
            int pref = (bulk ? 2 : 1) - (ifs[i].alt == 0 ? 0 : 1);
            int cur = best < 0 ? -99
                               : (best_bulk ? 2 : 1) - (best_alt == 0 ? 0 : 1);
            if (best < 0 || pref > cur) {
                best = i;
                best_alt = ifs[i].alt;
                best_ep = e->addr;
                best_mp = e->maxpkt;
                best_bulk = bulk;
            }
        }
    }
    if (best < 0) return -1;
    *alt = best_alt;
    *ep = best_ep;
    *maxpkt = best_mp;
    return best;
}

/* USB-MIDI event packet: byte 0 = (cable << 4) | code index, then up to
 * three MIDI bytes.  The payload length follows from the code index. */
static const unsigned char CIN_LEN[16] = {
    0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 3, 3, 3, 1
};

int usbmidi_expand(const unsigned char *pkt, int len,
                   unsigned char *out, int cap)
{
    int n = 0;
    for (int i = 0; i + 4 <= len; i += 4) {
        int cin = pkt[i] & 0x0F;
        int k = CIN_LEN[cin];
        if (!k) continue;
        for (int j = 0; j < k && n < cap; j++)
            out[n++] = pkt[i + 1 + j];
    }
    return n;
}

/* -------------------------------------------------------- Linux reader */

#ifdef __linux__

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/usbdevice_fs.h>

#define USBMIDI_POLL_MS 150         /* rescan cadence while detached */
#define USBMIDI_READ_MS 200         /* bulk read timeout while attached */

static void nap(int ms)
{
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static int read_text(const char *path, char *out, int cap)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int n = 0;
    int c;
    while (n < cap - 1 && (c = fgetc(f)) != EOF) {
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') break;
        out[n++] = (char)c;
    }
    out[n] = 0;
    fclose(f);
    return n;
}

/* Find a USB device with a MIDI streaming interface.  On success the device
 * node, interface number, altsetting and IN endpoint are returned. */
static int usbmidi_scan(char *node, int cap, int *ifnum, int *alt,
                        int *ep, int *maxpkt)
{
    DIR *d = opendir("/sys/bus/usb/devices");
    if (!d) return -1;
    struct dirent *de;
    int found = -1;
    char names[64][16];
    int nn = 0;
    while ((de = readdir(d)) && nn < 64) {
        if (de->d_name[0] == '.') continue;
        snprintf(names[nn], sizeof names[nn], "%s", de->d_name);
        nn++;
    }
    closedir(d);

    for (int i = 0; i < nn && found < 0; i++) {
        char path[128];
        snprintf(path, sizeof path, "/sys/bus/usb/devices/%s/idVendor",
                 names[i]);
        char vid[8];
        if (read_text(path, vid, sizeof vid) < 0) continue;   /* hubs etc. */

        snprintf(path, sizeof path, "/sys/bus/usb/devices/%s/descriptors",
                 names[i]);
        unsigned char blob[4096];
        int len = 0;
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        len = (int)fread(blob, 1, sizeof blob, f);
        fclose(f);

        UsbMidiIface ifs[USBMIDI_MAX_IF];
        int n = usbmidi_parse_config(blob, len, ifs, USBMIDI_MAX_IF);
        int ialt, iep, imp;
        int pick = usbmidi_pick(ifs, n, &ialt, &iep, &imp);
        if (pick < 0) continue;

        char bus[8], dev[8];
        snprintf(path, sizeof path, "/sys/bus/usb/devices/%s/busnum", names[i]);
        if (read_text(path, bus, sizeof bus) < 0) continue;
        snprintf(path, sizeof path, "/sys/bus/usb/devices/%s/devnum", names[i]);
        if (read_text(path, dev, sizeof dev) < 0) continue;
        snprintf(node, cap, "/dev/bus/usb/%03d/%03d", atoi(bus), atoi(dev));
        struct stat stt;
        if (stat(node, &stt) != 0) continue;

        *ifnum = ifs[pick].num;
        *alt = ialt;
        *ep = iep;
        *maxpkt = imp;
        found = 0;
    }
    return found;
}

/* Frame clock: the engine's 64-bit total_pos.  On 32-bit builds a torn read
 * could land far in the future, which would stall queued events, so a jump
 * much larger than any real progress is clamped to the previous value. */
static unsigned long long usbmidi_now(const UsbMidi *u)
{
    static unsigned long long last;
    unsigned long long now = *u->now;
    if (last && now > last && now - last > (1ull << 24)) now = last;
    last = now;
    return now;
}

static void *usbmidi_thread(void *arg)
{
    UsbMidi *u = arg;

    while (!u->stop) {
        char node[48];
        int ifnum, alt, ep, maxpkt;
        if (usbmidi_scan(node, sizeof node, &ifnum, &alt, &ep, &maxpkt) != 0) {
            u->attached = 0;
            u->node[0] = 0;
            nap(USBMIDI_POLL_MS);
            continue;
        }

        int fd = open(node, O_RDWR);
        if (fd < 0) {
            nap(USBMIDI_POLL_MS);
            continue;
        }
        if (ioctl(fd, USBDEVFS_CLAIMINTERFACE, &ifnum) < 0) {
            close(fd);                  /* something else owns it: try later */
            nap(USBMIDI_POLL_MS);
            continue;
        }
        if (alt != 0) {
            struct usbdevfs_setinterface si = { (unsigned)ifnum,
                                                (unsigned)alt };
            ioctl(fd, USBDEVFS_SETINTERFACE, &si);
        }

        snprintf(u->node, sizeof u->node, "%s", node);
        u->attached = 1;

        unsigned char pkt[512], bytes[384];
        int rlen = maxpkt > (int)sizeof pkt ? (int)sizeof pkt : maxpkt;
        while (!u->stop) {
            struct usbdevfs_bulktransfer req;
            memset(&req, 0, sizeof req);
            req.ep = (unsigned)ep;
            req.len = (unsigned)rlen;
            req.timeout = USBMIDI_READ_MS;
            req.data = pkt;
            int got = ioctl(fd, USBDEVFS_BULK, &req);
            if (got > 0) {
                int n = usbmidi_expand(pkt, got, bytes, (int)sizeof bytes);
                if (n > 0 && u->midi)
                    midi_feed(u->midi, bytes, n, usbmidi_now(u));
            } else if (got < 0 && errno != ETIMEDOUT && errno != EAGAIN &&
                       errno != EINTR) {
                break;                  /* device gone: rescan */
            }
        }

        u->attached = 0;
        ioctl(fd, USBDEVFS_RELEASEINTERFACE, &ifnum);
        close(fd);
        u->node[0] = 0;
        if (!u->stop) nap(USBMIDI_POLL_MS);
    }
    return NULL;
}

int usbmidi_start(UsbMidi *u, MidiClock *midi, const unsigned long long *now)
{
    memset(u, 0, sizeof *u);
    u->midi = midi;
    u->now = now;
    u->node[0] = 0;
    if (pthread_create((pthread_t *)&u->thread, NULL, usbmidi_thread, u) != 0)
        return -1;
    u->started = 1;
    return 0;
}

void usbmidi_stop(UsbMidi *u)
{
    if (!u->started) return;
    u->stop = 1;
    pthread_join(*(pthread_t *)u->thread, NULL);
    u->thread = NULL;
    u->started = 0;
    u->attached = 0;
}

int usbmidi_attached(const UsbMidi *u)
{
    return u->attached;
}

#else /* !__linux__ */

int usbmidi_start(UsbMidi *u, MidiClock *midi, const unsigned long long *now)
{
    (void)midi; (void)now;
    memset(u, 0, sizeof *u);
    return 0;                               /* no usbfs here: stays detached */
}

void usbmidi_stop(UsbMidi *u)
{
    (void)u;
}

int usbmidi_attached(const UsbMidi *u)
{
    (void)u;
    return 0;
}

#endif
