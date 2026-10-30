#pragma once
#include <vector>

// Replay viewer playback logic, free of any UI so it can be unit-tested.
struct Clock {  // map time in ms
    double t = 0;
    float rate = 1, speed = 1;  // rate: 1.5 DT/NC, 0.75 HT; speed: user's 0.25..1
    bool running = false;
    void advance(double realMs) { if (running) t += (realMs < 100 ? realMs : 100) * rate * speed; }  // a long frame (window was idle) never leaps
    // While the song plays: move smoothly with real time and steer toward the music position, which only updates
    // in sound-card-sized chunks (10-100 ms); jump only when they are far apart (after a seek).
    void follow(double realMs, double audioMs);
};

struct Stops {  // coach stops at mistake times (map ms, ascending)
    std::vector<int> times;
    size_t next = 0;
    void seek(double t);                // the next stop is the first one after t (stops after t fire again)
    bool cross(double from, double to); // a stop lies in (from, to]: consume it and return true
};

inline float drawY(float y, bool hr) { return hr ? 384 - y : y; }  // objects only: HR replay frames are already flipped
