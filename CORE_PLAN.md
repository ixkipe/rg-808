# RG-808 Core Split Plan

**Status: planning only (2026-11-07).** No code has been moved yet. The working
808 machine remains on `master` until the split is proven.

This document covers M12 in `PLAN.md`: separating the sample-agnostic drum
machine core from the TR-808 machine definition so the same sequencer, pattern
library, song mode, effects, shortcuts and performance features can power
other drum machines.

## 1. Goal

Move everything that is not specific to the TR-808 sample kit into a shared
`core` branch, and keep one branch per machine:

```
core
 ├── 808      (current RG-808; parity target)
 ├── 909      (RD-909)
 ├── 707      (RD-707)
 └── retro    (RD-RETRO: LM-1 / DMX / Drumulator combined)
```

The core owns behaviour. A machine branch owns only:

- machine/instrument metadata (names, labels, colours, gains, defaults),
- sample assets and variant families,
- optional machine-specific voices or choke rules,
- port metadata, launcher, screenshots, README and licence notes.

**Not in scope:** runtime machine switching in one binary, mixed-machine
pattern libraries, mixed-machine songs, or per-voice synthesis. Machines are
separate builds/ports sharing one code base.

## 2. Branch model and workflow

| Branch | Purpose | Ships? |
|---|---|---|
| `master` | Current 808 release line until the split lands | Yes, temporarily |
| `core` | Shared engine/UI/sequencer/pattern/song/FX/performance code + a synthetic test machine | No port zip; headless tests only |
| `808` | 808 machine data + assets, built on `core` | Yes |
| `909` | RD-909 machine data + assets, built on `core` | Yes |
| `707` | RD-707 machine data + assets, built on `core` | Yes |
| `retro` | RD-RETRO combined machine data + assets, built on `core` | Yes |

Rules:

1. **Core changes happen on `core` first.** Merge `core` into each machine
   branch; never merge machine branches into `core`.
2. Machine-specific fixes stay on the machine branch. If a fix is generic,
   cherry-pick/port it to `core`, then merge back down.
3. Every machine branch records the core revision it was built from
   (`CORE_VERSION` in its machine metadata and/or build files).
4. Core must build and pass its tests **without any real sample pack**, using a
   synthetic test machine. This is the guard against 808 assumptions creeping
   back in.
5. `master` remains the current 808 release until the `808` branch reaches
   parity. After that, `808` is the canonical 808 port; `master` can be moved
   to `808` or kept as a release-only pointer (decide before merging).
6. Do not merge `909`/`707`/`retro` into each other. They only receive `core`
   updates.

## 3. What is coupled today

The current code is a good 808 implementation but not a generic core:

- `engine.h`: `N_TRACKS` is a compile-time `11`.
- `engine.c`: `TRACKS[N_TRACKS]` hard-codes the 808 instruments, colours,
  gains and default variants.
- `Engine`, `PatternSnapshot` and `SongSet` use `[N_TRACKS]` arrays.
- `pattern.c` matches 808 track labels (`BD`, `SD`, `LT`, …) and assumes 11
  lines.
- `samples.c` loads `<id>NN.wav` families from `TRACKS[]`.
- `main.c` draws an 11-row grid and an 11-row kit editor at fixed geometry.
- The 200-slot library, song chain, FX, swing, ratchets, mute and shortcuts are
  already mostly machine-agnostic in behaviour, but their data structures are
  sized by `N_TRACKS`.
- The UI and docs use the 808 name/colour scheme throughout.

The split should remove the machine assumptions from the left column and keep
the right column in machine branches.

## 4. Target architecture

### 4.1 Source layout

Proposed layout on `core`:

```
src/
  core/
    engine.c/.h        voices, clock, mixer, choke groups
    fx.c/.h            master FX chain
    perf_fx.c/.h       punch-in performance FX (M11)
    pattern.c/.h       pattern file + 200-slot library
    song.c/.h          song chain + snapshot set
    samples.c/.h       generic WAV family loader
    canvas.c/.h        software canvas
    ui.c/.h            generic grid/menu/overlays (split out of main.c)
    input.c/.h         keyboard/gamepad/shoulder layer
    machine.h          MachineDef/InstrumentDef interface
  machine/
    machine_data.c     branch-specific machine definition
  main.c               thin app shell, CLI modes, init
assets/wav/            branch-specific samples
port/                  branch-specific packaging
tools/                 shared build/test tooling
```

`main.c` is currently ~3.4k lines. Splitting UI/input out of it is part of the
core work, not optional, because the grid geometry and track labels are where
the 808 assumptions live.

### 4.2 Machine definition

The core exposes one interface, supplied by each machine branch:

