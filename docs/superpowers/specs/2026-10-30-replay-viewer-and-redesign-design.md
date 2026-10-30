# Replay viewer, plain-English wording and scoreboard redesign

Date: 2026-10-30. Status: design approved in chat, pending written review.
Builds on `2026-10-30-osu-coach-design.md`. The analysis engine is unchanged.

## Goal

1. **Replay viewer.** Play a replay back with its music, showing the circles, sliders, your cursor and your clicks. The coach pauses at each mistake and explains it in simple English.
2. **Readable and plain.** Bigger text everywhere, and every technical term rewritten so a beginner understands it.
3. **A look with a point of view.** Replace the v0.2.0 violet dashboard, which used things the user has now banned.

## Banned (user's list)

These must not appear anywhere in the app:
- Inter or Roboto, or any font with no personality
- purple or indigo gradients
- a centred hero with one call to action
- a row of three icon cards
- faint drop shadows on everything
- rounded corners on every element
- a white or light-grey layout with no point of view

Earlier constraints still apply:
- no glassmorphism, stock photos or floating icons
- an 8 px spacing grid
- high contrast
- 44 px touch targets

## Visual system

| Token | Value | Use |
|---|---|---|
| `ink` | `#141210` | window background |
| `panel` | `#1C1915` | panels, table rows (alternating with `ink`) |
| `rule` | `#3A342C` | 1 px dividers and borders |
| `cream` | `#F2EADB` | main text (contrast on `ink` ≈ 15.8:1) |
| `muted` | `#B8AE9C` | secondary text (≈ 8.3:1 on `ink`, ≥ 7:1 on `panel`) |
| `pink` | `#FF66AA` | the one accent: active tab, primary buttons, headline numbers, cursor (≈ 6.4:1 on `ink`) |
| `green` | `#C8F031` | good / improving / 300 |
| `amber` | `#FFB020` | 100 and 50 |
| `red` | `#FF5A36` | miss / regressing / finger lock |

- **Flat colour only:** no gradients and no shadows.
- **Corners:** panels, buttons, tabs, inputs, bars and charts are square (radius 0). The only round shapes are osu! circles, approach circles, the cursor, click dots, and the radar's rings.
- **Text on accent fills:** active tabs and primary buttons use `ink` text on `pink`, about 6.4:1.
- **Colour is never the only signal:** every green/amber/red value also has a word or symbol (✕, "early", "improving").

**Type.** Embedded as compressed TTF in the exe, all SIL Open Font License; licences go in `third_party/fonts/`.

| Role | Font | Size |
|---|---|---|
| Body text, buttons, table cells | IBM Plex Sans Regular | 18 px |
| Labels, captions (the minimum anywhere) | IBM Plex Sans SemiBold | 15 px |
| Section titles | Barlow Condensed SemiBold | 28 px |
| Headline numbers | Barlow Condensed SemiBold | 52 px |
| Timestamps, ms values, table numbers | IBM Plex Mono Regular | 17 px |

Text sizes are at 100% scale; everything is multiplied by the monitor's DPI scale.

**Layout**
- Left-aligned and asymmetric. Each tab opens with one headline number at the left (Barlow 52 px) and supporting facts in rows to its right, separated by 1 px rules, like a scoresheet. There is no centred hero and no grid of identical cards.
- Spacing is on the 8 px grid: page padding 32, section padding 16, gaps 8/16/24.
- Every interactive control is at least 44 px tall: tabs, buttons, combos, sliders, the timeline and table rows. Tabs sit in one row of square 44 px tabs with a 4 px pink underline on the active one.

## Plain-English wording

Each technical term is replaced by a plain phrase. Where the technical name helps learning, it follows in small `muted` text.

