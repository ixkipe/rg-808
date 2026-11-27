#!/usr/bin/env python3
"""usbmidi_probe.py - prove USB-MIDI reception on the Anbernic.

The device kernel has no snd-usb-audio, so a class-compliant interface never
gets an ALSA card or /dev/snd/midi*. That does not matter: with no kernel
driver bound, usbfs hands the interface to userspace.  This script does what
the app will do in C - find the MIDI streaming interface from sysfs, claim it,
read the bulk IN endpoint and parse USB-MIDI packets - so the chain is proven
before any app code is written.

Runs on the stock muOS python3.  No libusb, no compiler, nothing to install.

  python3 usbmidi_probe.py --list        # devices and interfaces only
  python3 usbmidi_probe.py               # listen on the first MIDI interface
  python3 usbmidi_probe.py 0582:029d     # pick a device by VID:PID
  python3 usbmidi_probe.py --seconds 20  # listen for 20 s (default 30)

Press PLAY on the master device: a running sequencer sends 24 clocks per
quarter note (0xF8), plus 0xFA on start and 0xFC on stop.
"""

import ctypes
import errno
import fcntl
import os
import struct
import sys
import time

SYSFS = "/sys/bus/usb/devices"
DEVFS = "/dev/bus/usb"

# linux/usbdevice_fs.h
USBDEVFS_CLAIMINTERFACE = 0x8004550F
USBDEVFS_RELEASEINTERFACE = 0x80045510


class _usbdevfs_bulktransfer(ctypes.Structure):
    _fields_ = [("ep", ctypes.c_uint),
                ("len", ctypes.c_uint),
                ("timeout", ctypes.c_uint),
                ("data", ctypes.c_void_p)]


# _IOWR('U', 2, struct usbdevfs_bulktransfer), sized for this ABI.
USBDEVFS_BULK = ((3 << 30) | (ctypes.sizeof(_usbdevfs_bulktransfer) << 16) |
                 (ord("U") << 8) | 2)


def read_text(path, default=""):
    try:
        with open(path, "r") as f:
            return f.read().strip()
    except OSError:
        return default


def devices():
    """Every attached USB device with its sysfs path and bus/dev numbers."""
    out = []
    try:
        names = sorted(os.listdir(SYSFS))
    except OSError:
        return out
    for name in names:
        path = os.path.join(SYSFS, name)
        vid = read_text(os.path.join(path, "idVendor"))
        if not vid:
            continue                      # a root hub or an interface node
        out.append({
            "path": path,
            "name": name,
            "vid": vid,
            "pid": read_text(os.path.join(path, "idProduct")),
            "product": read_text(os.path.join(path, "product")),
            "busnum": read_text(os.path.join(path, "busnum")),
            "devnum": read_text(os.path.join(path, "devnum")),
        })
    return out


def parse_config(blob):
    """Interfaces and their endpoints from a raw configuration descriptor."""
    ifaces = []
    cur = None
    i = 0
    while i + 2 <= len(blob):
        blen = blob[i]
        btype = blob[i + 1]
        if blen < 2 or i + blen > len(blob):
            break
        if btype == 4 and blen >= 9:              # INTERFACE
            cur = {
                "num": blob[i + 2],
                "alt": blob[i + 3],
                "nep": blob[i + 4],
                "cls": blob[i + 5],
                "sub": blob[i + 6],
                "proto": blob[i + 7],
                "eps": [],
            }
            ifaces.append(cur)
        elif btype == 5 and blen >= 7 and cur is not None:   # ENDPOINT
            cur["eps"].append({
                "addr": blob[i + 2],
                "attr": blob[i + 3],
                "maxpkt": blob[i + 4] | (blob[i + 5] << 8),
            })
        i += blen
    return ifaces


def interfaces(dev):
    path = os.path.join(dev["path"], "descriptors")
    try:
        with open(path, "rb") as f:
            blob = f.read()
    except OSError as e:
        print("cannot read %s: %s" % (path, e))
        return []
    return parse_config(blob)


def describe(dev, ifaces):
    print("%s  %s:%s  %s" % (dev["path"], dev["vid"], dev["pid"],
                             dev["product"] or "(no product)"))
    print("   device node: %s/%03d/%03d" % (DEVFS, int(dev["busnum"]),
                                            int(dev["devnum"])))
    seen = set()
    for it in ifaces:
        if (it["num"], it["alt"]) in seen:
            continue
        seen.add((it["num"], it["alt"]))
        link = os.path.join(dev["path"], "%s:1.%d" % (dev["name"], it["num"]))
        try:
            drv = os.path.basename(os.readlink(os.path.join(link, "driver")))
        except OSError:
            drv = "none"
        print("   if %d alt %d  class=%02x sub=%02x proto=%02x  "
              "endpoints=%d  driver=%s" %
              (it["num"], it["alt"], it["cls"], it["sub"], it["proto"],
               len(it["eps"]), drv))
        for ep in it["eps"]:
            direction = "IN" if ep["addr"] & 0x80 else "OUT"
            kind = {0: "control", 1: "isochronous", 2: "bulk", 3: "interrupt"}
            print("      ep 0x%02x %-3s %-11s maxpkt %d" %
                  (ep["addr"], direction, kind.get(ep["attr"] & 3, "?"),
                   ep["maxpkt"]))


def midi_interface(ifaces):
    """(interface, bulk IN endpoint) of the MIDI streaming function."""
    best = None
    for it in ifaces:
        if it["cls"] == 1 and it["sub"] == 3:          # audio / MIDI streaming
            for ep in it["eps"]:
                if (ep["addr"] & 0x80) and (ep["attr"] & 3) == 2:
                    best = (it, ep)
                    break
        if best:
            break
    return best


