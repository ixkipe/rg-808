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

Core-specific checks:

- `--test-swing` / `tools/test-swing.ps1`: the grid LED follows the swung hit.
- `--test-pattern` / `tools/test-pattern.ps1`: main-menu save writes only the
  active slot; library SAVE HERE writes the cursor slot and switches to it;
  selecting an empty slot creates a clean pattern and switches to it.
- `--test-levels` / `tools/test-levels.ps1`: ghost/weak/regular/accent gain
  ordering, the VLV sidecar round-trip, and the hold-A modal state machine.

## Step levels

The sequencer has four audible step levels:

| Level | Value | Gain |
|---|---:|---:|
| Ghost | 3 | 0.12 |
| Weak | 4 | 0.40 |
| Regular | 1 | 0.72 |
| Accented | 2 | 1.00 |

- A quick A tap keeps the old off -> regular -> accented cycle.
- Holding A for 500 ms opens the level modal; D-pad selects, A confirms,
  B cancels. The selection is auditioned as the cursor moves.
- On disk the existing 16-digit line stays 0/1/2. A `VLV <label> <16 digits>`
  sidecar carries the exact 0..4 values only for tracks that use ghost/weak.
  Older builds ignore `VLV` and play those steps as regular.

## Voice limits

- **RG-808:** `track_voice_limit = 0`; voice allocation uses the global-oldest
  policy across the 64-voice pool.
- **RG-SMP:** each track keeps up to **4** voices. This is the minimum needed
  for a 4x ratchet; a lower cap cuts the third/fourth hit and makes 3x/4x
  ratchets sound broken. Longer overlaps still use the global voice pool.

## Pattern library behavior

- Main menu **SAVE PATTERN** writes the editor to the active slot only.
- Library **SAVE HERE** writes the editor to the cursor slot and makes it
  active (save-as).
- Selecting an empty slot creates a clean pattern in that slot: notes and
  ratchets are cleared, but tempo, swing, length, FX, 808 kit variants and SMP
  sample paths are kept. The new slot becomes active and the app returns to the
  sequencer.
