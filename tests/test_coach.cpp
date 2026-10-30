#include "t.h"
#include <filesystem>
#include <fstream>
#include "../src/coach.h"

namespace {
constexpr int64_t DAY = 86400000LL, NOW = 1793318400000LL;

PlayStats mk(int64_t when, float stars, int nJump, int jumpMiss, bool fc, float acc) {
    PlayStats p{};
    p.when = when, p.stars = stars, p.od = 8, p.rate = 1, p.acc = acc, p.gmiss = fc ? 0 : (uint16_t)std::max(1, jumpMiss);
    for (int i = 0; i < nJump + 50; i++) {
        ObjRes o{};
        bool jump = i < nJump;
        o.bucket = jump ? JUMP : DENSE, o.spacing = jump ? 5.f : 1.f, o.gap = 300, o.t = i * 300;
        o.judge = jump && i < jumpMiss ? 0 : 300;
        o.cause = o.judge ? HIT : AIM;
        o.err = i % 2 ? 10.f : -10.f;
        if (o.judge) p.err.add(o.err);
        p.objs.push_back(o);
    }
    p.nobj = (int)p.objs.size();
    tallySkills(p);
    return p;
}
}

static void test_pp() {
    double a = ppEstimate(4.0f, 8, 800, 0.97f, 0);
    CHECK(a > 100 && a < 250);
    CHECK(ppEstimate(4.0f, 8, 800, 0.97f, 1) < a);
    CHECK(ppEstimate(4.5f, 8, 800, 0.97f, 0) > a);
}

static void test_radar_neutral() {
    std::vector<PlayStats> ps;
    for (int i = 0; i < 10; i++) ps.push_back(mk(NOW - 20 * DAY + i * 1000, 4, 100, 5, false, 0.95f));
    for (int i = 0; i < 10; i++) ps.push_back(mk(NOW - 2 * DAY + i * 1000, 4, 100, 5, false, 0.95f));
    std::vector<Day> h;
    Report r = coach(ps, h, NOW);
    CHECK(!r.needHistory);
    for (int s = 0; s < NSKILL; s++) NEAR(r.radar[s], 50, 1);
    CHECK(!h.empty());
}

static void test_radar_half() {
    std::vector<PlayStats> ps;
    for (int i = 0; i < 10; i++) ps.push_back(mk(NOW - 20 * DAY + i * 1000, 4, 1000, 100, false, 0.9f));
    for (int i = 0; i < 10; i++) ps.push_back(mk(NOW - 2 * DAY + i * 1000, 4, 1000, 50, false, 0.9f));
    std::vector<Day> h;
    Report r = coach(ps, h, NOW);
    NEAR(r.radar[JUMPS], 75, 3);
    CHECK(r.nTop >= 1 && r.top[0].s == JUMPS);
    CHECK(!r.top[0].metric.empty() && !r.top[0].fix.empty());
}

static void test_no_baseline() {
    std::vector<PlayStats> ps;
    for (int i = 0; i < 3; i++) ps.push_back(mk(NOW - DAY + i * 1000, 4, 100, 5, false, 0.95f));
    std::vector<Day> h;
    Report r = coach(ps, h, NOW);
    CHECK(r.needHistory);
    for (int s = 0; s < NSKILL; s++) CHECK(r.radar[s] == 50);
    CHECK(!r.verdict.empty());
    for (int i = 0; i < r.nTop; i++) CHECK(std::isfinite(r.top[i].pp));
    std::vector<PlayStats> none;
    Report e = coach(none, h, NOW);
    CHECK(!e.verdict.empty() && e.nTop == 0);
}

static void test_history_roundtrip() {
    auto dir = std::filesystem::temp_directory_path() / L"osu-coach-hist";
    std::filesystem::create_directories(dir);
    std::wstring p = (dir / L"history.json").wstring();
    std::vector<PlayStats> ps{mk(NOW - DAY, 4.1f, 100, 5, false, 0.95f), mk(NOW, 3.3f, 20, 0, true, 0.99f)};
    std::vector<Day> h, back;
    coach(ps, h, NOW);
    saveHistory(p, h);
    CHECK(loadHistory(p, back));
    CHECK(back.size() == h.size() && back.size() == 2);
    if (back.size() == 2) {
        CHECK(back[0].date == h[0].date && back[0].bin == h[0].bin && back[0].objs == h[0].objs);
        CHECK(back[1].miss[JUMPS] == h[1].miss[JUMPS] && back[1].fcs == h[1].fcs);
        NEAR(back[0].ess, h[0].ess, 1e-6);
    }
    { std::ofstream f(p); f << "[{\"d\":2026"; }
    CHECK(!loadHistory(p, back) && back.empty());
    CHECK(std::filesystem::exists(p + L".bak"));
    std::filesystem::remove_all(dir);
}

