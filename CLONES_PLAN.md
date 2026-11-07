# RG-808 Clone Machines Plan

**Status: planning only (2026-11-07).** This document covers M13 in `PLAN.md`:
building other drum machines on the sample-agnostic `core` described in
`CORE_PLAN.md`.

Working names:

- **RD-909** — Roland TR-909 style
- **RD-707** — Roland TR-707 style
- **RD-RETRO** — combined Linn LM-1 / Oberheim DMX / E-mu Drumulator style

Each is a separate machine branch and a separate PortMaster port:

```
core
 ├── 808      (existing RG-808)
 ├── 909      (RD-909)
 ├── 707      (RD-707)
 └── retro    (RD-RETRO)
```

## 1. Shared decisions

These apply to all machines:

- **Fixed one-shot samples**, loaded as `<sample_id>NN.wav` variant families.
- **Accent is gain-only**, as in the current 808 engine. No per-voice tune,
  decay, pan, filter or synthesis modelling in this plan.
- Same 16-step sequencer, per-step accents, swing, ratchets/flams, pattern
  length, 200-slot pattern library, 512-bar song chains, WAV export, master FX
  chain, performance mute and punch-in FX.
- Same shortcuts and UI conventions; only labels, colours, gains, sample
  families and track count come from the machine definition.
- **Choke groups** will be a core feature (added during M12): firing a voice in
  a group cuts ringing voices in the same group. Primary use: closed hi-hat
  cuts open hi-hat on the 808/909/707. This is not per-voice synthesis, just
  voice stealing.
- No mixed-machine pattern or song libraries. Each port has its own `conf/` and
  validates the `MACHINE` id.
- Each machine branch owns its assets, licence notes, port metadata,
  screenshots and README.
- Sample-pack licensing must be confirmed before any public release, just like
  the current 808 pack.

## 2. RD-909

### Machine

TR-909-style, 11 tracks — fits the current 11-row grid without a layout
change.

| # | Label | Voice | Choke |
|--|--|--|--|
| 1 | BD | Bass drum | — |
| 2 | SD | Snare drum | — |
| 3 | LT | Low tom | — |
| 4 | MT | Mid tom | — |
| 5 | HT | High tom | — |
| 6 | CH | Closed hi-hat | 1 |
| 7 | OH | Open hi-hat | 1 |
| 8 | CR | Crash cymbal | — |
| 9 | RD | Ride cymbal | — |
| 10 | HC | Hand clap | — |
| 11 | RS | Rim shot | — |

- Choke group 1 = CH/OH: closed hat cuts the open hat.
- Accent uses the existing `LVL_ACC` gain path.
- Swing, ratchets and flams are core features and work unchanged.
- Sample families: `<id>NN.wav` with as many variants as the pack provides.
  Kit mode selects variants per voice exactly as on the 808.
- Colours/labels should echo the 909 panel but stay readable on the 640×480
  screen.
- Port name proposal: `RD909` / `rd909` folder, `RD909.sh`, `dist/rd909.zip`.

### UI/build

- No grid resize needed (11 rows).
- Add a machine header/name and 909 colours.
- Pattern/song files write `MACHINE 909`.
- A 909 pattern must be rejected by the 808/707/RETRO ports with a clear
  "WRONG MACHINE" message.

### Assets

- A complete 909 one-shot pack with all 11 voices and any useful variants
  (e.g. accented hits, alternate snares/toms if the pack has them).
- `assets/wav/` naming follows the machine definition (`bd01.wav`,
  `sd01.wav`, …).
- `port/rd909/licenses/ASSETS.txt` with source and licence terms.

## 3. RD-707

### Machine

TR-707-style, 12 tracks — needs the core's compact 12-row layout (or page
support), but otherwise fits.

| # | Label | Voice | Choke |
|--|--|--|--|
| 1 | BD | Bass drum | — |
| 2 | SD | Snare drum | — |
| 3 | LT | Low tom | — |
| 4 | MT | Mid tom | — |
| 5 | HT | High tom | — |
| 6 | CH | Closed hi-hat | 1 |
| 7 | OH | Open hi-hat | 1 |
| 8 | CR | Crash cymbal | — |
| 9 | RD | Ride cymbal | — |
| 10 | CB | Cowbell | — |
| 11 | TA | Tambourine | — |
| 12 | CP | Hand clap | — |

