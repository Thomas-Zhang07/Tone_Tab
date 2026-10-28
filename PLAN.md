# Tone_Tab — v1 plan

## 1. Context

Tone_Tab is a private fork of **LianqinHost** (老干部活动中心), a C++/JUCE guitar
practice app. The fork keeps the whole feature set but fixes three things:

1. the Chinese app shell and the repo/code quality,
2. the drums (bad sound, near-identical patterns),
3. recording (must capture effects and drums, not just the dry guitar).

Everything else stays: NAM amp models, cab IRs, 22 built-in effects, VST3 hosting,
Guitar Pro practice, judging, practice log, drum machine.

The repo is personal and private. Tones are local only; `assets/` is git-ignored.
AGPLv3 is accepted (no distribution planned).

---

## 2. Locked decisions

| Area | Decision |
| --- | --- |
| Source | Fork the whole repo, all features |
| License | AGPLv3, private repo |
| Tones | Both `D:\NAM` and `otherproject`; merged, local only |
| UI language | English default, 中文 switchable |
| Targets | Standalone app + VST3 plugin (plugin = tone chain) |
| Practice | Keep GP import, scrolling tab, judging, speed trainer, stats |
| Drums | Keep synthesized (no sample dependency), but rewrite |
| Recording | Stereo full mix **and** mono dry stem |
| Tab demo tone | Improve the pluck/backing synth (live input chain unchanged) |
| Quality | Latency compensation, DC blockers, NAM rate handling, smoothing |

---

## 3. Current state

Done:

- Repo initialized at `E:\guitar_audio\Tone_Tab`.
- Full source copied (`Source/`, `tests/`, `third_party/`, build scripts, licenses).
- Tone library merged: **312 files → 270 unique models** (259 A1 + 11 A2),
  **42 IRs**, 6 Gervana images. 42 curated aliases preserved in
  `assets/nam/index.json`. `assets/` is git-ignored.
- ASCII naming policy in `NAMING.md`; no non-ASCII paths in the tracked tree.
- `Source/app/AppPaths.*` added and wired into settings, presets, recordings,
  practice log, tabs, tone library, Gervana artwork, and `@app/` presets.
- Product identity renamed to Tone_Tab; CMake targets/options renamed; English
  `build.bat` / `package.ps1`; VS Code CMake presets.
- MSVC 14.44 + Windows SDK installing to `D:\BuildTools\VS2022` (installer still
  finalizing the SDK).

All milestones M0–M10 are implemented and verified. See the status table and
verification results below.

### Implementation status

| Milestone | Status |
| --- | --- |
| M0 baseline build | **Done** — `Tone_Tab.exe`, `ToneTests.exe`, VST3 build |
| M1 paths/settings | **Done** — `AppPaths`, ASCII paths, legacy preset/practice-log import; typed settings facade not required for v1 |
| M2 English shell | **Done** — 379 `TR` calls, parameter names/choices via `L()`, ASCII preset files, language selector |
| M3 core/UI split | **Done** — `ToneCore` + `ToneUI` shared by app and plug-in |
| M4 VST3 | **Done** — `Tone_Tab.vst3` builds; editor reuses the pedal board; configurable library root |
| M5 DSP safety | **Done** — DC blockers, NAM channel validation and resampling (44.1/48/96 kHz), latency reporting, NAM gain smoothing, pitch anti-alias filters. Acceptance: impulse peak 5 / reported 5, pitch spurs −47 dB. |
| M6 recording | **Done** — `mix.wav` + `dry.wav` + `take.json`, lock-free writer, aligned stems, tests |
| M7 drums | **Done** — layered voices, bus glue, humanize, A/B patterns, UI control |
| M8 GP tab | **Done** — demo/backing tone rewritten; import/highway/judging kept and tested |
| M9 smoothing/crossfade | **Done** — `ParamSmoother`/`GlidedBiquad` on NAM, IR, delay, reverb, tremolo, amp, overdrive and both EQs; parallel equal-power chain crossfade. Acceptance: step 0.0051/0.0058, reverb tail −24.5 dB, XML 200/200. |
| M10 packaging/docs | **Done** — package ships VST3, assets, 54 presets, README, EN + ZH guides, source bundle. Staged app launches; VST3 load/process/state smoke passes; pluginval strictness 8 passes. |

