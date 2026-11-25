# RG-808 Development Plan

**Current version: 1.0** (2026-11-15). M1â€“M11 are implemented (including the
M12 shared-core split), both ARM cross-builds pass the automated suites, and
RG-808/RG-SMP 1.0 were validated on a physical RG35XX SP.

Roland TR-808 clone for the Anbernic RG35XX SP (muOS / PortMaster).
1â€“16-step sequencer, single pattern, 11 classic voices, per-step accents.

## Release 1.0 (2026-11-15)

RG-808 and RG-SMP ship together as version 1.0. Both use the shared `core`
branch and the same C11/SDL2 stack. The 1.0 release includes the full M1â€“M11
feature set: the pattern library, song chains with timestamped WAV export,
per-pattern effects, swing/ratchets/step levels, per-track mute, R1 punch-in FX
and the L2+Down export shortcut. Both systems were exercised on a physical
RG35XX SP before release.

## Application stack (decided 2026-11-06)

| Layer | Choice | Notes |
|---|---|---|
| Language | C11, CMake + Ninja | MinGW on Windows for dev, gcc-12 cross in Docker for release |
| Video/input | SDL2 (2.32 headers on PC, Debian 2.26 on cross, device runtime SDL2) | software canvas â†’ one streaming texture; works on fbcon/KMS |
| Audio | SDL2 audio callback + hand-written 48-voice mixer | sequencer clock inside the callback = sample-accurate steps |
| Font | font8x8 (public domain, vendored) | no SDL2_ttf dependency |
| Persistence | plain-text `conf/pattern.txt` + 5 s autosave | PortMaster `conf/` folder convention |
| Packaging | PortMaster "new port structure": `RG808.sh` + `rg808/` in a zip | `port.json` v4, dual arch `aarch64` + `armhf` (`${DEVICE_ARCH}`) |
| Kit | variant families from the "Roland TR-808" pack shipped as `<id>NN.wav` (bd/sd 16, toms 7, ch 5, oh 3, cy 6, cp 3, cl/cb 2 â€” 74 files) | selectable per voice in kit mode; mapping in `tools/prepare-assets.ps1`, defaults in `src/engine.c` |

Key device facts: RG35XX SP = Allwinner H700 (4Ã— Cortex-A53), 1 GB RAM,
640Ã—480 screen, D-pad + ABXY + L/R + START/SELECT, no Wi-Fi (offline installs
matter). muOS integrates PortMaster; ports live in `<card>/ports/<name>/`,
launch shims in `<roms>/PORTS/`, port logs in `ports/rg808/log.txt`.

## Milestones

- [x] **M0 â€” Toolchain & device probe** *(toolchain and device probe complete for 1.0)*
  - [x] Windows native toolchain: MinGW + CMake + vendored SDL2/MinGW + font8x8 fetch script
  - [x] Docker cross-build (`tools/portmaster-build`): `rg808.aarch64` + `rg808.armhf`, zip assembly inside container
  - [x] `tools/device/probe.sh` written (kernel, arch, glibc, SDL2, ALSA, paths)
  - [x] Run probe on the device, file back the output â†’ pin glibc + SDL2 + storage paths
- [x] **M1 â€” Engine (PC)** â€” WAVâ†’float loader w/ resample; voice mixer; sample-accurate
  16-step clock; accents (off/on/accent gain); BPM 4â€“250; `--bounce out.wav` offline
  render verified (timing math checked at 90.5 and 120 BPM; soft-clip curve in place).
- [x] **M2 â€” UI (PC)** â€” 640Ã—480 canvas UI: track labels + 16-cell grid, cursor, LED
  playhead row, beat markers, tempo header, status bar, MENU + TEMPO + KIT modes,
  on/off/accent visual hierarchy. Verified via `--screenshot` (see
  `port/rg808/screenshot.png`) and a stable live run.
- [x] **M3 â€” Device port & parity** *(validated on SP for 1.0)*
  - [x] Dual-arch binaries in zip; `RG808.sh` launch script follows the PortMaster
    template (control.txt, get_controls, LD_LIBRARY_PATH `libs.${DEVICE_ARCH}`,
    gptokeyb kill-daemon, `pm_platform_helper`/`pm_finish`) **and** degrades
    gracefully to a manual install without PortMaster
  - [x] `tools/device/install.sh` + `tools/deploy.ps1` (SSH/SFTP path)
  - [x] First real-device launch (2026-11-06): app runs â€” video + sound + install
    path confirmed working end-to-end
  - [x] D-pad double-fire fixed (SDL emitted controller-button AND raw-hat events
    for the same press; raw joystick events now suppressed for mapped controllers,
    mapping is checked up front for dpad buttons) â€” validated on device
  - [x] Controls retested; audio latency/cleanliness and A/B/X/Y/START/SELECT/L/R
    mappings validated on device in 1.0
  - [x] Audio period size / `samples` validated on hardware; no retune needed for 1.0