- Choke group 1 = CH/OH.
- Accent is gain-only; no separate accent timbre.
- Kit mode selects sample variants as usual.
- Port name proposal: `RD707` / `rd707` folder, `RD707.sh`, `dist/rd707.zip`.

### UI/build

- Use the core's auto-fit or compact 12-row grid; verify the step cells remain
  comfortably pressable and the status bar/legend still fits.
- If the core auto-fit is not good enough, add a 12-row layout preset rather
  than machine-specific code in the UI.
- Pattern/song files write `MACHINE 707`.

### Assets

- A complete 707 one-shot pack for all 12 voices, with variants where
  available.
- Confirm the voice list against the actual sample pack before freezing the
  machine definition; the table above is the working target.
- `port/rd707/licenses/ASSETS.txt`.

## 4. RD-RETRO

### Concept

One combined machine built from Linn LM-1, Oberheim DMX and E-mu Drumulator
samples. These machines have relatively few samples each, so they share one
machine and one grid rather than becoming three ports.

The existing **kit mode** is the combining mechanism: each track exposes the
matching sample variants from all three source machines, and the user picks the
variant per instrument. This avoids doubling the track count just to expose
three versions of every drum.

### Proposed machine

Working target: **12–14 tracks**, depending on the asset audit. A provisional
list:

| # | Label | Voice | Variants from |
|--|--|--|--|
| 1 | BD | Bass drum | LM-1, DMX, Drumulator |
| 2 | SD | Snare drum | LM-1, DMX, Drumulator |
| 3 | RS | Rim / sidestick | LM-1, DMX, Drumulator |
| 4 | CH | Closed hi-hat | LM-1, DMX, Drumulator |
| 5 | OH | Open hi-hat | LM-1, DMX, Drumulator |
| 6 | CR | Crash cymbal | LM-1, DMX, Drumulator |
| 7 | RD | Ride cymbal | LM-1, DMX, Drumulator |
| 8 | LT | Low tom | LM-1, DMX, Drumulator |
| 9 | MT | Mid tom | LM-1, DMX, Drumulator |
| 10 | HT | High tom | LM-1, DMX, Drumulator |
| 11 | CP | Hand clap | LM-1, DMX, Drumulator |
| 12 | CB | Cowbell | LM-1, DMX, Drumulator |
| 13 | TA | Tambourine | LM-1, DMX, Drumulator |
| 14 | SH | Shaker / cabasa | LM-1, DMX, Drumulator |

Open points:

- LM-1/DMX/Drumulator kits do not all contain every voice. Tracks with no
  source sample are either omitted or filled with the closest source; decide
  after the asset audit.
- Some machines have congas or additional percussion. If the packs contain
  them, consider replacing SH or adding one or two tracks within the 14-track
  compact layout.
- Choke group 1 = CH/OH.
- If the final count is 12–14, the core's compact grid fits one page. More than
  that needs paging.

### Assets

- Three one-shot collections, one per source machine, mapped into shared
  families:
  ```
  bd01..bd0N  = LM-1 / DMX / Drumulator bass drums
  sd01..sd0N  = snares
  ch01..ch0N  = closed hats
  ...
  ```
- Preserve source order and document the mapping in
  `port/rdretro/licenses/ASSETS.txt` and the kit README.
- Decide whether variants are interleaved by source (LM-1, DMX, Drumulator) or
  grouped; interleaving makes kit mode easier to audition by source.
- Port name proposal: `RDRETRO` / `rdretro` folder, `RDRETRO.sh`,
  `dist/rdretro.zip`.

## 5. Common implementation checklist

For each machine branch:

- [ ] `src/machine/machine_data.c` with the `MachineDef`/`InstrumentDef` table.
- [ ] `MACHINE <id>` in pattern/song files; wrong-machine rejection.
- [ ] Assets in `assets/wav/` with the documented family/variant mapping.
- [ ] `tools/prepare-assets.ps1` mapping for the pack (or a machine-specific
      copy of the family arrays).
- [ ] Choke group(s) configured.
- [ ] UI labels, colours, gains and default variants.
- [ ] Port folder, launcher, `port.json`, `gameinfo.xml`, screenshots, README,
      licence notes.
- [ ] Tests: kit loading, variant selection, choke, accent gain, pattern/song
      round-trip, mute, punch-in FX, length/swing/ratchets, bounce
      determinism.
- [ ] Both ARM cross-builds and a device smoke test.
- [ ] `dist/<machine>.zip` and install test.

## 6. UI/layout strategy

