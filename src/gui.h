#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include "coach.h"
#include "scan.h"
#include "viewer.h"

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
    bool viewing = false;             // the replay viewer covers the window
    std::unique_ptr<Viewer> viewer;
    void openViewer();                // opens the selected play
};
void drawViewer(App& a);
bool viewerWantsFrames(const App& a);  // true while the viewer is playing
void drawApp(App& a);
void setupStyle(float dpi);  // fonts, colours and the 8 px spacing grid
extern int gForceTab;  // >= 0 selects that tab on the next frame (used by --screenshot)

// ---- design system: retro desktop. Cream windows with 2 px ink outlines, hard offset shadows and coloured title bars on a
// gridded blue desk; square corners, 8 px grid, 44 px controls. Buttons press in (the shadow disappears) instead of animating.
#include "imgui.h"
constexpr ImU32 cDesk = IM_COL32(126, 178, 212, 255), cWin = IM_COL32(247, 241, 229, 255), cWell = IM_COL32(236, 227, 210, 255),
                cInk = IM_COL32(30, 27, 44, 255), cRule = IM_COL32(214, 202, 182, 255), cMuted = IM_COL32(94, 87, 104, 255),
                cOrange = IM_COL32(236, 102, 62, 255), cPink = IM_COL32(246, 166, 186, 255), cYellow = IM_COL32(245, 187, 64, 255),
                cBlue = IM_COL32(48, 108, 176, 255), cGood = IM_COL32(22, 108, 60, 255), cBad = IM_COL32(182, 42, 20, 255),
                cWarn = IM_COL32(150, 86, 0, 255);
// the replay playfield is a dark screen inside its window
constexpr ImU32 cScreen = IM_COL32(36, 32, 54, 255), cScreenRule = IM_COL32(78, 72, 102, 255), cScreenMuted = IM_COL32(172, 164, 188, 255),
                cHotPink = IM_COL32(255, 128, 176, 255), cHotRed = IM_COL32(255, 96, 70, 255), cHotAmber = IM_COL32(255, 190, 70, 255),
                cLime = IM_COL32(186, 232, 96, 255);
enum { BODY, LABEL, TITLE, HEAD, BAR };  // Plex Sans 18, Plex Sans SemiBold 15, Bricolage Grotesque 26 / 48 / 19
extern float gS;                          // DPI scale
float S(float v);                         // px at 100% -> px on screen
ImFont* font(int i);
ImU32 alpha(ImU32 c, int a);
std::string sf(const char* f, ...);
void txt(int f, ImU32 c, const std::string& s, bool wrap = false);
// A window: optional title bar in `bar` colour, cream body with 16 px padding, ink outline and a hard shadow.
// w / h <= 0 fill the space left (minus the shadow) plus that amount; autoH sizes the height to the content.
void window(const char* id, const char* title, ImU32 bar, float w, float h, bool autoH = false, const std::string& note = "");
void endWindow();
void gap();   // 16 px between windows
void rule();  // full-width 1 px divider
bool button1(const char* label, ImVec2 size = {0, 0});  // primary: yellow
bool button2(const char* label, ImVec2 size = {0, 0});  // secondary: cream
float buttonW(const char* label);                       // width of a button with this label
void flow(float w, float gap);  // put the next item (w wide) after the last one, or on a new line if it doesn't fit
bool table(const char* id, int cols, float h = 0, int extra = 0);
void headerRow();
void cell(ImU32 c, const std::string& s);
