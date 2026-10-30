# Replay Viewer and Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
> **This project:** the user forbids subagents, so it runs with superpowers:executing-plans, inline.

**Goal:** Add a replay viewer with music and coach stops at each mistake, rewrite the GUI wording in plain English, and replace the violet look with the scoreboard visual system.

**Architecture:**
- The analysis engine is unchanged.
- `parse` gains slider curves and the audio file name.
- `coach` gains `explainMiss()` and the mistake list.
- New `audio` (miniaudio wrapper) and `viewer` (clock, stop tracker, drawing) units.
- `gui`/`main` get the new theme, fonts, wording and the viewer mode.

**Tech Stack:** C++20 on MSVC, Dear ImGui 1.91.9 with DX11, miniaudio 0.11.x, stb_vorbis 1.22, and the fonts IBM Plex Sans / IBM Plex Mono / Barlow Condensed (OFL), embedded with ImGui's `binary_to_compressed_c`.

**Spec:** `docs/superpowers/specs/2026-10-30-replay-viewer-and-redesign-design.md`

## Global Constraints

- **Banned everywhere:** Inter/Roboto, purple or indigo gradients, a centred hero with one CTA, a row of three icon cards, drop shadows, rounded corners on every element, an all-light layout. Also banned: glassmorphism, stock photos, floating icons.
- **Colours:**

  | Token | Value |
  |---|---|
  | ink | `#141210` |
  | panel | `#1C1915` |
  | rule | `#3A342C` |
  | cream | `#F2EADB` |
  | muted | `#B8AE9C` |
  | pink | `#FF66AA` |
  | green | `#C8F031` |
  | amber | `#FFB020` |
  | red | `#FF5A36` |

  Flat fills only, no gradients. Ink-coloured text on pink fills.
- **Corners:** radius 0 on panels, buttons, tabs, inputs, bars and charts. Round shapes only for osu! circles, approach circles, the cursor, click dots and the radar.
- **Fonts:**

  | Role | Font | Size |
  |---|---|---|
  | body | Plex Sans | 18 |
  | label (minimum) | Plex Sans SemiBold | 15 |
  | section title | Barlow Condensed SemiBold | 28 |
  | headline | Barlow Condensed SemiBold | 52 |
  | mono | Plex Mono | 17 |

  All are multiplied by the DPI scale.
- **Grid and targets:** 8 px grid (page padding 32, section padding 16, gaps 8/16/24). Every interactive control is at least 44 px tall.
- **Wording:** plain English per the spec's wording table. A technical name may follow in small muted text.
- **Viewer stops:** at misses by default; "Also stop at 50s" is off by default; volume defaults to 60%. The playback speed set is 0.25/0.5/0.75/1×; audio speed = `rate × speed` via pitch.
- **Idle:** the window waits for input unless the viewer is playing.
- **Commits:** every commit message ends with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. **DT/HT replays in the viewer:** with no audio, map time advances at `rate × speed` per real ms (test in Task 4, `test_clock_rate`).
2. **Seeking backwards:** a stop already passed fires again; seeking forwards past stops doesn't fire them (Task 4, `test_stops_seek`).
3. **Broken slider data:** a slider with only its start point, or an unknown curve letter, gives a valid path and no crash (Task 1, `test_slider_degenerate`).
4. **Missing or corrupt audio file:** `Audio::load` returns false and the viewer runs on the fallback clock (Task 3, `test_audio_missing`).
5. **HR replays:** objects are drawn flipped, `y' = 384 − y`, and cursor frames are drawn as recorded, so they line up (Task 4, `test_hr_draw_y`).

---

### Task 1: Slider curves and audio file in the parser

**Files:** Modify `src/parse.h`, `src/parse.cpp`. Test: `tests/test_curves.cpp` (new), registered in `tests/test.cpp`.

**Interfaces:**
- Produces:
```cpp
struct HitObj { float x, y; int t, end; uint8_t kind; bool nc; char curve = 'B'; int slides = 1; float length = 0;
                std::vector<std::pair<float, float>> pts; };  // pts = control points after the head (slider only)
struct Beatmap { ...; std::string audioFile; };               // [General] AudioFilename
std::vector<std::pair<float, float>> sliderPath(const HitObj& o);  // head first, ~4 px steps, total length == o.length
std::pair<float, float> sliderBall(const HitObj& o, const std::vector<std::pair<float, float>>& path, int t);  // map time
```
- The `.osu` slider field 5 is `C|x:y|x:y…`. A repeated consecutive point in B marks a new segment ("red anchor").