static void test_history_rebin() {
    // the same plays come back in another star bin (star rating recalculated): the date is replaced, not counted twice
    std::vector<PlayStats> ps{mk(NOW - DAY, 4.1f, 100, 5, false, 0.95f), mk(NOW - DAY + 1000, 4.1f, 100, 5, false, 0.95f)};
    std::vector<Day> h;
    coach(ps, h, NOW);
    CHECK(h.size() == 1);
    for (auto& p : ps) p.stars = 4.6f;
    coach(ps, h, NOW);
    CHECK(h.size() == 1 && h[0].bin == 18 && h[0].plays == 2);
    ps.pop_back();  // a replay was deleted: the fuller stored day wins
    coach(ps, h, NOW);
    CHECK(h.size() == 1 && h[0].plays == 2);
}

static void test_timing_rate() {
    // the 300 window is 1.5x tighter in real time on DT, so a perfect-UR play gains less from better timing
    auto timingPp = [](float rate) {
        std::vector<PlayStats> ps;
        for (int i = 0; i < 10; i++) ps.push_back(mk(NOW - DAY + i * 1000, 4, 0, 0, true, 0.95f)), ps.back().rate = rate;
        std::vector<Day> h;
        Report r = coach(ps, h, NOW);
        return r.nTop && r.top[0].s == TIMING ? r.top[0].pp : -1;
    };
    double nm = timingPp(1), dt = timingPp(1.5f);
    CHECK(nm > 0 && dt > 0 && dt < nm);
}

static void test_trend_windows() {
    // a day exactly 7 days back belongs to last week, so this week (fewer jump misses) reads as improving
    std::vector<PlayStats> ps;
    for (int i = 0; i < 3; i++) ps.push_back(mk(NOW - 7 * DAY + i * 1000, 4, 100, 20, false, 0.95f));
    for (int i = 0; i < 3; i++) ps.push_back(mk(NOW - DAY + i * 1000, 4, 100, 5, false, 0.95f));
    std::vector<Day> h;
    Report r = coach(ps, h, NOW);
    CHECK(r.trend[1] == 1);
}

static void test_verdict() {
    std::vector<PlayStats> ps;
    for (int i = 0; i < 6; i++) ps.push_back(mk(NOW - 10 * DAY + i * 1000, 4.0f, 50, 2, i < 3, 0.97f));
    for (int i = 0; i < 6; i++) ps.push_back(mk(NOW - DAY + i * 1000, 4.0f, 50, 2, i < 3, 0.97f));
    std::vector<Day> h;
    Report r = coach(ps, h, NOW);
    NEAR(r.tier, 4.0, 0.01);
    CHECK(r.trend[0] == 0);
    CHECK(r.verdict == "Ready to push to 4.5\xE2\x98\x85");
}

static void test_features() {
    Beatmap b{9, 4, 8, 5, 200, 0, "t", {}};
    float r = 54.4f - 4.48f * 4;
    for (int i = 0; i < 10; i++) b.objs.push_back({50.f + 6 * r * (i % 2), 192, 1000 + 300 * i, 1000 + 300 * i, 0, false});
    MapFeat f = features(b);
    NEAR(f.jump, 1, 0.01);
    CHECK(f.stream == 0);
    b.objs.clear();
    b.bpm = 150;  // 100 ms = 1/4
    for (int i = 0; i < 12; i++) b.objs.push_back({50.f + 1.5f * r * i, 192, 1000 + 100 * i, 1000 + 100 * i, 0, false});
    f = features(b);
    NEAR(f.stream, 1, 0.01);
    CHECK(f.longRun == 12);
    NEAR(f.jump, 0, 0.01);
}

static void test_suggest() {
    std::vector<DbMap> db(3);
    const char* md5[] = {"m-a", "m-b", "m-c"};
    float stars[] = {4.0f, 4.2f, 5.0f}, jump[] = {0.2f, 0.8f, 0.9f};
    for (int i = 0; i < 3; i++) {
        db[i].md5 = md5[i], db[i].stars = {{0, stars[i]}}, db[i].title = md5[i];
        cacheFeatures(md5[i], MapFeat{jump[i], 0, 0, 0, 9, 8, 180, 0});
    }
    auto s = suggest(db, {}, JUMPS, 3.5f, 4.5f, 0, NOW);
    CHECK(s.size() == 2);
    if (s.size() == 2) CHECK(s[0].map == &db[1] && s[1].map == &db[0] && !s[0].why.empty());
    db.push_back(db[1]);  // the same map imported into a second folder
    CHECK(suggest(db, {}, JUMPS, 3.5f, 4.5f, 0, NOW).size() == 2);
}

