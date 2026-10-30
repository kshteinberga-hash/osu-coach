#pragma once
#include <array>
#include "analyze.h"
#include "audio.h"
#include "coach.h"
#include "playback.h"

// Replay viewer state: one play, its map, its per-note analysis, the song and the playback clock.
struct Viewer {
    bool ok = false;
    std::string error, audioNote;
    PlayStats play;
    Replay rep;
    Beatmap map;
    std::vector<ObjRes> objs;                                // per note (no spinners), as in analyze()
    std::vector<int> objMap;                                 // objs[i] -> index in map.objs
    std::vector<int> combo;                                  // combo number per map object
    std::vector<std::vector<std::pair<float, float>>> paths; // slider path per map object (empty for others)
    struct Press { int t; float x, y; int key; };
    std::vector<Press> presses;                              // key-down events in map time
    std::vector<Tap> taps;                                   // key presses with releases, real ms like objs
    std::vector<Stop> spots;                                 // the coach's stops (a broken burst or stream is one)
    std::vector<Explain> notes;                              // what the coach says at each stop
    Explain summary;                                         // the play's main problem
    std::vector<int> miss;                                   // indexes into objs of the coach's stops
    struct Hold { int t0, t1; int lane; };                   // a key held from t0 to t1 (map time); lane 0 K1, 1 K2, 2 M1, 3 M2
    std::vector<Hold> holds;                                 // by start time
    int longestHold = 0;
    std::vector<int> lanes;                                  // the lanes this replay uses (K1 and K2 when nothing is pressed)
    std::vector<int> colour;                                 // combo colour index per map object
    std::vector<int> objOf;                                  // map object -> index in objs, -1 for spinners
    std::vector<std::array<int, 4>> judged;                  // judged[k] = 300s, 100s, 50s, misses among objs[0..k]
    Acc hitErr;                                              // hit errors of the whole play (real ms)
    float laneMs = 3000;                                     // width of the key lanes, in map ms (mouse wheel zooms)
    Clock clock;
    Stops stops;
    Audio audio;
    bool coach = true, stop50 = false, muted = false;
    float volume = 0.6f, preempt = 1200, radius = 36;
    int stoppedAt = -1;                                      // index into spots while paused at a stop
    double endT = 0;

    bool open(const std::wstring& osuDir, const PlayStats& p, const std::vector<DbMap>& db);
    int mapT(int i) const { return map.objs[objMap[i]].t; }  // map time of objs[i]
    void setPlaying(bool on);
    void seek(double t);           // also re-arms coach stops after t
    void jumpTo(int k);            // pause at stop k (index into spots) and show its explanation
    void setSpeed(float s);
    void applyVolume();
    void rebuildStops();
    void tick(double realMs);      // advance the clock (audio-driven when the song plays) and fire coach stops
    std::pair<float, float> cursorAt(double t, int* keys = nullptr) const;
    int judgedBefore(double t) const;  // number of objs whose time is <= t
};