- [x] **M4 â€” Persistence & polish** â€” save/load `pattern.txt` round-trip verified;
  kit selections persisted as `KIT <track> <n>` lines (old kit-less files load with
  defaults â€” confirmed against a real save); autosave (5 s after edits) + save on
  quit; built-in demo groove; master gain + soft-clip. Kit audio path verified by
  `tools/test-kits.ps1` (same pattern, variant 3 vs 12 â†’ different bounce output).
- [x] **M5 â€” Packaging** *(1.0 package shipped)*
  - [x] `dist/rg808.zip` â‰ˆ 3.0 MB: launcher + binaries + 74-variant kit + licenses +
    README + gameinfo.xml + screenshot; layout matches `port.json` `items: ["RG808.sh","rg808"]`
  - [x] Reproducible build: `docker build -t rg808build tools/portmaster-build` then
    `docker run --rm -v ${PWD}:/src rg808build`; Windows fallback `tools/package-port.ps1`
  - [ ] Replace screenshot with a real device capture (4:3 640Ã—480)
  - [ ] Confirm sample-pack licence terms before any public release (`port/rg808/licenses/ASSETS.txt`)
  - [ ] Optional: upstream PR to PortsMaster/PortMaster-New (requires their Discord
    testing thread + multi-CFW testing; not needed for personal use)

## v2 feature milestones (grouped from `todo.md`)

Ordered so each layer builds on a stable base: step model â†’ pattern storage â†’
song assembly â†’ output DSP. M6 and M9 are independent; M8 requires M7; M9.5
extends M9; M11 is the performance FX layer.

- [x] **M6 â€” Timing & feel** *(swing + ratchets; validated on device in 1.0)*
  - [x] **Swing/shuffle**: one global amount (0â€“100 % of a 16th), delays every even
    16th step; applied in the clock math (`next_step += sps` becomes
    alternate long/short). UI: menu â†’ SWING value page (TEMPO-style editor).
    Implementation note: hit offset via voice delays; boundaries stay on the
    straight grid (loop length unchanged, verified by bounce byte size).
  - [x] **Ratchets**: per-step repeat mode â€” 1/2 (2 hits), 1/3 (3 hits), 1/4 (4 hits),
    and flam (2 hits, second at ~14 ms offset, quieter). Third step dimension
    `unsigned char rch[N_TRACKS][N_STEPS]` + sub-hit scheduling (`fire_sched`);
    roll hits at full level (decay shaping = later polish). MAX_VOICES raised 48â†’64.
  - [x] UI: RATCHET overlay mode (menu â†’ RATCHET NOTES; 16 columns = steps, digit
    drawn inside set cells in both edit and ratchet views).
  - [x] Persistence: `RCH <label> <16 chars 0..4>` lines + `SWING <n>`; old files
    default to none (backward compatible, same trick as KIT lines).
  - [x] Verified by `tools/test-m6.ps1` (ratchet/swing change bounce audio, length
    preserved) + `--mode rch` screenshot. During testing **fixed a real bug**:
    uninitialized pad bytes in the WAV header made `--bounce` files
    non-deterministic (only affects export tooling, not live playback).
  - [x] Device CPU spot-check at 250 BPM with 4Ã— rolls on all 11 tracks passed in 1.0.
- [x] **M7 â€” Pattern library** *(todo: save up to 200 patterns)*
  - [x] Storage: `conf/patterns/NNN.txt` (same file format per pattern) +
    `conf/patterns/index.txt` (slot â†’ name, plus the active slot); atomic
    writes (temp + fsync + rename) against SD power-loss.
  - [x] Import migration of the legacy single `pattern.txt` into slot 001 on
    first run (only when the library has no pattern files; later edits to the
    legacy file never overwrite slot 001).
  - [x] UI: PATTERN browser â€” numbered list with names, 12 slots/page;
    load (A), SELECT actions (SAVE HERE, COPY ACTIVE HERE, RENAME, DELETE),
    D-pad letter-picker rename (16 chars), active-slot marker in the header.
  - [x] Autosave retargets to the active slot; slot number + name shown in
    the header.
  - [x] Verified by `tools/test-m7.ps1` (migration, no re-import, save/reload
    bit-identical, index active/name, no `.tmp` leftovers) plus browser and
    rename screenshots; existing M6/kit/round-trip tests still pass.
