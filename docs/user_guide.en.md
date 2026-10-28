# Tone_Tab quick guide

## First run

1. Open **Audio Settings…** (top right).
2. Choose your interface (ASIO preferred), the guitar input channel, and a
   64–128 sample buffer.
3. Strum hard; aim for peaks around −12 to −6 dBFS. If the CLIP lamp lights,
   lower the interface gain.

## Tone view

- The preset bar loads and saves whole chains. Preset files are ASCII-named; the
  display name is bilingual and stored inside the XML.
- Click a pedal to bypass it; double-click to open its controls; drag to reorder;
  right-click for replace/duplicate/delete.
- **+ Add Effect** adds a built-in effect, a NAM amp model, a cab IR, or a VST3
  plug-in.
- NAM models expect 48 kHz. A model marked *amp only* needs a cab IR; a
  *full rig* model already contains the cabinet.
- The drum machine is at the bottom. Space starts/stops it in the tone view.
  Humanize varies velocity slightly; Auto Fill adds a fill every 2/4/8 bars.

## Practice view

- Open a `.gp3`–`.gp5`/`.gpx`/`.gp` file, or drag it onto the window.
- Pick a track, set the speed and an A-B loop, then play.
- **Play** scrolls the tab; **Demo** plays the selected track through the
  improved backing synth, with the other tracks as accompaniment.
- **Judge** (default on) scores Perfect/Good/wrong/miss from the dry guitar.
  Run **Latency Calibration…** once with the click.
- **Wait mode** stops at each note until you play it correctly.
- The speed trainer raises the tempo after a number of clean passes.

## Recording

- **Record** writes a take folder under `Documents\Tone_Tab\recordings`:
  - `mix.wav` — guitar + effects + drums + backing (stereo)
  - `dry.wav` — raw guitar before input gain (mono)
  - `take.json` — song, track, speed, loop, count-in and sync information
- **Replay…** plays the dry stem through the current effects chain, so you can
  audition a take with a different tone. The take list also shows older mono
  recordings.

## Keyboard

| Key | Action |
| --- | --- |
| Space | Play / stop |
| ← / → | Previous / next bar |
| Home | Back to the start |
| A / B | Set loop start / end |
| ↑ / ↓ | Speed up / slow down |
| W | Wait mode |
| N | Note names |

## Files

- Settings: `%APPDATA%\Tone_Tab\settings.xml`
- Presets: `Documents\Tone_Tab\presets`
- Recordings: `Documents\Tone_Tab\recordings`
- Practice log: `%APPDATA%\Tone_Tab\practice_log.json`
- Tone library: `assets\` next to the executable (local only)
