#include "t.h"
#include <windows.h>
#include <cstdlib>
#include <filesystem>
#include "../src/audio.h"
#include "../src/parse.h"

static void test_audio_missing() {
    Audio a;
    CHECK(!a.load(L"nope-does-not-exist.mp3"));
    CHECK(!a.loaded() && !a.playing());
    a.play(), a.seekMs(1000), a.setSpeed(0.5f), a.setVolume(0.3f), a.pause();
    CHECK(a.positionMs() == 0);
}

static void test_audio_real() {
    const char* d = std::getenv("OSU_DIR");
    if (!d) { std::printf("  (OSU_DIR not set, skipping real audio test)\n"); return; }
    std::wstring dir = widen(d);
    std::vector<DbMap> db;
    if (!readOsuDb(dir, db)) { CHECK(false); return; }
    // newest replay whose map is installed
    std::vector<std::filesystem::directory_entry> reps;
    for (auto& e : std::filesystem::directory_iterator(dir + L"\\Data\\r")) reps.push_back(e);
    std::sort(reps.begin(), reps.end(), [](auto& a, auto& b) { return a.last_write_time() > b.last_write_time(); });
    for (auto& e : reps) {
        Replay r;
        if (!readOsr(e.path().wstring(), r, false)) continue;
        auto m = std::find_if(db.begin(), db.end(), [&](const DbMap& x) { return x.md5 == r.md5; });
        Beatmap b;
        if (m == db.end() || !loadOsu(m->path, b) || b.audioFile.empty()) continue;
        std::wstring audio = m->path.substr(0, m->path.find_last_of(L'\\') + 1) + widen(b.audioFile);
        Audio a;
        CHECK(a.load(audio));
        if (!a.loaded()) return;
        a.setVolume(0);
        a.seekMs(10000);
        NEAR(a.positionMs(), 10000, 30);
        a.play();
        Sleep(500);
        a.pause();
        NEAR(a.positionMs(), 10500, 80);
        a.setSpeed(0.5f);
        a.play();
        Sleep(500);
        a.pause();
        NEAR(a.positionMs(), 10750, 80);
        return;
    }
    std::printf("  (no replay with installed map audio found)\n");
}

void audioTests() {
    test_audio_missing();
    test_audio_real();
}
