# Tone_Tab

A Windows guitar practice and tone app: NAM amp captures, cab IRs, a pedal chain,
a drum machine, Guitar Pro practice, and recording.

Tone_Tab is a private fork of **LianqinHost** by 痛风老大爷, rebuilt with an English
interface, ASCII paths, a rewritten drum engine, and dual-stem recording.

## Features

- **Tone chain**: 22 built-in effects + 6 Gervana effects, NAM amp models, cabinet
  IRs, VST3 hosting, presets, tuner.
- **NAM library**: 270 unique captures (259 A1 + 11 A2 full-rig) and 42 IRs,
  merged from the local NAM and LianqinHost libraries. Category and curated-name
  aliases are kept in `assets/nam/index.json`.
- **Practice**: Guitar Pro 3–8 import, scrolling tab highway, demo/backing
  playback, judging (Perfect/Good/wrong/miss), wait mode, latency calibration,
  speed trainer, practice log.
- **Drums**: rewritten synthesized kit with layered kick/snare/hats, bus glue,
  humanize, A/B groove variations and style-specific fills.
- **Recording**: one take folder with `mix.wav` (guitar + effects + drums +
  backing, stereo) and `dry.wav` (raw guitar, mono), plus `take.json`.
- **VST3**: the tone chain is also built as a VST3 plug-in for a DAW
  (`build/Tone_TabPlugin_artefacts/Release/VST3/Tone_Tab.vst3`).

### VST3 asset folder

The plug-in cannot find the assets next to the standalone automatically. Set
`library.root` to the Tone_Tab `assets` folder in
`%APPDATA%\Tone_Tab\plugin.xml`, or copy/symlink the assets to
`%APPDATA%\Tone_Tab\assets`.

Unsigned builds may be blocked by Windows Smart App Control. If the standalone
runs but the VST3 is rejected by the DAW, allow the app/plug-in in Windows
Security or sign the binaries.

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
- Recordings: `Documents\Tone_Tab\recordings\<take>\`.
- Settings: `%APPDATA%\Tone_Tab\settings.xml`.
- Practice log: `%APPDATA%\Tone_Tab\practice_log.json`.

## License

AGPLv3. The original application and its source are by 痛风老大爷 (LianqinHost).
Third-party notices are in `licenses/THIRD_PARTY_NOTICES.txt`. JUCE is used under
its AGPLv3 option. NAM models keep their own licenses; see
`assets/nam/LICENSES/`.