| Before | After |
|---|---|
| Unstable rate 229 | **Timing steadiness 229** — "UR, lower is steadier" |
| Mean offset −1.1 ms | **You click 1 ms early on average** (or "late", or "right on time" within ±2 ms) |
| Within ±15 ms | **Clicks within 15 ms of perfect** |
| Dense / stream spacing / jumps / big jumps (r) | **Close notes / short moves / jumps / big jumps**, sizes in **circle widths** (1 width = 2 radii) |
| Mean aim error 0.36 r | **Average miss of the centre: 0.2 circle widths** |
| Undershoot / overshoot | **Stopped short of the circle / went past the circle** |
| 1/4, 1/2, 1/1 | **Fast notes (stream speed) / normal notes / slow notes** |
| Alternate % | **Switching fingers** |
| Overlap %, finger lock | **Holding a key too long (finger lock)** |
| Switch zone 100–130 ms | **Notes 100–130 ms apart (your hardest speed)** |
| First note after a break | **First note after a pause** |
| AR below 8 / AR 8+ | **Slow-appearing / fast-appearing circles (AR)** |
| Skill radar, 50 = usual | **Your skills compared with your usual. 50 = normal for you, higher = better than usual** |
| pp lost per play | **Costs you about N pp per play** |
| Miss causes | **Aim:** "you clicked on time but missed the circle". **Tap:** "one click was used up by the note before". **Timing:** "you clicked too early". **No press:** "you didn't click" |

Coach weakness text (`metric`, `fix`) is rewritten in the same plain style.

## Replay viewer

**Opening it**
- On the Plays tab, the selected play gets a **Watch replay** button (44 px, pink). The viewer takes over the window, and **Back** returns to Plays with the same play selected.
- The viewer loads the replay frames, the map with its slider paths, the per-note analysis (`analyze()`), and the audio, all on open. It needs no network.
- If the replay or map is gone, it shows "This replay or its map is no longer on your PC" and only Back.

**Layout**
- The left ~70% is the playfield. The right ~30% is the coach panel.
- The full-width timeline sits underneath, with the controls row below it.

**Playfield**
- Drawn in osu! pixel space (512×384), scaled uniformly to fit, with a 1 px `rule` border.
- With HR the map is drawn flipped vertically, as the player saw it.
- **Objects** are visible from `t − preempt` to their end.
  - Preempt comes from AR (mods applied): `AR < 5 ? 1800 − 120·AR : 1200 − 150·(AR − 5)` ms. Objects fade in over the first 400 ms of that.
  - Circle radius is `54.4 − 4.48·CS`, with mods applied.
- **Circles:** `panel` fill, a 3 px `cream` outline, and the combo number in Barlow. The approach circle is a 2 px `pink` ring shrinking from 3× to 1× the radius.
- **Sliders:** the body is drawn as a thick path in `rule`, with a `cream` outline, a head circle, and a ball that follows the path while the slider is active. Repeat arrows are shown as "↺".
- **Spinners:** a large `rule` ring at the centre with the remaining time.
- **Results:** a judgement appears at each note's time and fades over 600 ms. A 300 shows nothing. A 100 shows "100" in `amber`, a 50 shows "50" in `amber`, and a miss shows a `red` ✕.
- **You:**
  - The cursor is a `pink` dot with a 150 ms trail of fading `pink` line segments.
  - Each key press leaves a dot where it landed for 600 ms: `cream` for key 1, `green` for key 2, outlined.
  - Two key indicators sit in the panel corner: square 44 px boxes labelled K1 and K2 that fill while the key is held.

**Timeline**
- A full-width bar 44 px tall. Clicking or dragging seeks.
- Ticks mark every miss (`red`, full height) and every 100/50 (`amber`, half height). The playhead is a 2 px `cream` line.
- Elapsed and total time are shown in Plex Mono.

**Controls** (all 44 px)
- Play/Pause (Space)
- Speed 0.25× / 0.5× / 0.75× / 1× (a segmented control)
- Previous / Next mistake (← / →)
- "Coach stops at mistakes" checkbox, on by default
- "Also stop at 50s" checkbox, off by default
- Volume slider and Mute

