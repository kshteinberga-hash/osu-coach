#include "coach.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <atomic>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>
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
            double base = ppEstimate(p.stars, p.od, tot, p.acc, p.gmiss);
            if (s == TIMING) {
                if (bestUr <= 0 || p.err.n < 2) continue;
                double w300 = hitWindow300(p.od) / p.rate, sd = bestUr / 10, m = p.err.mean();  // real ms, like sd and m
                double p300 = phi((w300 - m) / sd) - phi((-w300 - m) / sd);
                double acc = (p300 * 300 + (1 - p300) * 100) / 300 * (tot - p.gmiss) / tot;
                cost += std::max(0.0, ppEstimate(p.stars, p.od, tot, (float)acc, p.gmiss) - base);
            } else {
                int k = std::min<int>(p.gmiss, p.skMiss[s]);
                float acc = std::min(1.f, p.acc + 300.f * k / (300.f * tot));
                cost += ppEstimate(p.stars, p.od, tot, acc, p.gmiss - k) - base;
            }
        }
        if (cost <= 0.05) continue;
        Weakness w{(Skill)s, cost, "", ""};
        if (s == TIMING) {
            w.metric = fmt("Your timing steadiness (UR) is %.0f; in your best week it was %.0f. Lower is steadier.", recentErr.ur(), bestUr);
            w.fix = std::fabs(recentErr.mean()) > 5
                ? fmt("You click %.0f ms too %s on average: tap a little %s, or change your offset in the settings.", std::fabs(recentErr.mean()),
                      recentErr.mean() < 0 ? "early" : "late", recentErr.mean() < 0 ? "later" : "earlier")
                : "Play maps with strict timing (OD 8 or more) around 3.5\xE2\x98\x85 for 10 minutes a day. Tap to the music, not to what you see.";
        } else {
            w.metric = r.needHistory
                ? fmt("You miss %.1f%% of %s (%.0f of %.0f lately).", 100 * r.actual[s], kLabel[s], act.miss[s], act.obj[s])
                : fmt("You miss %.1f%% of %s. Normally at this star rating you miss %.1f%%.", 100 * r.actual[s], kLabel[s], 100 * r.expected[s]);
            double ov = sig[3] ? 100.0 * sig[2] / sig[3] : 0, j = sig[6] + sig[7] ? 100.0 * sig[6] / (sig[6] + sig[7]) : 50;
            double alt = sig[4] + sig[5] ? 100.0 * sig[4] / (sig[4] + sig[5]) : 100, late = lateN ? lateSum / lateN : 0;
            switch (s) {
                case JUMPS: case BIGJUMPS:
                    w.fix = sig[0] >= sig[1]
                        ? fmt("You usually stop short of the circle (%d times, against %d times going past it). Move all the way to the centre. Practise jump maps for 10 minutes a day.", sig[0], sig[1])
                        : fmt("You usually go past the circle (%d times, against %d times stopping short). Slow down at the end of each jump and stop on the circle.", sig[1], sig[0]);
                    break;
                case STREAMS:
                    w.fix = late < -8 ? fmt("You speed up in long streams (later notes come %.0f ms too early). Tap a little slower and follow the music, not your fingers.", -late)
                          : alt < 60 ? fmt("You switch fingers on only %.0f%% of stream notes. Switch on every note: J, K, J, K.", alt)
                                     : std::string("Practise streams a little slower than your limit until you full-combo them, then go 5 BPM faster.");
                    break;
                case SPEED:
                    w.fix = ov > 20 ? fmt("Finger lock: on %.0f%% of fast notes you still hold the last key when you press the next one. Let go quickly and switch fingers on every note.", ov)
                          : j < 35 || j > 65 ? fmt("%.0f%% of your taps are on %s. Use both fingers, even on slower notes, so both stay warmed up.", std::max(j, 100 - j), j < 35 ? "K" : "J")
                                             : std::string("Practise short fast bursts a bit above your comfortable speed, then go back to full maps.");
                    break;
                case READING: w.fix = "You miss the first note after a pause. Keep your eyes on the screen during pauses, and play maps where circles appear quickly (AR 8.5 to 9)."; break;
                default: w.fix = "You miss more near the end of long runs: your hand gets tired. Play longer stream maps at a speed you can manage, and relax your hand between bursts."; break;
            }
        }
        ws.push_back(std::move(w));
    }
    std::sort(ws.begin(), ws.end(), [](auto& a, auto& b) { return a.pp > b.pp; });
    r.nTop = (int)std::min<size_t>(3, ws.size());
    for (int i = 0; i < r.nTop; i++) r.top[i] = ws[i];

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

