# Tone_Tab

A Windows guitar practice and tone app: NAM amp captures, cab IRs, a pedal chain,
a drum machine, Guitar Pro practice, and recording.

Tone_Tab is a private fork of **LianqinHost** by 痛风老大爷, rebuilt with an English
interface, ASCII paths, a rewritten drum engine, and dual-stem recording.

## Features

- **Tone chain**: 22 built-in effects + 6 Gervana effects, NAM amp models, cabinet
  IRs, VST3 hosting, presets, tuner.
- **Tone browser**: searchable list of all 270 NAM models with category filters,
  plus a Fender shortcut at the top of the add menu; preset dropdown starts with
  Fender NAM and Captures.
- **Record view**: dedicated tab with a real-time input waveform while recording,
  a take timeline, click-to-seek replay, region selection, record/stop, take
  list, dry-through-chain replay, delete, refresh and open-folder controls.
  **Punch Selection** records a non-destructive layer over the selected region
  (the original mix.wav/dry.wav are never modified; clips live in the take's
  `clips/` folder and are listed in `take.json`). **Export WAV…** bounces the
  guitar take with all punches, either re-amped through the current effect chain
  or as a dry DI stem (the original mix.wav still holds the backing/drums mix).
- **Drums view**: visible style grid, 16-step pattern display, transport,
  Verse/Chorus/Fill, feel controls and a per-piece mixer.
- **NAM library**: 270 unique captures (259 A1 + 11 A2 full-rig) and 42 IRs,
  merged from the local NAM and LianqinHost libraries. Category and curated-name
  aliases are kept in `assets/nam/index.json`.
- **Loop station**: dedicated Loop tab. Space records, closes the loop and
  overdubs. Start and close snap to the bar, so an early or late key press still
  produces a tight loop; with the drums playing the loop locks to the drum bar
  grid. The timeline shows the loop take with bar lines and a playhead, plus a
  LIVE lane of what you are playing now. Auto or fixed 1/2/4/8-bar takes,
  30-second maximum.
- **Practice**: Guitar Pro 3–8 import, scrolling tab highway, demo/backing
  playback, judging (Perfect/Good/wrong/miss), wait mode, latency calibration,
  speed trainer, practice log.
- **Drums**: rewritten synthesized kit with layered kick/snare/hats, bus glue,
  humanize, A/B groove variations and style-specific fills.
- **Recording**: one take folder with `mix.wav` (guitar + effects + drums +
  backing, stereo), `dry.wav` (raw guitar, mono), `take.json`, plus optional
  `clips/` punch layers. Punch a selected region non-destructively, then export
  the effective take as a re-amped WAV or the dry DI stem.
- **VST3**: the tone chain is also built as a VST3 plug-in for a DAW
  (`build/Tone_TabPlugin_artefacts/Release/VST3/Tone_Tab.vst3`).

### VST3 asset folder

The plug-in cannot find the assets next to the standalone automatically. Set
`library.root` to the Tone_Tab `assets` folder in
`%APPDATA%\Tone_Tab\plugin.xml`, or copy/symlink the assets to
`%APPDATA%\Tone_Tab\assets`.

Unsigned builds may be blocked by Windows Smart App Control ("无法验证发布者" /
"An Application Control policy has blocked this file"). Smart App Control has no
per-app allow list, and a self-signed certificate does **not** satisfy it — it
only trusts certificates from the Microsoft Trusted Root Program. To run a
locally built binary you have two options:

1. Turn **Smart App Control** off: Windows Security → App & browser control →
   Smart App Control → Settings → Off. On older Windows 11 builds this was
   one-way (it needed a Windows reset to re-enable); on current builds
   (24H2/25H2) it can be switched back on.
2. Sign `Tone_Tab.exe` and the VST3 with a certificate from the Microsoft
   Trusted Root Program (a CA/EV code-signing certificate or Azure Trusted
   Signing). A self-signed certificate is not accepted by Smart App Control.

The `release` folder keeps older builds. An older binary may still run if it was
already allowed on the machine, but only the current release (`v0.5.0`) contains
the loop station, the real-time Record timeline, non-destructive punch and WAV
export.

## Build

Requirements: Windows 10/11, Visual Studio 2022 Build Tools with the C++ workload
and Windows 11 SDK, CMake 3.22+.

```bat
build.bat
```

Or with an existing VS/CMake setup:

```bat
cmake -S . -B build -A x64 -DTONE_BUILD_TESTS=ON
cmake --build build --config Release --parallel
```

Outputs:

- `build/Tone_Tab_artefacts/Release/Tone_Tab.exe`
- `build/Tone_TabPlugin_artefacts/Release/VST3/Tone_Tab.vst3`
- `build/ToneTests_artefacts/Release/ToneTests.exe`

VS Code users can open the folder and use `CMakePresets.json`.

## Tests

The offline suite needs no audio device. It is embedded in the app because
Windows Smart App Control blocks a second unsigned console binary:

```powershell
.\build\Tone_Tab_artefacts\Release\Tone_Tab.exe --run-tests
```

The report is written to `build/test-out/embedded-tests.log`.

## Tone assets (local only)

`assets/` is **not** committed. Rebuild it from the local libraries:

```powershell
powershell -ExecutionPolicy Bypass -File tools\import_assets.ps1
```

The import script deduplicates by SHA-256, writes `assets/nam/index.json`, and
copies the IR packs and Gervana artwork. See `NAMING.md` for the ASCII path
policy and `assets/nam/README.md` for licensing notes.

## Recordings and presets

- Presets: `Documents\Tone_Tab\presets` (ASCII file names; display names are
  stored inside each XML and are bilingual).
- Recordings: configurable in the Record view (**Change Folder…**); default
  `Documents\Tone_Tab\recordings\<take>\`.
- Settings: `%APPDATA%\Tone_Tab\settings.xml`.
- Practice log: `%APPDATA%\Tone_Tab\practice_log.json`.

## License

AGPLv3. The original application and its source are by 痛风老大爷 (LianqinHost).
Third-party notices are in `licenses/THIRD_PARTY_NOTICES.txt`. JUCE is used under
its AGPLv3 option. NAM models keep their own licenses; see
`assets/nam/LICENSES/`.
