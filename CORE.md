# RG-808 / RG-SMP shared core

The `core` branch holds the sample-agnostic drum-machine core. Machine
branches merge it and keep only their machine definition, app shell, assets and
port packaging.

**Release 1.0 (2026-11-15):** this shared core backs both RG-SMP 1.0 and
RG-808 1.0.

## Layout

```
src/core/          engine, sequencer, patterns, songs, FX, punch-in FX,
                   MIDI clock + USB-MIDI input, canvas
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
- `--test-midi` / `tools/test-midi.ps1`: the MIDI engine (below) driven by a
  synthetic clock stream, in both shells.

## MIDI clock (v1.1 P4)

`src/core/midi.c` is the engine-first half of MIDI in: a byte-stream parser
(running status, sysex-safe, system real-time anywhere), a clock -> BPM lock,
and the transport. The device input (usbfs reader) lands with P5 and feeds
`midi_feed()`; nothing in the app reads hardware yet.

- **Parser:** channel messages keep running status; `0xF8` clock, `0xFA`
  start, `0xFB` continue and `0xFC` stop are handled anywhere (also inside
  sysex dumps and multi-byte messages); `0xF2` song position sets the start
  position in 16ths (MIDI beats), pending until the next start/continue.
- **Tempo lock:** the gap between clocks (24 per quarter) is smoothed by 0.25
  per clock, so the lock tracks the master within a couple of clocks and
  re-seeds after a stall. The measured `bpm_x100` feeds the engine tempo (FX
  delay sync, tempo header).
- **External-clock mode:** `engine_midi_attach()` points the engine at a
  `MidiClock`. The master then supplies tempo and transport, while **our**
  pattern length and song chain stay ours: every 6th timing clock is one of
  our 16th-step boundaries and fires sample-accurately at the clock's
  timestamp; start/continue position our grid at the pending song position
  (step = position % length, or walked through the song chain by per-bar
  lengths); stop keeps the position so continue-without-SPP resumes. Phase
  keeps counting while stopped, so a local play press against a running
  master lands on the master's 16ths. Punch-in quantization uses clocked
  boundaries exactly like the internal clock. Exports (`--bounce`, song WAV)
  never attach a clock and render on the internal one.
- **Threading:** the input thread pushes timestamped events into an SPSC ring
  (C11 atomics on the indices; clocks never fill the last slots, so a stalled
  consumer cannot bury a START). The audio callback drains it per buffer.

## USB-MIDI device input (v1.1 P5)

`src/core/usbmidi.c` is the app-side half: a Linux usbfs reader that owns the
device side of what `tools/device/usbmidi_probe.py` proved on the T-8.

- **Reader thread** (pthread, guarded to Linux; other builds are a clean
  no-op and the app stays on the internal clock): scans
  `/sys/bus/usb/devices` for any interface with subclass 0x03 (generic class
  01 and Roland vendor class ff both count), claims it with
  `USBDEVFS_CLAIMINTERFACE` and drains its bulk IN endpoint with
  `USBDEVFS_BULK`. Nothing else is claimed - no audio interface, no iso.
- **Attach window is the whole game:** a master whose IN pipe nobody drains
  fills its buffer and gives up (the T-8's "two clocks then silence"). The
  reader polls every 150 ms, so a plug is claimed within a fraction of a
  second of enumeration; reads time out after 200 ms, so an unplug is noticed
  and the rescan starts at once.
- **Feed:** 4-byte USB-MIDI event packets are expanded to a MIDI byte stream
  and pushed into `midi_feed()` timestamped on the engine's sample clock
  (a torn 64-bit `total_pos` read on 32-bit builds is clamped).
- **App wiring:** both shells create the `MidiClock`, attach it and toggle
  `eng.ext_clock` with `usbmidi_attached()` each frame - master tempo and
  transport while a device is there, internal clock when not. While the clock
  drives the engine the header hides our tempo (it carries the master's
  measured value and shows a red `MIDI` chip instead); the tempo the user had
  before the sync is restored on detach, and the tempo editor's left/right
  steps the hundredths (`up/down` = 0.5, `L/R` = 0.01) so any leftover value
  is reachable. A tempo-synced delay is bypassed outright in external-clock
  mode - the master's measured tempo wobbles with the lock and so would the
  delay time; the setting stays and returns (with a cleared line) on detach.
  Exports never attach a clock.
- **Checks:** `--test-usbmidi` / `tools/test-usbmidi.ps1` cover descriptor
  parsing, endpoint selection in both T-8 personalities, packet expansion and
  the feed-through into the P4 clock; the usbfs reader itself was validated
  on the device with the probe (`logs/usbmidi_probe_5.txt`).

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

## Slice page (P3, pattern-wide cuts)

The slice page (L1+B) edits one **start/end pair per track**, in 0.01 % units -
every step of the pattern plays that slice, so programming a cut is a single
edit rather than one per step. Pitch stays per track (`PIT` lines) and is
independent of the cut.

- Pattern files carry `TRM <label> <start> <end>`; lines are written only for
  tracks that actually cut their sample. The legacy per-step format
  (`TRM <label> <16 starts> <16 ends>`) still loads and migrates to the first
  pair that cut the sample.
- On the page, left/right or A switch between START and END, up/down nudges
  (0.01 % per tap, accelerating while held), X hears the track's slice and B
  leaves. The waveform strip highlights the cut.

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