- [ ] **Step 1: Write the failing tests** in `tests/test_curves.cpp`. Lengths are summed segment distances.
  - `test_slider_linear`: L from (0,0) to (100,0), length 80 → last point (80,0) ±2, length 80 ±2.
  - `test_slider_perfect`: P through (0,0), (50,50), (100,0), length = the arc length → some point within 1 px of (50,50).
  - `test_slider_bezier`: B (0,0), (50,100), (100,0) → first point (0,0); length matches `o.length` ±2.
  - `test_slider_red_anchor`: B (0,0), (50,0), (50,0), (50,50), length 100 → passes within 1 px of (50,0) and ends at (50,50) ±2.
  - `test_slider_catmull`: C with 3 points → first point equals the head.
  - `test_slider_degenerate`: no `pts`, curve 'X', length 50 → returns at least 1 point, no NaN.
  - `test_slider_extend`: L (0,0)→(10,0), length 40 → last point (40,0) ±2.
  - `test_ball`: L length 100, slides 2, t from start to end → at the middle time the ball is at (100,0) ±3; at the end it is back at (0,0) ±3.
  - `test_osu_curve_parse`: parsing `100,100,2000,2,0,L|200:100,2,140` gives curve 'L', `pts == {(200,100)}`, slides 2, length 140, and `audioFile` from `AudioFilename: audio.mp3`.
- [ ] **Step 2:** Run `build.bat test`. Expected: compile/link FAIL.
- [ ] **Step 3: Implement.**
  - Parsing: keep the curve letter, points, slides and length in `parseOsu`.
  - `sliderPath`: build raw points per curve type:
    - **P:** circumscribed circle via the perpendicular-bisector intersection; arc direction from the cross product; falls back to B when collinear.
    - **B:** de Casteljau on each segment, 50 steps.
    - **C:** centripetal-free uniform catmull, 50 steps per span.
    - **L:** straight lines.
  - Then resample every 4 px and cut or extend to `length` (extend along the last direction).
  - `sliderBall`: `slideMs = (end − t0) / slides`, progress `p = clamp((t − t0) / slideMs, 0, slides)`. Even slides go forward, odd ones backward; position by arc length.
- [ ] **Step 4:** Run `build.bat test`. Expected: all checks pass.
- [ ] **Step 5:** Commit with "feat: slider curves and audio file name in the parser".

### Task 2: Plain-English explanations and the mistake list

**Files:** Modify `src/coach.h`, `src/coach.cpp`. Test: `tests/test_coach.cpp`.

**Interfaces:**
- Consumes `ObjRes`.
- Produces:
```cpp
struct Explain { std::string title, text, tip; };
Explain explainMiss(const ObjRes& o, float beatMs);                         // o.judge == 0 or 50
std::vector<int> mistakes(const std::vector<ObjRes>& objs, bool with50s);  // indexes, ascending time
```
- Copy is from the spec's explainMiss section, verbatim in meaning. Distances are `dist / 2` circle widths, one decimal. "Jump of {w} circle widths" uses `spacing / 2`, added when `spacing > 4`.
- Context clauses: `inStream` → " in the middle of a stream", `afterBreak` → " right after a pause", `slider` → " on a slider start".
- Titles:

  | Cause | Title |
  |---|---|
  | AIM | "Missed: aim" |
  | TAP | "Missed: one press for two notes" |
  | OFFTIME | "Missed: too early" |
  | NOPRESS | "Missed: no click" |
  | judge 50 | "50: early" or "50: late" |

- Rewrite the `Weakness.metric`/`fix` texts in `coach()` using the spec's wording table, e.g. "You miss 1.3% of jumps (2–3 circle widths); your usual at this star rating is 1.6%."

- [ ] **Step 1: Write the failing tests.**
  - `test_explain_aim`: AIM, along −1, dist 2.4, spacing 9, err +20 → title "Missed: aim"; text contains "stopped short", "1.2 circle widths", "jump of 4.5 circle widths"; tip contains "all the way to the centre".
  - `test_explain_over`: along +1 → text contains "went past".
  - `test_explain_tap` → text contains "pressed once for two notes".
  - `test_explain_early`: OFFTIME, err −160 → text contains "160 ms too early".
  - `test_explain_nopress` → "didn't click".
  - `test_explain_50`: judge 50, err +70 → title "50: late".
  - `test_mistakes`: objs with judges 300, 0, 50, 0 → `mistakes(objs, false) == {1, 3}` and `mistakes(objs, true) == {1, 2, 3}`.
  - The existing `test_radar_half` still requires a non-empty metric and fix.
