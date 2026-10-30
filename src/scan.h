#pragma once
#include <atomic>
#include "analyze.h"

struct ScanResult {
    std::vector<PlayStats> plays;  // sorted by time
    std::vector<DbMap> db;
    int corrupt = 0, deleted = 0, otherMode = 0, modSkip = 0;
    bool dbOk = false;
};
ScanResult scan(const std::wstring& osuDir, std::atomic<int>* done = nullptr, std::atomic<int>* total = nullptr);
// Per-note results for one scanned play (scan() drops them): re-reads its replay and map. Empty if either is gone.
std::vector<ObjRes> reanalyze(const std::wstring& osuDir, const PlayStats& p, const std::vector<DbMap>& db);
void dumpJsonl(const ScanResult& r, const std::wstring& outPath);
std::string narrow(const std::wstring& w);
