#include "coach.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <atomic>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <windows.h>

const char* const kSkillName[NSKILL] = {"Timing", "Jumps", "Big jumps", "Streams", "Speed", "Reading", "Stamina"};
static const char* const kLabel[NSKILL] = {"", "jumps (2 to 3.5 circle widths)", "big jumps (over 3.5 circle widths)", "stream notes",
                                           "fast notes (less than 130 ms apart)", "first notes after a pause", "notes near the end of long runs"};
static constexpr int64_t DAY_MS = 86400000LL;

int ymd(int64_t ms) {
    time_t t = (time_t)(ms / 1000);
    tm l{};
    localtime_s(&l, &t);
    return (l.tm_year + 1900) * 10000 + (l.tm_mon + 1) * 100 + l.tm_mday;
}

static int dayIndex(int d) {  // days since 1970-01-01 (civil calendar)
    int y = d / 10000, m = d / 100 % 100, dd = d % 100;
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + dd - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

double ppEstimate(float stars, float od, int n, float acc, int misses) {
    double len = 0.95 + 0.4 * std::min(1.0, n / 2000.0), pen = std::pow(0.97, misses);
    double skill = std::pow(5 * std::max(1.0, (stars / 2) / 0.0675) - 4, 3) / 100000 * len * pen;
    double accTerm = std::pow(1.52163, od) * std::pow(std::clamp(acc, 0.f, 1.f), 24) * 2.83 * std::min(1.15, std::pow(n / 1000.0, 0.3));
    return std::pow(2 * std::pow(skill, 1.1) + std::pow(accTerm, 1.1), 1 / 1.1) * 1.14;
}

namespace {
struct Agg {
    double obj[NSKILL]{}, miss[NSKILL]{}, es = 0, ess = 0, en = 0;
    void add(const Day& d) {
        for (int s = 0; s < NSKILL; s++) obj[s] += d.obj[s], miss[s] += d.miss[s];
        es += d.es, ess += d.ess, en += d.en;
    }
    double ur() const { Acc a; a.n = en, a.s = es, a.ss = ess; return a.ur(); }
    double rate(int s) const { return obj[s] ? miss[s] / obj[s] : 0; }
};
int binOf(float stars) { return stars > 0 ? (int)std::floor(stars * 4) : -1; }
int total(const PlayStats& p) { int t = p.g300 + p.g100 + p.g50 + p.gmiss; return t ? t : p.nobj; }
double phi(double x) { return 0.5 * std::erfc(-x / std::sqrt(2.0)); }
std::string fmt(const char* f, ...) {
    char b[512];
    va_list a;
    va_start(a, f);
    vsnprintf(b, sizeof b, f, a);
    va_end(a);
    return b;
}
}

// Habits seen across plays, from common osu! community advice: retrying one map over and over, playing maps that are too
// hard, getting worse late in long sessions, and starting cold. Each needs enough plays to be sure; at most 3, worst first.
std::vector<Habit> habits(const std::vector<PlayStats>& plays, size_t from, int64_t now) {
    std::vector<std::pair<double, Habit>> out;
    size_t n = plays.size() - from;
    // retrying: the longest run of the same map back to back
    if (n >= 10) {
        size_t best = 0, at = from, run = 0;
        int inStreaks = 0;
        for (size_t i = from; i < plays.size(); i++) {
            run = i > from && plays[i].md5 == plays[i - 1].md5 ? run + 1 : 1;
            if (run > best) best = run, at = i;
            if (i + 1 == plays.size() || plays[i + 1].md5 != plays[i].md5)
                if (run > 3) inStreaks += (int)run;
        }
        if (best >= 5 && inStreaks >= 0.25 * n)
            out.push_back({(double)inStreaks / n, {"Too many retries of one map",
                fmt("You played %s %zu times in a row, and %d of your last %zu plays were in runs of 4 or more on the same map. Replaying one map "
                    "mostly teaches you that map, and long retry runs are when finger lock and tension creep in. Play a map at most 3 times in "
                    "a row, then move on to a different one with similar patterns and come back later.", plays[at].title.c_str(), best, inStreaks, n)}});
    }
    // too hard: most plays under 85% accuracy
    int hard = 0, rated = 0;
    for (size_t i = from; i < plays.size(); i++)
        if (plays[i].stars > 0) rated++, hard += plays[i].acc < 0.85f;
    if (rated >= 10 && hard >= 0.5 * rated)
        out.push_back({(double)hard / rated, {"Most of your maps are too hard right now",
            fmt("%d of your last %d plays were under 85%% accuracy. You learn the most from maps you can clear at 85%% or better and then push "
                "towards 98%%: there you can see and fix what you're doing. Spend most of your time there, and only some of it on harder maps.", hard, rated)}});
    // sessions: plays less than 30 minutes apart, over the last 30 days
    struct Ses { size_t a, b; };
    std::vector<Ses> ses;
    for (size_t i = 0; i < plays.size(); i++) {
        if (plays[i].when < now - 30 * DAY_MS) continue;
        if (ses.empty() || plays[i].when - plays[ses.back().b - 1].when > 30 * 60000LL) ses.push_back({i, i + 1});
        else ses.back().b = i + 1;
    }
    auto mean = [&](size_t a, size_t b, auto f) { double s = 0; for (size_t i = a; i < b; i++) s += f(plays[i]); return b > a ? s / (b - a) : 0.0; };
    auto acc = [](const PlayStats& p) { return (double)p.acc; };
    auto stars = [](const PlayStats& p) { return (double)p.stars; };
    // tiring: accuracy in the last third of sessions over 90 minutes, on maps no harder than in the first third
    double drop = 0, early = 0;
    int longN = 0;
    for (auto& s : ses) {
        size_t k = s.b - s.a, t = k / 3;
        if (k < 9 || plays[s.b - 1].when - plays[s.a].when < 90 * 60000LL) continue;
        if (mean(s.b - t, s.b, stars) > mean(s.a, s.a + t, stars) + 0.25) continue;
        longN++, early += mean(s.a, s.a + t, acc), drop += mean(s.a, s.a + t, acc) - mean(s.b - t, s.b, acc);
    }
    if (longN >= 2 && drop / longN >= 0.02)
        out.push_back({drop / longN * 10, {"You get worse late in long sessions",
            fmt("In your %d sessions over an hour and a half, your accuracy drops from %.1f%% in the first third to %.1f%% in the last, on maps "
                "that aren't harder. Take a break of up to 5 minutes every half hour or so and stretch your fingers, wrists and forearms, and "
                "stop when your hand feels tense: tired, tense hands are when finger lock happens.", longN, 100 * early / longN, 100 * (early - drop) / longN)}});
    // starting cold: the first 2 plays of a session against the rest, on maps no harder
    double cold = 0, rest = 0;
    int coldN = 0;
    for (auto& s : ses) {
        if (s.b - s.a < 6 || mean(s.a, s.a + 2, stars) > mean(s.a + 2, s.b, stars) + 0.25) continue;
        coldN++, cold += mean(s.a, s.a + 2, acc), rest += mean(s.a + 2, s.b, acc);
    }
    if (coldN >= 3 && (rest - cold) / coldN >= 0.02)
        out.push_back({(rest - cold) / coldN * 10, {"Warm up before your hard maps",
            fmt("Your first 2 plays of a session average %.1f%% accuracy against %.1f%% for the rest. Spend the first 10 to 15 minutes on maps "
                "a bit below your level, going from slow to fast and from low to high approach rate, before the maps you want to push.",
                100 * cold / coldN, 100 * rest / coldN)}});
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::vector<Habit> h;
    for (size_t i = 0; i < out.size() && i < 3; i++) h.push_back(out[i].second);
    return h;
}

Report coach(const std::vector<PlayStats>& plays, std::vector<Day>& hist, int64_t now) {
    Report r{};
    // 1. fold this scan into the daily history. A stored date is replaced, all bins at once, only when this scan has
    //    at least as many plays on it: deleted replays survive, and plays that moved bin (star rating recalculated) do not count twice.
    std::map<std::pair<int, int>, Day> fresh;
    for (auto& p : plays) {
        Day& d = fresh[{ymd(p.when), binOf(p.stars)}];
        d.date = ymd(p.when), d.bin = binOf(p.stars);
        d.plays++, d.objs += p.nobj, d.fcs += p.gmiss == 0, d.accSum += p.acc;
        for (int s = 0; s < NSKILL; s++) d.obj[s] += p.skObj[s], d.miss[s] += p.skMiss[s];
        d.es += p.err.s, d.ess += p.err.ss, d.en += p.err.n;
    }
    std::map<int, int> freshPlays, storedPlays;
    for (auto& [k, d] : fresh) freshPlays[k.first] += d.plays;
    for (auto& d : hist) storedPlays[d.date] += d.plays;
    auto replaced = [&](int date) { return freshPlays.count(date) && freshPlays[date] >= storedPlays[date]; };
    std::map<std::pair<int, int>, Day> all;
    for (auto& d : hist)
        if (!replaced(d.date)) all[{d.date, d.bin}] = d;
    for (auto& [k, d] : fresh)
        if (replaced(k.first)) all[k] = d;
    hist.clear();
    for (auto& [k, d] : all) hist.push_back(d);
    r.days = hist;

    // 2. recent window and baseline
    size_t from = plays.size();
    while (from > 0 && plays[from - 1].when >= now - 7 * DAY_MS) from--;
    if (plays.size() - from < 10) from = plays.size() > 30 ? plays.size() - 30 : 0;
    r.recentFrom = from;
    int cut = from < plays.size() ? ymd(plays[from].when) : ymd(now);
    std::map<int, Agg> base;
    Agg pooled;
    for (auto& d : hist)
        if (d.date < cut) base[d.bin].add(d), pooled.add(d);
    r.needHistory = pooled.en == 0;

    auto baseFor = [&](int bin, int s) -> const Agg* {
        for (int dd = 0; bin >= 0 && dd <= 40; dd++)
            for (int b : {bin - dd, bin + dd})
                if (auto it = base.find(b); it != base.end() && (s ? it->second.obj[s] : it->second.en) >= 50) return &it->second;
        return (s ? pooled.obj[s] : pooled.en) > 0 ? &pooled : nullptr;
    };

    // 3. radar: recent vs your own baseline at the same star rating
    Acc recentErr;
    int sig[8]{};  // under, over, zOv(fast+switch), zPairs, alt, dbl, keyJ, keyK
    double lateSum = 0, lateN = 0;
    Agg act;
    double expMiss[NSKILL]{}, expUr = 0, expW = 0;
    for (size_t i = from; i < plays.size(); i++) {
        auto& p = plays[i];
        recentErr.add(p.err);
        for (int s = 1; s < NSKILL; s++) {
            act.obj[s] += p.skObj[s], act.miss[s] += p.skMiss[s];
            if (auto b = baseFor(binOf(p.stars), s)) expMiss[s] += b->rate(s) * p.skObj[s];
        }
        if (auto b = baseFor(binOf(p.stars), 0)) expUr += b->ur() * p.err.n, expW += p.err.n;
        sig[0] += p.under, sig[1] += p.over, sig[2] += p.zOv[0] + p.zOv[1], sig[3] += p.zPairs[0] + p.zPairs[1];
        sig[6] += p.keyHits[0], sig[7] += p.keyHits[1];
        for (auto& sg : p.streams) {
            sig[4] += sg.alt, sig[5] += sg.dbl;
            if (sg.n > 4) lateSum += sg.errLate * (sg.n - 4), lateN += sg.n - 4;
        }
    }
    for (int s = 0; s < NSKILL; s++) r.radar[s] = 50;
    for (int s = 1; s < NSKILL; s++) {
        r.actual[s] = act.obj[s] ? (float)(act.miss[s] / act.obj[s]) : 0;
        r.expected[s] = act.obj[s] ? (float)(expMiss[s] / act.obj[s]) : 0;
        if (!r.needHistory && act.obj[s] > 0 && act.miss[s] + expMiss[s] >= 5)  // fewer misses than that is noise
            r.radar[s] = (float)std::clamp(50 + 25 * std::log2((expMiss[s] + 1) / (act.miss[s] + 1)), 0.0, 100.0);
    }
    r.actual[TIMING] = (float)recentErr.ur();
    r.expected[TIMING] = expW ? (float)(expUr / expW) : 0;
    if (!r.needHistory && r.actual[TIMING] > 0 && r.expected[TIMING] > 0)
        r.radar[TIMING] = (float)std::clamp(50 + 25 * std::log2((double)r.expected[TIMING] / r.actual[TIMING]), 0.0, 100.0);

    // 4. weaknesses ranked by estimated pp lost on recent plays
    std::map<int, Agg> weeks;
    for (auto& d : hist) weeks[dayIndex(d.date) / 7].add(d);
    double bestUr = recentErr.ur();
    for (auto& [w, a] : weeks)
        if (a.en >= 500) bestUr = std::min(bestUr, a.ur());
    std::vector<Weakness> ws;
    for (int s = 0; s < NSKILL; s++) {
        double cost = 0;
        for (size_t i = from; i < plays.size(); i++) {
            auto& p = plays[i];
            if (p.stars <= 0) continue;
            int tot = total(p);
            float od = (80 - hitWindow300(p.od) / p.rate) / 6;  // the OD whose 300 window matches the real one (DT/HT)
            double base = ppEstimate(p.stars, od, tot, p.acc, p.gmiss);
            if (s == TIMING) {
                if (bestUr <= 0 || p.err.n < 2) continue;
                double w300 = hitWindow300(p.od) / p.rate, sd = bestUr / 10, m = p.err.mean();  // real ms, like sd and m
                double p300 = phi((w300 - m) / sd) - phi((-w300 - m) / sd);
                double acc = (p300 * 300 + (1 - p300) * 100) / 300 * (tot - p.gmiss) / tot;
                cost += std::max(0.0, ppEstimate(p.stars, od, tot, (float)acc, p.gmiss) - base);
            } else {
                int k = std::min<int>(p.gmiss, p.skMiss[s]);
                float acc = std::min(1.f, p.acc + 300.f * k / (300.f * tot));
                cost += ppEstimate(p.stars, od, tot, acc, p.gmiss - k) - base;
            }
        }
        if (cost <= 0.05) continue;
        Weakness w{(Skill)s, cost, "", ""};
        if (s == TIMING) {
            w.metric = fmt("Your timing steadiness (UR) is %.0f; in your best week it was %.0f. Lower is steadier.", recentErr.ur(), bestUr);
            int sameSide = 0, sided = 0;
            for (size_t i = from; i < plays.size(); i++)
                if (plays[i].err.n >= 20) sided++, sameSide += (plays[i].err.mean() < 0) == (recentErr.mean() < 0);
            bool steadyBias = std::fabs(recentErr.mean()) > 5 && sided >= 5 && sameSide >= 0.8 * sided;
            w.fix = steadyBias
                ? fmt("You click %.0f ms too %s on average, in %d of your last %d plays. That's steady enough to fix with your offset: change the universal offset in the settings by about %.0f ms, then check the hit error bar on a few easy maps.",
                      std::fabs(recentErr.mean()), recentErr.mean() < 0 ? "early" : "late", sameSide, sided, std::fabs(recentErr.mean()))
                : std::fabs(recentErr.mean()) > 5
                ? fmt("You click %.0f ms too %s on average, but it changes from play to play, so don't touch your offset yet: get steadier first. Tap to the music, not to what you see.",
                      std::fabs(recentErr.mean()), recentErr.mean() < 0 ? "early" : "late")
                : "Play maps you can pass easily with strict timing (OD 8 or more) for 10 minutes a day, and aim for 98% accuracy; restart when you drop below it. Tap to the music, not to what you see.";
        } else {
            w.metric = r.needHistory
                ? fmt("You miss %.1f%% of %s (%.0f of %.0f lately).", 100 * r.actual[s], kLabel[s], act.miss[s], act.obj[s])
                : fmt("You miss %.1f%% of %s. Normally at this star rating you miss %.1f%%.", 100 * r.actual[s], kLabel[s], 100 * r.expected[s]);
            double ov = sig[3] ? 100.0 * sig[2] / sig[3] : 0, j = sig[6] + sig[7] ? 100.0 * sig[6] / (sig[6] + sig[7]) : 50;
            double alt = sig[4] + sig[5] ? 100.0 * sig[4] / (sig[4] + sig[5]) : 100, late = lateN ? lateSum / lateN : 0;
            switch (s) {
                case JUMPS: case BIGJUMPS:
                    w.fix = sig[0] >= sig[1]
                        ? fmt("You usually stop short of the circle (%d times, against %d times going past it). That's holding back: jumps should feel confident, so play looser and move all the way to the centre. Tap to the beat and let your aim follow your taps. Practise jump maps for 10 minutes a day, and replay easier maps with DT or HR to make normal jumps feel slow.", sig[0], sig[1])
                        : fmt("You usually go past the circle (%d times, against %d times stopping short). Slow down at the end of each jump and stop on the circle. If it feels different from day to day, check your setup: Raw Input on, Windows pointer speed at 6/11 with Enhance Pointer Precision off, and stop changing your sensitivity or tablet area: each change resets your muscle memory.", sig[1], sig[0]);
                    break;
                case STREAMS:
                    w.fix = late < -8 ? fmt("You speed up in long streams (later notes come %.0f ms too early). Tap a little slower and follow the music, not your fingers.", -late)
                          : alt < 60 ? fmt("You switch fingers on only %.0f%% of stream notes. Switch on every note: K1, K2, K1, K2. For a week, start every burst and stream with your other finger on purpose until it's automatic.", alt)
                                     : std::string("Find the stream speed where you hit 98 to 100% accuracy. Play streams at that speed until your forearm feels tired, rest 2 minutes, and do that 4 times. Once it's easy, go 5 to 10 BPM faster.");
                    break;
                case SPEED:
                    w.fix = ov > 20 ? fmt("On %.0f%% of fast notes you still hold the last key when you press the next one: that's how finger lock starts. Press lightly and let go at once, keep your wrist still and your hand loose, and switch fingers on every note.", ov)
                          : j < 35 || j > 65 ? fmt("%.0f%% of your taps are on %s. Use both fingers, even on slower notes, so both stay warmed up.", std::max(j, 100 - j), j < 35 ? "K" : "J")
                                             : std::string("Practise short fast bursts a bit above your comfortable speed, then go back to full maps.");
                    break;
                case READING: w.fix = "You miss the first note after a pause. Keep your eyes on the screen during pauses and count in the last beats. To read faster in general, step up the approach rate slowly (get comfortable at AR 8 before AR 9), and read the next circle instead of clicking each one in a panic as soon as it appears."; break;
                default: w.fix = "You miss more near the end of long runs: your hand gets tired. A tense hand tires much faster, so stay relaxed and keep your wrist still. Build up with longer stream maps at a speed you can manage, a few sets with 2-minute rests. If your hand or forearm hurts, skip stream practice for a few days."; break;
            }
        }
        ws.push_back(std::move(w));
    }
    std::sort(ws.begin(), ws.end(), [](auto& a, auto& b) { return a.pp > b.pp; });
    r.nTop = (int)std::min<size_t>(3, ws.size());
    for (int i = 0; i < r.nTop; i++) r.top[i] = ws[i];

    r.habits = habits(plays, from, now);

    // 5. trends: this week vs last week
    int d7 = ymd(now - 6 * DAY_MS), d14 = ymd(now - 13 * DAY_MS);  // today + 6 days, and the 7 days before
    Agg thisW, lastW;
    for (auto& d : hist)
        if (d.date >= d7) thisW.add(d);
        else if (d.date >= d14) lastW.add(d);
    auto trend = [](double now_, double before, bool ok) { return !ok ? 0 : now_ < before * 0.95 ? 1 : now_ > before * 1.05 ? -1 : 0; };
    r.trend[0] = trend(thisW.ur(), lastW.ur(), thisW.en >= 50 && lastW.en >= 50);
    r.trend[1] = trend(thisW.rate(JUMPS), lastW.rate(JUMPS), thisW.obj[JUMPS] >= 20 && lastW.obj[JUMPS] >= 20);
    r.trend[2] = trend(thisW.rate(STREAMS), lastW.rate(STREAMS), thisW.obj[STREAMS] >= 20 && lastW.obj[STREAMS] >= 20);

    // 6. tier and verdict
    struct T { int n = 0, fc = 0; double acc = 0; };
    std::map<int, T> tiers;  // key = stars * 2
    for (size_t i = from; i < plays.size(); i++)
        if (plays[i].stars > 0) {
            T& t = tiers[(int)std::floor(plays[i].stars * 2)];
            t.n++, t.fc += plays[i].gmiss == 0, t.acc += plays[i].acc;
        }
    int best = -1, most = -1, mostN = 0;
    for (auto& [k, t] : tiers) {
        if (t.n >= 5 && t.fc >= 0.3 * t.n && t.acc / t.n >= 0.96) best = k;
        if (t.n > mostN) mostN = t.n, most = k;
    }
    const char* focus = r.nTop ? kSkillName[r.top[0].s] : "consistency";
    if (tiers.empty()) r.tier = 0, r.verdict = "Play a few maps, then refresh: there are no recent plays to judge yet.";
    else if (best >= 0 && r.trend[0] != -1) r.tier = best / 2.f, r.verdict = fmt("Ready to push to %.1f\xE2\x98\x85", r.tier + 0.5f);
    else r.tier = (best >= 0 ? best : most) / 2.f, r.verdict = fmt("Still grinding %.1f\xE2\x98\x85 \xE2\x80\x94 focus on %s", r.tier, focus);
    return r;
}

Explain explainMiss(const ObjRes& o, float beatMs, const ObjRes* prev) {
    std::string where = o.inStream ? " in the middle of a stream" : o.afterBreak ? " right after a pause" : "";
    if (o.slider) where += " on the start of a slider";
    const char* side = o.err < 0 ? "early" : "late";
    bool timed = o.gap < 1e8f;
    if (o.judge == 50)
        return {o.err < 0 ? "50: early" : "50: late", fmt("You hit it, but %.0f ms %s, so it only counted as a 50%s.", std::fabs(o.err), side, where.c_str()),
                o.err < 0 ? "You were ahead of the music. Wait for the beat: don't hit as soon as the approach circle looks close."
                          : "You were behind the music. Tap on the beat you hear, instead of reacting when the circle is already there.",
                "Replay a few maps you know well with the music loud and the hit error bar on, and keep your hits in its centre.", D_50};
    switch (o.cause) {
        case AIM: {
            float out = (o.dist - 1) / 2;  // circle widths beyond the circle's edge (dist is in radii)
            std::string t = fmt("You clicked on time (%.0f ms %s), but your cursor was ", std::fabs(o.err), side) +
                            (out < 0.25f ? std::string("just outside the edge of the circle. ") : fmt("%.1f circle widths outside the circle. ", out));
            t += o.along < 0 ? "You stopped short of it." : o.along > 0 ? "You went past it." : "You were off to the side.";
            bool jump = o.spacing > 4;
            if (jump) t += fmt(" This was a jump of %.1f circle widths", o.spacing / 2) + (timed ? fmt(" in %.0f ms", o.gap) : "") + where + ".";
            else if (!where.empty()) t += " This happened" + where + ".";
            std::string tip = o.along < 0 ? "Move all the way to the centre of the circle; don't slow down before you arrive. Aim for the middle, not the edge: the edge leaves no room for error. "
                                            "Stopping short usually means holding back: commit to the jump, and tap to the beat with your aim following your taps."
                            : o.along > 0 ? "Slow down at the end of the move and stop on the circle. Land in the middle and stay still there for a moment before the next move."
                                          : "Watch the next circle, not your cursor, and move straight to it.";
            std::string pr = jump && timed
                ? fmt("Practise jumps of about %.1f circle widths, %.0f ms apart: jump maps a little below your star rating, aiming for the centre of every circle. "
                      "Replaying easier maps with DT or HR also helps: afterwards, normal jumps feel slow.", o.spacing / 2, o.gap)
                : std::string("Practise maps with lots of small movements at your level, landing in the centre of each circle instead of anywhere on it.");
            return {"Missed: aim", t, tip, pr, D_AIM};
        }
        case TAP: {
            bool held = prev && prev->judge && prev->hold > o.gap;
            std::string t = held ? fmt("You pressed K%d for the note before and kept it down for %.0f ms, longer than the %.0f ms to this note. One press only hits one note, so this one got nothing",
                                       prev->key + 1, prev->hold, o.gap) + where + "."
                                 : "You pressed once for two notes: that press counted for the note before, so this one got nothing" + where + ".";
            return {"Missed: one press for two notes", t,
                    held ? "Let go right after each tap: a short, light press. Holding a key down is how finger lock starts."
                         : "Tap every note. In fast notes, switch fingers on each one: K1, K2, K1, K2.",
                    timed && o.gap < 200 ? fmt("Practise short bursts at this speed (%.0f ms between notes, %.0f BPM in 1/4 notes), alternating K1 and K2, until every note gets its own press.", o.gap, 15000 / o.gap)
                                         : std::string("Practise maps with two notes per beat and tap each one on purpose, with alternating fingers."),
                    D_TAP};
        }
        case OFFTIME:
            return {"Missed: too early", fmt("You clicked %.0f ms too early, before this note could be hit%s.", std::fabs(o.err), where.c_str()) +
                        (timed && beatMs > 0 ? fmt(" It came %.2g beat%s after the note before: you expected it sooner.", o.gap / beatMs, o.gap == beatMs ? "" : "s") : ""),
                    "Wait for the approach circle to touch the circle, and follow the music, not your fingers.",
                    "Play maps with changing rhythms at a comfortable star rating and tap only on the sounds you hear.", D_EARLY};
        default:
            if (o.afterBreak)
                return {"Missed: no click", "You didn't click anywhere near this note. It was the first note after a pause, so you weren't ready for it.",
                        "During a pause, keep your eyes on the screen and count the last beats before the music comes back, with your cursor already moving to where the first note appears.",
                        "Play maps with breaks and quick returns, at an approach rate of 8.5 to 9 so the first note doesn't surprise you.", D_NOPRESS};
            return {"Missed: no click", "You didn't click anywhere near this note" + where + ".",
                    "Keep tapping to the rhythm even when you're lost: a guess can still hit.",
                    "If you lose track of the notes, practise maps with the same approach rate a bit below your star rating until the patterns read easily.", D_NOPRESS};
    }
}

std::vector<Stop> coachStops(const std::vector<ObjRes>& objs, float bpm, bool with50s) {
    std::vector<Stop> out;
    float maxGap = streamMaxGap(bpm);
    for (size_t i = 0; i < objs.size();) {
        size_t j = i + 1;
        while (j < objs.size() && objs[j].gap <= maxGap) j++;
        bool run = j - i >= 3;
        for (size_t k = i; k < j; k++) {
            if (objs[k].judge != 0 && !(with50s && objs[k].judge == 50)) continue;
            if (run && !out.empty() && out.back().first == (int)i) out.back().bad++;
            else out.push_back(run ? Stop{(int)k, (int)i, (int)j - 1, 1} : Stop{(int)k, (int)k, (int)k, 1});
        }
        i = j;
    }
    return out;
}

namespace {
struct Run {
    int n, before, misses = 0, meh = 0, aim = 0, early = 0, along[3]{};  // meh: 100s and 50s; along: short, side, past
    float gap, bpm;
    int tailNotes, tailTaps = 0, taps = 0;
    int sameRun = 1, sameKey = 0, longHold = 0, longKey = 0; float longMs = 0;
    float step[2]{}, hold[2]{}; int nStep[2]{}, nHold[2]{};  // step[k]: ms from a K(k+1) press to the next press of the other key
    float errEarly = 0, errLate = 0;
};

Run measure(const std::vector<ObjRes>& objs, const std::vector<Tap>& taps, const Stop& s) {
    Run r{};
    r.n = s.last - s.first + 1, r.before = s.at - s.first, r.tailNotes = s.last - s.at + 1;
    std::vector<float> gaps;
    for (int k = s.first + 1; k <= s.last; k++) gaps.push_back(objs[k].gap);
    std::nth_element(gaps.begin(), gaps.begin() + gaps.size() / 2, gaps.end());
    r.gap = gaps[gaps.size() / 2], r.bpm = 15000 / r.gap;
    Acc ea, la;
    for (int k = s.first; k <= s.last; k++) {
        auto& o = objs[k];
        if (!o.judge) {
            r.misses++, r.aim += o.cause == AIM, r.early += o.cause == OFFTIME;
            if (o.cause == AIM) r.along[o.along + 1]++;
        } else {
            r.meh += o.judge < 300;
            (2 * (k - s.first) < r.n ? ea : la).add(o.err);
        }
    }
    r.errEarly = (float)ea.mean(), r.errLate = (float)la.mean();
    if (!ea.n || !la.n) r.errEarly = r.errLate = 0;

    float lo = objs[s.first].t - r.gap * 0.6f, hi = objs[s.last].t + r.gap * 0.6f, tail = objs[s.at].t - r.gap * 0.5f;
    auto it = std::lower_bound(taps.begin(), taps.end(), lo, [](const Tap& a, float t) { return a.t < t; });
    const Tap* pv = nullptr;
    int same = 1;
    for (; it != taps.end() && it->t <= hi; ++it) {
        const Tap& a = *it;
        r.taps++, r.tailTaps += a.t >= tail;
        float held = a.up >= 0 ? a.up - a.t : 1e9f;
        if (held <= 1.5f * r.gap) r.hold[a.key] += held, r.nHold[a.key]++;  // a typical press; frozen ones are counted below
        else if (held > 1.5f * r.gap && a.t + 1.5f * r.gap <= objs[s.last].t) {  // still down when the next note (and more) came
            r.longHold++;
            if (std::min(held, hi - a.t) > r.longMs) r.longMs = std::min(held, hi - a.t), r.longKey = a.key;
        }
        if (pv) {
            if (pv->key == a.key) {
                if (++same > r.sameRun) r.sameRun = same, r.sameKey = a.key;
            } else same = 1, r.step[pv->key] += a.t - pv->t, r.nStep[pv->key]++;
        }
        pv = &a;
    }
    for (int k = 0; k < 2; k++) {
        if (r.nStep[k]) r.step[k] /= r.nStep[k];
        if (r.nHold[k]) r.hold[k] /= r.nHold[k];
    }
    return r;
}

float round5(float x) { return 5 * std::round(x / 5); }
}

Explain explainStop(const std::vector<ObjRes>& objs, const std::vector<Tap>& taps, const Stop& s, float bpm) {
    float beat = bpm > 0 ? 60000 / bpm : 500;
    if (s.first == s.last) return explainMiss(objs[s.at], beat, s.at > 0 ? &objs[s.at - 1] : nullptr);
    Run r = measure(objs, taps, s);
    const char* kind = r.n >= 8 ? "stream" : "burst";
    std::string head = fmt("This was a %d-note %s at %.0f BPM (%.0f ms between notes).", r.n, kind, r.bpm, r.gap);
    std::string result = r.misses ? fmt(" %d note%s missed", r.misses, r.misses == 1 ? "" : "s") + (r.meh ? fmt(", %d hit as 100 or 50.", r.meh) : ".")
                                  : fmt(" %d note%s hit as 100 or 50.", r.meh, r.meh == 1 ? "" : "s");
    int dropped = r.tailNotes - r.tailTaps;
    bool aimy = r.misses && 2 * r.aim >= r.misses;
    bool lock = !aimy && r.misses && (dropped >= 2 || r.sameRun >= 3 || r.longHold);
    float uneven = std::fabs(r.step[0] - r.step[1]);
    bool gallop = r.nStep[0] >= 2 && r.nStep[1] >= 2 && uneven >= 0.3f * r.gap;
    int slowK = r.step[0] >= r.step[1] ? 0 : 1;  // the long step starts on this key
    std::string gallopNums = fmt("K%d to K%d took %.0f ms on average, K%d to K%d took %.0f ms; both should be %.0f ms", slowK + 1, 2 - slowK, r.step[slowK],
                                 2 - slowK, slowK + 1, r.step[1 - slowK], r.gap);
    int hardK = r.hold[0] >= r.hold[1] ? 0 : 1;
    bool hard = r.nHold[0] >= 2 && r.nHold[1] >= 2 && r.hold[hardK] >= 1.5f * r.hold[1 - hardK] && r.hold[hardK] >= 0.5f * r.gap;
    std::string hardTxt = hard ? fmt(" You also hold K%d down for %.0f ms per tap against %.0f ms for K%d: you press it too hard, so that finger comes back up late.",
                                     hardK + 1, r.hold[hardK], r.hold[1 - hardK], 2 - hardK)
                               : "";
    std::string notes = r.before == 1 ? "the first note" : fmt("the first %d notes", r.before);

    if (aimy) {
        std::string how = r.along[0] >= r.along[2] && r.along[0] >= r.along[1] ? "cutting inside the path or stopping short of the notes"
                        : r.along[2] >= r.along[1] ? "going past the notes" : "drifting off to the side of the path";
        return {fmt("Aim slipped in a %s", kind), head + fmt(" Your tapping kept up, but on %d note%s your cursor wasn't on the circle: you were %s.", r.aim, r.aim == 1 ? "" : "s", how.c_str()) + result,
                r.along[2] > r.along[0] ? "Smaller, calmer movements: let the cursor glide from note to note instead of throwing it. Aim and tap together."
                                        : "Move the cursor through every note, following the shape of the stream. Don't cut corners or let the cursor fall behind your taps.",
                fmt("Practise spaced streams and streams with turns at about %.0f BPM, slow enough that the cursor stays on every note, then speed up 5 BPM at a time.", round5(r.bpm * 0.9f)),
                D_RUNAIM};
    }
    if (lock) {
        std::string t = head + (r.before ? fmt(" You kept up for %s, then", notes.c_str()) : std::string(" From the first note,"));
        t += dropped >= 2 ? fmt(" you pressed only %d time%s for the last %d notes, so %d of them got no press of their own.", r.tailTaps, r.tailTaps == 1 ? "" : "s", r.tailNotes, dropped)
                          : std::string(" your fingers stopped switching.");
        if (r.sameRun >= 3) t += fmt(" You pressed K%d %d times in a row instead of switching keys.", r.sameKey + 1, r.sameRun);
        if (r.longHold) t += fmt(" K%d stayed down for %.0f ms, %.1f notes long: that finger froze on the key.", r.longKey + 1, r.longMs, r.longMs / r.gap);
        if (gallop) t += " Before that your taps were already uneven (" + gallopNums + ").";
        t += hardTxt + result;
        return {fmt("Finger lock in a %s", kind), t,
                "This is finger lock: your hand tenses up and the fingers stop taking turns. Keep them moving, K1, K2, K1, K2, even when you feel "
                "you're falling behind: a late tap still scores a 100 or 50, a frozen finger scores nothing. Keep your hand and wrist loose and still "
                "before the stream starts, and press the keys lightly. It shows up most in long sessions and long retry runs of one map: if it keeps "
                "happening, take a short break and stretch your fingers.",
                fmt("Practise %d-note %ss at about %.0f BPM (10%% slower than this one) until you can play ten in a row without locking, then go up 5 BPM at "
                    "a time. Start each one with your other finger on purpose, so switching becomes automatic. Between maps, tap K1 K2 along to a %.0f BPM "
                    "metronome, four taps per beat, for 20 seconds at a time, staying relaxed.",
                    r.n, kind, round5(r.bpm * 0.9f), round5(r.bpm * 0.9f)),
                D_LOCK};
    }
    if (gallop)
        return {fmt("Galloping in a %s", kind),
                head + " Your taps came in uneven pairs: " + gallopNums + ". That long-short, ta-dum ta-dum rhythm is galloping." + hardTxt + result,
                "Press lighter: just hard enough for the key to count, without slamming it to the bottom, and let both fingers bounce back up at the "
                "same speed. Rest both fingers on the keys so neither has farther to travel. If your keyboard lets you set the actuation point, a "
                "shallower one helps.",
                fmt("Slow right down: play stream and burst maps at about %.0f BPM, with No Fail so stamina doesn't get in the way, and listen for an even "
                    "tick-tick-tick instead of ta-dum ta-dum. Tap both fingers slowly at exactly the same pace first, then speed up. Aim the start of each "
                    "stream well, so the hit error bar shows your tapping, not your aim: K1 and K2 hits should land in the same spot. Go up 10 BPM once you "
                    "full-combo with 95%% accuracy or better. Galloping fades with steady practice, so give it a few weeks.",
                    std::clamp(round5(r.bpm * 0.75f), 100.f, 160.f)),
                D_GALLOP};
    float drift = r.errLate - r.errEarly;
    if (std::fabs(drift) >= std::max(12.f, 0.25f * r.gap)) {
        auto start = fmt(" Your first notes were %.0f ms %s, but by the end you were %.0f ms %s", std::fabs(r.errEarly), r.errEarly < 0 ? "early" : "late",
                         std::fabs(r.errLate), r.errLate < 0 ? "early" : "late");
        if (drift < 0)
            return {fmt("Rushed the %s", kind), head + start + ": you sped up and ran ahead of the music." + result,
                    "Let the music set the speed. Close-together notes look fast, but they're no faster than the beat: tap with the music, not with "
                    "what you see, and stay relaxed so your fingers don't run off.",
                    fmt("Play streams at %.0f BPM with the music loud and keep the hit error bar centred from the first note to the last.", round5(r.bpm)), D_RUSH};
        return {fmt("Fell behind in the %s", kind), head + start + ": this speed is at the edge of what your fingers can do right now." + result,
                "Don't tense up to go faster; stay loose and keep alternating. Tension is what slows fingers down and then locks them. Falling behind "
                "late in a stream is usually stamina, not finger control.",
                fmt("Build speed and stamina: play streams at %.0f BPM until your forearm feels tired, rest 2 minutes, and do that 4 times. Once that's clean "
                    "and centred, go up 5 BPM at a time towards %.0f.", round5(r.bpm * 0.9f), round5(r.bpm)), D_DRAG};
    }
    return {fmt("Broke in a %s", kind),
            head + (r.before ? fmt(" You kept up for %s.", notes.c_str()) : std::string()) +
                (r.early ? fmt(" %d note%s pressed too early to count.", r.early, r.early == 1 ? " was" : "s were") : std::string()) + hardTxt + result,
            "Keep a steady rhythm through the whole run, and switch fingers on every note: K1, K2, K1, K2.",
            fmt("Practise %d-note %ss at about %.0f BPM until you can hit them clean, then speed up 5 BPM at a time.", r.n, kind, round5(r.bpm * 0.9f)), D_RUN};
}

Explain summarize(const std::vector<Explain>& stops) {
    static const char* const name[NDIAG] = {"aim", "one press for two notes", "clicking too early", "notes you didn't click", "50s",
                                            "finger lock", "galloping", "rushing streams", "falling behind in streams", "aim in streams", "broken streams"};
    int n[NDIAG]{}, best = -1;
    for (auto& e : stops) n[e.diag]++;
    for (int d = 0; d < NDIAG; d++)
        if (n[d] && (best < 0 || n[d] > n[best])) best = d;
    if (best < 0) return {"", "", "", "", NDIAG};
    std::string list;
    for (int d = 0; d < NDIAG; d++)
        if (n[d]) list += (list.empty() ? "" : ", ") + fmt("%s %d", name[d], n[d]);
    auto& e = *std::find_if(stops.begin(), stops.end(), [&](const Explain& x) { return x.diag == best; });
    return {fmt("Most stops: %s (%d of %zu)", name[best], n[best], stops.size()), "Stops by cause: " + list + ".", e.tip, e.practice, (Diag)best};
}

MapFeat features(const Beatmap& b) {
    MapFeat f{0, 0, 0, 0, b.ar, b.od, b.bpm, 0};
    float rad = circleRadius(b.cs);
    std::vector<float> sp, gap;
    const HitObj* p = nullptr;
    int n = 0, jumps = 0, rhythm = 0, sliders = 0;
    for (auto& o : b.objs) {
        if (o.kind == 2) continue;
        sp.push_back(p ? std::hypot(o.x - p->x, o.y - p->y) / rad : 0);
        gap.push_back(p ? (float)(o.t - p->t) : 1e9f);
        if (p) {
            n++, jumps += sp.back() >= 4;
            if (gap.size() > 2 && gap[gap.size() - 2] > 0 && gap[gap.size() - 2] < 1e8f && std::fabs(gap.back() / gap[gap.size() - 2] - 1) > 0.25f) rhythm++;
        }
        sliders += o.kind == 1;
        p = &o;
    }
    if (sp.empty()) return f;
    int inStream = 0;
    for (auto [i, j] : findStreams(sp, gap, streamMaxGap(b.bpm))) inStream += (int)(j - i), f.longRun = std::max(f.longRun, (int)(j - i));
    f.jump = n ? (float)jumps / n : 0, f.rhythm = n ? (float)rhythm / n : 0;
    f.stream = (float)inStream / sp.size(), f.slider = (float)sliders / sp.size();
    return f;
}

static std::mutex gFeatMu;
static std::unordered_map<std::string, MapFeat> gFeat;
void cacheFeatures(const std::string& md5, const MapFeat& f) { std::lock_guard g(gFeatMu); gFeat[md5] = f; }

static std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
// whole word (or phrase) w in s, both lower case; words end at anything that isn't a letter or digit
static bool hasWord(const std::string& s, std::string_view w) {
    auto alnum = [](char c) { return std::isalnum((unsigned char)c) != 0; };
    for (size_t i = s.find(w); i != std::string::npos; i = s.find(w, i + 1))
        if ((i == 0 || !alnum(s[i - 1])) && (i + w.size() >= s.size() || !alnum(s[i + w.size()]))) return true;
    return false;
}

bool isAnime(const DbMap& m) {
    static const char* const kTv[] = {"tv size", "tv ver", "tv version", "tv edit"};
    static const char* const kAnime[] = {"anime", "anison", "opening", "ending", "op", "insert song", "character song",
                                         "\xE3\x82\xA2\xE3\x83\x8B\xE3\x83\xA1"};  // アニメ
    static const char* const kGame[] = {"game", "touhou", "\xE6\x9D\xB1\xE6\x96\xB9", "undertale", "deltarune", "osu!", "cytus", "arcaea",
                                        "deemo", "djmax", "beatmania", "iidx", "sound voltex", "maimai", "chunithm", "muse dash",
                                        "phigros", "pop'n music", "jubeat", "taiko no tatsujin", "geometry dash", "minecraft", "celeste",
                                        "persona", "final fantasy", "genshin", "honkai", "blue archive", "project sekai", "bang dream",
                                        "love live", "idolm@ster", "idolmaster", "vocaloid"};  // games and idol/vocaloid franchises, not anime
    std::string src = lower(m.source), tags = lower(m.tags), title = lower(m.title);
    for (auto w : kTv) if (hasWord(tags, w) || hasWord(title, w)) return true;
    for (auto w : kAnime) if (hasWord(tags, w) || hasWord(src, w)) return true;
    if (src.empty()) return false;
    for (auto w : kGame) if (src.find(w) != std::string::npos || hasWord(tags, w)) return false;
    return true;  // most other maps with a source are from an anime
}

const char* statusName(uint8_t s) {
    switch (s) {
        case 4: return "Ranked";
        case 5: return "Approved";
        case 6: return "Qualified";
        case 7: return "Loved";
        case 2: return "Graveyard";
        case 1: return "Unsubmitted";
        default: return "Unknown status";
    }
}
static bool statusOk(uint8_t s, int f) { return f == 0 || (f == 1 ? s >= 4 && s <= 7 : s == 4 || s == 5); }
int mapLenS(const DbMap& m) { return m.totalMs > 0 ? (m.totalMs + 500) / 1000 : m.drainS; }

void defaultRange(MapMode mode, float tier, float& lo, float& hi) {
    static const float kNoData[NMODE][2] = {{3.5f, 4.5f}, {2, 3}, {2.5f, 3.5f}};
    static const float kOffset[NMODE][2] = {{0, 0.6f}, {-1.5f, -0.5f}, {-1, -0.2f}};  // around the tier you FC or grind
    if (tier <= 0) { lo = kNoData[mode][0], hi = kNoData[mode][1]; return; }
    lo = std::max(1.f, tier + kOffset[mode][0]), hi = std::max(lo + 0.5f, tier + kOffset[mode][1]);
}

std::vector<Suggestion> suggest(const std::vector<DbMap>& db, const std::vector<PlayStats>& plays, const MapQuery& q, int64_t now,
                                const std::atomic<bool>* cancel, size_t max) {
    std::vector<const DbMap*> cand, todo;
    std::unordered_set<std::string_view> seen;  // a map imported twice is in osu!.db twice
    std::string needle = lower(q.text);
    for (auto& m : db) {
        float s = starsFor(m, 0);
        if (s < q.lo || s > q.hi || (q.maxLenS > 0 && mapLenS(m) > q.maxLenS) || !statusOk(m.status, q.status)) continue;
        if (q.noAnime && isAnime(m)) continue;
        if (!needle.empty() && lower(m.title + " " + m.creator + " " + m.source + " " + m.tags).find(needle) == std::string::npos) continue;
        if (seen.insert(m.md5).second) cand.push_back(&m);
    }
    {
        std::lock_guard g(gFeatMu);
        for (auto m : cand) if (!gFeat.count(m->md5)) todo.push_back(m);
    }
    std::atomic<size_t> next = 0;
    auto work = [&] {
        Beatmap b;
        for (size_t i; (i = next++) < todo.size() && !(cancel && *cancel);)
            cacheFeatures(todo[i]->md5, loadOsu(todo[i]->path, b) ? features(b) : MapFeat{});
    };
    std::vector<std::thread> pool(std::max(1u, std::thread::hardware_concurrency()));
    for (auto& t : pool) t = std::thread(work);
    for (auto& t : pool) t.join();

    std::unordered_map<std::string, int> recent;
    for (auto& p : plays) if (p.when >= now - 30 * DAY_MS) recent[p.md5]++;
    std::vector<Suggestion> out;
    std::lock_guard g(gFeatMu);
    for (auto m : cand) {
        auto it = gFeat.find(m->md5);
        if (it == gFeat.end() || it->second.bpm <= 0) continue;  // not read (cancelled) or the .osu is gone
        const MapFeat& f = it->second;
        float sc = 0, stars = starsFor(*m, 0);
        int n = recent.count(m->md5) ? recent[m->md5] : 0;
        bool fastStreams = q.streamBpm > 0 && f.stream > 0.05f && f.bpm > q.streamBpm;  // streams faster than you hold
        std::string why;
        if (q.mode == PRACTICE) {
            switch (q.weak) {
                case JUMPS: case BIGJUMPS:
                    sc = f.jump + (f.ar >= 9 ? 0.1f : 0), why = fmt("%.0f%% of the notes are jumps; circles appear at AR %.1f", 100 * f.jump, f.ar); break;
                case STREAMS: case SPEED: {
                    float target = q.streamBpm > 0 ? q.streamBpm * 1.05f : f.bpm;
                    sc = f.stream * std::exp(-std::fabs(f.bpm - target) / 20), why = fmt("%.0f%% of the notes are streams, at %.0f BPM", 100 * f.stream, f.bpm);
                    break;
                }
                case READING: sc = f.rhythm + (f.ar >= 8.5f ? 0.3f : 0), why = fmt("the rhythm changes on %.0f%% of the notes; circles appear at AR %.1f", 100 * f.rhythm, f.ar); break;
                case STAMINA: sc = f.stream * std::min(1.f, f.longRun / 50.f), why = fmt("its longest stream is %d notes", f.longRun); break;
                default: sc = std::min(1.f, f.od / 8) * (1 - f.slider), why = fmt("strict timing (OD %.1f), %.0f%% plain circles", f.od, 100 * (1 - f.slider)); break;
            }
            sc *= n <= 3 ? 1.2f : n > 10 ? 0.5f : 1.f;  // fresh maps first
        } else if (q.mode == WARMUP) {
            // nothing crazy: no streams faster than you hold, no long streams, no huge share of jumps, no very high AR
            if (fastStreams || (q.streamBpm > 0 && f.stream > 0.05f && f.bpm > 0.9f * q.streamBpm) || f.longRun > 24 || f.jump > 0.45f || f.ar > 9.5f) continue;
            float steady = 1 - std::min(1.f, 2 * f.rhythm);
            sc = steady * (0.4f + std::min(f.stream, 0.25f) + std::min(f.jump, 0.25f));  // a bit of tapping and a bit of moving
            sc *= n > 0 ? 1.1f : 1.f;                                                  // a familiar map warms up fastest
            why = fmt("steady rhythm (changes on %.0f%% of the notes), %.0f%% streams and %.0f%% jumps: easy to settle into",
                      100 * f.rhythm, 100 * f.stream, 100 * f.jump);
        } else {
            sc = 1;
            const char* light = nullptr;
            switch (q.weak) {
                case JUMPS: case BIGJUMPS: sc *= 1 - 0.8f * std::min(1.f, f.jump * 2), light = "jumps"; why = fmt("only %.0f%% jumps", 100 * f.jump); break;
                case STREAMS: case SPEED: case STAMINA:
                    sc *= 1 - 0.8f * std::min(1.f, f.stream * 3), light = "streams";
                    why = f.longRun >= 4 ? fmt("only %.0f%% streams, the longest %d notes", 100 * f.stream, f.longRun) : std::string("no streams");
                    break;
                case READING: sc *= (1 - 0.8f * std::min(1.f, f.rhythm * 2)) * (f.ar > 9.3f ? 0.6f : 1), light = "rhythm changes";
                    why = fmt("a steady rhythm (changes on %.0f%% of the notes) at AR %.1f", 100 * f.rhythm, f.ar); break;
                default: sc *= 1 - 0.5f * std::clamp((f.od - 7) / 3, 0.f, 1.f), light = "strict timing"; why = fmt("forgiving timing (OD %.1f)", f.od); break;
            }
            if (fastStreams) sc *= 0.3f;
            sc *= 1 - 0.4f * (stars - q.lo) / std::max(0.1f, q.hi - q.lo);  // lower in the range is easier
            sc *= n == 0 ? 1.1f : n > 10 ? 0.7f : 1.f;                     // new maps first
            why += fmt(", so it goes easy on %s, your weak spot", light);
            if (fastStreams) why += fmt(" (but its streams at %.0f BPM are faster than you usually hold)", f.bpm);
        }
        int len = mapLenS(*m);
        why += fmt(". %.2f\xE2\x98\x85, %d:%02d, %.0f BPM, AR %.1f, %s. You played it %d time%s in the last 30 days.", stars, len / 60, len % 60,
                   f.bpm, f.ar, statusName(m->status), n, n == 1 ? "" : "s");
        if (sc > 0) out.push_back({m, stars, sc, why});
    }
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.score > b.score; });
    std::unordered_set<int> sets;  // one difficulty per mapset: the best one
    std::vector<Suggestion> best;
    for (auto& s : out) {
        if (s.map->sid > 0 && !sets.insert(s.map->sid).second) continue;
        best.push_back(std::move(s));
        if (best.size() >= max) break;
    }
    return best;
}

