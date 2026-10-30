#include "t.h"
#include "../src/playback.h"

static void test_clock_rate() {
    Clock c;
    c.rate = 1.5f, c.speed = 0.5f;
    c.advance(1000);
    NEAR(c.t, 0, 0);  // not running
    c.running = true;
    c.advance(1000);
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

void viewerTests() {
    test_clock_rate();
    test_stops_seek();
    test_hr_draw_y();
}
