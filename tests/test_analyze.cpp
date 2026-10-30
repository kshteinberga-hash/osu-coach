#include "t.h"
#include "../src/analyze.h"

namespace {
constexpr float R = 54.4f - 4.48f * 4;  // CS 4 radius

Beatmap map3() {
    Beatmap b{9, 4, 5, 5, 120, 0, "t", {}};
    b.objs = {{100, 100, 1000, 1000, 0, true}, {300, 100, 1500, 1500, 0, false}, {300, 300, 2000, 2000, 0, false}};
    return b;
}
struct P { int t; float x, y; int key = 0; };
Replay rep(std::initializer_list<P> ps, int mods = 0, int endT = 0) {
    Replay r{};
    r.mods = mods;
    r.frames.push_back({0, 0, 0, 0});
    for (auto& p : ps) {
        int k = p.key ? 10 : 5;
        r.frames.push_back({p.t, p.x, p.y, k});
        r.frames.push_back({p.t + 40, p.x, p.y, 0});
    }
    if (endT) r.frames.push_back({endT, 0, 0, 0});
    return r;
}
}

static void test_perfect() {
    auto s = analyze(rep({{1010, 100, 100}, {1510, 300, 100}, {2010, 300, 300}}), map3(), 4);
    CHECK(s.nobj == 3 && s.sMiss == 0);
    NEAR(s.err.mean(), 10, 0.01);
    CHECK(s.within[0] == 3);
    NEAR(s.err.ur(), 0, 0.01);
}

static void test_aim_undershoot() {
    auto s = analyze(rep({{1000, 100, 100}, {1500, 300 - 2 * R, 100}, {2000, 300, 300}}), map3(), 4);
    CHECK(s.sMiss == 1);
    CHECK(s.objs[1].cause == AIM && s.objs[1].along == -1);
    CHECK(s.under == 1 && s.over == 0);
}

static void test_timing() {
    auto s = analyze(rep({{1000, 100, 100}, {1650, 300, 100}, {2000, 300, 300}}), map3(), 4);
    CHECK(s.objs[1].judge == 50);
    s = analyze(rep({{1000, 100, 100}, {1340, 300, 100}, {2000, 300, 300}}), map3(), 4);
    CHECK(s.objs[1].judge == 0 && s.objs[1].cause == OFFTIME);
}

static void test_tap() {
    Beatmap b = map3();
    b.objs = {{100, 100, 1000, 1000, 0, true}, {100, 100, 1020, 1020, 0, false}};
    auto s = analyze(rep({{1000, 100, 100}}), b, 4);
    CHECK(s.objs[0].judge == 300);
    CHECK(s.objs[1].judge == 0 && s.objs[1].cause == TAP);
}

static void test_hr_flip() {
    auto s = analyze(rep({{1000, 100, 284}, {1500, 300, 284}, {2000, 300, 84}}, 16), map3(), 4);
    CHECK(s.sMiss == 0);
}

static void test_dt_rate() {
    auto s = analyze(rep({{1015, 100, 100}, {1515, 300, 100}, {2015, 300, 300}}, 64), map3(), 4);
    CHECK(s.sMiss == 0);
    NEAR(s.err.mean(), 10, 0.01);
    NEAR(s.rate, 1.5, 0);
    NEAR(s.ar, 5 + (1200 - 400) / 150.0, 0.01);  // AR 9 = 600 ms preempt, 400 ms on DT
    s = analyze(rep({}, 256), map3(), 4);
    NEAR(s.ar, 5 + (1200 - 800) / 150.0, 0.01);  // HT: 800 ms
}

static void test_zero_gap() {
    // objects stacked on one timestamp (2B maps) are not a stream with an infinite BPM
    std::vector<float> sp(6, 0.f), gap(6, 0.f);
    gap[0] = 1e9f;
    CHECK(findStreams(sp, gap, 200).empty());
}

static void test_quit() {
    auto s = analyze(rep({}, 0, 200), map3(), 4);
    CHECK(s.nobj == 1);
}

static void test_degenerate() {
    Beatmap b = map3();
    b.bpm = 0;
    b.objs.resize(1);
    auto s = analyze(rep({{1000, 100, 100}}), b, 0);
    CHECK(s.nobj == 1 && s.streams.empty());
    CHECK(s.err.ur() == 0);
    Replay empty{};
    CHECK(analyze(empty, b, 0).nobj == 0);
}

static void test_stream() {
    Beatmap b = map3();
    b.objs.clear();
    for (int i = 0; i < 6; i++) b.objs.push_back({100.f + 30 * i, 100, 1000 + 125 * i, 1000 + 125 * i, 0, i == 0});
    Replay r = rep({{1000, 100, 100, 0}, {1125, 130, 100, 1}, {1250, 160, 100, 0}, {1375, 190, 100, 1}, {1500, 220, 100, 0}, {1625, 250, 100, 1}});
    auto s = analyze(r, b, 4);
    CHECK(s.streams.size() == 1);
    if (s.streams.empty()) return;
    CHECK(s.streams[0].n == 6 && s.streams[0].alt == 5 && s.streams[0].dbl == 0);
    NEAR(s.streams[0].bpm, 120, 0.1);
    CHECK(s.zPairs[1] == 5 && s.zAlt[1] == 5);
    CHECK(s.keyHits[0] == 3 && s.keyHits[1] == 3);
    CHECK(s.objs[3].inStream);
}

static void test_half_not_stream() {
    Beatmap b = map3();
    b.bpm = 180, b.objs.clear();
    for (int i = 0; i < 6; i++) b.objs.push_back({100.f + 30 * i, 100, 1000 + 167 * i, 1000 + 167 * i, 0, i == 0});
    auto s = analyze(rep({{1000, 100, 100}}), b, 4);
    CHECK(s.streams.empty());
}

void analyzeTests() {
    test_half_not_stream();
    test_perfect();
    test_aim_undershoot();
    test_timing();
    test_tap();
    test_hr_flip();
    test_dt_rate();
    test_quit();
    test_degenerate();
    test_stream();
    test_zero_gap();
}