Explain explainMiss(const ObjRes& o, float beatMs) {
    std::string where = o.inStream ? " in the middle of a stream" : o.afterBreak ? " right after a pause" : "";
    if (o.slider) where += " on the start of a slider";
    const char* side = o.err < 0 ? "early" : "late";
    if (o.judge == 50)
        return {o.err < 0 ? "50: early" : "50: late", fmt("You hit it, but %.0f ms %s, so it only counted as a 50%s.", std::fabs(o.err), side, where.c_str()),
                "Keep a steady rhythm: listen to the beat instead of watching the circles."};
    switch (o.cause) {
        case AIM: {
            float out = (o.dist - 1) / 2;  // circle widths beyond the circle's edge (dist is in radii)
            std::string t = fmt("You clicked on time (%.0f ms %s), but your cursor was ", std::fabs(o.err), side) +
                            (out < 0.25f ? std::string("just outside the edge of the circle. ") : fmt("%.1f circle widths outside the circle. ", out));
            t += o.along < 0 ? "You stopped short of it." : o.along > 0 ? "You went past it." : "You were off to the side.";
            if (o.spacing > 4) t += fmt(" This was a jump of %.1f circle widths", o.spacing / 2) + where + ".";
            else if (!where.empty()) t += " This happened" + where + ".";
            return {"Missed: aim", t,
                    o.along < 0 ? "Move all the way to the centre of the circle; don't slow down before you arrive."
                    : o.along > 0 ? "Slow down at the end of the move and stop on the circle."
                                  : "Watch the circle, not your cursor."};
        }
        case TAP:
            return {"Missed: one press for two notes", "You pressed once for two notes: that press counted for the note before, so this one got nothing" + where + ".",
                    "Tap every note. In fast notes, switch fingers on each one."};
        case OFFTIME:
            return {"Missed: too early", fmt("You clicked %.0f ms too early, before this note could be hit%s.", std::fabs(o.err), where.c_str()),
                    "Wait for the approach circle to touch the circle, and follow the music, not your fingers."};
        default:
            return {"Missed: no click", "You didn't click anywhere near this note" + where + ".",
                    "Keep tapping to the rhythm even when you're lost: a guess can still hit."};
    }
    (void)beatMs;
}

std::vector<int> mistakes(const std::vector<ObjRes>& objs, bool with50s) {
    std::vector<int> v;
    for (size_t i = 0; i < objs.size(); i++)
        if (objs[i].judge == 0 || (with50s && objs[i].judge == 50)) v.push_back((int)i);
    return v;
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

std::vector<Suggestion> suggest(const std::vector<DbMap>& db, const std::vector<PlayStats>& plays, Skill weak,
                                float lo, float hi, float streamBpm, int64_t now) {
    std::vector<const DbMap*> cand, todo;
    for (auto& m : db)
        if (float s = starsFor(m, 0); s >= lo && s <= hi) cand.push_back(&m);
    {
        std::lock_guard g(gFeatMu);
        for (auto m : cand) if (!gFeat.count(m->md5)) todo.push_back(m);
    }
    std::atomic<size_t> next = 0;
    auto work = [&] {
        Beatmap b;
        for (size_t i; (i = next++) < todo.size();)
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
        const MapFeat& f = gFeat[m->md5];
        float sc, stars = starsFor(*m, 0);
        std::string why;
        switch (weak) {
            case JUMPS: case BIGJUMPS:
                sc = f.jump + (f.ar >= 9 ? 0.1f : 0), why = fmt("%.0f%% of the notes are jumps; circles appear at AR %.1f", 100 * f.jump, f.ar); break;
            case STREAMS: case SPEED: {
                float target = streamBpm > 0 ? streamBpm * 1.05f : f.bpm;
                sc = f.stream * std::exp(-std::fabs(f.bpm - target) / 20), why = fmt("%.0f%% of the notes are streams, at %.0f BPM", 100 * f.stream, f.bpm);
                break;
            }
            case READING: sc = f.rhythm + (f.ar >= 8.5f ? 0.3f : 0), why = fmt("the rhythm changes on %.0f%% of the notes; circles appear at AR %.1f", 100 * f.rhythm, f.ar); break;
            case STAMINA: sc = f.stream * std::min(1.f, f.longRun / 50.f), why = fmt("its longest stream is %d notes", f.longRun); break;
            default: sc = std::min(1.f, f.od / 8) * (1 - f.slider), why = fmt("strict timing (OD %.1f), %.0f%% plain circles", f.od, 100 * (1 - f.slider)); break;
        }
        int n = recent.count(m->md5) ? recent[m->md5] : 0;
        sc *= n <= 3 ? 1.2f : n > 10 ? 0.5f : 1.f;
        why += fmt(". %.2f\xE2\x98\x85, you played it %d time%s in the last 30 days.", stars, n, n == 1 ? "" : "s");
        if (sc > 0) out.push_back({m, stars, sc, why});
    }
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.score > b.score; });
    if (out.size() > 5) out.resize(5);
    return out;
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
