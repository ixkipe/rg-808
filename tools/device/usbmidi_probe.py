#!/usr/bin/env python3
"""usbmidi_probe.py - read USB-MIDI from a class-compliant or vendor interface.

The device kernel has no snd-usb-audio, so a MIDI interface never gets an ALSA
card or /dev/snd/midi*.  That does not matter: with no kernel driver bound,
usbfs hands the interface to userspace.  This script does what the app will do
in C - find the MIDI streaming interface from sysfs, claim it, read its IN
endpoint and parse USB-MIDI packets.

It accepts both personalities a device may offer:
  class 0x01 sub 0x03   standard USB MIDI streaming (generic mode)
  other class, sub 0x03 Roland-style vendor MIDI streaming (vendor mode)

Runs on the stock muOS python3.  No libusb, no compiler, nothing to install.

  python3 usbmidi_probe.py --list          # devices and interfaces only
  python3 usbmidi_probe.py                 # listen on the best MIDI interface
  python3 usbmidi_probe.py --try-all       # try every interface/alt in turn
  python3 usbmidi_probe.py 0582:029d       # pick a device by VID:PID
  python3 usbmidi_probe.py --seconds 20    # listen for 20 s (default 30)
  python3 usbmidi_probe.py --raw           # dump every packet with a timestamp
  python3 usbmidi_probe.py --timeout 2000  # keep each read armed for 2 s
  python3 usbmidi_probe.py --delay 2       # wait 2 s after claiming, then read

While it listens, start the master's sequencer and also move a knob or press a
pad: clock proves tempo sync, controller/note traffic proves the pipe is alive
even when no clock is being sent.
"""

import ctypes
import errno
import fcntl
import os
import sys
import time

SYSFS = "/sys/bus/usb/devices"
DEVFS = "/dev/bus/usb"

# linux/usbdevice_fs.h
USBDEVFS_SETINTERFACE = 0x80085504
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

KINDS = {0: "control", 1: "isochronous", 2: "bulk", 3: "interrupt"}

# USB-MIDI event packet: byte 0 is (cable << 4) | code index, then up to three
# MIDI bytes.  The packet length follows from the code index.
CIN_LEN = {0x2: 2, 0x3: 3, 0x4: 3, 0x5: 1, 0x6: 2, 0x7: 3,
           0x8: 3, 0x9: 3, 0xA: 3, 0xB: 3, 0xC: 3, 0xD: 3, 0xE: 3, 0xF: 1}

SYSTEM = {0xF2: "SONG POS", 0xF6: "TUNE REQ", 0xF8: "CLOCK", 0xFA: "START",
          0xFB: "CONTINUE", 0xFC: "STOP", 0xFE: "SENSE"}


def read_text(path, default=""):
    try:
        with open(path, "r") as f:
            return f.read().strip()
    except OSError:
        return default


