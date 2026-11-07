# RG-808 Development Plan

**Current version: 0.1** (2026-11-07). M1–M10 are implemented on PC and in the
dual-arch cross-build; physical-device validation and CPU tuning remain open.

Roland TR-808 clone for the Anbernic RG35XX SP (muOS / PortMaster).
1–16-step sequencer, single pattern, 11 classic voices, per-step accents.

## Application stack (decided 2026-11-06)

| Layer | Choice | Notes |
|---|---|---|
| Language | C11, CMake + Ninja | MinGW on Windows for dev, gcc-12 cross in Docker for release |
| Video/input | SDL2 (2.32 headers on PC, Debian 2.26 on cross, device runtime SDL2) | software canvas → one streaming texture; works on fbcon/KMS |
| Audio | SDL2 audio callback + hand-written 48-voice mixer | sequencer clock inside the callback = sample-accurate steps |
| Font | font8x8 (public domain, vendored) | no SDL2_ttf dependency |
| Persistence | plain-text `conf/pattern.txt` + 5 s autosave | PortMaster `conf/` folder convention |
| Packaging | PortMaster "new port structure": `RG808.sh` + `rg808/` in a zip | `port.json` v4, dual arch `aarch64` + `armhf` (`${DEVICE_ARCH}`) |
| Kit | variant families from the "Roland TR-808" pack shipped as `<id>NN.wav` (bd/sd 16, toms 7, ch 5, oh 3, cy 6, cp 3, cl/cb 2 — 74 files) | selectable per voice in kit mode; mapping in `tools/prepare-assets.ps1`, defaults in `src/engine.c` |

Key device facts: RG35XX SP = Allwinner H700 (4× Cortex-A53), 1 GB RAM,
640×480 screen, D-pad + ABXY + L/R + START/SELECT, no Wi-Fi (offline installs
matter). muOS integrates PortMaster; ports live in `<card>/ports/<name>/`,
launch shims in `<roms>/PORTS/`, port logs in `ports/rg808/log.txt`.

## Milestones

- [ ] **M0 — Toolchain & device probe** *(toolchain done, probe pending)*
  - [x] Windows native toolchain: MinGW + CMake + vendored SDL2/MinGW + font8x8 fetch script
  - [x] Docker cross-build (`tools/portmaster-build`): `rg808.aarch64` + `rg808.armhf`, zip assembly inside container
  - [x] `tools/device/probe.sh` written (kernel, arch, glibc, SDL2, ALSA, paths)
  - [ ] Run probe on the device, file back the output → pin glibc + SDL2 + storage paths
- [x] **M1 — Engine (PC)** — WAV→float loader w/ resample; voice mixer; sample-accurate
  16-step clock; accents (off/on/accent gain); BPM 4–250; `--bounce out.wav` offline
  render verified (timing math checked at 90.5 and 120 BPM; soft-clip curve in place).
- [x] **M2 — UI (PC)** — 640×480 canvas UI: track labels + 16-cell grid, cursor, LED
  playhead row, beat markers, tempo header, status bar, MENU + TEMPO + KIT modes,
  on/off/accent visual hierarchy. Verified via `--screenshot` (see
  `port/rg808/screenshot.png`) and a stable live run.
- [ ] **M3 — Device port & parity** *(first launch on SP: SUCCESS)*
  - [x] Dual-arch binaries in zip; `RG808.sh` launch script follows the PortMaster
    template (control.txt, get_controls, LD_LIBRARY_PATH `libs.${DEVICE_ARCH}`,
    gptokeyb kill-daemon, `pm_platform_helper`/`pm_finish`) **and** degrades
    gracefully to a manual install without PortMaster
  - [x] `tools/device/install.sh` + `tools/deploy.ps1` (SSH/SFTP path)
  - [x] First real-device launch (2026-11-06): app runs — video + sound + install
    path confirmed working end-to-end
  - [x] D-pad double-fire fixed (SDL emitted controller-button AND raw-hat events
    for the same press; raw joystick events now suppressed for mapped controllers,
    mapping is checked up front for dpad buttons) — needs on-device retest
  - [ ] Retest controls after fix; report audio latency/cleanliness and whether
    A/B/X/Y/START/SELECT/L/R all map correctly
  - [ ] Tune audio period size / `samples` on hardware if needed