static const char* kOut ="{\"d\":%d,\"b\":%d,\"p\":%d,\"o\":%d,\"m\":[%d,%d,%d,%d,%d,%d,%d],\"ob\":[%d,%d,%d,%d,%d,%d,%d],\"es\":%.17g,\"ess\":%.17g,\"en\":%.17g,\"fc\":%d,\"as\":%.17g}";
static const char* kIn = "{\"d\":%d,\"b\":%d,\"p\":%d,\"o\":%d,\"m\":[%d,%d,%d,%d,%d,%d,%d],\"ob\":[%d,%d,%d,%d,%d,%d,%d],\"es\":%lf,\"ess\":%lf,\"en\":%lf,\"fc\":%d,\"as\":%lf}";

void saveHistory(const std::wstring& path, const std::vector<Day>& days) {
    std::wstring tmp = path + L".tmp";
    FILE* f = _wfopen(tmp.c_str(), L"wb");
    if (!f) return;
    fputs("[\n", f);
    for (size_t i = 0; i < days.size(); i++) {
        auto& d = days[i];
        fprintf(f, kOut, d.date, d.bin, d.plays, d.objs, d.miss[0], d.miss[1], d.miss[2], d.miss[3], d.miss[4], d.miss[5], d.miss[6],
                d.obj[0], d.obj[1], d.obj[2], d.obj[3], d.obj[4], d.obj[5], d.obj[6], d.es, d.ess, d.en, d.fcs, d.accSum);
        fputs(i + 1 < days.size() ? ",\n" : "\n", f);
    }
    fputs("]\n", f);
    bool bad = ferror(f) != 0;
    if (fclose(f) != 0 || bad) { _wremove(tmp.c_str()); return; }  // e.g. disk full: keep the old history
    MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

bool loadHistory(const std::wstring& path, std::vector<Day>& out) {
    out.clear();
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return true;  // no history yet
    char line[1024];
    bool ok = true;
    while (ok && fgets(line, sizeof line, f)) {
        char* p = line;
        while (*p == ' ' || *p == '[' || *p == '\t') p++;
        if (*p == ']' || *p == '\r' || *p == '\n' || !*p) continue;
        Day d{};
        ok = sscanf(p, kIn, &d.date, &d.bin, &d.plays, &d.objs, &d.miss[0], &d.miss[1], &d.miss[2], &d.miss[3], &d.miss[4], &d.miss[5],
                    &d.miss[6], &d.obj[0], &d.obj[1], &d.obj[2], &d.obj[3], &d.obj[4], &d.obj[5], &d.obj[6], &d.es, &d.ess, &d.en,
                    &d.fcs, &d.accSum) == 23;
        if (ok) out.push_back(d);
    }
    fclose(f);
    if (!ok) out.clear(), MoveFileExW(path.c_str(), (path + L".bak").c_str(), MOVEFILE_REPLACE_EXISTING);
    return ok;
}
