#include "t.h"
#include "../src/scan.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>

static const char* kMap =
    "osu file format v14\n\n[General]\nMode: 0\n\n[Metadata]\nTitle:Song\nArtist:Art\nVersion:Hard\nBeatmapID:123\n\n"
    "[Difficulty]\nHPDrainRate:5\nCircleSize:4\nOverallDifficulty:8\nApproachRate:9\nSliderMultiplier:1.4\n\n"
    "[TimingPoints]\n0,500,4,2,0,50,1,0\n0,-50,4,2,0,50,0,0\n10000,400,4,2,0,50,1,0\n\n"
    "[HitObjects]\n256,192,1000,5,0,0:0:0:0:\n100,100,2000,2,0,L|200:100,2,140\n256,192,4000,12,0,6000,0:0:0:0:\n";

static void test_osu_basic() {
    Beatmap b;
    CHECK(parseOsu(kMap, b));
    NEAR(b.bpm, 120, 0.01);
    NEAR(b.od, 8, 0); NEAR(b.cs, 4, 0); NEAR(b.ar, 9, 0); NEAR(b.hp, 5, 0);
    CHECK(b.bid == 123);
    CHECK(b.title == "Art - Song [Hard]");
    CHECK(b.objs.size() == 3);
    if (b.objs.size() != 3) return;
    CHECK(b.objs[0].kind == 0 && b.objs[1].kind == 1 && b.objs[2].kind == 2);
    CHECK(b.objs[0].nc);
    CHECK(b.objs[1].end == 2500);
    CHECK(b.objs[2].end == 6000);
}

static void test_osu_degenerate() {
    Beatmap b;
    CHECK(parseOsu("[General]\nMode: 0\n[Difficulty]\nOverallDifficulty:7\n[HitObjects]\n10,10,500,1,0\n", b));
    CHECK(b.bpm == 0);
    CHECK(b.objs.size() == 1);
}

static void test_osu_ar_default() {
    Beatmap b;
    CHECK(parseOsu("[Difficulty]\nOverallDifficulty:7\n", b));
    NEAR(b.ar, 7, 0);
    CHECK(!parseOsu("[General]\nMode: 3\n", b));
}

static void uleb(std::string& b, const std::string& s) {
    if (s.empty()) { b += '\0'; return; }
    b += '\x0b';
    size_t n = s.size();
    do { unsigned char c = n & 0x7f; n >>= 7; if (n) c |= 0x80; b += (char)c; } while (n);
    b += s;
}
template <class T> static void put(std::string& b, T v) { b.append((const char*)&v, sizeof v); }

static void test_osr_header() {
    std::string b;
    put<uint8_t>(b, 0); put<int>(b, 20240101);
    uleb(b, "0123456789abcdef0123456789abcdef"); uleb(b, "Inkushka"); uleb(b, "");
    for (uint16_t v : {1, 2, 3, 0, 0, 4}) put<uint16_t>(b, v);
    put<int>(b, 1000); put<uint16_t>(b, 50); put<uint8_t>(b, 0); put<int>(b, 16);
    uleb(b, "");
    put<int64_t>(b, 1793318400000LL * 10000 + 621355968000000000LL);
    put<int>(b, 0);
    Replay r;
    CHECK(parseOsr(b, r, false));
    CHECK(r.mode == 0 && r.ver == 20240101 && r.player == "Inkushka" && r.md5.size() == 32);
    CHECK(r.n300 == 1 && r.n100 == 2 && r.n50 == 3 && r.miss == 4 && r.score == 1000 && r.combo == 50 && r.mods == 16);
    CHECK(r.when == 1793318400000LL);
    CHECK(!parseOsr(std::string_view(b).substr(0, 20), r, false));
}