```c
typedef struct {
    const char *id;            /* "808", "909", "707", "retro" */
    const char *name;          /* display name, e.g. "RG-808" */
    const char *version;       /* port version */
    int track_count;           /* <= MAX_TRACKS */
    const InstrumentDef *tracks;
} MachineDef;

typedef struct {
    const char *sample_id;     /* "bd" -> bd01.wav ... */
    const char *label;         /* "BD" (grid + pattern file key) */
    const char *name;          /* "BASS DRUM" */
    unsigned int color;        /* 0xRRGGBB */
    float gain;                /* nominal level */
    int default_variant;       /* 1-based */
    int choke_group;           /* 0 = none; e.g. hats share 1 */
} InstrumentDef;

const MachineDef *machine_get(void);
```

Rules:

- `MAX_TRACKS` is a compile-time ceiling (proposal: **24**); `track_count` is
  runtime. Arrays stay `[MAX_TRACKS]`, loops use `track_count`.
- Labels are unique within a machine and are the pattern-file keys. They may be
  reused across machines because branches do not share files.
- `choke_group` cuts ringing voices in the same group when a new voice fires
  (open/closed hats). This is a core engine feature and also improves the 808.
- Accent remains gain-only, as decided for all machines. No per-voice tune,
  decay, pan or filter in this plan.
- Sample variants remain `<sample_id>NN.wav` families; kit mode is unchanged.

### 4.3 Engine

- Replace `N_TRACKS` loops with `machine->track_count` (or a cached
  `n_tracks`).
- `Engine`, `PatternSnapshot` and `SongSet` arrays are `MAX_TRACKS`; only the
  first `n_tracks` are meaningful. Snapshot builders must zero the unused rows.
- Add choke-group handling in `fire()`/`alloc_voice()`.
- Keep the current sample-accurate clock, swing, ratchets, length, song
  snapshots, mute, master FX and punch-in FX exactly as they are.
- Keep `MAX_VOICES` at least 64; machines with more tracks may need a slightly
  higher ceiling only if a stress test shows voice stealing.

### 4.4 UI

The core draws from `MachineDef`, not from constants:

- `rows = machine->track_count`.
- Auto-fit row pitch between current 36 px and a compact ~26–28 px so 11/12/14
  tracks fit without scrolling. 909 (11), 707 (12) and a 12–14-track RETRO
  machine fit one page.
- If a future machine exceeds the fitted limit, page the grid (e.g. 12 rows per
  page, page indicator in the header). Do not hard-code machine names for
  layout.
- Kit mode columns use the actual variant count; if a family exceeds the
  visible columns, page or clamp it (current packs are <=16).
- Header shows the machine name/slot, not the literal `RG-808`.
- Header/status strings come from the machine definition or are generic.

### 4.5 Persistence

Pattern files remain plain text and human-editable.

- Keep `RG808P1` as an accepted legacy magic. Add a new canonical magic
  `RGDRUMP1` (or keep `RG808P1`; decide before C1) and always write
  `MACHINE <id>`.
- Old files without `MACHINE` are treated as `808` on the 808 branch.
- A machine branch validates the id. Loading a 909 pattern in the 808 port must
  fail with a clear "WRONG MACHINE" message and must not partially overwrite
  the editor. Never silently reinterpret track labels.
- `index.txt` writes a machine id header; `song.txt` writes `MACHINE <id>`.
- Each port keeps its own `conf/`; pattern/song libraries are per machine. No
  mixed-machine songs.
- Keep the atomic temp+fsync+rename writes and legacy `conf/pattern.txt`
  migration behaviour on the 808 branch.
- Optional later: a `--convert-pattern` tool that maps a pattern between
  machines by label when the target has the same labels. Not required for M12.

### 4.6 Build and packaging

- Same CMake project on every branch; each machine branch provides
  `src/machine/machine_data.c` and its `assets/wav/`.
- No `-DMACHINE=` runtime selector is needed for the branch model. A compile
  definition can still carry the machine id/name for titles and paths.
- Each machine branch has its own `port/` launcher name, `port.json`,
  `gameinfo.xml`, screenshots, README and licence notes, and produces its own
  `dist/<machine>.zip`.
- Shared test scripts live on `core`; machine branches extend them with
  machine-specific kit/asset checks.
- The core's `--test-core` uses a synthetic 4-track and 16-track machine so
  generic code is exercised at both small and near-maximum sizes.

## 5. Migration milestones

### C0 — Branch setup and documentation

- Create `core` from the current 808 release.
- Create `808` as the machine branch that will receive the split, initially
  identical to `master`.
- Keep `master` untouched until `808` parity; do not create `909`/`707`/`retro`
  until `core` is proven.
