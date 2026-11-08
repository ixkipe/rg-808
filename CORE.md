# RG-808 / RG-SMP shared core

The `core` branch holds the sample-agnostic drum-machine core. Machine
branches merge it and keep only their machine definition, app shell, assets and
port packaging.

## Layout

```
src/core/          engine, sequencer, patterns, songs, FX, punch-in FX, canvas
src/machines/808/  TRACKS table, kit sample families, KIT page, 808 app shell
src/machines/smp/  track slots, sample bank/browser, SMP app shell
assets/wav/        tracked 808 kit; sampler/user packs live in ignored subdirs
```

The machine seam is `src/core/machine.h`:

- identity and pattern magic
- track table (`TRACKS`, `N_TRACKS`)
- engine init and sample resolution
- pattern/song sample metadata (`KIT` vs `SMP` lines)
- default snapshot setup

The core engine plays `sample_data[]` pointers. It no longer calls the 808 kit
loader directly. `PatternSnapshot` carries both `kit[]` (808) and
`sample_path[]` (SMP); each machine resolves the one it uses.

## Build both machines

```powershell
cmake -S . -B build-808 -G Ninja -DRG_MACHINE=808
cmake --build build-808

cmake -S . -B build-smp -G Ninja -DRG_MACHINE=smp
cmake --build build-smp
```

`rg808.exe` is the target name for both; the build directory selects the
machine. The 808 build defaults to `assets/wav`; the SMP build uses the same
root and can be pointed elsewhere with `--assets` or `RG808_ASSETS`.

## Branch workflow

- Core changes land on `core` first, or on `core/<topic>` then merge into
  `core`.
- Merge `core` into `master` (RG-808) and `refs/heads/sampler` (RG-SMP).
- Never merge a machine branch into `core`.
- Machine-specific changes stay on the machine branch; if a fix is generic,
  port it to `core` first.
- Tag core revisions (`core-v0.2`, ...) and record them in the machine
  branches.

## Tests

The machine branches keep their existing suites (`tools/test-*.ps1`). Core
changes must pass the 808 suites and the SMP sampler suite. Bounce parity
against the pre-split 808 build is the guard against machine assumptions
creeping back into the core.