- [ ] **Step 2:** Run `build.bat test`. Expected: FAIL.
- [ ] **Step 3:** Implement with `sf`-style formatting. No new state.
- [ ] **Step 4:** Run `build.bat test`. Expected: pass.
- [ ] **Step 5:** Commit with "feat: plain-English miss explanations".

### Task 3: Vendoring (miniaudio, stb_vorbis, fonts) and the audio wrapper

**Files:** Create `src/audio.h`, `src/audio.cpp`, `tools/embed_fonts.bat`. Add `third_party/miniaudio/miniaudio.h` + LICENSE, `third_party/stb/stb_vorbis.c` + LICENSE, and `third_party/fonts/{IBMPlexSans-Regular,IBMPlexSans-SemiBold,IBMPlexMono-Regular,BarlowCondensed-SemiBold}.ttf` + `OFL.txt` + generated `fonts.inl`. Modify `build.bat`. Test: `tests/test_audio.cpp`.

**Interfaces:**
- Produces:
```cpp
class Audio {  // pimpl over ma_engine + ma_sound; all methods safe when not loaded
public:
    Audio(); ~Audio();
    bool load(const std::wstring& path);  // false: missing/undecodable
    bool loaded() const;
    void play(); void pause(); bool playing() const;
    void seekMs(double mapMs);            // map time = audio time
    void setSpeed(float s);               // pitch = s (rate x user speed)
    void setVolume(float v01);
    double positionMs() const;            // audio cursor, map time
    bool ended() const;
};
```
- `fonts.inl` defines `kFontPlexSans`, `kFontPlexSansSemi`, `kFontPlexMono`, `kFontBarlowSemi`, each `const unsigned int X_compressed_size` and `X_compressed_data[]`, generated by ImGui's `misc/fonts/binary_to_compressed_c.cpp` (also vendored, built by `tools/embed_fonts.bat`).
- Sources:
  - miniaudio `0.11.21` from github.com/mackron/miniaudio tag; stb_vorbis from github.com/nothings/stb at master `stb_vorbis.c`.
  - Fonts from github.com/IBM/plex (`packages/plex-sans`, `plex-mono`, TTF) and github.com/jenskutilek/… or Google Fonts `ofl/barlowcondensed/BarlowCondensed-SemiBold.ttf`.
- `build.bat`: build `build\audio_impl.obj` once (`#define MINIAUDIO_IMPLEMENTATION` + stb_vorbis in a tiny `src/audio_impl.c`), and link `ole32 winmm` for the WASAPI backend.

- [ ] **Step 1:** Download the vendored files listed above. Verify each one exists and the font files start with `00 01 00 00`. Run `tools/embed_fonts.bat` → `third_party/fonts/fonts.inl`. Expected: 4 arrays defined.
- [ ] **Step 2: Write the failing tests** in `tests/test_audio.cpp`.
  - `test_audio_missing`: `Audio a; a.load(L"nope.mp3")` is false, `a.loaded()` is false, `positionMs()` is 0, and play/pause/seek don't crash.
  - `test_audio_real`, only with `OSU_DIR`: load the audio of the newest replay's map; it loads. `seekMs(10000)`, then `positionMs()` is 10000 ±30. play, sleep 500 ms, pause → position advanced 500 ±80. `setSpeed(0.5)`, play, 500 ms → advanced 250 ±60.
- [ ] **Step 3:** Run `build.bat test`. Expected: FAIL (no Audio).
- [ ] **Step 4:** Implement `Audio` with `ma_engine_init` (default device), `ma_sound_init_from_file_w` (flags `MA_SOUND_FLAG_DECODE` so seeking is exact), `ma_sound_set_pitch`, `ma_sound_seek_to_pcm_frame`, `ma_sound_get_cursor_in_seconds`, `ma_sound_at_end`.
- [ ] **Step 5:** Run `build.bat test` with OSU_DIR set. Expected: pass.
- [ ] **Step 6:** Commit with "feat: vendored miniaudio, stb_vorbis and OFL fonts; audio wrapper".