- [x] **M4 — Persistence & polish** — save/load `pattern.txt` round-trip verified;
  kit selections persisted as `KIT <track> <n>` lines (old kit-less files load with
  defaults — confirmed against a real save); autosave (5 s after edits) + save on
  quit; built-in demo groove; master gain + soft-clip. Kit audio path verified by
  `tools/test-kits.ps1` (same pattern, variant 3 vs 12 → different bounce output).
- [ ] **M5 — Packaging** *(structure done, final polish pending)*
  - [x] `dist/rg808.zip` ≈ 3.0 MB: launcher + binaries + 74-variant kit + licenses +
    README + gameinfo.xml + screenshot; layout matches `port.json` `items: ["RG808.sh","rg808"]`
  - [x] Reproducible build: `docker build -t rg808build tools/portmaster-build` then
    `docker run --rm -v ${PWD}:/src rg808build`; Windows fallback `tools/package-port.ps1`
  - [ ] Replace screenshot with a real device capture (4:3 640×480)
  - [ ] Confirm sample-pack licence terms before any public release (`port/rg808/licenses/ASSETS.txt`)
  - [ ] Optional: upstream PR to PortsMaster/PortMaster-New (requires their Discord
    testing thread + multi-CFW testing; not needed for personal use)

## v2 feature milestones (grouped from `todo.md`)

Ordered so each layer builds on a stable base: step model → pattern storage →
song assembly → output DSP. M6 and M9 are independent; M8 requires M7; M9.5
extends M9.

- [ ] **M6 — Timing & feel** *(todo: swing; ratchet notes) — done on PC, device check pending*
  - [x] **Swing/shuffle**: one global amount (0–100 % of a 16th), delays every even
    16th step; applied in the clock math (`next_step += sps` becomes
    alternate long/short). UI: menu → SWING value page (TEMPO-style editor).
    Implementation note: hit offset via voice delays; boundaries stay on the
    straight grid (loop length unchanged, verified by bounce byte size).
  - [x] **Ratchets**: per-step repeat mode — 1/2 (2 hits), 1/3 (3 hits), 1/4 (4 hits),
    and flam (2 hits, second at ~14 ms offset, quieter). Third step dimension
    `unsigned char rch[N_TRACKS][N_STEPS]` + sub-hit scheduling (`fire_sched`);
    roll hits at full level (decay shaping = later polish). MAX_VOICES raised 48→64.
  - [x] UI: RATCHET overlay mode (menu → RATCHET NOTES; 16 columns = steps, digit
    drawn inside set cells in both edit and ratchet views).
  - [x] Persistence: `RCH <label> <16 chars 0..4>` lines + `SWING <n>`; old files
    default to none (backward compatible, same trick as KIT lines).
  - [x] Verified by `tools/test-m6.ps1` (ratchet/swing change bounce audio, length
    preserved) + `--mode rch` screenshot. During testing **fixed a real bug**:
    uninitialized pad bytes in the WAV header made `--bounce` files
    non-deterministic (only affects export tooling, not live playback).
  - [ ] Device CPU spot-check at 250 BPM with 4× rolls on all 11 tracks.
- [x] **M7 — Pattern library** *(todo: save up to 200 patterns)*
  - [x] Storage: `conf/patterns/NNN.txt` (same file format per pattern) +
    `conf/patterns/index.txt` (slot → name, plus the active slot); atomic
    writes (temp + fsync + rename) against SD power-loss.
  - [x] Import migration of the legacy single `pattern.txt` into slot 001 on
    first run (only when the library has no pattern files; later edits to the
    legacy file never overwrite slot 001).
  - [x] UI: PATTERN browser — numbered list with names, 12 slots/page;
    load (A), SELECT actions (SAVE HERE, COPY ACTIVE HERE, RENAME, DELETE),
    D-pad letter-picker rename (16 chars), active-slot marker in the header.
  - [x] Autosave retargets to the active slot; slot number + name shown in
    the header.
  - [x] Verified by `tools/test-m7.ps1` (migration, no re-import, save/reload
    bit-identical, index active/name, no `.tmp` leftovers) plus browser and
    rename screenshots; existing M6/kit/round-trip tests still pass.
