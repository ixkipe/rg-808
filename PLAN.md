# RG-808 Development Plan

Roland TR-808 clone for the Anbernic RG35XX SP (muOS / PortMaster).
16-step sequencer, single pattern, 11 classic voices, per-step accents.

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
- [ ] **M3 — Device port & parity** *(artifacts ready, hardware test pending)*
  - [x] Dual-arch binaries in zip; `RG808.sh` launch script follows the PortMaster
    template (control.txt, get_controls, LD_LIBRARY_PATH `libs.${DEVICE_ARCH}`,
    gptokeyb kill-daemon, `pm_platform_helper`/`pm_finish`) **and** degrades
    gracefully to a manual install without PortMaster
  - [x] `tools/device/install.sh` + `tools/deploy.ps1` (SSH/SFTP path)
  - [ ] Launch on SP from muOS Ports; report: display ok? sound crackle/latency?
    button mapping (controllerdb / gptokeyb / raw-joystick fallback)?
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

## v2 feature milestones (grouped from `todo.md` — planned, not started)

Ordered so each layer builds on a stable base: step model → pattern storage →
song assembly → output DSP. M6 and M9 are independent; M8 requires M7.

- [ ] **M6 — Timing & feel** *(todo: swing; ratchet notes)*
  - **Swing/shuffle**: one global amount (0–100 % of a 16th), delays every even
    16th step; applied in the clock math (`next_step += sps` becomes
    alternate long/short). UI: menu → SWING value page (TEMPO-style editor).
  - **Ratchets**: per-step repeat mode — 1/2 (2 hits), 1/3 (3 hits), 1/4 (4 hits),
    and flam (2 hits, second at ~10–30 ms offset, quieter). Needs a third step
    dimension: `unsigned char rch[N_TRACKS][N_STEPS]` + sub-hit scheduling with
    fixed divisions of the step and roll-decay gains.
  - UI: RATCHET overlay mode (same picker pattern as KIT mode; small repeat
    number rendered inside set cells in edit mode).
  - Persistence: `RCH <label> <16 chars>` lines + `SWING <n>`; old files default
    to none (backward compatible, same trick as KIT lines).
  - Risks/acceptance: voice budget (48 may need raising for hat rolls at high
    BPM), audible machine-gun/flam in bounce tests, device CPU spot-check.
- [ ] **M7 — Pattern library** *(todo: save up to 200 patterns)*
  - Storage: `conf/patterns/NNN.txt` (same file format per pattern) + `index.txt`
    (slot → name); atomic writes (temp + rename) against SD power-loss.
  - Import migration of the legacy single `pattern.txt` into slot 001 on first run.
  - UI: PATTERN browser — numbered grid/list with names; load (A), overwrite-save
    current (menu), delete/copy, D-pad letter-picker rename (optional if fiddly).
  - Autosave retargets to the active slot; slot number shown in header.
  - Risks/acceptance: 200 × tiny files (negligible), browse-edit-save-reload
    round-trip test, crash-safety (never lose the last edit beyond ~5 s).
- [ ] **M8 — Song mode** *(todo: chain up to 512 patterns; save song as WAV) — requires M7*
  - Song data: ordered chain of pattern slots (1 bar per entry, up to 512 entries);
    per-pattern BPM applies on each switch (bpm lives with the pattern, as today).
  - Engine: at loop boundary, audio thread swaps the pattern snapshot pointer
    (double-buffered snapshot set prepared by UI thread — first real data-structure
    change of the engine, keep the race-free design).
  - UI: SONG page — list editor (pattern number per bar, insert/delete/shift,
    jump-to-playbar).
  - **WAV export**: streaming renderer (chunk loop writing straight to the file —
    today's `--bounce` holds the whole render in RAM, which would explode for a
    512-bar song); progress bar; output 44.1 k/16 stereo.
  - Risks/acceptance: song length math, gapless pattern switches at 250 BPM,
    export bit-exact vs live playback on the first 4 bars.
- [ ] **M9 — Effects** *(todo: compression/overdrive/distortion — one; flanger/phaser —
  one; delay; reverb) — independent of M6–M8, last because of CPU risk*
  - Scope per the "either/or" note: master chain
    `distortion → flanger → delay → reverb → master` (fixed order, per-stage on/off
    + params), float, simple algorithms first (delay: circular buffer; reverb:
    2–4 comb + damp; flanger: LFO 1-tap; distortion: soft-clip/saturation).
  - Per-voice sends are explicitly out of scope for the first pass.
  - UI: FX menu page (parameter editor reusing the TEMPO page component);
    global bypass; `FX ...` config line(s) persisted.
  - Risks/acceptance: measured CPU headroom on the H700 at 128-frame periods,
    no dropouts with all four stages on, bypass = bit-identical to pre-M9 render.
- [ ] **M10 — Ergonomics & machine feel** *(older backlog, grab-bag, reorderable)*
  - Per-track volume/mute, accent-driven BD decay (true 808 behaviour, not just
    gain), master volume UI, per-track pan maybe.

## Current state (as of 2026-11-06)

- Repo: `master`, all work committed (M1/M2 → tooling/packaging → autosave & fixes).
- Working on PC: full app runs natively (`build/rg808.exe`) with audio + keyboard/gamepad;
  engine and UI validated by automated paths (bounce frame-count math, save/load round
  trip, screenshot inspection, 4 s live run without errors).
- Built for device: `dist/rg808.zip` ready to install; ELF sanity confirmed
  (ARM aarch64 + ARM EABI5 armhf, dynamically linked against system SDL2).
- Kit selection shipped: every variant family from the pack is selectable per
  voice via the new KIT mode (menu → SELECT KITS).
- Not yet done: anything that needs the physical device (probe output, first launch,
  latency/button mapping tuning, device screenshot).

## Risks & mitigations

| Risk | Status |
|---|---|
| Device ABI unknown (armv7 vs aarch64) | Mitigated: both arch binaries ship, launcher picks `${DEVICE_ARCH}`, with `uname -m` fallback |
| Cross-toolchain glibc newer than muOS | Open (low): bookworm = glibc 2.36; probe will confirm |
| No PortMaster control.txt on manual installs | Mitigated: launcher stubs PM helpers, finds its folder, chmods binaries |
| Small-buffer ALSA crackle on H700 | Open: first device run; tune `desired.samples` / SDL env |
| CRLF breaking shell scripts | Mitigated: `.gitattributes eol=lf` + `tools/check-eol.ps1` verified all scripts are LF |
| Button GUID missing on raw joystick path | Mitigated: controller API → raw joystick → keyboard fallbacks; probe + device test will tell |
| Sample pack licence unclear | Flagged in licenses note before any public release |

## Next actions

1. User: run `tools/device/probe.sh` on the SP (Mustard Terminal) and share the output.
2. User: enable SSH (muOS Connectivity) or copy `dist/rg808.zip` to the SD card;
   install via `tools/deploy.ps1 <ip>` or `tools/device/install.sh`.
3. First-launch feedback: video correct? audio latency/cleanliness? buttons? Then tune.
4. Swap in a device screenshot; final zip refresh (`tools/package-port.ps1`).
5. v2 feature development: **not started** — scope and sequencing for the
   `todo.md` items live in milestones M6–M10 above; confirm priority when we
   pick the first one (proposal: M3 device validation → M6 → M7 → M8, M9 anytime).