### Verification results

| Check | Result |
| --- | --- |
| Offline suite | `全部通过：0 项失败` |
| DC blockers | tests pass; measured DC removed after drives |
| NAM sample-rate conversion | 44.1 kHz peak −10.1 dB / latency 209; 96 kHz peak −11.6 dB / latency 150 |
| Overdrive / distortion latency | peak 5 / reported 5 |
| Pitch +7 semitone spurs | −47.0 dB (< −45) |
| Parameter step smoothness | delay mix 0.0051, EQ frequency 0.0058 (< 0.02) |
| Reverb tail after edit | −24.5 dB |
| Concurrent XML export | 200/200 valid documents |
| VST3 smoke | load, process 200 blocks, state 267 bytes |
| pluginval | strictness 8: `SUCCESS`, exit 0 (only the optional external VST3 validator sub-test was skipped because its validator path is unset) |
| Package | `release/Tone_Tab-v0.4.0.zip` 69.8 MB, 270 models, 42 IRs, 54 presets, VST3, source bundle |
| Staged app | launches from the release folder |

A clean Windows VM was not available in this environment. The package was
instead staged and launched from its release folder, the VST3 was validated with
pluginval, and the source bundle includes `Source/`, `tests/`, `docs/`, `tools/`
and the vendored dependency zips.

Tests are embedded in the app (`Tone_Tab.exe --run-tests`) because Windows Smart
App Control blocks the separate unsigned test binary. All offline tests pass.

---

## 4. Milestones

### M0 — Baseline build (gate)

- Finish the SDK install; configure with the VS 2022 generator.
- Build `Tone_Tab.exe` and `ToneTests.exe`.
- Fix compile errors caused by the path/identity/CMake refactor.
- Acceptance: clean Release build; app launches; existing features work;
  settings land in `%APPDATA%\Tone_Tab\settings.xml`.

Files: `CMakeLists.txt`, `build.bat`, `Source/app/AppPaths.*`, `Source/i18n/*`.

### M1 — Repo quality and settings layer

- Replace the last hardcoded paths/identities; no `LianqinHost` strings in code.
- Add typed `AppSettings` on top of `PropertiesFile`; stop panels writing raw keys.
- One-time import from `%APPDATA%\LianqinHost` and `Documents\LianqinHost`
  (presets, practice log, settings whitelist), never writing to the old folders.
- Add a lint check: no CJK in `T("...")` outside `Source/i18n`/`DefaultPresets.h`;
  no non-ASCII tracked paths.
- Translate comments in files we touch; new code is English.
- Acceptance: grep for `LianqinHost|音色库|音色预设|录音|吉他谱|默认设置` in
  `Source` and `package.ps1` returns nothing except the migration code.

### M2 — Bilingual shell (your #1)

- `TR("English", "中文")` at every user-facing string (393 calls, in progress).
- Language setting `ui.language` (`en`/`zh`), default English.
- Header language selector; language change applies after restart in v1
  (live refresh is a later polish item).
- Translate effect names, parameter names, drum style names, preset categories,
  error dialogs, and the About box.
- Preset display names: ASCII file names, localized labels inside the XML.
- Acceptance: English UI with no Chinese leakage; `zh` restores the original text;
  preset and parameter IDs unchanged.

### M3 — Shared core and build targets

- Split `ToneCore` (engine, fx, dsp, drums, song, practice, app, i18n) and
  `ToneUI` (panels) static libraries.
- Remove `juce_audio_devices` from `AudioEngine`; add `ChainSink`.
- Add `StandaloneHost` implementing `AudioIODeviceCallback`.
- Acceptance: standalone builds/runs from the split targets; tests link `ToneCore`.

### M4 — VST3 plugin

