#!/bin/sh
# RG-SMP installer - run on the device (Mustard Terminal / SSH) after scp-ing the zip.
# Usage: sh install.sh [path-to-rgsmp.zip]   (default: /tmp/rgsmp.zip)
# Zip layout (PortMaster ready-to-run): RGSMP.sh + rgsmp/<files>
ZIP="${1:-/tmp/rgsmp.zip}"
[ -f "$ZIP" ] || { echo "no zip at $ZIP"; exit 1; }

# locate the card root that holds roms/ (muOS exposes /roms directly or under /storage/rom)
CARD=""
for base in / /storage/rom /mnt/mmc; do
  [ -d "$base/roms" ] && CARD="$base" && break
done
[ -n "$CARD" ] || { echo "could not find roms/ mount"; exit 1; }
echo "card root: $CARD"

TMP="/tmp/rgsmp-unpack.$$"
mkdir -p "$TMP"
unzip -o -q "$ZIP" -d "$TMP" 2>/dev/null || busybox unzip -o -q "$ZIP" -d "$TMP" || { echo "unzip failed"; exit 1; }
[ -f "$TMP/RGSMP.sh" ] && [ -d "$TMP/rgsmp" ] || { echo "unexpected zip layout"; ls "$TMP"; exit 1; }

mkdir -p "$CARD/ports"
rm -rf "$CARD/ports/rgsmp"
mv "$TMP/rgsmp" "$CARD/ports/"
chmod +x "$CARD/ports/rgsmp"/rgsmp.* 2>/dev/null
cp "$TMP/RGSMP.sh" "$CARD/ports/"

SHIMDIR="$CARD/roms/PORTS"; [ -d "$SHIMDIR" ] || SHIMDIR="$CARD/roms/Ports"
[ -d "$SHIMDIR" ] || { echo "no Ports menu folder in $CARD/roms"; ls "$CARD/roms"; }
cp "$TMP/RGSMP.sh" "$SHIMDIR/RGSMP.sh"
chmod +x "$SHIMDIR/RGSMP.sh" "$CARD/ports/RGSMP.sh" 2>/dev/null
rm -rf "$TMP"

echo "installed:"
ls -l "$CARD/ports/rgsmp" | head -8
echo "shim: $SHIMDIR/RGSMP.sh"
echo "Launch 'RG-SMP' from Explore Content > Ports (may need a library rescan or reboot)."
