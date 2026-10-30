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
    void startScan();
    void startSuggest();
    void pickFolder();
    void poll();
};
void drawApp(App& a);
extern int gForceTab;  // >= 0 selects that tab on the next frame (used by --screenshot)