- Land this document and `CLONES_PLAN.md`.

### C1 — De-808 the data model (on `core`)

- Add `machine.h`, `MachineDef`/`InstrumentDef`, `MAX_TRACKS` and runtime
  `track_count`.
- Convert `TRACKS[]`, sample loading, pattern read/write, snapshots, engine
  loops and UI geometry to the machine definition.
- Add `MACHINE` metadata and wrong-machine validation.
- Keep the 808 definition in the tree temporarily so the refactor can be
  bounced against the old build.
- **Acceptance:** 808 bounce output is bit-identical to the pre-refactor
  build; all M1–M11 tests pass; pattern round-trip and song export unchanged.

### C2 — Extract the core branch

- Move generic code into `src/core/` and the app shell into `main.c`.
- Replace the 808 definition with a synthetic test machine on `core`.
- Split UI/input out of `main.c`.
- Core builds without assets and passes `--test-core`.
- **Acceptance:** core test machine passes sequencer, pattern, song, FX,
  mute, punch-in, shortcuts and screenshot tests.

### C3 — 808 branch parity

- Merge `core` into `808`, add the 808 definition and assets.
- Restore every existing 808 feature and port packaging.
- **Acceptance:** byte-identical bounces versus the pre-split 808 build,
  pattern/song compatibility, UI screenshot parity, both ARM builds, device
  smoke test.

### C4 — New machine branches

- After C3, create `909`, `707` and `retro` from the proven `core`, each adding
  only machine data/assets/packaging. See `CLONES_PLAN.md`.
- First new machine validates the abstraction; do not add another until it
  works on hardware.

### C5 — Cleanup

- Decide `master` fate (point it at `808` or keep it release-only).
- Remove dead 808 constants from `core`.
- Document the branch workflow in `README.md` / contributor notes.
- Tag `core` revisions and each machine port release.

## 6. Testing strategy

Core:

- Synthetic 4-track and 16-track machines.
- Pattern save/load, wrong-machine rejection, legacy 808 import.
- Song build/play/export and per-bar FX.
- Swing, ratchets, length, mute, punch-in FX, shortcuts.
- UI screenshots at 11, 12 and 14 rows.
- No asset dependency.

808 branch:

- Pre/post-split bounce byte comparison.
- Existing `test-m6`…`test-m9`, `test-length`, `test-mute`, `test-shortcuts`,
  round-trip, kit and determinism suites.
- Song export and pattern library migration.
- Both ARM cross-builds and device smoke test.

Machine branches:

- Kit family loading and variant selection.
- Choke groups (closed hat cuts open hat).
- Accent gain.
- Pattern/song round-trip and wrong-machine rejection.
- Punch-in/mute/shortcut smoke tests.
- Screenshots and packaging.

## 7. Risks

| Risk | Mitigation |
|---|---|
| UI space for >12 tracks | Auto-fit rows; page above the limit; test 12/14 rows early |
| Pattern compatibility | Legacy magic + `MACHINE` validation; never partial-load a wrong machine |
| Branch divergence | Core-first workflow; merge core down regularly; machine fixes stay local |
| 808 assumptions left in core | Core must build and test with no assets and a synthetic machine |
| Snapshot memory with 24 tracks | `MAX_TRACKS 24`; snapshots are ~0.8 MiB per song set; acceptable |
| Punch-in FX memory/CPU | 16 s ring buffer on the heap; measure on H700 with all FX on |
| Asset licensing | Per-machine `licenses/ASSETS.txt`; confirm before public release |
| `master` confusion | Freeze it until `808` parity; then point it at `808` or make it release-only |

## 8. Open decisions

1. Canonical pattern magic: keep `RG808P1` for all machines, or introduce
   `RGDRUMP1` and accept `RG808P1` as legacy? (Recommend `RGDRUMP1`.)
2. `MAX_TRACKS`: 24 is proposed; confirm after the RETRO asset audit.
3. `master` fate after the split: point at `808`, or keep as a release-only
   pointer.
4. Whether `core` should also carry a minimal silent/demo kit for local
   screenshots, or rely only on the synthetic test machine.
5. Whether the shared UI stays in `main.c` or moves to `ui.c` immediately in C2
   (recommend moving it; the grid/kit geometry is where machine assumptions
   hide).

## 9. Definition of done

- `core` contains no 808-specific names, labels, colours or sample paths.
- `core` builds and passes its headless tests with no real sample assets.
- `808` is byte-identical to the pre-split 808 in bounces and preserves
  pattern/song/library compatibility, mute and punch-in FX.
- `909`, `707` and `retro` build from the same core and pass their machine
  tests and device smoke test.
- The branch workflow and release tagging are documented.
