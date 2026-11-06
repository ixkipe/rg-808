# RG-808 — a Roland TR-808 clone for the Anbernic RG35XX SP (muOS / PortMaster)

A 16-step, 11-track drum machine written in C with SDL2, packaged as a PortMaster port.

## Layout
```
src/          C sources (engine, audio, ui, wav loader, storage)
assets/wav/   curated kit samples (copied from the Roland TR-808 pack)
tools/        dev & packaging scripts
vendor/       SDL2 + public-domain font (fetched by tools/fetch-deps.ps1)
port/         PortMaster packaging files
```

## Building (Windows dev)
```
cmake -S . -B build -G Ninja
cmake --build build
build\rg808.exe              # run the app (SDL2 DLL is copied next to it)
```
Useful CLI modes:
```
rg808.exe --bounce out.wav [loops]     # render pattern to WAV without audio device
rg808.exe --screenshot shot.bmp        # render one UI frame to BMP
```

## Building (muOS device, cross)
```
docker build -t rg808build tools/portmaster-build
docker run --rm -v %cd%:/src rg808build
bash tools/package-port.sh             # produces dist/rg808.zip + dist/RG808.sh
```

## Install on device
Unzip the port into `/ports/rg808/` and place `RG808.sh` in `/roms/PORTS/`
(or use PortMaster on a Wi-Fi capable device). See port/README.md.

## Controls
| Button | Action |
|--|--|
| D-Pad | Move cursor |
| A | Cycle step: off → on → accent |
| B | Clear step under cursor |
| X | Hear sound of the selected track |
| START | Play / stop |
| SELECT | Menu (tempo, clear, save, quit) |
| L / R | Tempo −5 / +5 |
| L+R | Clear the whole track row |

## Saving
Your pattern and tempo are saved automatically when you quit (or from the menu).
The file lives in `conf/pattern.txt` inside the port folder.
