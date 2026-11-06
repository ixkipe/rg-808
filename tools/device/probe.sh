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