static void test_dt_accuracy_pp() {
    // DT tightens the real hit window: misses cost more accuracy pp than the same play at 1x
    auto jumpPp = [](float rate) {
        std::vector<PlayStats> ps;
        for (int i = 0; i < 10; i++) ps.push_back(mk(NOW - DAY + i * 1000, 4, 100, 5, false, 0.95f)), ps.back().rate = rate;
        std::vector<Day> h;
        Report r = coach(ps, h, NOW);
        for (int i = 0; i < r.nTop; i++) if (r.top[i].s == JUMPS) return r.top[i].pp;
        return -1.0;
    };
    double nm = jumpPp(1), dt = jumpPp(1.5f);
    CHECK(nm > 0 && dt > nm);
}

static bool has(const std::string& s, const char* w) { return s.find(w) != std::string::npos; }

static ObjRes miss(uint8_t cause, float err = 0, float dist = 0, float spacing = 0, int8_t along = 0) {
    ObjRes o{};
    o.cause = cause, o.err = err, o.dist = dist, o.spacing = spacing, o.along = along, o.gap = 300;
    return o;
}

static void test_explain() {
    Explain e = explainMiss(miss(AIM, 20, 2.4f, 9, -1), 500);
    CHECK(e.title == "Missed: aim");
    CHECK(has(e.text, "stopped short") && has(e.text, "0.7 circle widths outside the circle") && has(e.text, "jump of 4.5 circle widths"));
    CHECK(has(explainMiss(miss(AIM, 0, 1.1f, 9, -1), 500).text, "just outside the edge"));
    CHECK(has(e.tip, "all the way to the centre"));
    CHECK(has(explainMiss(miss(AIM, 0, 2, 9, 1), 500).text, "went past"));
    CHECK(has(explainMiss(miss(AIM, 0, 2, 1, 0), 500).text, "off to the side"));
    CHECK(has(explainMiss(miss(TAP), 500).text, "pressed once for two notes"));
    CHECK(has(explainMiss(miss(OFFTIME, -160), 500).text, "160 ms too early"));
    CHECK(has(explainMiss(miss(NOPRESS), 500).text, "didn't click"));
    ObjRes f = miss(HIT, 70);
    f.judge = 50;
    CHECK(explainMiss(f, 500).title == "50: late");
    f.err = -70;
    CHECK(explainMiss(f, 500).title == "50: early");
    ObjRes s = miss(AIM, 0, 3, 2, -1);
    s.inStream = true;
    CHECK(has(explainMiss(s, 500).text, "in the middle of a stream"));
}

static void test_explain_held() {
    ObjRes prev = miss(HIT), o = miss(TAP);
    prev.judge = 300, prev.key = 1, prev.hold = 160, o.gap = 90;
    Explain e = explainMiss(o, 500, &prev);
    CHECK(e.diag == D_TAP && has(e.text, "pressed K2") && has(e.text, "160 ms") && has(e.tip, "Let go") && has(e.practice, "90 ms"));
}

// 3 slow notes (500 ms apart), then a 12-note stream 80 ms apart (187 BPM), then one slow note
static std::vector<ObjRes> streamPlay() {
    std::vector<ObjRes> v;
    for (int i = 0; i < 16; i++) {
        ObjRes o{};
        bool st = i >= 3 && i < 15;
        o.t = i < 3 ? i * 500 : i < 15 ? 1500 + (i - 3) * 80 : 1500 + 11 * 80 + 500;
        o.gap = i == 0 ? 1e9f : (float)(o.t - v.back().t), o.judge = 300, o.inStream = st, o.spacing = 1;
        v.push_back(o);
    }
    return v;
}
static Tap tap(float t, int key, float hold = 30) { return {t, t + hold, key}; }

static void test_stops_group() {
    auto v = streamPlay();
    v[1].judge = 0, v[1].cause = AIM;
    for (int i : {8, 9, 11, 13}) v[i].judge = 0, v[i].cause = NOPRESS;
    v[10].judge = 50;
    auto s = coachStops(v, 180, false);
    CHECK(s.size() == 2);
    if (s.size() == 2) {
        CHECK(s[0].at == 1 && s[0].first == 1 && s[0].last == 1 && s[0].bad == 1);
        CHECK(s[1].at == 8 && s[1].first == 3 && s[1].last == 14 && s[1].bad == 4);  // one stop for the whole stream
    }
    CHECK(coachStops(v, 180, true).back().bad == 5);
}

