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
void dumpJsonl(const ScanResult& r, const std::wstring& outPath);
std::string narrow(const std::wstring& w);
