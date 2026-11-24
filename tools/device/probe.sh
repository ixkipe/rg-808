#!/bin/sh
# RG-808 device probe — run in Mustard Terminal (muOS) or over SSH.
# Prints the facts the cross-build needs. Pipe to a file and send back:
#   sh probe.sh > /storage/rom/probe.txt 2>&1
echo "=== RG-808 device probe ==="
echo "date: $(date)"
echo
echo "--- kernel/arch ---"
uname -a
echo "uname -m: $(uname -m)"
echo
echo "--- cpu ---"
head -n 12 /proc/cpuinfo
echo
echo "--- memory ---"
cat /proc/meminfo | head -3
echo
echo "--- glibc ---"
/sbin/ldconfig -p 2>/dev/null | grep -m1 libc.so || true
ls /lib/libc-*.so /lib/libc.so* 2>/dev/null | head -4
echo
echo "--- SDL2 availability ---"
/sbin/ldconfig -p 2>/dev/null | grep -i sdl2
find /usr/lib /opt /roms /storage -maxdepth 4 -name "libSDL2*" 2>/dev/null | head -8
echo
echo "--- PortMaster ---"
for d in /opt/system/Tools/PortMaster /opt/tools/PortMaster "$HOME/.local/share/PortMaster" /roms/ports/PortMaster /storage/rom/ports/PortMaster; do
  [ -f "$d/control.txt" ] && echo "control.txt: $d/control.txt" && grep -E "^(DEVICE_ARCH|CFW_NAME|directory)=|DEVICE_ARCH|export directory" "$d/control.txt" | head -8
done
echo
echo "--- muOS / storage layout ---"
ls -d /roms /ports /storage/rom /storage/rom/roms /storage/rom/ports 2>/dev/null
ls / | head -20
echo
echo "--- audio ---"
cat /proc/asound/cards 2>/dev/null
ls /usr/bin/aplay /usr/bin/amixer /usr/sbin/aplay 2>/dev/null
echo
echo "--- usb host mode / midi (v1.1 MIDI gate) ---"
# The MIDI phase needs the RG35XX to act as a USB *host* with a USB MIDI
# (class compliant) driver. These lines tell us whether that is possible.
echo "usb devices in sysfs:"
ls /sys/bus/usb/devices 2>/dev/null | head -12
echo "usb bus nodes (/dev/bus/usb):"
ls -R /dev/bus/usb 2>/dev/null | head -12
echo "usb host controllers:"
ls /sys/bus/platform/drivers/dwc2 2>/dev/null | head -6
ls /sys/class/usb_host 2>/dev/null | head -6
echo "dwc2 dr_mode:"
for f in /sys/kernel/debug/usb/*/dr_mode /sys/devices/platform/*/usb*/dr_mode; do
  if [ -f "$f" ]; then echo "$f: $(cat "$f" 2>/dev/null)"; fi
done
echo "usb gadget (device mode) support:"
ls /sys/kernel/config/usb_gadget 2>/dev/null | head -4
ls /sys/class/udc 2>/dev/null | head -4
echo "loaded usb/sound modules:"
lsmod 2>/dev/null | grep -E "dwc2|usb-storage|usbhid|snd-usb|snd-rawmidi|snd-seq|g_midi" | head -12
echo "lsusb:"
command -v lsusb >/dev/null 2>&1 && lsusb 2>/dev/null | head -8 || echo "lsusb not installed"
echo "kernel config (usb/midi):"
for cfg in /proc/config.gz /boot/config-$(uname -r); do
  [ -r "$cfg" ] || continue
  case "$cfg" in
    *.gz) zcat "$cfg" 2>/dev/null ;;
    *) cat "$cfg" ;;
  esac | grep -E "CONFIG_(USB_DWC2|USB_HOST|USB_GADGET|USB_F_|SNDRV|SND_USB_AUDIO|SND_SEQ|SND_RAW_MIDI|USB_SND)=" | head -20
  break
done
echo "--- midi devices ---"
ls -l /dev/snd 2>/dev/null | head -20
ls /dev/snd/midi* 2>/dev/null || echo "no /dev/snd/midi* (no USB MIDI driver or nothing plugged in)"
ls /proc/asound/seq 2>/dev/null | head -8
command -v aconnect >/dev/null 2>&1 && aconnect -l 2>&1 | head -10
echo
echo "--- video ---"
cat /sys/class/graphics/fb0/virtual_size 2>/dev/null
ls /dev/dri 2>/dev/null
echo "SDL_VIDEODRIVER=$SDL_VIDEODRIVER  SDL_AUDIODRIVER=$SDL_AUDIODRIVER"
echo
echo "--- tools ---"
for t in gcc make cmake unzip sshd busybox python3; do
  command -v $t >/dev/null 2>&1 && echo "$t: $(command -v $t)"
done
echo "=== done ==="
