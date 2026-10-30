#include "playback.h"
#include <algorithm>

void Stops::seek(double t) { next = std::upper_bound(times.begin(), times.end(), t, [](double v, int s) { return v < s; }) - times.begin(); }

bool Stops::cross(double from, double to) {
    while (next < times.size() && times[next] <= from) next++;  // passed without a seek (e.g. stops were off)
    if (next < times.size() && times[next] <= to) return next++, true;
    return false;
}
