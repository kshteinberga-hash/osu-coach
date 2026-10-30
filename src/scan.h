#pragma once
#include <atomic>
#include "analyze.h"

struct ScanResult {
    std::wstring dir;              // the osu! folder this scan read: replays are re-read from here, not the current setting
    std::vector<PlayStats> plays;  // sorted by time
    std::vector<DbMap> db;
    int corrupt = 0, deleted = 0, otherMode = 0, modSkip = 0;
    bool dbOk = false;
};
// cancel: stop early and return what was analysed so far
ScanResult scan(const std::wstring& osuDir, std::atomic<int>* done = nullptr, std::atomic<int>* total = nullptr,
                const std::atomic<bool>* cancel = nullptr);
const DbMap* findMap(const std::vector<DbMap>& db, const std::string& md5);  // nullptr if not in osu!.db
std::wstring replayPath(const std::wstring& osuDir, const PlayStats& p);
// Per-note results for one play (scan() drops them): re-reads its replay and map. Empty if either is gone.
// Takes paths, not the scan, so it can run on a worker while the UI replaces the scan.
std::vector<ObjRes> reanalyze(const std::wstring& replay, const std::wstring& map, float stars);
void dumpJsonl(const ScanResult& r, const std::wstring& outPath);
std::string narrow(const std::wstring& w);
