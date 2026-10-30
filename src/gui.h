#pragma once
#include <atomic>
#include <mutex>
#include "coach.h"
#include "scan.h"

struct App {
    std::wstring osuDir;
    ScanResult res;
    Report rep;
    std::vector<Suggestion> sugg;
    float lo = 3.5f, hi = 4.5f, streamBpm = 0;
    int trainSkill = -1, selPlay = -1;
    bool scanned = false;
    int gen = 0;  // bumped each time a scan lands: GUI caches key on it
    // background work: a worker fills p* and sets *Ready; the UI thread moves them in via poll()
    std::atomic<bool> busy = false, scanReady = false, suggReady = false;
    std::atomic<int> done = 0, total = 0;
    ScanResult pRes;
    Report pRep;
    std::vector<Suggestion> pSugg;
    float pBpm = 0;
    void* hwnd = nullptr;
    std::vector<ObjRes> selObjs;  // per-note results of the selected play, see selectedObjs()
    int selObjsFor = -1, selObjsGen = -1;
    const std::vector<ObjRes>& selectedObjs();
    void startScan();
    void startSuggest();
    void pickFolder();
    void poll();
};
void drawApp(App& a);
void setupStyle(float dpi);  // fonts, colours and the 8 px spacing grid
extern int gForceTab;  // >= 0 selects that tab on the next frame (used by --screenshot)

// ---- design system: scoreboard palette, flat fills, square corners, 8 px grid, 44 px controls ----
#include "imgui.h"
constexpr ImU32 cInk = IM_COL32(20, 18, 16, 255), cPanel = IM_COL32(28, 25, 21, 255), cRule = IM_COL32(58, 52, 44, 255),
                cCream = IM_COL32(242, 234, 219, 255), cMuted = IM_COL32(184, 174, 156, 255), cPink = IM_COL32(255, 102, 170, 255),
                cGreen = IM_COL32(200, 240, 49, 255), cAmber = IM_COL32(255, 176, 32, 255), cRed = IM_COL32(255, 90, 54, 255);
enum { BODY, LABEL, TITLE, HEAD, MONO };  // Plex Sans 18, Plex Sans SemiBold 15, Barlow Condensed 28 / 52, Plex Mono 17
extern float gS;                          // DPI scale
float S(float v);                         // px at 100% -> px on screen
ImFont* font(int i);
ImU32 alpha(ImU32 c, int a);
std::string sf(const char* f, ...);
void txt(int f, ImU32 c, const std::string& s, bool wrap = false);
void section(const char* id, float w, float h, bool autoH = false);  // flat panel, 1 px rule border, 16 px padding
void endSection();
void gap();   // 16 px between sections
void rule();  // full-width 1 px divider
bool button2(const char* label);  // secondary button (panel fill, rule border); ImGui::Button is the pink primary
bool table(const char* id, int cols, float h = 0, int extra = 0);
void headerRow();
void cell(ImU32 c, const std::string& s);
void cellMono(const std::string& s);
