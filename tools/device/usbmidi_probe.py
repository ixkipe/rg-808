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
  python3 usbmidi_probe.py --start-audio   # arm the audio altsettings, too
  python3 usbmidi_probe.py --iso           # real iso audio traffic, not just armed
  python3 usbmidi_probe.py --poke          # write one active-sensing event to the OUT pipe
  python3 usbmidi_probe.py --wait          # start first, then plug the device in

While it listens, start the master's sequencer and also move a knob or press a
pad: clock proves tempo sync, controller/note traffic proves the pipe is alive
even when no clock is being sent.

Confirmed on the T-8 (2026-11-27, logs/usbmidi_probe_5.txt): the reader has to
be attached when the master enumerates.  A master whose MIDI IN pipe nobody
drains fills its small endpoint buffer and gives up; any later reader only
finds a couple of stale events - the classic "two clocks then silence".
--wait is therefore the default way to probe a new master: arm the reader
first, plug in, press play at once.  MIDI needs nothing else - no audio
altsettings, no iso traffic (--start-audio/--iso stay as diagnostics).
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
USBDEVFS_SETINTERFACE = 0x80085504
USBDEVFS_CLAIMINTERFACE = 0x8004550F
USBDEVFS_RELEASEINTERFACE = 0x80045510

# usbdevfs_urb + iso streaming.  The SUBMITURB size field is the urb struct
# (56 bytes on 64-bit), not an int - encoding it as _IOR('U', 10, unsigned int)
# yields 0x8004550A, which the kernel does not know and answers with ENOTTY
# (that was the --iso failure in usbmidi_probe_5.txt).
USBDEVFS_SUBMITURB = 0x8038550A          # _IOR('U', 10, struct usbdevfs_urb)
USBDEVFS_DISCARDURB = 0x0000550B         # _IO('U', 11)
USBDEVFS_REAPURBNDELAY = 0x4008550D      # _IOW('U', 13, void *)
USBDEVFS_URB_ISO_ASAP = 2


class _usbdevfs_urb(ctypes.Structure):
    _fields_ = [
        ("type", ctypes.c_ubyte),
        ("endpoint", ctypes.c_ubyte),
        ("status", ctypes.c_int),
        ("flags", ctypes.c_uint),
        ("buffer", ctypes.c_void_p),
        ("buffer_length", ctypes.c_int),
        ("actual_length", ctypes.c_int),
        ("start_frame", ctypes.c_int),
        ("number_of_packets", ctypes.c_int),   # union with stream_id
        ("error_count", ctypes.c_int),
        ("signr", ctypes.c_uint),
        ("usercontext", ctypes.c_void_p),
    ]


class _iso_desc(ctypes.Structure):
    _fields_ = [
        ("effective_length", ctypes.c_uint),
        ("actual_length", ctypes.c_uint),
        ("status", ctypes.c_int),
    ]


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


def audio_stream_alts(ifaces):
    """(interface, alt) pairs for every audio streaming setting.

    Both directions are armed, because that is what a host that opened the
    device looks like: the playback stream towards the device and the capture
    stream back.
    """
    out = []
    for it in ifaces:
        if it["sub"] != 2 or it["alt"] == 0:
            continue
        if any((ep["attr"] & 3) == 1 for ep in it["eps"]):
            out.append((it["num"], it["alt"]))
    return out


def bulk_write(fd, ep, data, timeout_ms):
    raw = bytes(data)
    buf = ctypes.create_string_buffer(raw, len(raw))
    t = _usbdevfs_bulktransfer(ep, len(raw), timeout_ms,
                               ctypes.cast(buf, ctypes.c_void_p))
    req = bytearray(ctypes.string_at(ctypes.addressof(t), ctypes.sizeof(t)))
    try:
        return fcntl.ioctl(fd, USBDEVFS_BULK, req, True)
    except OSError as e:
        if e.errno in (errno.ETIMEDOUT, errno.EAGAIN, errno.EINTR):
            return 0
        raise


