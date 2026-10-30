#pragma once
#include "analyze.h"

extern const char* const kSkillName[NSKILL];

// Daily totals per 0.25-star bin (bin = floor(stars * 4), -1 = unknown). Survives deleted maps.
struct Day {
    int date, bin, plays, objs, miss[NSKILL], obj[NSKILL];
    double es, ess, en;  // hit errors: sum, sum of squares, count
    int fcs; double accSum;
};
struct Weakness { Skill s; double pp; std::string metric, fix; };
struct Report {
    float radar[NSKILL]; float actual[NSKILL], expected[NSKILL];  // miss rates (timing: UR)
    bool needHistory;
    Weakness top[3]; int nTop;
    float tier; std::string verdict;
    int trend[3];  // UR, jump miss rate, stream miss rate: +1 improving, 0 flat, -1 regressing
    std::vector<Day> days;
    size_t recentFrom;  // index of the first recent play
};

int ymd(int64_t unixMs);  // local date as yyyymmdd
double ppEstimate(float stars, float od, int nobj, float acc, int misses);
Report coach(const std::vector<PlayStats>& plays, std::vector<Day>& history, int64_t nowMs);
// Plain-English account of a mistake: what happened, what to do instead, and what to practise.
enum Diag { D_AIM, D_TAP, D_EARLY, D_NOPRESS, D_50, D_LOCK, D_GALLOP, D_RUSH, D_DRAG, D_RUNAIM, D_RUN, NDIAG };
struct Explain { std::string title, text, tip, practice; Diag diag; };
// One miss (judge 0) or 50. prev = the note before it, when there is one (to spot a key still held from it).
Explain explainMiss(const ObjRes& o, float beatMs, const ObjRes* prev = nullptr);
// The replay viewer's stops, in time order. A burst or stream (3+ notes, each within 1/4 beat of the last) with
// mistakes in it is one stop, at its first mistake, covering [first, last]; any other mistake is a stop of its own.
struct Stop { int at, first, last, bad; };  // indexes into objs; bad = mistakes it covers
std::vector<Stop> coachStops(const std::vector<ObjRes>& objs, float bpm, bool with50s);
// taps: the play's key presses (see taps()), used to tell finger lock from galloping in a burst or stream.
Explain explainStop(const std::vector<ObjRes>& objs, const std::vector<Tap>& taps, const Stop& s, float bpm);
Explain summarize(const std::vector<Explain>& stops);  // the play's main problem and what to practise
struct MapFeat { float jump, stream, rhythm, slider, ar, od, bpm; int longRun; };
MapFeat features(const Beatmap& b);
void cacheFeatures(const std::string& md5, const MapFeat& f);
struct Suggestion { const DbMap* map; float stars, score; std::string why; };
std::vector<Suggestion> suggest(const std::vector<DbMap>& db, const std::vector<PlayStats>& plays, Skill weak,
                                float lo, float hi, float streamBpm, int64_t nowMs);  // best 5
bool loadHistory(const std::wstring& path, std::vector<Day>& out);  // corrupt -> renamed to .bak, empty
void saveHistory(const std::wstring& path, const std::vector<Day>& days);
