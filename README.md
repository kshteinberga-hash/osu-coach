# osu-coach

A personal coach for **osu! stable** (standard mode). It reads your local replays and beatmaps, works out where you lose notes and pp, and tells you what to practise next, with training maps from your own Songs folder.

![osu-coach overview](docs/screenshot.png)

It's one ~1 MB Windows exe with no installer, no account and no API key. It only reads files on your PC.

## What it measures

| Area | Metrics |
|---|---|
| **Timing** | hit error per note, mean (early/late), unstable rate, % of hits within ±15/30/50 ms, error histogram, UR per key |
| **Aim** | cursor distance from the note centre (in circle radii) per spacing bucket (dense ≤2r, stream 2–4r, jump 4–7r, big >7r), jump-distance histogram, miss rate per screen quadrant, overshoot vs undershoot |
| **Speed** | stream segments (4+ evenly spaced 1/4 notes within 3r): BPM, alternation, same-finger double taps, miss rate, early/late on notes 1–4 vs 5+; tapping by gap (<100, 100–130, 130–260, 260+ ms) with UR, alternation and finger lock (a key still held when the next press comes); stamina (longest run with no break over 500 ms) |
| **Reading** | miss rate on the first note after a break vs mid-flow; AR below 8 vs AR 8+ |
| **Per play** | every miss with its map timestamp (`m:ss.mmm`), rhythm, jump size and cause (aim / tap used up / outside the hit window / no press) |

## Coach

- **Skill radar (0–100).** 50 means you miss as often as *you usually do* on maps of that star rating. Your earlier plays are the baseline, and each halving of misses adds 25 points.
- **Top 3 weaknesses**, ranked by an estimate of the pp they cost per play. Each one comes with the measured number and a concrete fix.
- **Map suggestions.** Five maps from your Songs folder in a star range you choose (default 3.5–4.5★), scored on how well they train your weakest skill (jump share, stream share at your stream BPM, rhythm changes and AR, OD). Maps you haven't played much come first.
- **Progress.** `history.json` next to the exe keeps daily totals, so trends survive deleting maps. It compares this week with last week (UR, jump misses, stream misses) and gives a verdict: *"Ready to push to 4.5★"* or *"Still grinding 4.0★ — focus on Jumps"*.

## Use

1. Download `osu-coach.exe` from [Releases](../../releases) and run it.
2. It finds osu! through the file association or `%LOCALAPPDATA%\osu!`. If it can't, it asks for the folder that contains `osu!.db`.
3. Play, then press **Refresh**.

Only plays whose beatmap is still installed can be analysed. osu! keeps the replay of every local play in `Data\r`.

Command line:

```
osu-coach.exe --dump plays.jsonl [--osu PATH]   per-play stats as JSON lines, no window
osu-coach.exe --screenshot DIR                  save every tab as a BMP and exit
```

## Build

Needs Visual Studio 2022 (or the Build Tools) with the C++ workload.

```
build.bat          builds osu-coach.exe
build.bat test     builds and runs the unit tests
```

`tools/check.py` compares `--dump` output with the reference Python analyzer (`tools/ref_analyze.py`). On 457 real replays, simulated misses and UR match it exactly. Circle misses come within 2 of the game's own count on 97% of plays (stable doesn't count a missed slider head as a miss).

## How it works

- `src/parse.cpp` reads `.osr` (LZMA replay frames), `.osu` (INI-style sections, slider end times) and `osu!.db` (star ratings per mod).
- `src/analyze.cpp` re-runs the hit detection on the replay: hit windows `80−6·OD / 140−8·OD / 200−10·OD`, radius `54.4−4.48·CS`, HR/EZ/DT/HT applied. For every note it records the error, key, hold time, aim distance and miss cause.
- `src/coach.cpp` builds the radar, the pp estimate (standard aim/speed/accuracy structure from star rating, OD, length and misses), the weaknesses, the map scores and the history.
- `src/scan.cpp` analyses replays on all CPU cores: about 1 s for 450 plays. `src/main.cpp` and `src/gui.cpp` are the Dear ImGui / DirectX 11 window, which sleeps when idle.

Third-party code: [Dear ImGui](https://github.com/ocornut/imgui) (MIT) and the [LZMA SDK](https://www.7-zip.org/sdk.html) decoder (public domain), both in `third_party/`.

## License

MIT