- [x] **M8 â€” Song mode** *(todo: chain up to 512 patterns; save song as WAV) â€” requires M7*
  - [x] Song data: ordered chain of pattern slots (1 bar per entry, up to 512
    entries) in `conf/song.txt`; per-pattern BPM/swing/length/kit/ratchets
    apply on each bar switch.
  - [x] Engine: immutable `SongSet` snapshot arrays prepared by the UI thread
    (two buffers), audio thread advances the snapshot pointer at every bar
    boundary; per-bar BPM recomputed for the next step so switches are gapless.
  - [x] UI: SONG page â€” bar list with pattern names, picker, insert/delete/
    move, play-from-here, loop toggle, jump-to-playing-bar, save/clear.
  - [x] **WAV export**: streaming renderer writes chunks straight to disk
    (header patched at close), progress bar, 44.1 kHz/16 stereo, one pass plus
    a two-second tail.
  - [x] Verified by `tools/test-m8.ps1` (persistence, snapshot BPMs, song
    length math, non-loop end, loop wrap, UI actions, 250 BPM bar switch,
    first-bar bit-exact vs a normal pattern bounce) plus song/action/export
    screenshots.
- [x] **M8.5 â€” Pattern length** *(pre-M9; per-pattern 1â€“16 steps)*
  - [x] Engine: `length` in the live engine and in every `PatternSnapshot`;
    the 16th clock wraps on the pattern length, and song bars advance on
    each bar's own length. Bounce length math uses it too.
  - [x] Persistence: `LEN <n>` line per pattern, clamped to 1â€“16; files from
    older builds (no `LEN`) load as 16 steps.
  - [x] UI: menu â†’ PATTERN LENGTH (also L1+Up), up/down Â±1 and left/right Â±4;
    steps past the end are shown dimmed with an end marker, and the edit
    cursor wraps inside the active length. Header shows `LEN nn` (and the
    current song bar's length while playing a song).
  - [x] Verified by `tools/test-length.ps1` / `--test-length` (round-trip,
    legacy default, clamps, live clock wrap, per-bar song length/end) and
    bounce/song-export frame counts for LEN 1/4/8/16 plus the length-editor
    screenshot.
- [x] **M9 â€” Effects** *(todo: compression/overdrive/bitcrusher â€” all selectable;
  flanger/phaser â€” both selectable; delay; reverb) â€” independent of M6â€“M8*
  - [x] Scope: master chain
    `drive â†’ modulation â†’ delay â†’ reverb â†’ master` (fixed order, per-stage
    on/off + params), float.
  - [x] Drive slot is selectable: bitcrusher (4â€“16 bits + 1â€“32Ã— sample-and-hold),
    overdrive (tanh drive/mix), or compression (stereo-linked peak compressor
    with threshold/ratio/makeup).
  - [x] Modulation slot is selectable: flanger (LFO 1-tap delay) or phaser
    (6-stage modulated allpass with a midrange coefficient sweep and feedback
    gain compensation), both with rate/depth/feedback/mix. The phaser was
    retuned after listening feedback: the coefficient sweeps the audible
    midrange and the wet path is compensated for feedback boost, so max
    settings no longer turn into a clipped overdrive.
  - [x] Delay: stereo circular buffer, 20â€“4000 ms free-running or tempo-synced
    to 1/4, 1/8, 1/16, 1/4T, 1/8T, 1/16T, 4D, 8D and 16D. Sync follows the live
    tempo or the current song bar's BPM.
  - [x] Reverb: 4 damped parallel combs + 2 series allpasses (room/damp/mix).
  - [x] Per-voice sends remain out of scope.
  - [x] UI: menu â†’ EFFECTS shows only the rows relevant to the selected drive/
    modulation algorithms; A toggles master/stages, left/right adjusts the
    selected value, B returns.
  - [x] Real-time safety: effect buffers are heap-allocated per engine; with
    master bypass off no effect code touches the mix, so bypass stays
    bit-identical to the pre-M9 render.
  - [x] Verified by `tools/test-m9.ps1` / `--test-fx` (defaults, clamps,
    persistence, bit-identical bypass/all-off, every drive/modulation algorithm,
    each stage changes audio, all nine delay divisions resolve correctly) plus
    the FX page screenshot. Device CPU spot-check at 128-frame periods with all
    stages on passed in 1.0.
- [x] **M9.5 â€” Per-pattern effects** *(follow-up to M9)*
  - [x] Data: `FxSettings` now lives in `PatternSnapshot`; each pattern file
    carries `FX ...` lines; `song_build_set()` carries them per bar. The engine
    applies the incoming snapshot's settings at every bar boundary and on a
    song restart.
  - [x] Runtime: **one shared effect state per Engine**, not one state per
    pattern. Parameter changes at bar switches are just a few integer writes;
    per-sample CPU is unchanged. At 250 BPM a 1-step bar lasts 60 ms, so even
    the fastest song only updates a handful of values a few times per second.
  - [x] Memory: the shared `FxState` is ~3.1 MiB per engine (mostly the 8 s
    stereo delay). Per-pattern state would cost ~620 MiB for 200 patterns and
    ~1.6 GiB for 512 song bars (doubled by the two song snapshot buffers), so it
    is explicitly rejected.
  - [x] Tails: shared delay/reverb tails continue across bar changes. Stage and
    master toggles are hard switches for now; ramps and an optional two-slot
    crossfade (~6.2 MiB) remain possible later if truly independent tails are
    wanted.
  - [x] Persistence/UI: the EFFECTS page edits the active pattern's chain and
    the normal pattern autosave stores it. `conf/fx.txt` is the fallback for
    old patterns without FX lines. Loading a slot restores its chain; copying a
    pattern copies its effects.
  - [x] Verified by `tools/test-m9.ps1` / `--test-fx` (pattern FX round-trip,
    legacy fallback, per-bar song FX, per-pattern bounce differences, synced
    delay timing, bit-identical bypass). Device CPU spot-check passed in 1.0.
- [x] **M10 â€” Performance mute** *(part of the old per-track volume/mute
  backlog; per-track volume remains open)*
  - [x] Engine: `mute[N_TRACKS]` global performance state, checked by the live
    and song step schedulers; `mute_kill_mask` lets the audio thread cut voices
    that are already ringing when a track is muted. Preview stays audible.
  - [x] UI: Y toggles the selected track in the sequencer (X remains HEAR);
    muted rows are tinted and marked with a red `M`; the status bar shows
    `Y:MUTE`. Y keeps auditioning in kit/ratchet mode and keeps its old
    behaviour elsewhere.
  - [x] Mutes are deliberately not persisted (performance state): they survive
    pattern loads and song bars but reset on restart. Since v1.1 they also
    reach the WAV export: the export engine inherits the live mute state, so a
    muted track stays muted in the bounce, and the export confirmation page can
    include the muted tracks on request.
  - [x] Verified by `tools/test-mute.ps1` / `--test-mute` (mute silences live and
    song playback, cuts ringing voices, preview still sounds, UI toggle and KIT
    audition alias, the export keeps muted tracks silent) plus a `--mode mute`
    screenshot; M6â€“M9/length/shortcut suites still pass.
- [x] **M11 â€” Performance punch-in FX** *(todo: MPC/Gross Beat-style held
  effects on R1)*
  - [x] **M11.0 â€” Free R1 first.** Ratchet moved from `R1 + Right` to
    `L1 + Down`; kit moved from `R1 + Left` to `L2 + Up`. Shortcut map, hint
    overlays, tests, README and port README updated. R1 is now reserved for
    the punch-in layer.
  - [x] **Engine/audio:** `PerfFxState` (heap-allocated, like `FxState`) runs
    **after the master FX chain and before master gain/soft-clip**, so
    stutters and rewinds capture the full mix including delay/reverb tails.
    The stage is bypassed by `--bounce` and song WAV export because those
    engines never request an effect; idle processing is bit-transparent.
  - [x] **Capture buffer:** continuously written stereo float ring buffer,
    **16 s** at the engine rate (5.4 MiB at 44.1 kHz) plus a 16 s frozen-slice
    buffer. Slices are copied at trigger time; live audio keeps being
    captured while the effect is active.
  - [x] **Effects:**
    - 1/4, 1/8, 1/16 and 1/32 **stutter loops** from the captured mix;
      division length comes from the tempo at trigger (live or current song
      bar).
    - **1/4 rewind loop**: the 1/4 slice played backwards with linear
      interpolation.
    - **1/16 gate**: smoothed square volume gate at 1/16.
    - **Tape stop**: the live ring is read at a rate ramping 1.0 â†’ 0 over
      **one 1/8 note at the trigger tempo** (250 ms at 120 BPM), with
      interpolation, a 6 kHz low-pass and a level fade to silence.
    - **Phone-like sound**: RBJ resonant band-pass at 1.2 kHz, Q = 2.5.
  - [x] **Trigger semantics:**
    - Hold `R1` and press a mapped direction/button. The effect engages on the
      **next 16th-step boundary** in live or song playback; while stopped it
      engages immediately for auditioning.
    - Exactly **one effect at a time**; further triggers are ignored until the
      active one is released.
    - Release crossfades back to live audio over 8 ms; start uses a 5 ms
      fade-in.
  - [x] **Mapping:**
    | Hold R1 + | Effect |
    |--|--|
    | Left | 1/4 loop |
    | Right | 1/8 loop |
    | Up | 1/16 loop |
    | Down | 1/32 loop |
    | A | 1/4 rewind loop |
    | B | 1/16 gate |
    | X | tape stop |
    | Y | phone filter |
  - [x] **Input/UI:** while R1 is held, A/B/X/Y and the D-pad are intercepted
    before the normal actions, so `R1+Y` does not mute, `R1+X` does not
    audition and no shortcut/menu opens. The overlay is laid out like the
    device: a D-pad on the left and the SP face-button diamond on the right
    (X top, A right, B bottom, Y left), highlighting the held/pending effect.
    Controller, raw joystick and keyboard paths all release the effect on
    key-up.
  - [x] **Interactions:** mutes still gate new live hits; a stutter that
    captured a now-muted instrument keeps looping that slice until release.
    Punch-ins are not persisted and do not affect exports.
  - [x] **Verification:** `--test-punch` / `tools/test-punch.ps1` cover the
    idle bypass, quantized start, one-at-a-time lockout, pending cancel,
    1/4 loop period, rewind direction, gate open/close, tape-stop fade,
    phone DC rejection, release crossfade and song-mode start, plus the input
    layer (R1+A, R1+Y does not mute, R1+Left). `--test-shortcuts` covers the
    new L1+Down/L2+Up map. R1 overlay screenshot rendered.
  - [x] **Risks / device check:** ~11.3 MiB extra heap per engine (ring +
    slice), click-free crossfades, and CPU headroom on the H700 with master FX
    plus punch-in active. Device spot-check passed in 1.0.
- [x] **M12 - Shared core** *(core branch; see `CORE.md`)*
  - [x] `src/core/` owns the sample-agnostic engine, pattern library, song
    chain, master FX, punch-in FX, canvas and WAV loader.
  - [x] `src/machines/808/` and `src/machines/smp/` provide only the track
    table, sample model/browser, app shell and branding.
  - [x] The engine plays resolved sample pointers; `PatternSnapshot` carries
    both the 808 `kit[]` and the SMP `sample_path[]`, resolved by a machine
    hook. `KIT` and `SMP` pattern lines are machine hooks too.
  - [x] One tree builds both machines with `-DRG_MACHINE=808|smp`.
  - [x] Verified: 808 bounce output is byte-identical to the pre-split build
    (with and without `KIT`/FX lines); all 808 suites and the SMP sampler,
    punch, mute, length and FX suites pass on PC.
  - [x] Both machine builds and their suites pass; the 1.0 device builds were
    validated on a physical RG35XX SP.
  - [ ] Post-1.0: add a core headless synthetic-machine test.

## v1.1 roadmap

Derived from the v1.1 roadmap in `todo.md`. Every item is a **core** change:
the shared engine, persistence, DSP and the new pages live in `src/core/`
(the new pages in `src/core/views/`), and the RG-808 / RG-SMP shells only wire
the shortcuts and the machine-specific labels. Phases land on `core` first,
then merge into `master` (RG-808) and `sampler` (RG-SMP).

**Decisions taken (2026-11-24)**

- **Pitch is per pattern**, stored as `PIT` lines exactly like the 808's `KIT`
  lines, so every song bar can carry its own pitch. The shift is rendered
  **offline** (phase vocoder) when the value changes and cached by
  (sample, semitones): playback costs nothing and the audio thread never
  touches the stretcher.
- **Sample start/end is programmed per sequencer step** (16 slices per track)
  in two-place decimal percentages, matching the project's own use of "step"
  for sequencer steps. The untouched track is the "no slice" case.
- **Muted tracks stay muted in an export.** The export engine inherits the live
  mute state; the confirmation page names the excluded tracks and can include
  them on request.
- **MIDI ships engine-first.** The parser, tempo lock, transport and external
  clock land with a synthetic-stream test, independent of the transport.
  Device input needs two things: the RG35XX acting as a **USB host** (proven
  2026-11-25) and a kernel with a USB-MIDI driver (missing today - the probe
  finds no rawmidi node). The chosen cable is USB-C <-> USB-C to a Roland
  S-1 / T-8; see P5.

### Phases

- [x] **P0 ΓÇö Groundwork**
  - [x] Port the L2+Down export confirmation from the machine branches onto
    `core`. It was implemented on `master`/`sampler` only, so the next merge
    into `master` would have dropped it.
  - [x] Fix `tools/portmaster-build/build.sh`: it still listed the pre-split
    source layout, so the Docker release build has been broken since M12. The
    script now builds both machines from the current layout (`MACHINE=smp` /
    `MACHINE=808`), and `tools/package-port.ps1` follows.
  - [x] Extend `tools/device/probe.sh` with the USB / ALSA-MIDI checks that
    gate the MIDI device phase.
  - [x] Add the `src/core/views/` seam: shared view context, action
    vocabulary, shortcut-label table and export panel, so the v1.1 pages are
    written once instead of twice. The shoulder hint is shared too: every
    shoulder draws the same panel (D-pad left, face diamond right), and a
    shoulder + face combination is one row in a label table - so the next
    face-button shortcut needs no layout work.
  - [x] `tools/test-machines.ps1` runs the automated suites for **both**
    shells. The 808 suites were never run on `core` before: the pattern-based
    scripts use the RG808P1 format and silently degraded against the sampler
    build, and the 808 `--test-mute` had a latent failure. Every
    `tools/test-*.ps1` now takes an `-Exe` parameter, and the chunk-size
    comparison in `--test-song` compares with a tolerance instead of
    `memcmp` (the voice pool frees its slots at chunk ends, so the summation
    order - and therefore the last bits - can differ).
- [x] **P1 ΓÇö Muted tracks in exports**
  - [x] Core: `engine_copy_mute()` / `engine_muted_count()`.
  - [x] Song export, `--song` and `--bounce` honour the mutes.
  - [x] Export confirmation lists the excluded tracks and can include them (Y).
  - [x] `--test-mute` covers the export path; README / CORE / port README /
    PLAN updated (1.0 documented the opposite behaviour).
- [x] **P2 — Pitch ±24 semitones with time stretch** (L1+X)
  - [x] Core: `src/core/pitch.c` — a phase vocoder with identity phase locking
    (the deviation is scaled to the synthesis hop, without which the pitch
    bends as the interval leaves 0) plus a cubic resampler that restores the
    length. 10 s of audio renders in ~0.3 s on the PC, so the work stays on
    the UI thread.
  - [x] `PitchCache` keyed by (sample, semitones) with a byte/entry budget: the
    live engine and the song bars share rendered copies, and a hit costs
    nothing. Entries are never evicted while the audio thread may read them;
    the cache is cleared where the owner knows playback stopped (a sample-bank
    reload). Over budget falls back to the unpitched sample and says so.
  - [x] `PIT <label> <n>` pattern lines, clamped to ±24, written only for
    pitched tracks; per song bar.
  - [x] L1+X opens the shared pitch page (`src/core/views/pitch_view.c`):
    ±1 semitone on up/down, ±12 on left/right, A auditions, B returns. The
    working copy is re-resolved immediately; the sequencer row shows the
    interval (`+7`) next to the track label.
  - [x] `--test-pitch` / `tools/test-pitch.ps1`: frequency accuracy at
    ±12/±24 (0.00 % error), length preserved, amplitude preserved, cache
    reuse, PIT round-trip and clamping, legacy files, the L1+X shortcut, and
    on the 808 the real kit resolution path.
  - [x] Verified in the bounce: a transposed pattern differs from the same
    pattern without the interval while the render length is unchanged.
- [x] **P3 — Programmable sample start/end** (L1+B)
  - [x] Core: voices carry a slice (`start`, `slice`, two fade frames); `TRM
    <label> <16 start> <16 end>` pattern lines in 0.01 % units, written only
    for cut tracks. A cut edge gets a 2 ms fade so an arbitrary point does not
    click, and the step mapping always yields at least one frame.
  - [x] The slice page (L1+B, `src/core/views/slice_view.c`): waveform strip
    with the slice highlighted, 16 step cells showing each step's cut, START
    and END in two-place percentages, frame count and duration. Left/right
    picks the step, up/down nudges the selected value (0.01 % per tap,
    accelerating while held), A swaps START/END, X auditions, B returns.
  - [x] The waveform envelope is cached (480 columns) and only rebuilt when the
    sample changes, so drawing the page costs nothing per frame.
  - [x] Auditioning follows the feature: the sequencer's A/X play the step's
    slice, and the slice page plays what is being edited.
  - [x] `--test-trim` / `tools/test-trim.ps1`: percentage mapping, clamping and
    the one-frame floor, slice playback (head vs cut start), the cut end
    stopping early and fading out, TRM round-trip and partial lines, the page's
    cursor wrap, handle swap, clamping and acceleration, plus a bounce that
    differs while keeping its length.
  - [x] L1+B appears in the shared shoulder hint as `B: TRIM`, next to
    `X: PITCH`.
- [ ] **P4 — MIDI engine**
  - [ ] Core: `src/core/midi.c` — running-status parser, clock → BPM lock,
    start/stop/continue, song position.
  - [ ] Engine external-clock mode: the master supplies tempo and transport,
    **our** pattern length and song chain stay ours.
  - [ ] `--test-midi` drives a synthetic clock stream (no hardware needed).
- [ ] **P5 - MIDI device input** *(blocked on the device kernel - see below)*
  - [x] **Device probe (2026-11-25, RG35XX SP / muOS)**: the Type-C port
    **can be a USB host**. Plugging a device makes muOS load the host driver
    (`insmod_host_driver`), register the `sunxi-ehci` / `sunxi-ohci` buses and
    enumerate it (seen: a USB audio DAC, full speed, bus 8). Unplugging tears
    the host stack down again, so the role switch is automatic and the
    launcher needs no preparation.
  - [ ] **Chosen chain**: Roland S-1 / T-8 **USB-C <-> USB-C** to the
    Anbernic - the Roland as a class-compliant USB MIDI device, the Anbernic
    as host. One cable, no adapter, no MIDI Type A pinout to get wrong.
    (The USB-C -> 3.5 mm Type-A idea is dropped: the adapter probed is an
    AB13X **USB audio DAC**, not a MIDI interface, so it can never carry
    MIDI.)
  - [ ] **Blocker found while probing**: the kernel has no USB audio/MIDI
    class driver. The AB13X enumerates and binds HID, but no ALSA card
    appears - `/proc/asound/cards` keeps its three built-in cards and
    `/dev/snd/` gains no `midiC*`. There is therefore no rawmidi node to
    read, whatever the cable. This is a kernel capability
    (`CONFIG_SND_USB_AUDIO`), not an app or cabling problem.
  - [ ] **Decision**: keep the MIDI engine independent of the transport (P4)
    and prove it on the PC with a synthetic stream; wire the device reader
    only against a kernel that exposes a rawmidi node. Re-check after a muOS
    update with `ls /sys/module | grep -i snd` and
    `zcat /proc/config.gz | grep SND_USB`, or with a USB-C <-> USB-C **data**
    cable on a build that has the driver (the user has charging-only cables
    for now).
  - [ ] **Fallback if the kernel never gains it**: `f_midi` gadget through
    configfs - the RG35XX appears as a USB MIDI *device* and a PC bridges
    the Roland. Same app-side code, because the gadget also creates a local
    rawmidi node.
  - [ ] Reader thread on `/dev/snd/midiC*D0` - rescan on plug, since the host
    stack is rebuilt and card/node numbers change - MIDI SYNC page, clock
    badge.
- [ ] **P6 - Release 1.1**
  - [ ] Docs (README / CORE / PLAN / SAMPLER), `port.json`, screenshots, both
    machine packages, device smoke test.

## Current state (as of 2026-11-15)

- **Version 1.0:** M1â€“M11 implemented, with the M12 shared-core split complete;
  PC and both ARM cross-builds pass the automated suites; controls, audio,
  effects CPU, punch-in FX and song export were validated on a physical RG35XX
  SP for both RG-808 and RG-SMP.
- Repo: `master`, all work committed (M1/M2 â†’ tooling/packaging â†’ autosave & fixes).
- Working on PC: full app runs natively (`build/rg808.exe`) with audio + keyboard/gamepad;
  engine and UI validated by automated paths (bounce frame-count math, save/load round
  trip, screenshot inspection, 4 s live run without errors).
- Built for device: 1.0 packages for RG-808 (`dist/rg808.zip`) and RG-SMP
  (`dist/rgsmp.zip`); ELF sanity confirmed (ARM aarch64 + ARM EABI5 armhf,
  dynamically linked against system SDL2).
- Kit selection shipped: every variant family from the pack is selectable per
  voice via the new KIT mode (menu â†’ SELECT KITS).
- M6 shipped (PC): global swing + per-step ratchets (2/3/4 rolls, flam) with
  RATCHET/SWING editor modes, persisted via `RCH`/`SWING` lines; WAV export
  header determinism bug fixed. Validated on device in 1.0.
- M7 shipped (PC + cross-build): 200-slot pattern library (`conf/patterns/`),
  index with names/active slot, atomic writes, legacy import, browser +
  action menu + letter-picker rename; autosave targets the active slot.
- Shoulder shortcut layer (post-M7): L1+Left tempo, L1+Right swing, L1+Up
  pattern length, L2+Left pattern library, L2+Right song mode, R1+Left kit
  selection, R1+Right ratchets; R2+Up/Left/Down/Right jump to the drive,
  modulation, delay and reverb FX sections. L2+Down opens the song export
  confirmation.
  Pressing any shoulder shows a D-pad-shaped hint overlay. L1/R1 no longer
  change tempo directly. Verified by `tools/test-shortcuts.ps1` (headless
  mapping test) plus `--mode l1|l2|r1|r2` hint screenshots.
- M8 shipped (PC + cross-build): 512-bar song chains (`conf/song.txt`),
  immutable double-buffered snapshot sets, per-bar BPM/swing/kit, song page
  with picker/insert/delete/move/play-from-here/loop/jump, streaming
  `conf/song_<tempo>_<yyyymmdd>_<hhmmss>.wav` export with progress bar. Verified by
  `tools/test-m8.ps1` (persistence, 250 BPM switch, loop/end, UI actions,
  first-bar bit-exact vs pattern bounce).
- M8.5 shipped (PC): per-pattern length 1â€“16 (`LEN` in the pattern file),
  live-clock wrap and per-bar song timing, menu/L1+Up editor, dimmed tail
  steps with an end marker. Verified by `tools/test-length.ps1` plus
  `--test-length`.
- M9 shipped (PC + cross-build): master effects chain drive â†’ modulation â†’
  delay â†’ reverb; drive selectable bitcrusher/overdrive/compression,
  modulation selectable flanger/phaser, free or tempo-synced delay, reverb,
  bit-identical bypass, menu â†’ EFFECTS page. Verified by `tools/test-m9.ps1` /
  `--test-fx`; validated on device in 1.0.
- M9.5 shipped (PC): per-pattern FX settings stored in each pattern file and
  carried per bar by song snapshots; one shared DSP state per engine, so fast
  1-step song bars only update parameters, not effect buffers. `conf/fx.txt`
  remains the fallback for old patterns. Verified by `tools/test-m9.ps1`.
- M10 shipped (PC): Y mutes/unmutes the selected instrument in the sequencer
  (X still auditions), with an `M` marker and immediate voice cut; mutes apply
  to live and song playback and are not persisted. Since v1.1 they also apply
  to the WAV export (the export confirmation page can include the muted
  tracks on request).
  Verified by `tools/test-mute.ps1` / `--test-mute`.
- M11 shipped (PC): R1 punch-in FX layer with 1/4â€“1/32 stutters, rewind, gate,
  tape stop and phone filter; quantized to the next 16th, one at a time,
  crossfaded release, live and song playback. Ratchet/kit shortcuts moved to
  L1+Down / L2+Up. Verified by `tools/test-punch.ps1` / `--test-punch` and the
  updated shortcut suite; validated on device in 1.0.
- **Device validation (2026-11-15): the 1.0 builds were exercised on a physical
  RG35XX SP.** Controls, audio, the pattern/song workflow, effects, punch-in FX
  and timestamped WAV export passed for both RG-808 and RG-SMP.

## Risks & mitigations

| Risk | Status |
|---|---|
| Device ABI unknown (armv7 vs aarch64) | Mitigated: both arch binaries ship, launcher picks `${DEVICE_ARCH}`, with `uname -m` fallback |
| Cross-toolchain glibc newer than muOS | Closed: 1.0 device run confirmed |
| No PortMaster control.txt on manual installs | Mitigated: launcher stubs PM helpers, finds its folder, chmods binaries |
| Small-buffer ALSA crackle on H700 | Closed: 1.0 device run validated |
| Effects CPU at 128-frame periods | Closed: 1.0 device run validated with all four stages |
| CRLF breaking shell scripts | Mitigated: `.gitattributes eol=lf` + `tools/check-eol.ps1` verified all scripts are LF |
| Button GUID missing on raw joystick path | Closed: all controls validated on device in 1.0 |
| Sample pack licence unclear | Flagged in licenses note before public distribution |

## Next actions (after 1.0)

1. Publish the 1.0 packages for RG-808 and RG-SMP.
2. Confirm the sample-pack licence terms before public distribution.
3. Optional: run `tools/device/probe.sh` and upstream the PortMaster package.
4. Keep `core`, `master` and `sampler` green: shared fixes land on `core` first,
   then merge into both machine branches.
5. Plan post-1.0 work from `todo.md` and the machine-port plans.
