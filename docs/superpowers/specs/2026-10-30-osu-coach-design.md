# osu-coach — design

Date: 2026-10-30. Status: approved in chat, pending written review.

## Goal

A single Windows executable, `osu-coach.exe`, that acts as a personal coach for osu! stable (standard mode). It reads the player's local replays and beatmaps, measures timing, aim, speed and reading, finds the weaknesses that cost the most pp, suggests training maps from the player's own Songs folder, and tracks progress week over week. The aim is to help a player who has plateaued (around 4★ today) climb tier by tier toward 6★+.

Constraints from the user:
- C++ for the core. Other languages only where they clearly fit (none are needed).
- Lean code. Fewer lines and faster runs win, and performance is a priority.
- Delivered as a GUI app and published to a public GitHub repo, with the exe attached to a release.

## Non-goals

- osu!lazer, taiko, catch and mania. Replays from those are skipped.
- An exact pp or star-rating calculator. Stars come from `osu!.db`, and pp is only an estimate used to rank weaknesses.
- Online features: no osu! API keys and no uploads. The only network use is opening a beatmap page in the browser when the user clicks.

## Architecture

```
osu-coach/
  src/parse.h/.cpp    .osr, .osu and osu!.db readers
  src/analyze.h/.cpp  hit simulation and per-play metrics
  src/coach.h/.cpp    aggregation, radar, weaknesses, map picks, history
  src/main.cpp        Win32 + DX11 window, ImGui screens, --dump CLI
  third_party/imgui/  Dear ImGui core + imgui_impl_win32 + imgui_impl_dx11
  third_party/lzma/   LZMA SDK decoder (LzmaDec.c, public domain)
  build.bat           cl.exe /O2 /MT, one static exe
  tools/check.py      compares --dump output with the reference Python analysis
```

Target: about 1,500 lines of our own code, not counting `third_party`.

### Module 1 — parsers (`parse`)

- **Replay (.osr):** mode, version, beatmap md5, player name, 300/100/50/geki/katu/miss counts, score, max combo, perfect flag, mods, timestamp (Windows ticks → UTC), and frames. The header is parsed separately (`readOsrHeader`) so that filtered replays are never decompressed. Frames are LZMA-decoded and turned into `{t, x, y, keys}`. The `-12345` seed frame is dropped.
- **Beatmap (.osu):** a hand-written INI-style reader for `[General]`, `[Metadata]`, `[Difficulty]`, `[TimingPoints]` and `[HitObjects]`. It extracts AR (defaults to OD when missing), CS, OD, HP, BeatmapID, title, the main BPM (the uninherited beat length with the longest duration), and the objects `{x, y, t, type (circle/slider/spinner), newCombo, endTime}`. Slider end times come from the pixel length, the slider multiplier, SV from inherited points, and the repeats.
- **osu!.db:** md5 → (folder, .osu filename), beatmap id, set id, ranked status, AR/CS/HP/OD, std star ratings for nomod/HR/DT/HT/EZ (exact entries from the mod→stars list), drain time, and circle/slider/spinner counts. Supports versions ≥ 20191106 (float stars); older versions use double stars and the pre-2014 layout is skipped. If the db can't be read, star-based features are turned off.
- **Finding the osu! folder:** first `HKCR\osu\shell\open\command`, then `%LOCALAPPDATA%\osu!`, then a folder picker. The result is saved to `osu-coach.ini` next to the exe.

### Module 2 — analysis engine (`analyze`)

**Mods:** HR (OD×1.4 and CS×1.3, capped at 10, y flipped), EZ (×0.5), DT/NC (rate 1.5), HT (rate 0.75). Hit windows: 300 = 80−6·OD, 100 = 140−8·OD, 50 = 200−10·OD. Radius r = 54.4−4.48·CS. All times are converted to real time (÷ rate).