### Task 4: Viewer core (clock, stops, coordinates)

**Files:** Create `src/viewer.h`, `src/viewer.cpp` (logic part). Test: `tests/test_viewer.cpp`.

**Interfaces:**
- Consumes `Audio` (Task 3), `mistakes()` (Task 2), `sliderPath`/`sliderBall` (Task 1), `analyze()`.
- Produces:
```cpp
struct Clock {                         // map time in ms
    double t = 0; float rate = 1, speed = 1; bool running = false;
    void advance(double realMs) { if (running) t += realMs * rate * speed; }
};
struct Stops {                         // coach stop bookkeeping over mistake times (map ms, ascending)
    std::vector<int> times; int next = 0;
    void seek(double t);               // next = first stop with time > t (re-arms stops after t)
    bool cross(double from, double to);// true if a stop lies in (from, to]; advances next
};
float drawY(float y, bool hr);          // hr ? 384 - y : y  (objects only; frames drawn as recorded)
struct Viewer;                          // full state, see Task 6
```

- [ ] **Step 1: Write the failing tests.**
  - `test_clock_rate`: rate 1.5, speed 0.5, running, `advance(1000)` → t 750.
  - `test_stops_seek`: times {1000, 2000, 3000}.
    - After `seek(0)`, `cross(0, 1500)` is true, and `cross(1500, 1600)` is false.
    - After `seek(2500)`, `cross(2500, 2900)` is false and `cross(2900, 3100)` is true.
    - After `seek(500)`, `cross(500, 1000)` is true (re-armed).
  - `test_hr_draw_y`: `drawY(100, true) == 284` and `drawY(100, false) == 100`.
- [ ] **Step 2:** Run `build.bat test`. Expected: FAIL.
- [ ] **Step 3:** Implement `Stops::seek` with `std::upper_bound`; `cross` checks `times[next] <= to`.
- [ ] **Step 4:** Run `build.bat test`. Expected: pass.
- [ ] **Step 5:** Commit with "feat: viewer clock, stops and coordinates".

### Task 5: Theme, fonts and plain-English GUI

**Files:** Modify `src/gui.cpp`, `src/gui.h`.

**Interfaces:**
- Consumes `fonts.inl` (Task 3) and `explainMiss` (Task 2).
- Keeps `setupStyle(float dpi)`, `drawApp(App&)` and `gForceTab`.
- Font enum becomes `BODY, LABEL, TITLE, HEAD, MONO`, matching the Global Constraints sizes.

- [ ] **Step 1:** Replace the palette, style and fonts in `setupStyle`:
  - Fonts load from memory with `AddFontFromMemoryCompressedTTF`; ★ ↑ ↓ → · ± × ≤ ✕ ↺ glyphs come from Segoe UI Symbol (merged).
  - Every rounding is 0, except a new `GrabRounding` 0 too.
  - `FramePadding.y` = (44 − 18) / 2 = 13.
- [ ] **Step 2:** Rework the layout helpers:
  - `card` becomes `section`: a panel fill, a 1 px rule border, radius 0.
  - Charts use flat pink bars: no `AddRectFilledMultiColor`, no area gradient. The area chart becomes a 2 px pink line with a flat `alpha(pink, 40)` fill.
  - Tabs become square 44 px tabs with a 4 px pink underline on the active one.
  - Each tab opens with one HEAD number on the left and fact rows on the right, separated by rules (no rows of identical cards).
