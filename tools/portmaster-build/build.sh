#!/bin/bash
# Runs inside the rg808build container with the repo mounted at /src.
# Produces /src/dist/rg808.zip (PortMaster installable) and the raw binaries.
set -e
cd /src

SRCS="src/main.c src/engine.c src/canvas.c src/samples.c src/wav.c src/pattern.c src/song.c src/fx.c"
CFLAGS="-O2 -std=gnu11 -Wall -Wextra -Wno-unused-parameter -I/usr/include/SDL2 -DNDEBUG"
LIBS="-lSDL2 -lm"

mkdir -p dist/rg808-staging

echo "== aarch64 =="
aarch64-linux-gnu-gcc $CFLAGS -o dist/rg808-staging/rg808.aarch64 $SRCS $LIBS

echo "== armhf =="
arm-linux-gnueabihf-gcc $CFLAGS -march=armv7-a -mfpu=neon-vfpv4 \
  -o dist/rg808-staging/rg808.armhf $SRCS $LIBS

file dist/rg808-staging/rg808.* || true

# assemble zip tree: RG808.sh + rg808/{binaries, assets, licenses}
rm -rf dist/build
mkdir -p dist/build/rg808
cp port/RG808.sh dist/build/
cp dist/rg808-staging/rg808.* dist/build/rg808/
mkdir -p dist/build/rg808/assets
cp -r assets/wav dist/build/rg808/assets/
cp -r port/rg808/licenses dist/build/rg808/
[ -f port/rg808/screenshot.png ] && cp port/rg808/screenshot.png dist/build/rg808/ || true
cp port/README.md dist/build/rg808/
[ -f port/gameinfo.xml ] && cp port/gameinfo.xml dist/build/rg808/ || true

chmod +x dist/build/RG808.sh dist/build/rg808/rg808.*

cd dist/build
rm -f ../rg808.zip
zip -r ../rg808.zip RG808.sh rg808 > /dev/null
cd ..
echo "== done: dist/rg808.zip =="
ls -la rg808.zip
