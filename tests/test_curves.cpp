#include "t.h"
#include "../src/parse.h"

namespace {
using Pt = std::pair<float, float>;
HitObj slider(char curve, std::vector<Pt> pts, float length, int slides = 1, float x = 0, float y = 0) {
    HitObj o{x, y, 1000, 2000, 1, true};
    o.curve = curve, o.pts = std::move(pts), o.length = length, o.slides = slides;
    return o;
}
float pathLen(const std::vector<Pt>& p) {
    float s = 0;
    for (size_t i = 1; i < p.size(); i++) s += std::hypot(p[i].first - p[i - 1].first, p[i].second - p[i - 1].second);
    return s;
}
float nearest(const std::vector<Pt>& p, Pt q) {  // distance from q to the drawn polyline
    float best = 1e9f;
    for (size_t i = 0; i + 1 < p.size(); i++) {
        float ax = p[i].first, ay = p[i].second, dx = p[i + 1].first - ax, dy = p[i + 1].second - ay, l2 = dx * dx + dy * dy;
        float t = l2 > 0 ? std::fmax(0.f, std::fmin(1.f, ((q.first - ax) * dx + (q.second - ay) * dy) / l2)) : 0;
        best = std::fmin(best, std::hypot(ax + t * dx - q.first, ay + t * dy - q.second));
    }
    return best;
}
}

static void test_slider_linear() {
    auto p = sliderPath(slider('L', {{100, 0}}, 80));
    NEAR(p.back().first, 80, 2); NEAR(p.back().second, 0, 2);
    NEAR(pathLen(p), 80, 2);
}

static void test_slider_perfect() {
    float arc = 3.14159265f * 50;  // half circle of radius 50 through (0,0) (50,50) (100,0)
    auto p = sliderPath(slider('P', {{50, 50}, {100, 0}}, arc));
    CHECK(nearest(p, {50, 50}) <= 1.5f);
    NEAR(pathLen(p), arc, 2);
    NEAR(p.back().first, 100, 2);
}

static void test_slider_bezier() {
    auto p = sliderPath(slider('B', {{50, 100}, {100, 0}}, 120));
    NEAR(p.front().first, 0, 0.01); NEAR(p.front().second, 0, 0.01);
    NEAR(pathLen(p), 120, 2);
}

static void test_slider_red_anchor() {
    auto p = sliderPath(slider('B', {{50, 0}, {50, 0}, {50, 50}}, 100));
    CHECK(nearest(p, {50, 0}) <= 1.5f);
    NEAR(p.back().first, 50, 2); NEAR(p.back().second, 50, 2);
}

static void test_slider_catmull() {
    auto p = sliderPath(slider('C', {{50, 30}, {100, 0}}, 60, 1, 10, 20));
    NEAR(p.front().first, 10, 0.01); NEAR(p.front().second, 20, 0.01);
}

static void test_slider_degenerate() {
    auto p = sliderPath(slider('X', {}, 50, 1, 7, 9));
    CHECK(!p.empty());
    for (auto& a : p) CHECK(std::isfinite(a.first) && std::isfinite(a.second));
    auto q = sliderPath(slider('P', {{50, 0}, {100, 0}}, 100));  // collinear: falls back to bezier
    NEAR(q.back().first, 100, 2);
}

static void test_slider_extend() {
    auto p = sliderPath(slider('L', {{10, 0}}, 40));
    NEAR(p.back().first, 40, 2); NEAR(p.back().second, 0, 2);
}

static void test_ball() {
    HitObj o = slider('L', {{100, 0}}, 100, 2);  // t 1000..2000, two slides
    auto p = sliderPath(o);
    auto mid = sliderBall(o, p, 1500), end = sliderBall(o, p, 2000), start = sliderBall(o, p, 1000);
    NEAR(mid.first, 100, 3); NEAR(end.first, 0, 3); NEAR(start.first, 0, 3);
}

static void test_osu_curve_parse() {
    Beatmap b;
    CHECK(parseOsu("[General]\nAudioFilename: audio.mp3\nMode: 0\n[Difficulty]\nSliderMultiplier:1.4\n"
                   "[TimingPoints]\n0,500,4,2,0,50,1,0\n[HitObjects]\n100,100,2000,2,0,L|200:100,2,140\n", b));
    CHECK(b.audioFile == "audio.mp3");
    CHECK(b.objs.size() == 1);
    if (b.objs.empty()) return;
    auto& o = b.objs[0];
    CHECK(o.curve == 'L' && o.slides == 2 && o.pts.size() == 1);
    NEAR(o.length, 140, 0.01);
    if (!o.pts.empty()) { NEAR(o.pts[0].first, 200, 0); NEAR(o.pts[0].second, 100, 0); }
}

void curveTests() {
    test_slider_linear();
    test_slider_perfect();
    test_slider_bezier();
    test_slider_red_anchor();
    test_slider_catmull();
    test_slider_degenerate();
    test_slider_extend();
    test_ball();
    test_osu_curve_parse();
}