- 909: 11 rows, current geometry.
- 707: 12 rows, compact/auto-fit geometry.
- RETRO: 12–14 rows, compact geometry; paging only if the audit forces more.
- The core owns row fitting; machine branches supply `track_count` and labels,
  not bespoke drawing code.
- Kit mode must handle a row's variant family from one source machine or a
  combined RETRO family without changing the control scheme.
- Keep the 640×480 4:3 target and the existing D-pad/ABXY/L/R controls.
- Punch-in FX and mute overlays are machine-agnostic and must remain legible
  on every machine.

## 7. Assets and licensing

This is the main non-code prerequisite.

- Each machine needs a sample pack with clear redistribution terms.
- Keep a `licenses/ASSETS.txt` per port with source, author, licence and any
  required attribution.
- The current 808 pack licence is already flagged as unresolved for public
  release; do not repeat that mistake for 909/707/RETRO.
- If a pack cannot be redistributed, the port can still be personal/private,
  but not published to PortMaster.
- Confirm whether packs include accented hits. If not, accent remains gain-only
  (already decided) and no accented variants are needed.

## 8. Branch/build/release flow

1. `909`, `707` and `retro` branch from a proven `core` tag.
2. Each branch adds only machine data, assets and packaging.
3. Merge `core` updates down regularly; keep machine changes out of `core`.
4. Build each port from its own branch with the shared Docker/CMake tooling.
5. Keep separate `conf/` folders on the device: mixing 808/909/707/RETRO
   patterns is not supported.
6. Tag releases per port (`rd909-v0.1`, etc.) and record the core revision.

## 9. Milestones

### P0 — Core and 808 parity

- Complete M12 / C1–C3 in `CORE_PLAN.md`.
- Do not start a new machine until the 808 branch is byte-identical and passes
  on PC and both ARM builds.

### P1 — RD-909

- Machine definition, 909 pack, 11-row UI, choke, tests, packaging.
- First proof that the core abstraction works for a non-808 machine.
- Device smoke test before starting 707.

### P2 — RD-707

- 12-track compact layout, 707 pack, choke, tests, packaging.
- Validate that the core layout scales without machine-specific UI hacks.

### P3 — RD-RETRO

- Audit the three sample packs; freeze the track list and variant mapping.
- 12–14-track compact layout, combined variant families, choke, tests,
  packaging.
- This is the largest asset/content effort, not the largest code effort.

### P4 — Device/packaging polish

- CPU spot-checks with all FX and punch-in active.
- Screenshots, README/licences, PortMaster metadata, install tests.
- Optional upstream PortMaster submission per machine, with the required
  testing thread and multi-CFW checks.

## 10. Acceptance tests per machine

- Kit assets load; every track has at least one sample; missing assets are
  reported clearly.
- Every variant family is selectable and auditions correctly.
- Accent step is louder than a normal step (gain-only).
- Choke group behaves (closed hat cuts open hat).
- Pattern save/load round-trip and `MACHINE` validation.
- Song chain save/load/export; per-bar BPM/swing/length/kit/FX.
- Swing, ratchets/flams, pattern length, mute and punch-in FX.
- Bounce output changes when notes/kit/FX change, and is deterministic.
- UI screenshot at the machine's track count; no overlapping labels.
- Both ARM binaries run on the SP; controls and audio verified.

## 11. Open decisions

1. **RD-909 sample pack**: which pack, and does it cover all 11 voices with
   useful variants?
2. **RD-707 voice list**: confirm the 12 voices against the chosen pack
   (especially whether it has a rim shot or tambourine variant).
3. **RD-RETRO track list**: final 12–14 track selection and per-track variant
   order after auditing LM-1/DMX/Drumulator packs.
4. **Naming**: keep `RD-` for the clones and `RG-808` for the current port, or
   rename the 808 to `RD-808` for consistency?
5. **Sample licensing**: which packs are cleared for public redistribution?
6. **Accented samples**: if a pack includes them, do we expose them as kit
   variants or keep accent strictly gain-only? (Current decision: gain-only.)
7. **Punch-in FX scope**: decided — M11 is shared core and is present in every
   machine port from day one.

## 12. Definition of done

- `core` + `808` are stable and parity-tested.
- RD-909, RD-707 and RD-RETRO each build from `core` with only machine data,
  assets and packaging added.
- Each machine passes the acceptance tests above and runs on the device.
- Pattern/song libraries are per machine and reject wrong-machine files.
- Assets and licences are documented; public-release eligibility is known.
