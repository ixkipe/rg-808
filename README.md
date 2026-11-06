# RG-808 — a Roland TR-808 clone for the Anbernic RG35XX SP (muOS / PortMaster)

A 16-step, 11-track drum machine written in C with SDL2, packaged as a
PortMaster "ready-to-run" port. Per-step accents, sample-accurate sequencing,
pattern save, 640×480 native UI, D-pad driven.

## Layout
```
src/          C sources (engine, audio, ui, wav loader, persistence)
assets/wav/   curated kit (11 samples copied from the Roland TR-808 pack)
tools/        dev & packaging scripts (Windows + Docker + device)
tools/device/ muOS probe / installer / deploy scripts
port/         PortMaster packaging staging (RG808.sh, port.json, gameinfo, ...)
vendor/       SDL2 + public-domain font (fetched by tools/fetch-deps.ps1)
dist/         built port zip (generated)
```

## Building for local dev (Windows)
```
tools\fetch-deps.ps1          # one-time: SDL2 mingw + font
powershell tools\prepare-assets.ps1   # one-time: kit from sample pack
cmake -S . -B build -G Ninja
cmake --build build
build\rg808.exe               # run (SDL2 DLL + assets are copied next to it)
```
Useful CLI modes (no audio device needed):
```
rg808.exe --bounce out.wav --loops 4    # render current pattern to WAV
rg808.exe --screenshot shot.bmp         # render one UI frame to BMP
```
Keyboard controls on PC: arrows=dpad, Z/Enter=A, X=B, C=X(hear), Space=START,
Esc/Backspace=SELECT-menu, [/]=L/R tempo, Q=quit.

## Building for the device (muOS / PortMaster)
Requires Docker Desktop (or any docker):
```
docker build -t rg808build tools/portmaster-build
docker run --rm -v ${PWD}:/src rg808build      # -> dist/rg808.zip  (PowerShell)
```
The container cross-compiles `rg808.aarch64` + `rg808.armhf` against Debian's
SDL2 (the device ships its own libSDL2 at runtime; both arch binaries are
inside the zip and the launcher picks the right one).

## Installing on the device
Option A — over SSH (enable SSH on muOS first):
```
powershell tools\deploy.ps1 <device-ip>
```
Option B — offline: put `dist/rg808.zip` on the SD card, or unzip it so that
the `rg808/` folder lands in `<card>/ports/` and `RG808.sh` in
`<card>/roms/PORTS/`. Launch "RG808" from Explore Content → Ports.

Before the first build, please run the device probe so we can pin the facts
(kernel, glibc, SDL2, paths):
```
tools\device\probe.sh > probe.txt 2>&1     # in Mustard Terminal
```

## Controls
| Button | Action |
|--|--|
| D-Pad | Move cursor (hold to repeat) |
| A | Cycle step: off → on → accent |
| B | Clear step (back in menus) |
| X / Y | Hear sound of the selected track |
| START | Play / stop |
| SELECT | Menu (tempo, kits, save, load, clear all, quit) |
| L / R | Tempo −5 / +5 |
| L+R | Clear the whole track row |

In **kit mode** (menu → SELECT KITS): D-Pad rows = instruments, columns =
sample variants (numbered boxes); A selects + auditions, X auditions only,
B returns. Each voice ships every matching variant from the pack
(BD/SN: 16, toms: 7, CH: 5, OH: 3, CL: 2, CP: 3, CB: 2, CY: 6).
Selections are saved with the pattern.

Your pattern and tempo are saved automatically on quit (menu: QUIT) and can be
saved/loaded manually. The file lives in `conf/pattern.txt` inside the port
folder on the card.

## Kit mapping
Each voice ships its whole variant family from the pack as `<id>NN.wav`
(e.g. `bd01..bd16` = TR-808Kick01..16). Edit the `$families` arrays in
`tools/prepare-assets.ps1` to add, remove, or reorder variants; defaults are
the `dflt` column in `src/engine.c`.

## Status / roadmap
- v1 (this): single pattern, 11 tracks, accents, sample selection per voice
  (kit mode), save.
- v2 ideas: per-track volume/mute, multiple patterns, machine-feel BD accent
  decay, master volume UI, pattern chain.

## Credits & licences
Code: this project (MIT where not otherwise noted). SDL2 (zlib), public-domain
font8x8, sample pack per `port/rg808/licenses/ASSETS.txt` (confirm before any
public release).
