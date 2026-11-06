# RG808

A Roland TR-808 clone for PortMaster handhelds: a 16-step, 11-track drum
machine with per-step accents, sample-accurate sequencing, one-tap sample
selection per voice (kit mode), and pattern save, written in C with SDL2.
Runs natively at 640x480.

## Notes

Thanks to the TR-808 sample pack contributors and the PortMaster crew for the
packaging conventions.

## Controls

| Button | Action |
|--|--|
| D-Pad | Move cursor |
| A | Cycle step: off / on / accent |
| B | Clear step (back in menus) |
| X or Y | Hear selected voice |
| START | Play / stop |
| SELECT | Menu (tempo, kits, save, load, clear, quit) |
| L / R | Tempo -5 / +5 |
| L+R | Clear current track row |

Kit mode (menu -> SELECT KITS): each row is an instrument, the numbered
columns are that instrument's sample variants. A picks and auditions, X
auditions, B returns. Pick is saved with the pattern.

## Compile

```
docker build -t rg808build tools/portmaster-build
docker run --rm -v "$PWD:/src" rg808build     # writes dist/rg808.zip
```
Native dev builds (Windows MinGW / Linux) use the top-level CMakeLists.