static void test_osr_long_lifebar() {
    std::string b;
    put<uint8_t>(b, 0); put<int>(b, 20240101); uleb(b, "0123456789abcdef0123456789abcdef"); uleb(b, "p"); uleb(b, "");
    for (int i = 0; i < 6; i++) put<uint16_t>(b, 1);
    put<int>(b, 0); put<uint16_t>(b, 0); put<uint8_t>(b, 0); put<int>(b, 64);
    uleb(b, std::string(5000, '1'));  // life bar graph longer than the 1 KB header read
    put<int64_t>(b, 1793318400000LL * 10000 + 621355968000000000LL); put<int>(b, 0);
    auto p = std::filesystem::temp_directory_path() / L"osu-coach-long.osr";
    { std::ofstream f(p, std::ios::binary); f << b; }
    Replay r;
    CHECK(readOsr(p.wstring(), r, false));
    CHECK(r.mods == 64 && r.when == 1793318400000LL);
    std::filesystem::remove(p);
}

static void test_real_files() {
    const char* d = std::getenv("OSU_DIR");
    if (!d) { std::printf("  (OSU_DIR not set, skipping real-file test)\n"); return; }
    std::wstring dir = widen(d);
    std::vector<DbMap> db;
    CHECK(readOsuDb(dir, db));
    CHECK(db.size() > 100);
    bool starOk = false;
    for (auto& m : db) { float s = starsFor(m, 0); if (s > 0 && s < 15) starOk = true; }
    CHECK(starOk);
    std::filesystem::path newest;
    std::filesystem::file_time_type nt{};
    for (auto& e : std::filesystem::directory_iterator(dir + L"\\Data\\r"))
        if (e.path().extension() == ".osr" && e.last_write_time() > nt) nt = e.last_write_time(), newest = e.path();
    Replay r;
    CHECK(readOsr(newest.wstring(), r, true));
    CHECK(r.frames.size() > 100);
    CHECK(!findOsuDir().empty());
}

static void test_unicode_path() {
    auto dir = std::filesystem::temp_directory_path() / L"osu-coach-тест-テスト";
    std::filesystem::create_directories(dir);
    { std::ofstream f(dir / L"a.osu", std::ios::binary); f << kMap; }
    Beatmap b;
    CHECK(loadOsu((dir / L"a.osu").wstring(), b));
    CHECK(b.objs.size() == 3);
    std::filesystem::remove_all(dir);
}

static void test_deleted_map() {
    auto dir = std::filesystem::temp_directory_path() / L"osu-coach-scan";
    std::filesystem::create_directories(dir / L"Data" / L"r");
    std::string db;
    put<int>(db, 20260924); put<int>(db, 0); put<uint8_t>(db, 1); put<int64_t>(db, 0); uleb(db, "x"); put<int>(db, 0); put<int>(db, 0);
    { std::ofstream f(dir / L"osu!.db", std::ios::binary); f << db; }
    std::string b;
    put<uint8_t>(b, 0); put<int>(b, 20240101); uleb(b, "0123456789abcdef0123456789abcdef"); uleb(b, "p"); uleb(b, "");
    for (int i = 0; i < 6; i++) put<uint16_t>(b, 0);
    put<int>(b, 0); put<uint16_t>(b, 0); put<uint8_t>(b, 0); put<int>(b, 0); uleb(b, ""); put<int64_t>(b, 0); put<int>(b, 0);
    { std::ofstream f(dir / L"Data" / L"r" / L"a.osr", std::ios::binary); f << b; }
    ScanResult r = scan(dir.wstring());
    CHECK(r.dbOk && r.db.empty());
    CHECK(r.deleted == 1 && r.plays.empty());
    std::filesystem::remove_all(dir);
}

int main() {
    test_deleted_map();
    test_osu_basic();
    test_osu_degenerate();
    test_osu_ar_default();
    test_osr_header();
    test_osr_long_lifebar();
    test_real_files();
    test_unicode_path();
    void analyzeTests(); analyzeTests();
    void coachTests(); coachTests();
    std::printf("%d checks, %d failures\n", t_run, t_fail);
    return t_fail != 0;
}