class IsoPump(object):
    """Keep a ring of iso URBs in flight on one endpoint.

    Selecting the streaming altsetting allocates bus bandwidth but moves no
    data.  A device that gates its engine on *actually streaming* audio only
    starts when its endpoints are serviced every frame.  This pushes real
    iso traffic: silence towards the device, drained IN packets back.  All
    payloads are discarded; only the bus activity matters.
    """

    def __init__(self, fd, ep, packets=64, n_urbs=8):
        self.fd = fd
        self.ep = ep["addr"]
        self.maxpkt = max(8, ep["maxpkt"] or 64)
        self.packets = packets
        self.n_urbs = n_urbs
        self.blocks = []
        self.inflight = []

    def submit_all(self):
        if ctypes.sizeof(_usbdevfs_urb) != 56:
            raise OSError("unexpected usbdevfs_urb size (%d)"
                          % ctypes.sizeof(_usbdevfs_urb))
        np = self.packets
        n = self.maxpkt * np

        class Block(ctypes.Structure):
            _fields_ = [("urb", _usbdevfs_urb), ("descs", _iso_desc * np)]

        for _ in range(self.n_urbs):
            b = Block()
            data = ctypes.create_string_buffer(n)
            b.urb.type = 0                       # USBDEVFS_URB_TYPE_ISO
            b.urb.endpoint = self.ep
            b.urb.flags = USBDEVFS_URB_ISO_ASAP
            b.urb.buffer = ctypes.cast(data, ctypes.c_void_p)
            b.urb.buffer_length = n
            b.urb.number_of_packets = np
            for i in range(np):
                b.descs[i].effective_length = self.maxpkt
            addr = ctypes.addressof(b)
            b.urb.usercontext = ctypes.c_void_p(addr)
            fcntl.ioctl(self.fd, USBDEVFS_SUBMITURB, addr)
            self.blocks.append((b, data, addr))
            self.inflight.append(addr)

    def pump(self):
        """Reap completed URBs and resubmit them (call between reads)."""
        slot = ctypes.create_string_buffer(8)
        while True:
            try:
                fcntl.ioctl(self.fd, USBDEVFS_REAPURBNDELAY, slot, True)
            except OSError as e:
                if e.errno in (errno.EAGAIN, errno.EINTR):
                    break
                return                          # device gone or ep broken
            addr = ctypes.cast(slot, ctypes.POINTER(ctypes.c_void_p)).contents.value
            if not addr:
                break
            if addr in self.inflight:
                self.inflight.remove(addr)
                self.inflight.append(addr)
            try:
                fcntl.ioctl(self.fd, USBDEVFS_SUBMITURB, addr)
            except OSError:
                return

    def stop(self):
        for addr in list(self.inflight):
            try:
                fcntl.ioctl(self.fd, USBDEVFS_DISCARDURB, addr)
            except OSError:
                pass
        # reap until quiet so the kernel frees everything it can
        slot = ctypes.create_string_buffer(8)
        for _ in range(4 * self.n_urbs):
            try:
                fcntl.ioctl(self.fd, USBDEVFS_REAPURBNDELAY, slot, True)
            except OSError:
                break


def iso_eps_for(ifaces, audio_alts):
    """iso endpoints of the armed audio altsettings: (interface, ep, is_out)."""
    out = []
    for num, alt in audio_alts:
        for it in ifaces:
            if it["num"] != num or it["alt"] != alt:
                continue
            for ep in it["eps"]:
                if (ep["attr"] & 3) == 1:
                    out.append((it, ep, (ep["addr"] & 0x80) == 0))
    return out


