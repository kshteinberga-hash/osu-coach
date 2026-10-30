#include "t.h"
#include <cmath>
#include "../src/playback.h"

static void test_clock_rate() {
    Clock c;
    c.rate = 1.5f, c.speed = 0.5f;
    c.advance(1000);
    NEAR(c.t, 0, 0);  // not running
    c.running = true;
    for (int i = 0; i < 10; i++) c.advance(100);  // ten 100 ms frames (a single step is capped at 100 ms)
    NEAR(c.t, 750, 0.01);
}

static void test_stops_seek() {
    Stops s;
    s.times = {1000, 2000, 3000};
    s.seek(0);
    CHECK(s.cross(0, 1500));
    CHECK(!s.cross(1500, 1600));
    s.seek(2500);
    CHECK(!s.cross(2500, 2900));
    CHECK(s.cross(2900, 3100));
    s.seek(500);
    CHECK(s.cross(500, 1000));  // re-armed after seeking back
    CHECK(!s.cross(1000, 1000));
}

static void test_hr_draw_y() {
    NEAR(drawY(100, true), 284, 0);
    NEAR(drawY(100, false), 100, 0);
}

static void test_clock_follow_smooth() {
    // the music position arrives in 30 ms chunks (as logged in the real GUI); the clock must still move every frame
    Clock c;
    c.running = true, c.t = 1000;
    double real = 0, prev = c.t, minStep = 1e9, maxStep = 0;
    for (int f = 0; f < 120; f++) {
        real += 16.67;
        double audio = 1000 + std::floor(real / 30) * 30;
        c.follow(16.67, audio);
        minStep = std::fmin(minStep, c.t - prev), maxStep = std::fmax(maxStep, c.t - prev), prev = c.t;
    }
    CHECK(minStep >= 8 && maxStep <= 26);  // every frame moves, never a whole 30 ms chunk at once
    NEAR(c.t, 1000 + real, 30);            // and stays with the music
}

static void test_clock_follow_snap() {
    Clock c;
    c.running = true, c.t = 1000;
    c.follow(16, 9000);  // a seek: the music is far away, so jump to it
    NEAR(c.t, 9000, 0.01);
}

static void test_clock_long_frame() {
    Clock c;
    c.running = true;
    c.advance(30000);  // first frame after the window was idle for 30 s
    CHECK(c.t <= 100);
}

void viewerTests() {
    test_clock_follow_smooth();
    test_clock_follow_snap();
    test_clock_long_frame();
    test_clock_rate();
    test_stops_seek();
    test_hr_draw_y();
}
