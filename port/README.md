# RG808

A Roland TR-808 clone for PortMaster handhelds: a 16-step, 11-track drum
machine with per-step accents, sample-accurate sequencing and pattern save,
written in C with SDL2. Runs natively at 640x480.

## Notes

Thanks to the TR-808 sample pack contributors and the PortMaster crew for the
packaging conventions.

## Controls

| Button | Action |
|--|--|
| D-Pad | Move cursor |
| A | Cycle step: off / on / accent |
| B | Clear step |
| X or Y | Hear selected voice |
| START | Play / stop |
| SELECT | Menu (tempo, save, load, clear, quit) |
| L / R | Tempo -5 / +5 |
| L+R | Clear current track row |

## Compile

```
docker build -t rg808build tools/portmaster-build
docker run --rm -v "$PWD:/src" rg808build     # writes dist/rg808.zip
```
Native dev builds (Windows MinGW / Linux) use the top-level CMakeLists.
