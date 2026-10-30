# osu-coach

A personal coach for **osu! stable** (standard mode). It reads the replays and maps already on your PC, finds what costs you notes and pp, and tells you in plain English what to practise next. It can also **play your replay back with the music and stop at every mistake to explain what went wrong.**

![osu-coach overview](docs/screenshot.png)

It's a single ~1.5 MB Windows exe: no installer, no account, no API key. It only reads files on your PC.

## Watch your replays with the coach

Pick a play on the **Plays** tab and press **Watch replay with the coach**. You'll see:

- the circles, sliders, spinners and approach circles in combo colours, timed and sized with your mods (HR is shown flipped, the way you played it); sliders show their ball, the follow circle while you hold, and reverse arrows
- your cursor's path through the actual replay frames, a dot wherever you clicked, and K1/K2/M1/M2 lighting up while held
- your 300s, 100s, 50s and misses so far, your UR, and the timing of the last note
- **key lanes** under the playfield: a zoomed strip around the playhead with every note (by result) and a bar for each key press showing how long you held it, in ms. Scroll to zoom, drag to scrub, click to jump
- a timeline of the whole play, with a red mark for every miss and an amber mark for every 100 or 50; hover it to see the time
- the song playing in sync, at 0.25×, 0.5×, 0.75× or 1× (slower speeds lower the pitch), with buttons to skip 5 s back or forward

The coach **stops at each mistake**. It draws a red ring on the note, plus a line from where you clicked to where you should have, and explains what happened, what to do instead and what to practise. A burst or stream that falls apart is **one stop**, not one per note, and the coach reads your key presses to tell *why* it fell apart: **finger lock** (your fingers stop taking turns, or one key stays down), **galloping** (uneven long-short taps, often from pressing one key too hard), rushing ahead of the music, falling behind, or aim slipping off the path. For example:

> **Finger lock in a stream.** This was a 12-note stream at 188 BPM (80 ms between notes). You kept up for the first 5 notes, then you pressed only 2 times for the last 7 notes, so 5 of them got no press of their own. K2 stayed down for 250 ms, 3.1 notes long: that finger froze on the key.
> **What to do:** Keep your fingers moving, K1, K2, K1, K2, even when you feel you're falling behind: a late tap still scores a 100 or 50, a frozen finger scores nothing.
> **Practise:** 12-note streams at about 170 BPM (10% slower than this one) until you can play ten in a row without locking, then go up 5 BPM at a time.

When it isn't stopped, the panel names the play's most common problem and what to practise for it.

Space plays and pauses, the arrow keys jump between mistakes, and Enter continues. Stops can be switched off, or extended to 50s.

![replay viewer](docs/viewer.png)

## What it tells you

| Tab | In plain words |
|---|---|
| **Overview** | the coach's verdict ("Ready to push to 4.5★" or "Still grinding 4.0★"), your skills compared with your usual, and the three things costing you the most pp, each with what to do |
| **Timing** | how steady your timing is (UR, lower is steadier), whether you click early or late, and how often you're within 15/30/50 ms of perfect |
| **Aim** | how often you miss close notes, short moves, jumps and big jumps (in circle widths), and whether you stop short of the circle or go past it |
| **Speed** | finger lock (still holding the last key when you press the next), switching fingers, speeding up in long streams, your comfortable stream BPM |
| **Reading** | misses on the first note after a pause, and on circles that appear slowly vs quickly (AR) |
| **Plays** | every play, and for each one every miss with its time and cause |
| **Practice maps** | five maps from your Songs folder that train your weakest skill |
| **Progress** | day-by-day timing, jump misses and stream misses, kept in `history.json` so it survives deleting maps |

## Use

1. Download `osu-coach.exe` from [Releases](../../releases) and run it.
2. It finds osu! by itself (file association, or `%LOCALAPPDATA%\osu!`). If it can't, it asks for the folder that has `osu!.db` in it.
3. Play some maps, then press **Refresh**.

Only plays whose map is still installed can be analysed. osu! keeps a replay of every local play in `Data\r`.

Command line:

```
osu-coach.exe --dump plays.jsonl [--osu PATH]   per-play stats as JSON lines, no window
osu-coach.exe --screenshot DIR                  save every tab and the viewer as BMPs, then exit
osu-coach.exe --synctest                        check that the music plays at the right speed
```

## Build

Needs Visual Studio 2022 (or the Build Tools) with the C++ workload.

```
build.bat          builds osu-coach.exe
build.bat test     builds and runs the unit tests
```

`tools/check.py` compares `--dump` output with the reference Python analyzer (`tools/ref_analyze.py`). On 466 real replays, simulated misses and UR match it exactly. Circle misses come within 2 of the game's own count on 97% of plays (stable doesn't count a missed slider head as a miss).

## How it works

- `src/parse.cpp` reads `.osr` (LZMA replay frames), `.osu` (sections, slider curves: straight, arc, bezier, catmull) and `osu!.db` (star ratings per mod).
- `src/analyze.cpp` re-runs hit detection on the replay: hit windows `80−6·OD / 140−8·OD / 200−10·OD`, radius `54.4−4.48·CS`, with HR/EZ/DT/HT applied. For every note it records the timing error, key, hold time, aim distance and why it was missed.
- `src/coach.cpp` builds the skill comparison, the pp estimate, the weaknesses and plain-English explanations, the practice-map scores and the history.
- `src/viewer.cpp`, `src/playback.cpp` and `src/audio.cpp` run the replay viewer. The music position drives the replay clock, so the two can't drift apart.
- `src/scan.cpp` analyses replays on all CPU cores, about 1 s for 450 plays. `src/main.cpp` and `src/gui.cpp` are the Dear ImGui / DirectX 11 window, which sleeps when idle.

Third-party code, all in `third_party/`:
- [Dear ImGui](https://github.com/ocornut/imgui) (MIT)
- the [LZMA SDK](https://www.7-zip.org/sdk.html) decoder (public domain)
- [miniaudio](https://github.com/mackron/miniaudio) and [stb_vorbis](https://github.com/nothings/stb) (public domain / MIT)
- the fonts [IBM Plex Sans](https://github.com/IBM/plex) and [Bricolage Grotesque](https://github.com/ateliertriay/bricolage) (SIL Open Font License)

## License

MIT
