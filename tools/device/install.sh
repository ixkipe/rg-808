#!/bin/sh
# RG-808 installer — run on the device (Mustard Terminal / SSH) after scp-ing the zip.
# Usage: sh install.sh [path-to-rg808.zip]   (default: /tmp/rg808.zip)
# Zip layout (PortMaster ready-to-run): RG808.sh + rg808/<files>
ZIP="${1:-/tmp/rg808.zip}"
[ -f "$ZIP" ] || { echo "no zip at $ZIP"; exit 1; }

# locate the card root that holds roms/ (muOS exposes /roms directly or under /storage/rom)
CARD=""
for base in / /storage/rom /mnt/mmc; do
  [ -d "$base/roms" ] && CARD="$base" && break
done
[ -n "$CARD" ] || { echo "could not find roms/ mount"; exit 1; }
echo "card root: $CARD"

TMP="/tmp/rg808-unpack.$$"
mkdir -p "$TMP"
unzip -o -q "$ZIP" -d "$TMP" 2>/dev/null || busybox unzip -o -q "$ZIP" -d "$TMP" || { echo "unzip failed"; exit 1; }
[ -f "$TMP/RG808.sh" ] && [ -d "$TMP/rg808" ] || { echo "unexpected zip layout"; ls "$TMP"; exit 1; }

mkdir -p "$CARD/ports"
rm -rf "$CARD/ports/rg808"
mv "$TMP/rg808" "$CARD/ports/"
chmod +x "$CARD/ports/rg808"/rg808.* 2>/dev/null
cp "$TMP/RG808.sh" "$CARD/ports/"

SHIMDIR="$CARD/roms/PORTS"; [ -d "$SHIMDIR" ] || SHIMDIR="$CARD/roms/Ports"
[ -d "$SHIMDIR" ] || { echo "no Ports menu folder in $CARD/roms"; ls "$CARD/roms"; }
cp "$TMP/RG808.sh" "$SHIMDIR/RG808.sh"
rm -rf "$TMP"

echo "installed:"
ls -l "$CARD/ports/rg808" | head -8
echo "shim: $SHIMDIR/RG808.sh"
echo "Launch 'RG808' from Explore Content > Ports (may need a library rescan or reboot)."
