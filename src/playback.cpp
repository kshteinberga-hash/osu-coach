#include "playback.h"
#include <algorithm>
#include <cmath>

void Stops::seek(double t) { next = std::upper_bound(times.begin(), times.end(), t, [](double v, int s) { return v < s; }) - times.begin(); }

bool Stops::cross(double from, double to) {
    while (next < times.size() && times[next] <= from) next++;  // passed without a seek (e.g. stops were off)
    if (next < times.size() && times[next] <= to) return next++, true;
    return false;
}

void Clock::follow(double realMs, double audioMs) {
    if (!running) return;
    advance(realMs);
    double err = audioMs - t;
    t = std::fabs(err) > 80 ? audioMs : t + err * 0.1;
}