**Coach stops**
- While "Coach stops" is on and playback crosses a mistake's time, playback pauses exactly there.
- The playfield shows that frame and highlights the note with a `red` ring. For aim mistakes it also draws a 2 px `red` line from where you clicked to the note's centre.
- **Coach panel:**
  - the timestamp (`m:ss.mmm`, Plex Mono)
  - a title: "Missed: aim", "Missed: extra/missing tap", "Missed: too early", "Missed: no click", or "50: late"
  - the explanation from `explainMiss()`
  - "What to do" with one tip
  - **Continue** (Enter), which resumes and won't stop at the same mistake again until you seek backwards
- When not stopped, the panel lists every mistake (time + one-line summary). Clicking one jumps there (rows 44 px).
- **Grouped stops (`coachStops()`):** a burst or stream (3+ notes, each within 1/4 beat of the last) with mistakes in it is one stop, at its first mistake. `explainStop()` reads the key presses (`taps()`, with release times) over the run and names the cause: finger lock (fewer presses than notes, 3+ presses of one key in a row, or a key held over 1.5 notes), galloping (K1→K2 and K2→K1 intervals differ by 30%+ of the note gap; a key held 1.5× longer than the other is called out as pressing too hard), rushing / falling behind (hit error drifts by 12 ms or a quarter gap), or aim (half the misses are aim). Every explanation has a "Practise" line with the run's length and a BPM about 10% below it.
- When not stopped, the panel heads the list with `summarize()`: the most common cause across the stops and its practice advice.

**`explainMiss(const ObjRes&, const ObjRes* prev, float beatMs)` → `{title, text, tip}`** in `coach.cpp`. It is plain English built from the cause and its context:
- **Aim:**
  - "You clicked on time ({ms} ms {early/late}), but your cursor was {d} circle widths from the centre."
  - Plus the direction: "You stopped short of it" / "You went past it" / "You were off to the side".
  - Plus the move: "This was a jump of {w} circle widths." Plus "in the middle of a stream" / "right after a pause" / "on a slider start" when those apply.
  - Tips:
    - stopped short: "Move all the way to the centre; don't slow down before you arrive."
    - went past: "Slow down at the end of the move and stop on the circle."
    - off to the side: "Watch the circle, not your cursor."
- **Tap:** "You pressed once for two notes: that press counted for the note before, so this one got nothing." Tip: "Tap every note; in fast notes switch fingers each time."
- **Timing:** "You clicked {ms} ms too early: outside the window for this note." Tip: "Wait for the approach circle to touch the circle, and follow the music, not your fingers."
- **No press:** "You didn't click anywhere near this note." Tip: "Keep tapping to the rhythm even when you're lost; a guess can still hit."
- **50 (optional stop):** "You hit it, but {ms} ms {early/late}: only a 50." Tip: "Steady rhythm: listen to the beat."
- Distances are reported in circle widths (radii ÷ 2), rounded to 0.1.

The Plays tab's miss list uses the same `explainMiss()` titles, so wording is consistent.

**Clock and music**
- Audio comes from **miniaudio** (`third_party/miniaudio/miniaudio.h`, MIT-0 / public domain) with **stb_vorbis** for .ogg; .mp3 and .wav are built into miniaudio. The file is `[General] AudioFilename` in the map's folder.
- **Rate:** playback runs at `rate × speed`, where rate is 1.5 for DT/NC and 0.75 for HT. It is applied with `ma_sound_set_pitch`, so pitch changes with speed: DT sounds like NC, and 0.5× sounds lower. This is accepted to avoid a time-stretch library.
- **Sync:**
  - While playing, the replay time is the audio position in ms (`ma_sound_get_cursor_in_seconds`). Map time = audio time, because .osu timestamps are audio positions.
  - When the audio has ended or is missing, a monotonic clock (`QueryPerformanceCounter`) advances map time at `rate × speed`.
  - Seeking or resuming calls `ma_sound_seek_to_pcm_frame`.