**Hit simulation (as validated before):**
- Key-down edges for K1/M1 and K2/M2 become presses with release times.
- Each circle or slider head takes the earliest unused press that falls within its 50 window and is inside the radius.
- A press inside the radius but outside the window counts as early/late miss. Notelock is approximated by consuming presses in order.
- A missed slider head counts as a miss for skill metrics. The game's own counts are kept for display.

**Per-object record:** error in ms, judgement, key, hold time, cursor distance in radii, spacing from the previous object in radii, gap in ms, quadrant, and the miss cause:
- **aim:** pressed on time but outside the radius, classed as undershoot or overshoot along the previous→current vector (±0.5r), or "side".
- **tap:** the press was used by the previous note.
- **timing:** outside the 50 window.
- **none:** no press.

**Per-play metrics:**
- **Timing:** mean error, UR (10·σ), % of hits within ±15/30/50 ms, error histogram (2 ms bins, ±100 ms).
- **Spacing buckets:** dense ≤2r, stream 2–4r, jump 4–7r, big >7r. For each: object count, misses and mean aim error.
- **Aim:** jump distance histogram (0.5r bins), miss counts per quadrant, overshoot/undershoot counts.
- **Streams:** segments of ≥4 consecutive objects with spacing ≤3r and an even gap (each gap within ±15% of the first one in the segment). For each segment: effective BPM (15000/gap, i.e. 1/4 notes), alternation % (the key changes between consecutive hits), same-finger double taps, misses, and the mean error for notes 1–4 vs 5+.
- **Stamina:** the longest run with no gap over 500 ms, in notes and seconds, and its notes per second.
- **Tapping:** J/K split, UR per key, and for each gap zone (fast <100, switch 100–130, half 130–260, slow 260+ ms): UR, mean, alternation % and overlap % (the hold lasts longer than the gap).
- **Reading:** misses on the first object after a gap over 500 ms vs all other objects. The map's AR band (≤7, 8+) is attached for aggregation.

### Module 3 — coach (`coach`)

**Windows:** "recent" = plays in the last 7 days (if there are fewer than 10, the last 30 plays). "Baseline" = all older plays plus the stored history.

**Skill radar (0–100):** one axis each for Timing, Jumps, Big jumps, Streams, Speed, Reading and Stamina.
- Expected value = the baseline's miss rate for that category, weighted by the recent plays' star bins (0.25★ bins, falling back to the nearest bin that has data).
- Score = clamp(50 + 25·log2(expected/actual), 0, 100). Rates are smoothed with +1 miss and +50 objects.
- Timing compares UR, so the score uses log2(expected UR / actual UR) with the same formula.
- With no baseline, every axis shows 50 and says "need more history".

**pp estimate:**
- aim = speed = (5·max(1, (SR/2)/0.0675)−4)³/100000.
- Length bonus: 0.95 + 0.4·min(1, n/2000).
- Miss penalty: 0.97^misses.
- Accuracy term: 1.52163^OD · acc^24 · 2.83 · min(1.15, (n/1000)^0.3).
- Total = (aim^1.1 + speed^1.1 + accTerm^1.1)^(1/1.1) · 1.14.

This is only used for comparisons.

**Weaknesses:**
- For each category, the cost is the sum over recent plays of the pp with that category's misses removed, minus the actual pp.
- For timing, the cost is the pp with acc recomputed from the best-week UR (assume normal errors, so the 300 share = P(|e| ≤ w300)) minus the actual pp.
- The top 3 by cost are shown, each with:
  - A metric line, e.g. "You miss 3.1% of 4–7r jumps; your usual at this star rating is 1.8%."
  - A fix taken from a small rule table keyed by category and sub-signal. For example, undershoot-heavy aim → "push through to the note centre, practise on jump maps at AR 9". High overlap → "alternate everything, start on J, try 1/2 at 120 ms gaps". Late-stream rushing → "aim slightly late on notes 5+".

