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
