#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct HitObj { float x, y; int t, end; uint8_t kind; bool nc; };  // kind: 0 circle, 1 slider, 2 spinner
struct Beatmap { float ar, cs, od, hp, bpm; int bid; std::string title; std::vector<HitObj> objs; };

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
