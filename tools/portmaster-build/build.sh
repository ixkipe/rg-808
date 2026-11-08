#!/bin/bash
# Runs inside the rgsmpbuild container with the repo mounted at /src.
# Produces /src/dist/rgsmp.zip (PortMaster installable) and the raw binaries.
set -e
cd /src

SRCS="src/core/engine.c src/core/canvas.c src/core/wav.c src/core/pattern.c src/core/song.c src/core/fx.c src/core/perf_fx.c src/core/step_ui.c src/machines/smp/samplebank.c src/machines/smp/machine.c src/machines/smp/main.c"
CFLAGS="-O2 -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Isrc/core -Isrc/machines/smp -I/usr/include/SDL2 -DNDEBUG"
LIBS="-lSDL2 -lm"

mkdir -p dist/rgsmp-staging

echo "== aarch64 =="
aarch64-linux-gnu-gcc $CFLAGS -o dist/rgsmp-staging/rgsmp.aarch64 $SRCS $LIBS

echo "== armhf =="
arm-linux-gnueabihf-gcc $CFLAGS -march=armv7-a -mfpu=neon-vfpv4 \
  -o dist/rgsmp-staging/rgsmp.armhf $SRCS $LIBS

file dist/rgsmp-staging/rgsmp.* || true

# assemble zip tree: RGSMP.sh + rgsmp/{binaries, assets, licenses}
rm -rf dist/build
mkdir -p dist/build/rgsmp
cp port/RGSMP.sh dist/build/
cp dist/rgsmp-staging/rgsmp.* dist/build/rgsmp/
mkdir -p dist/build/rgsmp/assets
cp -r assets/wav dist/build/rgsmp/assets/
cp -r port/rgsmp/licenses dist/build/rgsmp/
[ -f port/rgsmp/screenshot.png ] && cp port/rgsmp/screenshot.png dist/build/rgsmp/ || true
cp port/README.md dist/build/rgsmp/
[ -f port/gameinfo.xml ] && cp port/gameinfo.xml dist/build/rgsmp/ || true

chmod +x dist/build/RGSMP.sh dist/build/rgsmp/rgsmp.*

cd dist/build
rm -f ../rgsmp.zip
zip -r ../rgsmp.zip RGSMP.sh rgsmp > /dev/null
cd ..
echo "== done: dist/rgsmp.zip =="
ls -la rgsmp.zip
