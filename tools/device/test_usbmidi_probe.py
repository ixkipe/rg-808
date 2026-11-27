"""Offline checks for usbmidi_probe.py - runs inside the container, no device.

Builds a configuration descriptor that mirrors a class-compliant Roland T-8
(4 interfaces: audio control, 2x audio streaming, MIDI streaming) and checks
the parser picks the MIDI streaming interface and its bulk IN endpoint, plus
the USB-MIDI packet expansion.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import usbmidi_probe as m


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


t8 = config([
    iface(0, 0, 0x01, 0x01, 0x00, []),
    iface(1, 0, 0x01, 0x02, 0x00, []),
    iface(1, 1, 0x01, 0x02, 0x00, [(0x01, 0x09, 192), (0x82, 0x09, 192)]),
    iface(2, 0, 0x01, 0x02, 0x00, []),
    iface(2, 1, 0x01, 0x02, 0x00, [(0x03, 0x09, 192), (0x84, 0x09, 192)]),
    iface(3, 0, 0x01, 0x03, 0x00, [(0x05, 0x02, 64), (0x85, 0x02, 64)]),
])

fails = 0


def check(cond, msg):
    global fails
    print(("PASS: " if cond else "FAIL: ") + msg)
    if not cond:
        fails += 1


parsed = m.parse_config(t8)
check(len(parsed) == 6, "parses every interface descriptor")
check(parsed[5]["num"] == 3 and parsed[5]["sub"] == 3,
      "finds the MIDI streaming interface")
check(len(parsed[5]["eps"]) == 2, "keeps both MIDI endpoints")

hit = m.midi_interface(parsed)
check(hit is not None, "selects a MIDI interface")
if hit:
    it, ep = hit
    check(it["num"] == 3, "claims the MIDI interface number")
    check(ep["addr"] == 0x85, "picks the bulk IN endpoint")
    check((ep["attr"] & 3) == 2, "endpoint is bulk")

# A device with no MIDI function must not be selected.
audio_only = m.parse_config(config([iface(0, 0, 0x01, 0x01, 0x00, []),
                                    iface(1, 0, 0x01, 0x02, 0x00, [])]))
check(m.midi_interface(audio_only) is None, "ignores an audio-only device")

# USB-MIDI packets -> MIDI bytes.  Real-time is CIN 0xF (single byte).
pkt = bytes([0x0F, 0xF8, 0x00, 0x00,      # clock
             0x0F, 0xFA, 0x00, 0x00,      # start
             0x0F, 0xFC, 0x00, 0x00,      # stop
             0x09, 0x90, 0x3C, 0x40,      # note on ch1
             0x03, 0xF2, 0x01, 0x02])     # song position
out = m.packets_to_bytes(pkt)
check(out == bytes([0xF8, 0xFA, 0xFC, 0x90, 0x3C, 0x40, 0xF2, 0x01, 0x02]),
      "expands USB-MIDI packets to one MIDI byte stream")

# Bulk-transfer struct size drives the ioctl code.
size = __import__("ctypes").sizeof(m._usbdevfs_bulktransfer)
check(m.USBDEVFS_BULK == ((3 << 30) | (size << 16) | (ord("U") << 8) | 2),
      "USBDEVFS_BULK matches this ABI (%d-byte struct)" % size)

print("")
print("failures: %d" % fails)
sys.exit(1 if fails else 0)
