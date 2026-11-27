"""Offline checks for usbmidi_probe.py - runs inside the container, no device.

Builds configuration descriptors that mirror what the T-8 actually presents:
  generic mode  0582:029d  class-01 interfaces, MIDI streaming on if 3 alt 0
  vendor mode   0582:029e  class-ff interfaces, same layout
and checks the parser/selector, plus the USB-MIDI packet expansion.
"""
import ctypes
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import usbmidi_probe as m

fails = 0


def check(cond, msg):
    global fails
    print(("PASS: " if cond else "FAIL: ") + msg)
    if not cond:
        fails += 1


def iface(num, alt, cls, sub, proto, eps):
    b = bytes([9, 4, num, alt, len(eps), cls, sub, proto, 0])
    for addr, attr, maxpkt in eps:
        b += bytes([7, 5, addr, attr, maxpkt & 0xFF, maxpkt >> 8, 0])
    return b


def config(ifaces):
    body = b"".join(ifaces)
    total = 9 + len(body)
    head = bytes([9, 2, total & 0xFF, total >> 8, len(ifaces), 1, 0, 0x80, 50])
    return head + body


# --- generic (class-compliant) personality ----------------------------------
t8 = m.parse_config(config([
    iface(0, 0, 0x01, 0x01, 0x00, []),
    iface(1, 0, 0x01, 0x02, 0x00, []),
    iface(1, 1, 0x01, 0x02, 0x00, [(0x01, 0x09, 192), (0x82, 0x09, 192)]),
    iface(2, 0, 0x01, 0x02, 0x00, []),
    iface(2, 1, 0x01, 0x02, 0x00, [(0x03, 0x09, 192), (0x84, 0x09, 192)]),
    iface(3, 0, 0x01, 0x03, 0x00, [(0x03, 0x02, 64), (0x84, 0x02, 64)]),
]))
check(len(t8) == 6, "parses every interface descriptor")

cands = m.midi_candidates(t8)
check(len(cands) == 1, "finds exactly one MIDI IN endpoint")
if cands:
    it, ep = cands[0]
    check(it["num"] == 3 and it["alt"] == 0, "selects if 3 alt 0")
    check(ep["addr"] == 0x84 and (ep["attr"] & 3) == 2, "picks bulk IN 0x84")

# --- vendor personality (0582:029e) ----------------------------------------
vendor = m.parse_config(config([
    iface(0, 0, 0xFF, 0xFF, 0x00, []),
    iface(1, 0, 0xFF, 0x02, 0x02, []),
    iface(3, 0, 0xFF, 0x03, 0x00, [(0x03, 0x02, 512), (0x84, 0x02, 512)]),
    iface(3, 1, 0xFF, 0x03, 0x00, [(0x03, 0x03, 512), (0x85, 0x03, 512)]),
]))
cands = m.midi_candidates(vendor)
check(len(cands) >= 1, "accepts a vendor-class MIDI interface")
if cands:
    it, ep = cands[0]
    check(it["num"] == 3 and it["alt"] == 0 and ep["addr"] == 0x84,
          "prefers bulk alt 0 in vendor mode too")

# --- alternate setting fallback --------------------------------------------
alts = m.parse_config(config([
    iface(3, 0, 0x01, 0x03, 0x00, [(0x03, 0x02, 64)]),          # OUT only
    iface(3, 1, 0x01, 0x03, 0x00, [(0x85, 0x03, 512)]),         # interrupt IN
]))
cands = m.midi_candidates(alts)
check(len(cands) == 1 and cands[0][0]["alt"] == 1 and
      cands[0][1]["addr"] == 0x85,
      "falls back to an alternate setting when alt 0 has no IN endpoint")

# --- audio-only device -----------------------------------------------------
audio_only = m.parse_config(config([iface(0, 0, 0x01, 0x01, 0x00, []),
                                    iface(1, 0, 0x01, 0x02, 0x00, [])]))
check(m.midi_candidates(audio_only) == [], "ignores an audio-only device")

# --- packet expansion ------------------------------------------------------
pkt = bytes([0x0F, 0xF8, 0x00, 0x00,      # clock
             0x0F, 0xFA, 0x00, 0x00,      # start
             0x0F, 0xFC, 0x00, 0x00,      # stop
             0x09, 0x90, 0x3C, 0x40,      # note on
             0x0B, 0xB0, 0x07, 0x64,      # control change
             0x03, 0xF2, 0x01, 0x02])     # song position
out = m.packets_to_bytes(pkt)
check(out == bytes([0xF8, 0xFA, 0xFC, 0x90, 0x3C, 0x40,
                    0xB0, 0x07, 0x64, 0xF2, 0x01, 0x02]),
      "expands USB-MIDI packets to one MIDI byte stream")

# --- ioctl codes -----------------------------------------------------------
size = ctypes.sizeof(m._usbdevfs_bulktransfer)
check(m.USBDEVFS_BULK == ((3 << 30) | (size << 16) | (ord("U") << 8) | 2),
      "USBDEVFS_BULK matches this ABI (%d-byte struct)" % size)
check(m.USBDEVFS_CLAIMINTERFACE == 0x8004550F, "claim ioctl code")
check(m.USBDEVFS_SETINTERFACE == 0x80085504, "set-interface ioctl code")

print("")
print("failures: %d" % fails)
sys.exit(1 if fails else 0)