def devices():
    out = []
    try:
        names = sorted(os.listdir(SYSFS))
    except OSError:
        return out
    for name in names:
        path = os.path.join(SYSFS, name)
        vid = read_text(os.path.join(path, "idVendor"))
        if not vid:
            continue                      # root hub or interface node
        out.append({
            "path": path,
            "name": name,
            "vid": vid,
            "pid": read_text(os.path.join(path, "idProduct")),
            "product": read_text(os.path.join(path, "product")),
            "busnum": read_text(os.path.join(path, "busnum")),
            "devnum": read_text(os.path.join(path, "devnum")),
            "speed": read_text(os.path.join(path, "speed")),
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
    print("%s  %s:%s  %s  (%s)" % (dev["path"], dev["vid"], dev["pid"],
                                   dev["product"] or "(no product)",
                                   dev["speed"] or "?"))
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
        tag = "  <- MIDI" if it["sub"] == 3 else ""
        print("   if %d alt %d  class=%02x sub=%02x proto=%02x  "
              "endpoints=%d  driver=%s%s" %
              (it["num"], it["alt"], it["cls"], it["sub"], it["proto"],
               len(it["eps"]), drv, tag))
        for ep in it["eps"]:
            direction = "IN" if ep["addr"] & 0x80 else "OUT"
            print("      ep 0x%02x %-3s %-11s maxpkt %d" %
                  (ep["addr"], direction, KINDS.get(ep["attr"] & 3, "?"),
                   ep["maxpkt"]))


def midi_candidates(ifaces):
    """(interface, IN endpoint) pairs for MIDI streaming, best first.

    Any interface with subclass 3 counts, whatever its class: generic devices
    use class 1, Roland's vendor mode hides the same layout behind class ff.
    """
    out = []
    for it in ifaces:
        if it["sub"] != 3:
            continue
        for ep in it["eps"]:
            if (ep["addr"] & 0x80) and (ep["attr"] & 3) in (2, 3):
                out.append((it, ep))
    # Prefer bulk over interrupt, and alt 0 over other settings.
    out.sort(key=lambda p: (0 if (p[1]["attr"] & 3) == 2 else 1,
                            0 if p[0]["alt"] == 0 else 1))
    return out


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


def packets_to_bytes(pkt):
    out = bytearray()
    for i in range(0, len(pkt) - 3, 4):
        cin = pkt[i] & 0x0F
        n = CIN_LEN.get(cin, 0)
        if n:
            out += pkt[i + 1:i + 1 + n]
    return bytes(out)


def audio_in_alts(ifaces):
    """(interface, alt) pairs that carry a device -> host audio stream."""
    out = []
    for it in ifaces:
        if it["sub"] != 2:
            continue
        for ep in it["eps"]:
            if (ep["addr"] & 0x80) and (ep["attr"] & 3) == 1:
                out.append((it["num"], it["alt"]))
    return out


def listen(node, it, ep, seconds, raw, timeout_ms=200, delay_s=0.0,
           audio_alts=()):
    """Claim the interface and read for `seconds`.  Returns a stats dict."""
    st = {"packets": 0, "bytes": 0, "clocks": 0, "events": 0, "bpm": 0.0,
          "error": None}

    if (ep["attr"] & 3) != 2:
        st["error"] = ("%s endpoint: USBDEVFS_BULK only does bulk transfers; "
                       "interrupt needs URB submission (not implemented)"
                       % KINDS.get(ep["attr"] & 3, "?"))
        return st

    try:
        fd = os.open(node, os.O_RDWR)
    except OSError as e:
        st["error"] = "cannot open %s: %s" % (node, e)
        return st

    claimed = []
    claim = bytearray(iface_arg(it["num"]))
    try:
        fcntl.ioctl(fd, USBDEVFS_CLAIMINTERFACE, claim, True)
        claimed.append(it["num"])
    except OSError as e:
        st["error"] = "claim failed: %s" % e
        os.close(fd)
        return st

    try:
        if it["alt"] != 0:
            alt = bytearray(iface_arg(it["num"], it["alt"]))
            try:
                fcntl.ioctl(fd, USBDEVFS_SETINTERFACE, alt, True)
            except OSError as e:
                st["error"] = "alt %d refused: %s" % (it["alt"], e)

        # Some devices only run their engine while the audio stream is open.
        # Selecting the streaming alternate setting is enough for the device to
        # start; nobody has to read the audio itself.
        for num, alt in audio_alts:
            try:
                if num not in claimed:
                    fcntl.ioctl(fd, USBDEVFS_CLAIMINTERFACE,
                                bytearray(iface_arg(num)), True)
                    claimed.append(num)
                fcntl.ioctl(fd, USBDEVFS_SETINTERFACE,
                            bytearray(iface_arg(num, alt)), True)
                print("    audio stream armed: interface %d alt %d" % (num, alt))
            except OSError as e:
                print("    audio arm failed on if %d alt %d: %s"
                      % (num, alt, e))

        if delay_s > 0:
            time.sleep(delay_s)

        length = min(max(ep["maxpkt"] or 64, 64), 512)
        started = time.time()
        next_report = started + 1.0
        last_clock = None
        intervals = []
        shown = 0
        try:
            while time.time() - started < seconds:
                pkt = bulk_read(fd, ep["addr"], length, timeout_ms)
                if not pkt:
                    if time.time() >= next_report:
                        print("    ... nothing (packets %d, bytes %d, "
                              "clocks %d)" % (st["packets"], st["bytes"],
                                              st["clocks"]))
                        next_report += 1.0
                    continue

                st["packets"] += 1
                st["bytes"] += len(pkt)
                now = time.time()
                if raw or shown < 8:
                    print("    %7.3f  %s" % (now - started, pkt.hex()))
                    shown += 1

                for b in packets_to_bytes(pkt):
                    if b == 0xF8:
                        st["clocks"] += 1
                        now = time.time()
                        if last_clock is not None:
                            dt = now - last_clock
                            if 0.002 < dt < 0.5:
                                intervals.append(dt)
                                if len(intervals) > 48:
                                    intervals.pop(0)
                                avg = sum(intervals) / len(intervals)
                                st["bpm"] = 60.0 / (24.0 * avg)
                        last_clock = now
                    elif b in SYSTEM:
                        st["events"] += 1
                        print("    %7.3f  %s" % (now - started, SYSTEM[b]))

                if time.time() >= next_report:
                    print("    clocks %4d   bpm %6.2f   packets %3d   bytes %5d"
                          % (st["clocks"], st["bpm"], st["packets"],
                             st["bytes"]))
                    next_report += 1.0
        except KeyboardInterrupt:
            print("    interrupted")
    finally:
        try:
            for num in claimed:
                fcntl.ioctl(fd, USBDEVFS_RELEASEINTERFACE,
                            bytearray(iface_arg(num)), True)
        except OSError:
            pass
        os.close(fd)
    return st


def iface_arg(num, alt=None):
    if alt is None:
        return bytes([num & 0xFF, (num >> 8) & 0xFF, 0, 0])
    return bytes([num & 0xFF, (num >> 8) & 0xFF,
                  alt & 0xFF, (alt >> 8) & 0xFF])


def verdict(st):
    if st["error"]:
        print("  -> %s" % st["error"])
        return 1
    if st["packets"] == 0:
        print("  -> NOTHING RECEIVED: pipe silent")
        return 1
    if st["clocks"] < 8:
        print("  -> DATA but almost no clock (%d): pipe works, clock is not "
              "being sent" % st["clocks"])
        return 1
    print("  -> MIDI clock flowing at %.2f BPM" % st["bpm"])
    return 0


def main(argv):
    want = None
    seconds = 30.0
    listing = False
    raw = False
    try_all = False
    timeout_ms = 200
    delay_s = 0.0
    start_audio = False
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == "--list":
            listing = True
        elif a == "--raw":
            raw = True
        elif a == "--try-all":
            try_all = True
        elif a == "--start-audio":
            start_audio = True
        elif a == "--seconds" and i + 1 < len(argv):
            i += 1
            seconds = float(argv[i])
        elif a == "--timeout" and i + 1 < len(argv):
            i += 1
            timeout_ms = int(argv[i])
        elif a == "--delay" and i + 1 < len(argv):
            i += 1
            delay_s = float(argv[i])
        elif ":" in a:
            want = a.lower()
        else:
            print("usage: %s [VID:PID] [--list] [--try-all] [--raw] "
                  "[--seconds N] [--timeout MS] [--delay S] [--start-audio]"
                  % argv[0])
            return 2
        i += 1

    found = devices()
    if not found:
        print("no USB devices found in %s" % SYSFS)
        return 1

    print("=== USB devices ===")
    for dev in found:
        describe(dev, interfaces(dev))

    if listing:
        return 0

    audio = []
    candidates = []
    for dev in found:
        if want and ("%s:%s" % (dev["vid"], dev["pid"])).lower() != want:
            continue
        ifaces = interfaces(dev)
        if start_audio:
            for a_num, a_alt in audio_in_alts(ifaces):
                audio.append((a_num, a_alt))
        for it, ep in midi_candidates(ifaces):
            candidates.append((dev, it, ep))
    if not candidates:
        print("\nno USB MIDI interface found%s" %
              ("" if not want else " on " + want))
        return 1

    if try_all:
        win = max(3.0, seconds / len(candidates))
        best = 0
        for dev, it, ep in candidates:
            node = "%s/%03d/%03d" % (DEVFS, int(dev["busnum"]),
                                     int(dev["devnum"]))
            print("\n=== try %s if %d alt %d ep 0x%02x (%s) for %.0fs ===" %
                  (node, it["num"], it["alt"], ep["addr"],
                   KINDS.get(ep["attr"] & 3, "?"), win))
            st = listen(node, it, ep, win, raw, timeout_ms, delay_s, audio)
            if verdict(st) == 0:
                return 0
            if st["packets"]:
                best += 1
        print("\nno candidate delivered a clock")
        return 1

    dev, it, ep = candidates[0]
    node = "%s/%03d/%03d" % (DEVFS, int(dev["busnum"]), int(dev["devnum"]))
    print("\n=== MIDI in ===")
    print("%s  %s:%s  %s  (%s)" % (dev["product"] or "device", dev["vid"],
                                   dev["pid"], node, dev["speed"] or "?"))
    print("interface %d alt %d, %s IN endpoint 0x%02x, maxpkt %d" %
          (it["num"], it["alt"], KINDS.get(ep["attr"] & 3, "?"),
           ep["addr"], ep["maxpkt"]))
    print("interface claimed - start the master and move a knob or pad")

    st = listen(node, it, ep, seconds, raw, timeout_ms, delay_s, audio)
    print("\n=== result ===")
    print("packets %d   bytes %d   clocks %d   transports %d   bpm %.2f" %
          (st["packets"], st["bytes"], st["clocks"], st["events"], st["bpm"]))
    return verdict(st)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
