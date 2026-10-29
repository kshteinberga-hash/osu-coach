#include "scan.h"
#include <algorithm>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <windows.h>

std::string narrow(const std::wstring& w) {
    std::string s(WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr), 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), (int)s.size(), nullptr, nullptr);
    return s;
}

ScanResult scan(const std::wstring& dir, std::atomic<int>* done, std::atomic<int>* total) {
    ScanResult res;
    res.dbOk = readOsuDb(dir, res.db);
    std::unordered_map<std::string, const DbMap*> byMd5;
    for (auto& m : res.db) byMd5[m.md5] = &m;

    std::vector<std::wstring> files;
    std::error_code ec;
    for (auto& e : std::filesystem::directory_iterator(dir + L"\\Data\\r", ec))
        if (e.path().extension() == L".osr") files.push_back(e.path().wstring());
    if (total) *total = (int)files.size();

    std::atomic<size_t> next = 0;
    std::atomic<int> corrupt = 0, deleted = 0, other = 0, modSkip = 0;
    std::mutex mu;
    auto work = [&] {
        Replay r;
        Beatmap b;
        for (size_t i; (i = next++) < files.size();) {
            if (done) ++*done;
            if (!readOsr(files[i], r, false)) { corrupt++; continue; }
            if (r.mode != 0) { other++; continue; }
            if (r.mods & (128 | 2048 | 8192)) { modSkip++; continue; }
            auto it = byMd5.find(r.md5);
            if (it == byMd5.end() || !loadOsu(it->second->path, b)) { deleted++; continue; }
            if (!readOsr(files[i], r, true)) { corrupt++; continue; }
            PlayStats s = analyze(r, b, starsFor(*it->second, r.mods));
            s.file = files[i].substr(files[i].find_last_of(L'\\') + 1);
            if (b.bid == 0) s.bid = it->second->bid;
            std::lock_guard g(mu);
            res.plays.push_back(std::move(s));
        }
    };
    std::vector<std::thread> pool(std::max(1u, std::thread::hardware_concurrency()));
    for (auto& t : pool) t = std::thread(work);
    for (auto& t : pool) t.join();
    res.corrupt = corrupt, res.deleted = deleted, res.otherMode = other, res.modSkip = modSkip;
    std::sort(res.plays.begin(), res.plays.end(), [](auto& a, auto& b) { return a.when < b.when; });
    return res;
}

void dumpJsonl(const ScanResult& r, const std::wstring& out) {
    FILE* f = _wfopen(out.c_str(), L"wb");
    if (!f) return;
    for (auto& p : r.plays) {
        fprintf(f, "{\"file\":\"%s\",\"md5\":\"%s\",\"when\":%lld,\"stars\":%.3f,\"mods\":%d,\"n300\":%d,\"n100\":%d,\"n50\":%d,"
                   "\"miss\":%d,\"smiss\":%d,\"smissc\":%d,\"nobj\":%d,\"ur\":%.2f,\"mean\":%.2f,\"bObj\":[%d,%d,%d,%d],\"bMiss\":[%d,%d,%d,%d]}\n",
                narrow(p.file).c_str(), p.md5.c_str(), (long long)p.when, p.stars, p.mods, p.g300, p.g100, p.g50, p.gmiss,
                p.sMiss, p.sMissCircle, p.nobj, p.err.ur(), p.err.mean(), p.bObj[0], p.bObj[1], p.bObj[2], p.bObj[3],
                p.bMiss[0], p.bMiss[1], p.bMiss[2], p.bMiss[3]);
    }
    fclose(f);
}