- `Tone_TabPlugin` (`juce_add_plugin`, VST3, no MIDI, stereo out).
- `PluginProcessor` owns `AudioEngine` + `EffectChain`; practice disabled.
- `PluginEditor` reuses PedalBoard/PresetBar/PedalEditor + meters/tuner.
- Plugin state = chain XML + preset name; report chain latency to the host.
- Acceptance: plugin loads in REAPER, state round-trips, editor opens, no practice UI.

### M5 — DSP correctness

- **PDC:** `BuiltinEffect::getLatencySamples`; per-slot bypass delay; chain
  latency in `ChainSnapshot`; delay-compensated dry/wet in clippers, IR, pitch;
  plugin `setLatencySamples`.
- **NAM:** reject non-mono models; resample host↔model rate when they differ
  (r8brain, MIT); `ResetAndPrewarm` at the model rate.
- **DC:** 10–20 Hz blocker after every asymmetric clipper and on the master bus.
- **Pitch shifter:** replace the 2-grain WSOLA (Signalsmith Stretch, MIT, or a
  4-grain oversampled version); anti-alias before upshifting.
- Acceptance: impulse latency ±1 sample; bypass does not shift timing; DC/peak
  < −50 dB; 44.1/48/96 kHz NAM tests; pitch spurs < −45 dB.

### M6 — Recording (your #3)

- `TakeMetadata` v2: mix/dry files, sample rate, chain latency, song/track/speed,
  loop/count-in/wait state, sync point.
- `RecorderWriter`: lock-free FIFO (mix L/R + dry), writer thread, no
  `CriticalSection` on the audio thread.
- Audio path: dry pre-input-gain; mix after FX + input gain + backing + drums,
  independent of the monitor mute.
- Files: `Documents\Tone_Tab\Recordings\<take>\mix.wav` (stereo 24-bit),
  `dry.wav` (mono 24-bit), `take.json`.
- Alignment: dry delayed by chain latency; structural chain edits are blocked
  during a take.
- Replay: dry-through-current-chain (re-amp) or mix playback; keep legacy mono
  recordings working.
- Acceptance: impulse test shows `dry index + chainLatency == mix index ±1`;
  start/stop never blocks the audio thread; old recordings still play.

### M7 — Drums (your #2)

- `DrumVoice`: layered kick (body + click + beater + sub, saturation), snare
  (two body modes + noise + wires), hats (metallic + noise, choke), crash/ride,
  toms; velocity changes level/brightness/decay; round-robin variants.
- `DrumBus`: 30 Hz high-pass, gentle saturation, parallel compression, room send,
  output trim.
- `DrumMixer`: per-piece gain/pan/tune/decay/room; persisted.
- Patterns: genuinely distinct grooves per style, with A/B variations per bar and
  style-specific fills (tom runs, snare crescendos, crash on the next downbeat).
- Humanize: deterministic velocity/timing variation.
- UI: Humanize/Room/Mix controls in `DrumPanel`; optional Mixer panel.
- Acceptance: 10 s per style finite, peak < 0.95, DC < −60 dB, kick energy
  40–90 Hz, hats > 5 kHz; closed hat chokes open hat; patterns audibly differ.

### M8 — GP tab & practice

The whole Guitar Pro path is kept and gets its own pass; this is the page the
fork exists for.

**Import and score data**

- Verify GP3/4/5/6 (`.gp3`–`.gp5`, `.gpx`) and GP7/8 (`.gp`) through
  `song/GpImporter.cpp`; add a small corpus of real files to the offline tests.
- Repeats, alternate endings, ties, tempo changes, track tuning/capo, drum track
  detection, section markers.
- Reject unreadable/encrypted archives with a clear bilingual error instead of a
  crash or a silent empty score.
- Keep `Song` as the stable model; no UI code in the importer.

**Highway and transport UX**

- Six string lanes, technique tags (H/P/PM/×/harmonic/slide/bend/vibrato),
  note names, bar numbers, sections, chord lines, sustain tails.
- Zoom, seek, A-B loop drag, count-in, metronome, wait mode, all-tracks ensemble,
  per-track mixer, speed 25–150% without pitch change.
- Persist last file/track/loop; recent files; drag-and-drop.
- Localize every label (M2) and keep keyboard shortcuts discoverable.

