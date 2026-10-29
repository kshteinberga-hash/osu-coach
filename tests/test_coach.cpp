#include "t.h"
#include <filesystem>
#include <fstream>
#include "../src/coach.h"

namespace {
constexpr int64_t DAY = 86400000LL, NOW = 1793318400000LL;

PlayStats mk(int64_t when, float stars, int nJump, int jumpMiss, bool fc, float acc) {
    PlayStats p{};
    p.when = when, p.stars = stars, p.od = 8, p.acc = acc, p.gmiss = fc ? 0 : (uint16_t)std::max(1, jumpMiss);
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
}

void coachTests() {
    test_features();
    test_suggest();
    test_pp();
    test_radar_neutral();
    test_radar_half();
    test_no_baseline();
    test_history_roundtrip();
    test_verdict();
}
