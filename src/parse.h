#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Mod bits as stored in replays and osu!.db (NC replays also carry DT, PF also carries SD).
namespace mod {
constexpr int NF = 1, EZ = 2, HD = 8, HR = 16, SD = 32, DT = 64, RX = 128, HT = 256, NC = 512, FL = 1024, AUTO = 2048,
              SO = 4096, AP = 8192, PF = 16384;
}
// osu!stable formulas. Hit windows are +- ms in map time (divide by the play rate for real time).
inline float circleRadius(float cs) { return 54.4f - 4.48f * cs; }
inline float hitWindow300(float od) { return 80 - 6 * od; }
inline float hitWindow100(float od) { return 140 - 8 * od; }
inline float hitWindow50(float od) { return 200 - 10 * od; }

struct HitObj {  // kind: 0 circle, 1 slider, 2 spinner
    float x, y; int t, end; uint8_t kind; bool nc;
    char curve = 'B'; int slides = 1; float length = 0;  // slider only: curve type L/P/B/C, repeats, pixel length
    std::vector<std::pair<float, float>> pts;               // slider control points after the head
};
struct Beatmap { float ar, cs, od, hp, bpm; int bid; std::string title; std::vector<HitObj> objs; std::string audioFile; };
// Slider body in osu! pixels: head first, ~4 px steps, cut or extended to o.length.
std::vector<std::pair<float, float>> sliderPath(const HitObj& o);
// Slider ball position at map time t (bounces on repeats).
std::pair<float, float> sliderBall(const HitObj& o, const std::vector<std::pair<float, float>>& path, int t);

bool parseOsu(std::string_view text, Beatmap& out);   // false if not std mode
bool loadOsu(const std::wstring& path, Beatmap& out);
bool readFile(const std::wstring& path, std::string& out);

struct Frame { int t; float x, y; int keys; };
struct Replay {
    uint8_t mode; int ver; std::string md5, player;
    uint16_t n300, n100, n50, geki, katu, miss; int score; uint16_t combo; int mods;
    int64_t when;  // unix ms
    std::vector<Frame> frames;
};
bool parseOsr(std::string_view bytes, Replay& r, bool withFrames);
bool readOsr(const std::wstring& path, Replay& r, bool withFrames);

struct DbMap {
    std::string md5; std::wstring path; std::string title; int bid, sid; uint8_t status;
    float ar, cs, hp, od; std::vector<std::pair<int, float>> stars; int drainS, nCircle, nSlider, nSpinner;
};
float starsFor(const DbMap& m, int mods);  // exact EZ/HR/DT/HT entry, else nomod, else -1
bool readOsuDb(const std::wstring& osuDir, std::vector<DbMap>& out);
std::wstring findOsuDir();  // osu-coach.ini -> registry -> %LOCALAPPDATA%\osu! -> L""
void saveOsuDir(const std::wstring& dir);
std::wstring exeDir();
std::wstring widen(std::string_view utf8);