def listen(node, it, ep, seconds, raw, timeout_ms=200, delay_s=0.0,
           audio_alts=(), claim_ifaces=(), iso_eps=(), poke=False):
    """Claim the interface and read for `seconds`.  Returns a stats dict."""
    st = {"packets": 0, "bytes": 0, "clocks": 0, "events": 0, "bpm": 0.0,
          "age": None, "error": None}

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

    try:
        st["age"] = time.time() - os.stat(node).st_ctime
    except OSError:
        pass

    claimed = []
    claim = bytearray(iface_arg(it["num"]))
    try:
        fcntl.ioctl(fd, USBDEVFS_CLAIMINTERFACE, claim, True)
        claimed.append(it["num"])
    except OSError as e:
        st["error"] = "claim failed: %s" % e
        os.close(fd)
        return st

    pumps = []

    try:
        if it["alt"] != 0:
            alt = bytearray(setiface_arg(it["num"], it["alt"]))
            try:
                fcntl.ioctl(fd, USBDEVFS_SETINTERFACE, alt, True)
            except OSError as e:
                st["error"] = "alt %d refused: %s" % (it["alt"], e)

        # Some devices only run their engine while audio is streaming.  The
        # 2026-11-27 T-8 logs disprove the weak form: selecting the streaming
        # altsetting alone is NOT enough.  The strong form (real iso traffic
        # on the bus) is what --iso below tests.
        for num, alt in audio_alts:
            try:
                if num not in claimed:
                    fcntl.ioctl(fd, USBDEVFS_CLAIMINTERFACE,
                                bytearray(iface_arg(num)), True)
                    claimed.append(num)
                fcntl.ioctl(fd, USBDEVFS_SETINTERFACE,
                            bytearray(setiface_arg(num, alt)), True)
                print("    audio stream armed: interface %d alt %d" % (num, alt))
            except OSError as e:
                print("    audio arm failed on if %d alt %d: %s"
                      % (num, alt, e))

        # Claim everything else the device has, the way a driver would.
        for num in claim_ifaces:
            if num in claimed:
                continue
            try:
                fcntl.ioctl(fd, USBDEVFS_CLAIMINTERFACE,
                            bytearray(iface_arg(num)), True)
                claimed.append(num)
                print("    interface %d claimed" % num)
            except OSError as e:
                print("    interface %d claim failed: %s" % (num, e))

        # Some endpoints stay idle until the host has written to the paired
        # OUT pipe.  Active Sensing (0xFE) is ignored by every device - but it
        # is an extra variable on the wire, so it only runs with --poke.
        if poke and (audio_alts or claim_ifaces):
            for oep in it["eps"]:
                if (oep["addr"] & 0x80) or (oep["attr"] & 3) != 2:
                    continue
                try:
                    n = bulk_write(fd, oep["addr"], bytes([0x0F, 0xFE, 0x00, 0x00]),
                                   timeout_ms)
                    print("    OUT pipe 0x%02x opened (%d bytes)"
                          % (oep["addr"], n))
                except OSError as e:
                    print("    OUT pipe 0x%02x write failed: %s"
                          % (oep["addr"], e))
                break

        # An armed altsetting allocates bandwidth but moves no data.  A
        # device that only runs its engine while audio really streams starts
        # once its iso endpoints are serviced every frame.
        for ait, aep, out_dir in iso_eps:
            try:
                p = IsoPump(fd, aep)
                p.submit_all()
                pumps.append(p)
                print("    iso stream running: ep 0x%02x %-3s %d urb(s) x "
                      "%d packet(s) of %d B"
                      % (aep["addr"], "OUT" if out_dir else "IN",
                         p.n_urbs, p.packets, p.maxpkt))
            except OSError as e:
                print("    iso stream failed on ep 0x%02x: %s"
                      % (aep["addr"], e))

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
                for p in pumps:
                    p.pump()
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
        for p in pumps:
            try:
                p.stop()
            except Exception:
                pass
        try:
            for num in claimed:
                fcntl.ioctl(fd, USBDEVFS_RELEASEINTERFACE,
                            bytearray(iface_arg(num)), True)
        except OSError:
            pass
        os.close(fd)
    return st


def iface_arg(num):
    """Claim/release take a single unsigned int (the interface number)."""
    return struct.pack("I", num)


def setiface_arg(num, alt):
    """usbdevfs_setinterface is two unsigned ints, not two shorts."""
    return struct.pack("II", num, alt)