static void test_finger_lock() {
    auto v = streamPlay();
    std::vector<Tap> taps;
    for (int i = 3; i < 8; i++) taps.push_back(tap((float)v[i].t, i % 2));  // alternates for 5 notes
    taps.push_back(tap((float)v[8].t, 1, 250));                               // then K2 goes down and stays
    taps.push_back(tap((float)v[12].t, 1));
    for (int i = 8; i < 15; i++) if (i != 12) v[i].judge = 0, v[i].cause = NOPRESS;
    auto s = coachStops(v, 180, false);
    Explain e = explainStop(v, taps, s.back(), 180);
    CHECK(e.diag == D_LOCK && e.title == "Finger lock in a stream");
    CHECK(has(e.text, "12-note stream at 188 BPM") && has(e.text, "first 5 notes") && has(e.text, "pressed only 2 times for the last 7 notes"));
    CHECK(has(e.text, "K2 stayed down for 250 ms"));
    CHECK(has(e.practice, "12-note streams at about 170 BPM"));
}

static void test_gallop() {
    auto v = streamPlay();
    std::vector<Tap> taps;
    for (int i = 3; i < 15; i++) {  // K1 comes 20 ms early: K1 -> K2 takes 100 ms, K2 -> K1 60 ms
        int k = (i - 3) % 2;
        float err = k ? 0.f : -20.f;
        taps.push_back(tap(v[i].t + err, k, k ? 25.f : 60.f));
        v[i].err = err, v[i].judge = k ? 300 : 100;
    }
    v[9].judge = 0, v[9].cause = OFFTIME;
    Explain e = explainStop(v, taps, coachStops(v, 180, false).back(), 180);
    CHECK(e.diag == D_GALLOP && e.title == "Galloping in a stream");
    CHECK(has(e.text, "K1 to K2 took 100 ms") && has(e.text, "K2 to K1 took 60 ms") && has(e.text, "both should be 80 ms"));
    CHECK(has(e.text, "hold K1 down for 60 ms") && has(e.tip, "Press lighter"));
    Explain sum = summarize({e, explainMiss(miss(AIM, 0, 2, 9, -1), 500), e});
    CHECK(sum.diag == D_GALLOP && has(sum.title, "galloping (2 of 3)") && sum.practice == e.practice);
}

static PlayStats hp(int64_t when, const char* md5, float acc, float stars = 4) {
    PlayStats p{};
    p.when = when, p.md5 = md5, p.title = md5, p.acc = acc, p.stars = stars;
    return p;
}

static void test_habits() {
    const int64_t MIN = 60000;
    std::vector<PlayStats> ps;
    for (int i = 0; i < 12; i++) ps.push_back(hp(NOW - DAY + i * 3 * MIN, i < 7 ? "same" : i % 2 ? "a" : "b", 0.95f));
    auto h = habits(ps, 0, NOW);
    CHECK(h.size() == 1 && h[0].title == "Too many retries of one map" && has(h[0].text, "same 7 times in a row"));

    ps.clear();
    for (int i = 0; i < 12; i++) ps.push_back(hp(NOW - DAY + i * 3 * MIN, i % 2 ? "a" : "b", i < 8 ? 0.80f : 0.95f));
    h = habits(ps, 0, NOW);
    CHECK(h.size() == 1 && h[0].title == "Most of your maps are too hard right now" && has(h[0].text, "8 of your last 12"));

    // two 2-hour sessions, 13 plays each 10 minutes apart: 96% at the start, 92% at the end, same stars
    ps.clear();
    for (int d = 2; d >= 1; d--)
        for (int i = 0; i < 13; i++) ps.push_back(hp(NOW - d * DAY + i * 10 * MIN, i % 2 ? "a" : "b", i < 4 ? 0.96f : i >= 9 ? 0.92f : 0.95f));
    h = habits(ps, 0, NOW);
    CHECK(!h.empty() && h[0].title == "You get worse late in long sessions" && has(h[0].text, "from 96.0% in the first third to 92.0%"));

    // three short sessions that each start cold
    ps.clear();
    for (int d = 3; d >= 1; d--)
        for (int i = 0; i < 6; i++) ps.push_back(hp(NOW - d * DAY + i * 5 * MIN, i % 2 ? "a" : "b", i < 2 ? 0.90f : 0.95f));
    h = habits(ps, 0, NOW);
    CHECK(h.size() == 1 && h[0].title == "Warm up before your hard maps");

    ps.clear();  // a normal week: nothing to say
    for (int i = 0; i < 20; i++) ps.push_back(hp(NOW - 5 * DAY + i * 4 * 60 * MIN, i % 3 ? "a" : "b", 0.95f));
    CHECK(habits(ps, 0, NOW).empty());
}

void coachTests() {
    test_explain();
    test_explain_held();
    test_stops_group();
    test_finger_lock();
    test_gallop();
    test_habits();
    test_features();
    test_suggest();
    test_dt_accuracy_pp();
    test_pp();
    test_radar_neutral();
    test_radar_half();
    test_no_baseline();
    test_history_roundtrip();
    test_history_rebin();
    test_timing_rate();
    test_trend_windows();
    test_verdict();
}