- [x] **M8 — Song mode** *(todo: chain up to 512 patterns; save song as WAV) — requires M7*
  - [x] Song data: ordered chain of pattern slots (1 bar per entry, up to 512
    entries) in `conf/song.txt`; per-pattern BPM/swing/length/kit/ratchets
    apply on each bar switch.
  - [x] Engine: immutable `SongSet` snapshot arrays prepared by the UI thread
    (two buffers), audio thread advances the snapshot pointer at every bar
    boundary; per-bar BPM recomputed for the next step so switches are gapless.
  - [x] UI: SONG page — bar list with pattern names, picker, insert/delete/
    move, play-from-here, loop toggle, jump-to-playing-bar, save/clear.
  - [x] **WAV export**: streaming renderer writes chunks straight to disk
    (header patched at close), progress bar, 44.1 kHz/16 stereo, one pass plus
    a two-second tail.
  - [x] Verified by `tools/test-m8.ps1` (persistence, snapshot BPMs, song
    length math, non-loop end, loop wrap, UI actions, 250 BPM bar switch,
    first-bar bit-exact vs a normal pattern bounce) plus song/action/export
    screenshots.
- [x] **M8.5 — Pattern length** *(pre-M9; per-pattern 1–16 steps)*
  - [x] Engine: `length` in the live engine and in every `PatternSnapshot`;
    the 16th clock wraps on the pattern length, and song bars advance on
    each bar's own length. Bounce length math uses it too.
  - [x] Persistence: `LEN <n>` line per pattern, clamped to 1–16; files from
    older builds (no `LEN`) load as 16 steps.
  - [x] UI: menu → PATTERN LENGTH (also L1+Up), up/down ±1 and left/right ±4;
    steps past the end are shown dimmed with an end marker, and the edit
    cursor wraps inside the active length. Header shows `LEN nn` (and the
    current song bar's length while playing a song).
  - [x] Verified by `tools/test-length.ps1` / `--test-length` (round-trip,
    legacy default, clamps, live clock wrap, per-bar song length/end) and
    bounce/song-export frame counts for LEN 1/4/8/16 plus the length-editor
    screenshot.
- [x] **M9 — Effects** *(todo: compression/overdrive/bitcrusher — all selectable;
  flanger/phaser — both selectable; delay; reverb) — independent of M6–M8*
  - [x] Scope: master chain
    `drive → modulation → delay → reverb → master` (fixed order, per-stage
    on/off + params), float.
  - [x] Drive slot is selectable: bitcrusher (4–16 bits + 1–32× sample-and-hold),
    overdrive (tanh drive/mix), or compression (stereo-linked peak compressor
    with threshold/ratio/makeup).
  - [x] Modulation slot is selectable: flanger (LFO 1-tap delay) or phaser
    (6-stage modulated allpass with a midrange coefficient sweep and feedback
    gain compensation), both with rate/depth/feedback/mix. The phaser was
    retuned after listening feedback: the coefficient sweeps the audible
    midrange and the wet path is compensated for feedback boost, so max
    settings no longer turn into a clipped overdrive.
  - [x] Delay: stereo circular buffer, 20–4000 ms free-running or tempo-synced
    to 1/4, 1/8, 1/16, 1/4T, 1/8T, 1/16T, 4D, 8D and 16D. Sync follows the live
    tempo or the current song bar's BPM.
  - [x] Reverb: 4 damped parallel combs + 2 series allpasses (room/damp/mix).
  - [x] Per-voice sends remain out of scope.
  - [x] UI: menu → EFFECTS shows only the rows relevant to the selected drive/
    modulation algorithms; A toggles master/stages, left/right adjusts the
    selected value, B returns.
  - [x] Real-time safety: effect buffers are heap-allocated per engine; with
    master bypass off no effect code touches the mix, so bypass stays
    bit-identical to the pre-M9 render.
  - [x] Verified by `tools/test-m9.ps1` / `--test-fx` (defaults, clamps,
    persistence, bit-identical bypass/all-off, every drive/modulation algorithm,
    each stage changes audio, all nine delay divisions resolve correctly) plus
    the FX page screenshot. Device CPU spot-check at 128-frame periods with all
    stages on is still pending.
- [x] **M9.5 — Per-pattern effects** *(follow-up to M9)*
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
    delay timing, bit-identical bypass). Device CPU spot-check remains pending.