def verdict(st):
    if st["error"]:
        print("  -> %s" % st["error"])
        return 1
    if st["age"] is not None:
        print("  (reader attached %.1f s after enumeration)" % st["age"])
    if st["packets"] == 0:
        print("  -> NOTHING RECEIVED: pipe silent")
        return 1
    if st["clocks"] < 8:
        print("  -> DATA but almost no clock (%d): pipe works, clock is not "
              "being sent" % st["clocks"])
        if 0 < st["clocks"] <= 4 and st["packets"] <= 4:
            print("     Signature: a few buffered events drained, then silence.")
            print("     The reader attached after enumeration and the master's")
            print("     engine gave up with nobody draining its IN pipe.")
            print("     Retry with --wait: arm the reader FIRST, then plug the")
            print("     master in and press play at once.")
        return 1
    print("  -> MIDI clock flowing at %.2f BPM" % st["bpm"])
    return 0


def find_midi_device(want):
    """(device, candidates) for the first matching USB MIDI device, or None."""
    for dev in devices():
        if want and ("%s:%s" % (dev["vid"], dev["pid"])).lower() != want:
            continue
        cands = midi_candidates(interfaces(dev))
        if cands:
            return dev, cands
    return None, None


def main(argv):
    want = None
    seconds = 30.0
    listing = False
    raw = False
    try_all = False
    timeout_ms = 200
    delay_s = 0.0
    start_audio = False
    iso = False
    poke = False
    wait = False
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
        elif a == "--iso":
            iso = True
            start_audio = True      # real streaming implies armed altsettings
        elif a == "--poke":
            poke = True
        elif a == "--wait":
            wait = True
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
                  " [--iso] [--poke] [--wait]" % argv[0])
            return 2
        i += 1

    found = devices()
    if wait:
        # Start listening before the device is plugged in: a device that gives
        # up when nobody drains its small IN buffer can only be caught this way.
        print("waiting for a USB MIDI device%s - plug it in now"
              % ("" if not want else " (" + want + ")"))
        while True:
            dev, cands = find_midi_device(want)
            if dev:
                print("device appeared: %s:%s" % (dev["vid"], dev["pid"]))
                time.sleep(0.15)          # let enumeration finish
                break
            time.sleep(0.2)
        found = devices()                 # re-scan with the device present
    if not found:
        print("no USB devices found in %s" % SYSFS)
        return 1

    print("=== USB devices ===")
    for dev in found:
        describe(dev, interfaces(dev))

    if listing:
        return 0

    audio = []
    claims = []
    iso_eps = []
    candidates = []
    for dev in found:
        if want and ("%s:%s" % (dev["vid"], dev["pid"])).lower() != want:
            continue
        ifaces = interfaces(dev)
        if start_audio:
            for a_num, a_alt in audio_stream_alts(ifaces):
                audio.append((a_num, a_alt))
            claims = sorted({i["num"] for i in ifaces if i["alt"] == 0})
            if iso:
                iso_eps.extend(iso_eps_for(ifaces, audio))
        for it, ep in midi_candidates(ifaces):
            candidates.append((dev, it, ep))
    if not candidates:
        print("\nno USB MIDI interface found%s" %
              ("" if not want else " on " + want))
        if want:
            for dev in found:
                print("note: %s is present as %s:%s - Roland devices can flip"
                      " personality on reconnect, so drop the VID:PID argument"
                      " and retry" % (dev["product"] or "device",
                                      dev["vid"], dev["pid"]))
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
            st = listen(node, it, ep, win, raw, timeout_ms, delay_s, audio,
                        claims, iso_eps, poke)
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

    st = listen(node, it, ep, seconds, raw, timeout_ms, delay_s, audio,
                claims, iso_eps, poke)
    print("\n=== result ===")
    print("packets %d   bytes %d   clocks %d   transports %d   bpm %.2f" %
          (st["packets"], st["bytes"], st["clocks"], st["events"], st["bpm"]))
    return verdict(st)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