**Map suggestions:**
- Candidates: std maps in `osu!.db` within the star range (default 3.5–4.5★, adjustable with a slider). Their .osu files are parsed on a background thread.
- Features: jump share (spacing ≥4r), stream share (objects in stream segments), AR, BPM, rhythm-change share (gap ratio ≠ the previous one by more than 25%), slider share.
- Score against the top weakness:
  - jumps → jump share
  - streams/speed → stream share, BPM near the player's stream BPM +0–10%
  - reading → rhythm changes, AR ≥8.5
  - timing → OD ≥8, low slider share
  - stamina → long stream runs
- Plays in the last 30 days: 0–3 gives a bonus, more than 10 a penalty. The top 5 are shown with "Open page" (osu.ppy.sh/b/id) and "Show in folder".

**Progress:**
- `history.json` sits next to the exe and holds one entry per day: plays, objects, misses per category, Σerr and Σerr² for UR, and stream objects and misses. On each run the newly analysed days are merged in and stored days are never deleted. This keeps trends alive after maps are deleted.
- Trend for UR, jump miss rate and stream miss rate: this week vs last week. A change of 5% or more is improving or regressing, otherwise flat.
- **Tier:** the highest 0.5★ bin with at least 5 recent plays, FC rate ≥30% and accuracy ≥96%.
  - If the UR trend is not regressing: "Ready to push to (tier+0.5)★".
  - Otherwise: "Still grinding (tier)★ — focus on (top weakness)".

## GUI

Dear ImGui on DX11. The window is resizable and the app waits for input when idle (no CPU use). Analysis runs on a worker pool (`std::thread::hardware_concurrency`). A progress bar shows while it runs, and a Refresh button re-scans.

Tabs:
1. **Overview:** radar (drawn with ImDrawList), verdict, the top 3 weakness cards, and a summary line (plays analysed / skipped by reason).
2. **Timing:** UR and mean, the ±15/30/50 percentages, the error histogram, and per-key UR.
3. **Aim:** a bucket table (objects, miss %, mean aim error), the jump-distance histogram, a 2×2 quadrant heat grid, and the over/undershoot ratio.
4. **Speed:** a stream table (BPM band, count, alternation %, miss %, early/late), the tapping zones table, and stamina.
5. **Reading:** after-break vs mid-flow, and AR ≤7 vs 8+.
6. **Plays:** a sortable table (date, map, stars, mods, acc, misses, UR). Selecting a play shows its miss list (timestamp m:ss.mmm, note #, context, cause) and its histogram.
7. **Maps:** the star-range slider and the 5 suggestions with their reason.
8. **Progress:** daily lines for UR, jump miss % and stream miss %, plus the trend labels and verdict.

CLI: `osu-coach.exe --dump out.jsonl [--osu PATH]` writes one JSON line per analysed play (file, md5, when, stars, mods, game counts, simulated misses, UR, mean, bucket counts) and exits. There is no GUI in this mode.

## Error handling

- Unreadable or truncated replays: skipped and counted as "corrupt".
- Map not in the db, or its file missing: counted as "map deleted".
- Other modes, and Relax, Autopilot and Auto plays: skipped and counted per reason.
- osu!.db fails to parse: a warning banner; stars become unknown and those plays are left out of star-weighted metrics.
- history.json fails to parse: renamed to `history.json.bak` and a fresh one is started.
- No osu! folder found: a folder picker. If cancelled, a message is shown with a Retry button.

## Testing

- `tools/check.py` runs `--dump` and compares it with the reference Python `analyze.py` on the same replays. Simulated misses must match exactly on ≥98% of plays, and UR within 1.
- Against the game's own miss counts: within 2 on ≥95% of plays.
- Timing: a full analysis of ~430 replays should finish in under 3 s on this PC (cold start, excluding the Songs scan for suggestions).
- Manual GUI check: every tab renders with real data, the radar and charts draw, and the buttons work.

## Publish

A public GitHub repo `kshteinberga-hash/osu-coach` with a README (what it does, screenshot, build steps, how the metrics are defined) and an MIT licence for our code; third-party licences are kept in `third_party/`. A v0.1.0 release with `osu-coach.exe` attached.
