#pragma once
#include "parse.h"

enum Cause : uint8_t { HIT, AIM, TAP, TIMING, NOPRESS };
enum Bucket : uint8_t { DENSE, STREAM, JUMP, BIG };

// One circle or slider head. Times in real ms (mods applied), distances in circle radii.
struct ObjRes {
    int t; float err, dist, spacing, gap, hold;
    uint16_t judge; uint8_t key, cause, bucket, quad;  // key 0 = K1/M1, 1 = K2/M2; quad 0 TL, 1 TR, 2 BL, 3 BR
    int8_t along;                             // aim misses: -1 undershoot, 0 side, +1 overshoot
    bool afterBreak, inStream, tired;         // tired = second half of a run of 20+ notes
};
struct StreamSeg { float bpm; int n, miss, alt, dbl; float errEarly, errLate; };
struct Acc {
    double n = 0, s = 0, ss = 0;
    void add(double v) { n++, s += v, ss += v * v; }
    void add(const Acc& a) { n += a.n, s += a.s, ss += a.ss; }
    double mean() const { return n ? s / n : 0; }
    double ur() const;  // 10 * standard deviation
};
struct PlayStats {
    std::wstring file; std::string md5, title; int bid; int64_t when; int mods; float stars, ar, od, bpm, rate;
    uint16_t g300, g100, g50, gmiss, combo; int nobj, sMiss; float acc;  // g* = game counts, sMiss = simulated
    Acc err, errKey[2], zone[4]; int within[3];                           // within +-15/30/50 ms
    int hist[100];                                                       // 2 ms bins, -100..+100
    int bObj[4], bMiss[4]; double bDist[4];
    int jumpHist[24]; int qObj[4], qMiss[4]; int under, over;
    int zPairs[4], zAlt[4], zOv[4], keyHits[2];                          // zones: <100, 100-130, 130-260, 260+ ms
    std::vector<StreamSeg> streams; int stamNotes; float stamSec, stamNps;
    int brkObj, brkMiss, flowObj, flowMiss;
    std::vector<ObjRes> objs;
};
int zoneOf(float gapMs);
PlayStats analyze(const Replay& r, const Beatmap& b, float stars);
