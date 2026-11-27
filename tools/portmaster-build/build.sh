#!/bin/bash
# Cross-build for the PortMaster ports. Runs inside the rg808build container
# with the repo mounted at /src.
#
#   MACHINE=smp  (default) -> dist/rgsmp.zip  (port/RGSMP.sh + port/rgsmp/)
#   MACHINE=808           -> dist/rg808.zip  (port/RG808.sh + port/rg808/)
#
# The sources follow the post-split layout: the shared core in src/core/ and
# the machine shell in src/machines/<machine>/.
set -e
cd /src

MACHINE="${MACHINE:-smp}"
case "$MACHINE" in
  808)
    PORT=rg808
    LAUNCHER=RG808.sh
    MDIR=src/machines/808
    MSRC="src/machines/808/machine.c src/machines/808/samples.c src/machines/808/main.c"
    ;;
  smp)
    PORT=rgsmp
    LAUNCHER=RGSMP.sh
    MDIR=src/machines/smp
    MSRC="src/machines/smp/machine.c src/machines/smp/samplebank.c src/machines/smp/main.c"
    ;;
  *)
    echo "unknown MACHINE=$MACHINE (expected smp or 808)" >&2
    exit 1
    ;;
esac

CORE="src/core/engine.c src/core/midi.c src/core/usbmidi.c src/core/canvas.c src/core/wav.c src/core/pitch.c src/core/pattern.c src/core/song.c src/core/fx.c src/core/perf_fx.c src/core/step_ui.c src/core/views/views.c src/core/views/pitch_view.c src/core/views/slice_view.c"
SRCS="$CORE $MSRC"
CFLAGS="-O2 -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Isrc/core -I$MDIR -I/usr/include/SDL2 -DNDEBUG"
LIBS="-lSDL2 -lm -lpthread"

STAGE="dist/$PORT-staging"
mkdir -p "$STAGE"

echo "== aarch64 =="
aarch64-linux-gnu-gcc $CFLAGS -o "$STAGE/$PORT.aarch64" $SRCS $LIBS

echo "== armhf =="
arm-linux-gnueabihf-gcc $CFLAGS -march=armv7-a -mfpu=neon-vfpv4 \
  -o "$STAGE/$PORT.armhf" $SRCS $LIBS

file "$STAGE/$PORT".* || true

# assemble zip tree: <LAUNCHER> + <PORT>/{binaries, assets, licenses}
rm -rf dist/build
mkdir -p "dist/build/$PORT"
cp "port/$LAUNCHER" dist/build/
cp "$STAGE/$PORT".* "dist/build/$PORT/"
mkdir -p "dist/build/$PORT/assets/wav"
if [ "$MACHINE" = 808 ]; then
  # The 808 kit is the flat <fam>NN.wav set. Sampler/user packs live in
  # untracked subdirs under assets/wav and must not ship in the port zip.
  find assets/wav -maxdepth 1 -type f -name '*.wav' \
    -exec cp {} "dist/build/$PORT/assets/wav/" \;
else
  cp -r assets/wav "dist/build/$PORT/assets/wav"
fi
cp -r "port/$PORT/licenses" "dist/build/$PORT/"
[ -f "port/$PORT/screenshot.png" ] && cp "port/$PORT/screenshot.png" "dist/build/$PORT/" || true
cp port/README.md "dist/build/$PORT/"
[ -f port/gameinfo.xml ] && cp port/gameinfo.xml "dist/build/$PORT/" || true

chmod +x "dist/build/$LAUNCHER" "dist/build/$PORT/$PORT".*

cd dist/build
rm -f "../$PORT.zip"
zip -r "../$PORT.zip" "$LAUNCHER" "$PORT" > /dev/null
cd ..
echo "== done: dist/$PORT.zip =="
ls -la "$PORT.zip"
