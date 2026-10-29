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
- **+ Add Tone / FX** opens the tone menu:
  - **Fender NAM** — all Fender captures straight from the library.
  - **Browse NAM Models…** — searchable browser for all 270 models, filtered by
    clean / crunch / high gain / boost / A2 full rig / starter.
  - **Load NAM Amp Model…** — file picker.
  - **Load Cab IR…**, then the built-in effect groups and VST3.
- The preset dropdown starts with **Fender NAM**, **Mayer** and **Captures**;
  the **Classic Rock** pack sits under **Classic**, and Gervana is further down.
- NAM models expect 48 kHz. A model marked *amp only* needs a cab IR; a
  *full rig* model already contains the cabinet.
- **Drums** is its own top tab: large Start/Stop, a visible style grid
  (Rock/Pop, Groove, Metal/Blues/Jazz, Blues/Soul, Other), a 16-step pattern
  display with playhead, Verse/Chorus/Fill, Auto Fill, tempo/tap, swing,
  humanize, level and a per-piece mixer (kick, snare, hats, toms, cymbals).
  Space also starts/stops it.
- To make your own groove, select **Custom** (or click any style's grid to copy
  it into Custom). Click a cell to cycle off → normal → accent → ghost;
  right-click clears it. **Edit: Verse/Chorus/Fill** chooses the part,
  **Clear Part** empties it, and the result is saved automatically.

## Practice view

- Open a `.gp3`–`.gp5`/`.gpx`/`.gp` file, or drag it onto the window.
- Pick a track, set the speed and an A-B loop, then play.
- **Play** scrolls the tab; **Demo** plays the selected track through the
  improved backing synth, with the other tracks as accompaniment.
- **Judge** (default on) scores Perfect/Good/wrong/miss from the dry guitar.
  Run **Latency Calibration…** once with the click.
- **Wait mode** stops at each note until you play it correctly.
- The speed trainer raises the tempo after a number of clean passes.

## Record view (top tab)

The **Record** tab is the dedicated recording page. It has the record button, the
take list, replay, delete, refresh and open-folder controls. The practice view's
small record/replay buttons still work for recording while a tab is open.

- **Record** writes a take folder under the recordings folder (default
  `Documents\Tone_Tab\recordings`; use **Change Folder…** in the Record view to
  choose another location, e.g. `E:\guitar_audio\recordings`):
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