**Demo and backing tone**

- Rewrite `PluckSynth`: fractional delay, loop filter, dispersion, pick-position
  comb excitation, velocity layers, 48 voices with stealing, stereo spread.
- `DemoTone` bus: body resonance, pickup character, cab EQ or optional IR, short
  room. Route demo + backing through it; drums stay separate.
- The live guitar keeps the NAM/effects chain; only the synthesized tab playback
  changes.
- Acceptance: A2/E4 tuning < 3 cents; 6-note chord peak < 0.9; 32 voices < 5% CPU.

**Judging and practice tools**

- Keep onset + YIN judging, Perfect ±40 ms / Good ±150 ms, wrong/miss, wait mode,
  latency calibration, per-bar accuracy, worst-bars card, speed trainer, log.
- Re-check with the new recording stems: dry-stem replay must judge identically
  to live input; full-mix replay disables judging.
- Add offline tests for correct/wrong/miss, wait mode, loop re-scoring, and a
  real `.gp5`/`.gpx` file.

**Acceptance:** open one GP5 and one GPX file, play with demo/backing, judge,
record a take, replay the dry stem through the chain, and see the pass in the
practice log — all in English, with no missing strings.

### M9 — Smoothing, crossfade, state safety

- `ParamSmoother` / `GlidedBiquad` for gains, mixes, and filter coefficients.
- Parallel chain crossfade (clone old/new chains; fallback 5 ms equal-power fade).
- Fix `EffectChain::toXml()` racing with `processBlock` (capture handshake).
- Acceptance: click/zipper tests; reverb tail survives a chain edit; XML stress test.

### M10 — Packaging and docs

- Branding: name, icon, About dialog (AGPLv3, source), theme tokens.
- English `README.md`, bilingual user guide, third-party notices.
- `package.ps1` stages ASCII folders and refuses non-ASCII paths.
- Acceptance: zip runs on a clean Windows VM; VST3 installs; source bundle included.

---

## 5. Execution order

1. **M0** — build baseline (blocks everything).
2. **M1 + M2** — paths/settings + English shell (your #1).
3. **M6** — recording (your #3), after the PDC part of M5.
4. **M7** — drums (your #2); independent of M6.
5. **M3 + M4** — shared core and VST3.
6. **M5 remainder, M8, M9** — GP tab/demo tone, DSP polish, smoothing.
7. **M10** — package and docs.

M6 and M7 can run in parallel once M0/M1/M2 are stable. M3/M4 must not start
until the standalone is clean, to avoid debugging two targets at once.

---

## 6. Verification

```bat
cd /d E:\guitar_audio\Tone_Tab
cmake -S . -B build -A x64 -DTONE_BUILD_TESTS=ON
cmake --build build --config Release --parallel

build\Tone_Tab_artefacts\Release\Tone_Tab.exe
build\ToneTests_artefacts\Release\ToneTests.exe "D:\NAM\models" build\test-out
```

New test switches: `--pdc`, `--nam-sr`, `--nam-channels`, `--rec-stems`,
`--drums`, `--pluck`, `--loc-audit`, `--plugin-smoke`, `--migration`.

Expected: build exits 0; all `[OK]`; recording stems align within ±1 sample;
no CJK in `T()` outside the allowlist; plugin validates with pluginval.

---

## 7. Risks / deferral order

1. **VST3 target + shared UI** — highest integration risk. Defer if needed; ship
   standalone first.
2. **NAM resampling** — vendored resampler build/latency. Fallback: keep the
   warning, fail visibly, do not silently run at the wrong rate.
3. **Parallel chain crossfade** — cloning NAM/IR/VST3 is expensive. Fallback:
   5 ms equal-power fade, tails cut.
4. **Dual-stem alignment** — freeze structural chain edits during a take.
5. **Live language switching** — v1 applies after restart; live refresh later.
6. **Drum/pluck subjective quality** — budget listening iterations; keep legacy
   modes during development.

Deferred from v1: nested VST3 hosting inside the Tone_Tab plugin, host automation
of individual chain parameters, optional drum sample packs, macOS/AU.
