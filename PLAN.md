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
5. v2 backlog (post-1.0): per-track volumes/mutes, multiple
   patterns/slots, accent-driven decay (BD machine feel), pattern chain, master volume UI.