- [x] **M10 — Performance mute** *(part of the old per-track volume/mute
  backlog; per-track volume remains open)*
  - [x] Engine: `mute[N_TRACKS]` global performance state, checked by the live
    and song step schedulers; `mute_kill_mask` lets the audio thread cut voices
    that are already ringing when a track is muted. Preview stays audible.
  - [x] UI: Y toggles the selected track in the sequencer (X remains HEAR);
    muted rows are tinted and marked with a red `M`; the status bar shows
    `Y:MUTE`. Y keeps auditioning in kit/ratchet mode and keeps its old
    behaviour elsewhere.
  - [x] Mutes are deliberately not persisted (performance state): they survive
    pattern loads and song bars but reset on restart, and do not affect WAV
    export.
  - [x] Verified by `tools/test-mute.ps1` / `--test-mute` (mute silences live and
    song playback, cuts ringing voices, preview still sounds, UI toggle and KIT
    audition alias) plus a `--mode mute` screenshot; M6–M9/length/shortcut
    suites still pass.

## Current state (as of 2026-11-07)

- **Version 0.1:** M1–M10 implemented; PC and both ARM cross-builds pass the
  automated suites. Device-only checks (controls, audio latency, effects CPU,
  device screenshot) remain.
- Repo: `master`, all work committed (M1/M2 → tooling/packaging → autosave & fixes).
- Working on PC: full app runs natively (`build/rg808.exe`) with audio + keyboard/gamepad;
  engine and UI validated by automated paths (bounce frame-count math, save/load round
  trip, screenshot inspection, 4 s live run without errors).
- Built for device: `dist/rg808.zip` ready to install; ELF sanity confirmed
  (ARM aarch64 + ARM EABI5 armhf, dynamically linked against system SDL2).
- Kit selection shipped: every variant family from the pack is selectable per
  voice via the new KIT mode (menu → SELECT KITS).
- M6 shipped (PC): global swing + per-step ratchets (2/3/4 rolls, flam) with
  RATCHET/SWING editor modes, persisted via `RCH`/`SWING` lines; WAV export
  header determinism bug fixed. Device retest needed (dpad fix + M6).
- M7 shipped (PC + cross-build): 200-slot pattern library (`conf/patterns/`),
  index with names/active slot, atomic writes, legacy import, browser +
  action menu + letter-picker rename; autosave targets the active slot.
- Shoulder shortcut layer (post-M7): L1+Left tempo, L1+Right swing, L1+Up
  pattern length, L2+Left pattern library, L2+Right song mode, R1+Left kit
  selection, R1+Right ratchets; R2+Up/Left/Down/Right jump to the drive,
  modulation, delay and reverb FX sections. Remaining combinations are reserved.
  Pressing any shoulder shows a D-pad-shaped hint overlay. L1/R1 no longer
  change tempo directly. Verified by `tools/test-shortcuts.ps1` (headless
  mapping test) plus `--mode l1|l2|r1|r2` hint screenshots.
- M8 shipped (PC + cross-build): 512-bar song chains (`conf/song.txt`),
  immutable double-buffered snapshot sets, per-bar BPM/swing/kit, song page
  with picker/insert/delete/move/play-from-here/loop/jump, streaming
  `conf/song.wav` export with progress bar. Verified by
  `tools/test-m8.ps1` (persistence, 250 BPM switch, loop/end, UI actions,
  first-bar bit-exact vs pattern bounce).