def bulk_read(fd, ep, length, timeout_ms):
    buf = ctypes.create_string_buffer(length)
    t = _usbdevfs_bulktransfer(ep, length, timeout_ms,
                               ctypes.cast(buf, ctypes.c_void_p))
    # Pass the struct's own bytes so the layout always matches this ABI.
    req = bytearray(ctypes.string_at(ctypes.addressof(t), ctypes.sizeof(t)))
    try:
        got = fcntl.ioctl(fd, USBDEVFS_BULK, req, True)
    except OSError as e:
        if e.errno in (errno.ETIMEDOUT, errno.EAGAIN, errno.EINTR):
            return b""
        raise
    if got <= 0:
        return b""
    return ctypes.string_at(ctypes.addressof(buf), got)


# USB-MIDI event packet: byte 0 is (cable << 4) | code index, then up to three
# MIDI bytes.  The packet length follows from the code index.
CIN_LEN = {0x2: 2, 0x3: 3, 0x4: 3, 0x5: 1, 0x6: 2, 0x7: 3,
           0x8: 3, 0x9: 3, 0xA: 3, 0xB: 3, 0xC: 3, 0xD: 3, 0xE: 3, 0xF: 1}


def packets_to_bytes(pkt):
    out = bytearray()
    for i in range(0, len(pkt) - 3, 4):
        cin = pkt[i] & 0x0F
        n = CIN_LEN.get(cin, 0)
        if n:
            out += pkt[i + 1:i + 1 + n]
    return bytes(out)


def main(argv):
    want = None
    seconds = 30.0
    listing = False
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == "--list":
            listing = True
        elif a == "--seconds" and i + 1 < len(argv):
            i += 1
            seconds = float(argv[i])
        elif ":" in a:
            want = a.lower()
        else:
            print("usage: %s [VID:PID] [--list] [--seconds N]" % argv[0])
            return 2
        i += 1

    found = devices()
    if not found:
        print("no USB devices found in %s" % SYSFS)
        return 1

    print("=== USB devices ===")
    for dev in found:
        ifaces = interfaces(dev)
        describe(dev, ifaces)

    if listing:
        return 0

    candidates = []
    for dev in found:
        if want and ("%s:%s" % (dev["vid"], dev["pid"])).lower() != want:
            continue
        ifaces = interfaces(dev)
        hit = midi_interface(ifaces)
        if hit:
            candidates.append((dev, hit))
    if not candidates:
        print("\nno MIDI streaming interface found%s" %
              ("" if not want else " on " + want))
        return 1

    dev, (it, ep) = candidates[0]
    node = "%s/%03d/%03d" % (DEVFS, int(dev["busnum"]), int(dev["devnum"]))
    print("\n=== MIDI in ===")
    print("%s %s:%s  %s" % (dev["product"] or "device", dev["vid"], dev["pid"],
                            node))
    print("interface %d (alt %d), bulk IN endpoint 0x%02x, maxpkt %d" %
          (it["num"], it["alt"], ep["addr"], ep["maxpkt"]))

    try:
        fd = os.open(node, os.O_RDWR)
    except OSError as e:
        print("cannot open %s: %s (are you root?)" % (node, e))
        return 1

    iface = bytearray(struct.pack("I", it["num"]))
    try:
        fcntl.ioctl(fd, USBDEVFS_CLAIMINTERFACE, iface, True)
        print("interface claimed - press PLAY on the master")
    except OSError as e:
        print("claim failed: %s (errno %d)" % (e, e.errno))
        os.close(fd)
        return 1

    length = min(max(ep["maxpkt"] or 64, 64), 512)
    clocks = 0
    last_clock = None
    intervals = []
    events = 0
    raw = 0
    started = time.time()
    next_report = started + 1.0
    bpm = 0.0

    try:
        while time.time() - started < seconds:
            pkt = bulk_read(fd, ep["addr"], length, 200)
            if not pkt:
                if time.time() >= next_report:
                    print("  ... no data (clocks %d, bpm %.2f)" % (clocks, bpm))
                    next_report += 1.0
                continue

            if raw < 8:
                print("  packet: %s" % pkt.hex())
                raw += 1

            for b in packets_to_bytes(pkt):
                now = time.time()
                if b == 0xF8:
                    clocks += 1
                    if last_clock is not None:
                        dt = now - last_clock
                        if 0.002 < dt < 0.5:
                            intervals.append(dt)
                            if len(intervals) > 48:
                                intervals.pop(0)
                            avg = sum(intervals) / len(intervals)
                            bpm = 60.0 / (24.0 * avg)
                    last_clock = now
                elif b in (0xFA, 0xFB, 0xFC):
                    events += 1
                    name = {0xFA: "START", 0xFB: "CONTINUE", 0xFC: "STOP"}[b]
                    print("  %s" % name)

            if time.time() >= next_report:
                print("  clocks %4d   bpm %6.2f   transports %d" %
                      (clocks, bpm, events))
                next_report += 1.0
    except KeyboardInterrupt:
        print("\ninterrupted")
    finally:
        try:
            fcntl.ioctl(fd, USBDEVFS_RELEASEINTERFACE, iface, True)
        except OSError:
            pass
        os.close(fd)

    print("\n=== result ===")
    print("clocks received: %d   transports: %d   estimated bpm: %.2f" %
          (clocks, events, bpm))
    if clocks == 0:
        print("no clock: is the clock master playing, and does it send MIDI "
              "clock on USB?")
        return 1
    print("USB MIDI works on this device - the app reader can use the same "
          "path")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