- [ ] **Step 3:** Rewrite every label and caption with the spec's wording table. The Plays miss list uses the `explainMiss().title` text. Add a **Watch replay** button (pink, 44 px) to the selected-play section; it sets `App::viewing = true`.
- [ ] **Step 4:** Build. Then run `osu-coach.exe --screenshot build\shotsR`, convert to PNG and check all 8 tabs:
  - no gradient fills, square corners
  - the smallest text is ≥ 15 px × DPI (measure a LABEL glyph's cap height in the PNG, about 11 px at 100%)
  - every control is 44 px
  - plain wording
  
  Run `build.bat test`. Expected: pass.
- [ ] **Step 5:** Commit with "feat: scoreboard theme, embedded fonts, plain-English wording".

### Task 6: Replay viewer screen

**Files:** Modify `src/viewer.h`, `src/viewer.cpp`, `src/gui.h` (`App::viewing`, `App::viewer`), `src/gui.cpp` (route to `drawViewer`), `src/main.cpp` (continuous rendering while playing; `--screenshot` adds `viewer.bmp`).

**Interfaces:**
- Produces:
```cpp
struct Viewer {
    bool ok = false; std::string error; std::wstring audioNote;
    PlayStats play; Replay rep; Beatmap map; std::vector<ObjRes> objs; std::vector<std::vector<std::pair<float,float>>> paths;
    std::vector<int> miss;              // mistakes(objs, stop50) indexes
    Clock clock; Stops stops; Audio audio; bool stop50 = false, coach = true, muted = false; float volume = 0.6f;
    int stoppedAt = -1;                 // index into objs while paused at a coach stop
    bool open(const std::wstring& osuDir, const PlayStats& p, const std::vector<DbMap>& db);
};
bool viewerWantsFrames(const Viewer& v);   // true while playing
void drawViewer(App& a);                   // whole-window viewer UI; Back sets a.viewing = false
```
- **Time base:** `ObjRes.t` is real ms, so map ms = `t × play.rate`. The clock runs in map ms. `clock.rate = play.rate`.
- **Audio:**
  - While audio is loaded and playing, each frame sets `clock.t = audio.positionMs()`. Otherwise `clock.advance(realMs)`.
  - Seek, resume and speed changes call `audio.seekMs(clock.t)` and `audio.setSpeed(rate × speed)`.
- **Drawing order:**
  1. playfield border
  2. sliders (body = path polyline, thickness 2r × 0.9 in `rule`, outline `cream` 3 px)
  3. circles (reverse time order, so the earliest is on top)
  4. approach circles
  5. judgement marks
  6. click dots
  7. cursor trail
  8. cursor
- **Stop overlay:** a red ring on the note and, for aim misses, a red line from the click to the centre.

- [ ] **Step 1:** Implement `Viewer::open`:
  - read the replay (`Data\r\` + file), the map, `analyze()` objs, and `sliderPath` for each slider
  - `audio.load(mapFolder + audioFile)`, setting `audioNote` on failure
  - `miss = mistakes(objs, stop50)`, and `stops.times` = those objects' map times
- [ ] **Step 2:** Implement `drawViewer`:
  - **Layout:** a 70/30 split; the timeline (44 px, click/drag seeks, red/amber ticks) and the controls row below it.
  - **Keys:** Space toggles play, ← / → jump between mistakes, Enter = Continue.
  - **Coach panel:** stopped shows `explainMiss` title/text/tip plus Continue; otherwise the clickable mistake list (44 px rows).
  - Key indicators K1/K2 as 44 px squares.
- [ ] **Step 3:** Wire it in:
  - `main.cpp` renders without `WaitMessage` while `viewerWantsFrames()`.
  - `--screenshot` additionally opens the viewer on the newest play with a miss, seeks to its first stop, renders 4 frames and saves `viewer.bmp`.
  - Watch replay opens it, and Back returns to Plays.
- [ ] **Step 4:** Build. Run `build.bat test` (expected: pass). Then `--screenshot build\shotsV` and check `viewer.png`:
  - the playfield shows the circles and cursor
  - the red ring and line are on the missed note
  - the panel shows the plain-English text
  - the timeline has ticks
  - no gradients, square corners
- [ ] **Step 5: Sync check.** Run `osu-coach.exe --synctest`, a hidden flag that opens the same viewer, plays 30 s at 1× and then 10 s at 0.5× with the window hidden, logs `max |clock.t − audio.positionMs()|`, prints it and exits. Expected: ≤ 15 ms at each speed.
- [ ] **Step 6:** Commit with "feat: replay viewer with music and coach stops".

### Task 7: Docs, verification, publish

**Files:** Modify `README.md` (viewer section, plain wording, new screenshots `docs/screenshot.png` and `docs/viewer.png`, credits for miniaudio, stb_vorbis and the fonts).

- [ ] **Step 1:** Run `build.bat test`. Then `--dump` + `tools/check.py`. Expected: 100% parity, since the analysis is unchanged.
- [ ] **Step 2:** Measure idle CPU with the normal GUI open and the viewer paused. Expected: about 0 ms over 10 s.
- [ ] **Step 3:** Update the README and screenshots. Commit with "docs: viewer and redesign". Push `main`.
- [ ] **Step 4:** Ask the user before publishing release v0.3.0 (an outward-facing step), and ask them to click through the viewer with osu! closed.
