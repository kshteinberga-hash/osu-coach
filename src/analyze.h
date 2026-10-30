#pragma once
#include <cmath>
#include "parse.h"

enum Cause : uint8_t { HIT, AIM, TAP, OFFTIME, NOPRESS };
enum Bucket : uint8_t { DENSE, STREAM, JUMP, BIG };
enum Skill { TIMING, JUMPS, BIGJUMPS, STREAMS, SPEED, READING, STAMINA, NSKILL };

// One circle or slider head. Times in real ms (mods applied), distances in circle radii.
struct ObjRes {
    int t; float err, dist, spacing, gap, hold;
    uint16_t judge; uint8_t key, cause, bucket, quad;  // key 0 = K1/M1, 1 = K2/M2; quad 0 TL, 1 TR, 2 BL, 3 BR
    int8_t along;                             // aim misses: -1 undershoot, 0 side, +1 overshoot
    bool afterBreak, inStream, tired, slider; // tired = second half of a run of 20+ notes
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
    uint16_t g300, g100, g50, gmiss, combo; float acc;  // g* = game counts
    int nobj, sMiss, sMissCircle;  // simulated: all misses incl. slider heads / circles only (comparable to the game's count)
    Acc err, errKey[2], zone[4]; int within[3];                           // within +-15/30/50 ms
    int hist[100];                                                       // 2 ms bins, -100..+100
    int bObj[4], bMiss[4]; double bDist[4];
    int jumpHist[24]; int qObj[4], qMiss[4]; int under, over;
    int zPairs[4], zAlt[4], zOv[4], keyHits[2];                          // zones: <100, 100-130, 130-260, 260+ ms
    std::vector<StreamSeg> streams; int stamNotes; float stamSec, stamNps;
    int brkObj, brkMiss, flowObj, flowMiss;
    int skObj[NSKILL], skMiss[NSKILL];  // notes and misses per skill (Timing: hits, no misses); see inSkill
    std::vector<ObjRes> objs;           // per note; scan() drops these to save memory, see reanalyze()
};
// One key press in real ms (mods applied), the same clock as ObjRes::t. key 0 = K1/M1, 1 = K2/M2; up = -1 if never released.
struct Tap { float t, up; int key; };
std::vector<Tap> taps(const std::vector<Frame>& frames, float rate);
bool inSkill(const ObjRes& o, Skill s);
void tallySkills(PlayStats& s);  // fills skObj/skMiss from objs
int zoneOf(float gapMs);
// Stream segments [first, last) over per-object spacing (radii) and gap (ms, gap[i] = t[i] - t[i-1]):
// >= 4 objects, spacing <= 3, every gap within 15% of the first one, first gap <= maxGap (1/4 of the beat).
std::vector<std::pair<size_t, size_t>> findStreams(const std::vector<float>& spacing, const std::vector<float>& gap, float maxGap);
inline float streamMaxGap(float bpm) { return bpm > 0 ? std::fmin(200.f, 1.2f * 15000 / bpm) : 200.f; }
PlayStats analyze(const Replay& r, const Beatmap& b, float stars);