- **Volume:** 0–100% with mute; the default is 60% so it isn't louder than osu!.
- **Missing audio:** "Music file not found, playing silently" in `muted`, and playback still works.
- **Repaint:** the window renders continuously only while the viewer is playing. Paused or stopped, it goes back to waiting for input (idle CPU ≈ 0).

**Slider paths:** `parse.cpp` keeps each slider's curve.
- `HitObj` gains `curveType` (L/P/B/C), control points, `slides` and `pixelLength`.
- `Beatmap` gains `audioFile`.
- `std::vector<std::pair<float, float>> sliderPath(const HitObj&)` (no ImGui types in the parser) returns points every ~4 osu! px, trimmed or extended to `pixelLength`:
  - **L:** straight segments
  - **P:** a circular arc through the three points; falls back to bezier when they are collinear
  - **B:** bezier, split into segments at repeated (red) anchor points
  - **C:** catmull-rom
- Repeats bounce along the same path. The ball position at time t = path position at `((t − start) / slideDuration)`, with every other slide reversed.

## Files

| File | Change |
|---|---|
| `src/parse.h/.cpp` | Slider curve data, `audioFile`, `sliderPath()` |
| `src/coach.h/.cpp` | `explainMiss()`; plain-English metric and fix text |
| `src/audio.h/.cpp` (new) | `Audio` wrapper: `load(path)`, `play()`, `pause()`, `seekMs()`, `setSpeed()`, `setVolume()`, `positionMs()`, `playing()`, `loaded()` |
| `src/viewer.h/.cpp` (new) | `Viewer` state (play, frames, objects, analysis, clock, stops) and `drawViewer(App&)` |
| `src/gui.cpp`, `src/main.cpp` | New theme, fonts, wording and layout; Watch button; viewer mode; continuous rendering while playing; `--screenshot` also captures the viewer at the first coach stop |
| `third_party/miniaudio/`, `third_party/stb/`, `third_party/fonts/` | Vendored with their licences; fonts as compressed C arrays (`*.inl`) |
| `build.bat` | Compile `audio.cpp` with miniaudio's implementation once into `build\audio.obj` |

The target is about +900 lines of our own code. The exe grows by about 0.5 MB for fonts and audio.

## Errors

- **Missing audio:** play silently (see above).
- **Corrupt audio:** miniaudio fails to load, so it is handled like missing audio.
- **Map or replay missing:** the viewer shows the message plus Back.
- **No mistakes in a play:** the panel shows "Clean play: nothing to stop at" and playback still works.
- **Unknown slider curve letter:** treated as bezier.
- **Fewer than 2 control points:** the slider is drawn as a circle only.

## Testing

**Unit tests** (`tests/`):
- `sliderPath`:
  - L: length equals `pixelLength` within 2 px, and the last point is on the segment
  - P: the arc passes within 1 px of the middle control point
  - B: starts at the first point, ends after `pixelLength`
  - B with a red anchor: two segments
  - C: endpoints
  - a too-short path is extended along the last direction
- `explainMiss`: for each cause, the title, and that the text contains its key words ("short of", "went past", "too early", "didn't click", "one press for two notes"). Distances are in circle widths.
- The mistake list for the timeline: sorted, misses plus optional 50s.

**Real data:** `--dump` parity with the Python reference stays at 100%, because the analysis is unchanged.

**Render check:** `--screenshot DIR` saves all 8 tabs plus `viewer.bmp`, the viewer at the first coach stop of the newest play with a miss. I check all of them against the banned list and the visual system: square corners, no gradients, nothing under 15 px, 44 px controls.

**Sync:** a test hook logs `|replayMs − audioMs|` every second for 30 s of playback. The pass mark is ≤ 15 ms drift at 1× and 0.5×.

**Manual:** the user clicks through the viewer while osu! is closed. I can't drive the GUI while osu! is in front.
