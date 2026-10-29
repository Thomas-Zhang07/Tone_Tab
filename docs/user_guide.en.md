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
- The preset dropdown starts with **Fender NAM**, **Mayer**, **Classic Rock** and
  **Captures**; Gervana is further down.
- NAM models expect 48 kHz. A model marked *amp only* needs a cab IR; a
  *full rig* model already contains the cabinet.
- **Drums** is its own top tab: large Start/Stop, a visible style grid
  (Rock/Pop, Groove, Metal/Blues/Jazz, Blues/Soul, Other), a 16-step pattern
  display with playhead, Verse/Chorus/Fill, Auto Fill, tempo/tap, swing,
  humanize, level and a per-piece mixer (kick, snare, hats, toms, cymbals).
  Space also starts/stops it.
- To make your own groove, select **Custom** (or click any style's grid to copy
  it into Custom). Left-click a cell to toggle a hit on/off, **Shift-click** for
  an accent, **Alt-click** for a ghost note, and right-click to clear it.
  **Edit: Verse/Chorus/Fill** chooses the part, **Clear Part** empties it, and
  the result is saved automatically. Verse and chorus use the same pattern on
  every bar; the fill plays when a fill bar comes around.

## Loop view (top tab)

- **SPACE** records. Press it again to close the loop; the loop starts playing
  immediately. While it plays, **SPACE** arms an overdub (it starts at the loop
  start) and **SPACE** again finishes it. **Stop** / **Esc** stops playback,
  **Clear** erases the loop.
- Start and close are quantised to **Snap to: Bar** by default, so the key
  press does not have to be on time:
  - press close *early* → it keeps recording until the bar line;
  - press close *late* → it trims back to the bar line.
  Choose **Beat** for shorter phrases or **Off** for free timing.
- With the drum machine playing, recording starts on the next drum bar and the
  loop stays locked to the drums. Start the drums first for the tightest
  result; the loop keeps playing when you switch tabs.
- **New loop length** can be **Auto** (close with a second press) or fixed at
  1 / 2 / 4 / 8 bars. A fixed length records with one press and starts looping
  by itself.
- The **LOOP** lane shows the take waveform with bar numbers and a playhead;
  the **LIVE** lane shows what you are playing right now (also before you start
  recording).
- Tempo is shared with the drum machine. Maximum loop length is 30 seconds.

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

The **Record** tab is the dedicated recording page. It has a waveform timeline,
the record button, the take list, replay, delete, refresh and open-folder
controls. The practice view's small record/replay buttons still work for
recording while a tab is open.

- Select a take to load its waveform (the dry stem that replay uses). **Click
  the waveform to replay from that point**; click again anywhere to jump while
  it plays. **Drag** to select a region (right-click clears it).
- **Play / Pause / Stop**: Play continues from the playhead (or starts at 0);
  Pause keeps the playhead where it is; Stop rewinds to the start. **SPACE**
  toggles play/pause. The playhead follows playback.
- **Full mix (off = dry)** switches playback between:
  - **On** — the recorded `mix.wav`: guitar + effects + drums + backing + loop
    exactly as you recorded it.
  - **Off** — the dry stem through the **current** effect chain (re-amping).
  It defaults to On. The waveform/playhead is the dry stem in both cases;
  punch edits only apply to the dry/re-amped path.
- The take list shows the take's date, length and whether it is synced to the tab.
- While **recording**, the timeline shows a live 15-second input waveform
  (`LIVE INPUT`) with the record head at the right.
- **Punch Selection**: drag a region, then press **Punch Selection**. You get a
  two-second pre-roll; the old part is muted during the punch so you hear your
  new playing, and recording stops automatically at the selection end. The
  punch is stored as a clip in the take's `clips/` folder and listed in
  `take.json`; `mix.wav` and `dry.wav` are never modified. Blue "PUNCH" bands on
  the timeline show the layers. Punching the same region again replaces it
  (the later clip wins).
- **Export WAV…**: choose **Re-amped (current chain)** to bounce the take with
  all punches through the current effect chain, **Dry DI stem** for the
  unprocessed mono take, or **Full recorded mix (pre-punch)** to export the
  original `mix.wav` (guitar + effects + drums + backing + loop as recorded;
  punch clips are not applied to this mode). The export uses the whole take,
  not just the selection.

- **Record** writes a take folder under the recordings folder (default
  `Documents\Tone_Tab\recordings`; use **Change Folder…** in the Record view to
  choose another location, e.g. `E:\guitar_audio\recordings`):
  - `mix.wav` — guitar + effects + drums + backing (stereo)
  - `dry.wav` — raw guitar before input gain (mono)
  - `take.json` — song, track, speed, loop, count-in, sync and punch list
  - `clips/` — non-destructive punch layers (only when you punch)
- **Replay…** plays the dry stem with all punches through the current effects
  chain, so you can audition a take with a different tone. The take list also
  shows older mono recordings.

## Keyboard

| Key | Action |
| --- | --- |
| Space | Practice: play / pause · Record: play / pause · Drums: start / stop · Loop: record / close / overdub |
| Esc (Loop) | Stop the loop |
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