- M8.5 shipped (PC): per-pattern length 1–16 (`LEN` in the pattern file),
  live-clock wrap and per-bar song timing, menu/L1+Up editor, dimmed tail
  steps with an end marker. Verified by `tools/test-length.ps1` plus
  `--test-length`.
- M9 shipped (PC + cross-build): master effects chain drive → modulation →
  delay → reverb; drive selectable bitcrusher/overdrive/compression,
  modulation selectable flanger/phaser, free or tempo-synced delay, reverb,
  bit-identical bypass, menu → EFFECTS page. Verified by `tools/test-m9.ps1` /
  `--test-fx`; device CPU check pending.
- M9.5 shipped (PC): per-pattern FX settings stored in each pattern file and
  carried per bar by song snapshots; one shared DSP state per engine, so fast
  1-step song bars only update parameters, not effect buffers. `conf/fx.txt`
  remains the fallback for old patterns. Verified by `tools/test-m9.ps1`.
- M10 shipped (PC): Y mutes/unmutes the selected instrument in the sequencer
  (X still auditions), with an `M` marker and immediate voice cut; mutes apply
  to live and song playback, are not persisted, and do not affect export.
  Verified by `tools/test-mute.ps1` / `--test-mute`.
- **First real-device session (2026-11-06): the port boots and runs on the SP.**
  Only issue found: D-pad double-fire (fixed in `src/main.c`, rebuilt zip —
  awaiting on-device retest).
- Not yet done: anything that needs the physical device (probe output, first launch,
  latency/button mapping tuning, device screenshot).

## Risks & mitigations

| Risk | Status |
|---|---|
| Device ABI unknown (armv7 vs aarch64) | Mitigated: both arch binaries ship, launcher picks `${DEVICE_ARCH}`, with `uname -m` fallback |
| Cross-toolchain glibc newer than muOS | Open (low): bookworm = glibc 2.36; probe will confirm |
| No PortMaster control.txt on manual installs | Mitigated: launcher stubs PM helpers, finds its folder, chmods binaries |
| Small-buffer ALSA crackle on H700 | Open: first device run; tune `desired.samples` / SDL env |
| Effects CPU at 128-frame periods | Open: PC verified; measure on H700 with all four stages on |
| CRLF breaking shell scripts | Mitigated: `.gitattributes eol=lf` + `tools/check-eol.ps1` verified all scripts are LF |
| Button GUID missing on raw joystick path | Mitigated: controller API → raw joystick → keyboard fallbacks; probe + device test will tell |
| Sample pack licence unclear | Flagged in licenses note before any public release |

## Next actions

1. User: reinstall the refreshed `dist/rg808.zip` (or just copy the two binaries
   to `<card>/ports/rg808/`) and retest the D-pad — expect single steps now.
2. User (optional but useful): run `tools/device/probe.sh` once — confirms glibc,
   SDL2 version and which arch binary actually ran; informs the upstream PR later.
3. Report from device: audio latency/crackle? other buttons (A/B/X/Y, START,
   SELECT, L/R) all correct? If clean → swap in a device screenshot, refresh the
   zip, and M3/M5 can close.
4. Then pick the v2 entry point (proposal: M3 validation → M6 → M7 → M8; M9 anytime).
   M6/M7/M8/M8.5/M9/M9.5/M10 are done on PC; the device still needs the
   M6–M10 control/DSP pass (pattern browser, song page and export, pattern
   length, per-pattern effects CPU, Y mute).
5. Device: check the shoulder shortcut layer - L1/L2/R1/R2 hint overlay and the
   L1+Left/Right/Up, L2+Left/Right, R1+Left/Right and R2+Up/Left/Down/Right
   mappings. L2/R2 are read from SDL's trigger axes (and raw joystick axes
   4/5). If the SP maps them differently, pin it from the device's
   gamecontrollerdb.
6. Device: play a short song chain and run EXPORT WAV; confirm gapless bar
   switches, per-bar tempo changes, effects and that `conf/song.wav` plays back
   cleanly.
