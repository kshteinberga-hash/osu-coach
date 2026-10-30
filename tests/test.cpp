#include "t.h"
#include "../src/scan.h"
#include <cstdlib>
#include <cstring>
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

static void test_osu_old_timing() {
    // v5: no uninherited column, the inherited point is marked only by its negative beat length
    Beatmap b;
    CHECK(parseOsu("osu file format v5\n[Difficulty]\nSliderMultiplier:1.4\n[TimingPoints]\n0,500,4,2,0,50\n0,-50,4,2,0,50\n"
                   "[HitObjects]\n100,100,2000,2,0,L|200:100,2,140\n", b));
    NEAR(b.bpm, 120, 0.01);
    CHECK(b.objs.size() == 1 && b.objs[0].end == 2500);
}

static void test_osu_bom_fullwidth() {
    // the BOM goes, a value starting with a full-width letter (EF BC A5) stays intact
    Beatmap b;
    CHECK(parseOsu("\xEF\xBB\xBFosu file format v14\n[Metadata]\nTitle:S\nArtist:A\nVersion:\xEF\xBC\xA5xtra\n", b));
    CHECK(b.title == "A - S [\xEF\xBC\xA5xtra]");
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
    CHECK(parseOsu("[Difficulty]\nOverallDifficulty: +7.5\nCircleSize:4e0\nHPDrainRate:x\n", b));
    NEAR(b.od, 7.5, 0); NEAR(b.cs, 4, 0); NEAR(b.hp, 0, 0);
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

static const unsigned char kFramesLzma[] = {  // LZMA-alone stream (python lzma.FORMAT_ALONE), size field = -1
    0x5d, 0x00, 0x00, 0x80, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x18, 0x1f, 0x02, 0x43, 0x51, 0x03,
    0xb4, 0x00, 0x55, 0x57, 0xd8, 0x53, 0xab, 0x04, 0x8d, 0x68, 0x02, 0x8a, 0x8f, 0xcc, 0xce, 0x58, 0x49, 0x4d, 0xb8, 0x23,
    0x54, 0x6f, 0xee, 0xb2, 0xd4, 0x53, 0x30, 0x5a, 0x04, 0x42, 0xa6, 0xce, 0x4f, 0x48, 0x5d, 0x2f, 0x2f, 0x6e, 0x20, 0xc3,
    0x68, 0x34, 0xcb, 0x9c, 0xbe, 0xaa, 0x33, 0x34, 0x1f, 0xed, 0xbd, 0x0d, 0xe4, 0x96, 0x70, 0x1f, 0xf2, 0x3a, 0x23, 0xe5,
    0xe5, 0xc8, 0x02, 0xd8, 0x4b, 0xc0, 0x3f, 0x05, 0xd3, 0x43, 0x66, 0xef, 0x3a, 0x33, 0x74, 0x16, 0xe1, 0x08, 0xb6, 0xc4,
    0x21, 0xe8, 0x7a, 0xd9, 0xad, 0x1e, 0xa2, 0x3e, 0xb9, 0x9a, 0x0c, 0xfb, 0xb2, 0x82, 0xd5, 0xe5, 0x23, 0xcb, 0x9c, 0x5e,
    0x19, 0xa8, 0x2c, 0xd5, 0x64, 0xcf, 0x89, 0x63, 0xd6, 0xf5, 0x73, 0x28, 0x9c, 0xbf, 0xfa, 0x49, 0x54, 0xce, 0x7b, 0x53,
    0xaf, 0xcd, 0x35, 0xa7, 0x37, 0x48, 0xc0, 0x4e, 0xcc, 0xc9, 0xe3, 0xc1, 0x20, 0xeb, 0x41, 0xb0, 0xb1, 0x9f, 0x7c, 0xbc,
    0x0f, 0xff, 0xc1, 0x4e, 0xde, 0xc3, 0x97, 0xe0, 0x8a, 0x90, 0xd4, 0xae, 0x9e, 0x9c, 0x90, 0x14, 0xff, 0xb5, 0x03, 0x59,
    0x69, 0xa1, 0x11, 0x3a, 0x8f, 0x5b, 0x60, 0xe7, 0x02, 0x6c, 0xdd, 0x3d, 0x35, 0x50, 0x26, 0x94, 0xb9, 0xf8, 0x90, 0x33,
    0x6d, 0x62, 0xab, 0x90, 0xf8, 0x4d, 0x0b, 0x39, 0x22, 0xc7, 0xde, 0xcc, 0x87, 0xf3, 0x64, 0x92, 0xff, 0xb0, 0x48, 0x2a,
    0x28, 0xb0, 0x09, 0x65, 0x5e, 0x2d, 0x1b, 0x7b, 0xbf, 0x32, 0x7b, 0x31, 0x0e, 0x01, 0x2a, 0x02, 0xf6, 0x12, 0xb6, 0x29,
    0xbe, 0x9a, 0x8f, 0x7b, 0x6b, 0xe9, 0xa2, 0xf7, 0x09, 0x42, 0xe5, 0x49, 0x7a, 0xfe, 0x8b, 0x6b, 0xcc, 0x13, 0xc3, 0x40,
    0x9d, 0xbb, 0x88, 0x5d, 0xc5, 0x80, 0x8f, 0xf2, 0xa0, 0x59, 0x54, 0x60, 0xdc, 0xa1, 0xac, 0x0b, 0x74, 0x04, 0x72, 0x7f,
    0xff, 0x45, 0x09, 0xa7, 0x00};

static void test_osr_frames() {
    // "0|256|-500|0,-1|256|-500|0," then 400 frames 16 ms apart alternating keys 0/5, then the seed frame
    for (bool knownSize : {false, true}) {
        std::string z((const char*)kFramesLzma, sizeof kFramesLzma);
        if (knownSize) { int64_t n = 5240; memcpy(z.data() + 5, &n, 8); }  // the size field osu! writes
        std::string b;
        put<uint8_t>(b, 0); put<int>(b, 20240101); uleb(b, "0123456789abcdef0123456789abcdef"); uleb(b, "p"); uleb(b, "");
        for (int i = 0; i < 6; i++) put<uint16_t>(b, 1);
        put<int>(b, 0); put<uint16_t>(b, 0); put<uint8_t>(b, 0); put<int>(b, 0); uleb(b, ""); put<int64_t>(b, 0);
        put<int>(b, (int)z.size()); b += z;
        Replay r;
        CHECK(parseOsr(b, r, true));
        CHECK(r.frames.size() == 402);
        if (r.frames.size() != 402) continue;
        CHECK(r.frames[3].keys == 5 && r.frames[4].keys == 0 && r.frames.back().t == 6399);
        NEAR(r.frames[2].x, 100, 0);
    }
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
    db.clear();  // a garbage beatmap count must fail the read, not throw bad_alloc
    put<int>(db, 20260924); put<int>(db, 0); put<uint8_t>(db, 1); put<int64_t>(db, 0); uleb(db, "x"); put<int>(db, 0x7fffffff);
    { std::ofstream f(dir / L"osu!.db", std::ios::binary); f << db; }
    std::vector<DbMap> maps;
    CHECK(!readOsuDb(dir.wstring(), maps));
    std::filesystem::remove_all(dir);
}

static void test_dump_escapes() {
    ScanResult r;
    r.plays.push_back(PlayStats{});
    r.plays[0].file = L"a\"b\\c.osr", r.plays[0].md5 = "m";
    auto p = std::filesystem::temp_directory_path() / L"osu-coach-dump.jsonl";
    dumpJsonl(r, p.wstring());
    std::ifstream f(p);
    std::string line;
    std::getline(f, line);
    CHECK(line.starts_with("{\"file\":\"a\\\"b\\\\c.osr\",\"md5\":\"m\","));
    f.close();
    std::filesystem::remove(p);
}

int main() {
    test_deleted_map();
    test_osu_basic();
    test_osu_old_timing();
    test_osu_bom_fullwidth();
    test_osu_degenerate();
    test_osu_ar_default();
    test_osr_header();
    test_osr_frames();
    test_osr_long_lifebar();
    test_real_files();
    test_unicode_path();
    test_dump_escapes();
    void analyzeTests(); analyzeTests();
    void curveTests(); curveTests();
    void audioTests(); audioTests();
    void viewerTests(); viewerTests();
    void coachTests(); coachTests();
    std::printf("%d checks, %d failures\n", t_run, t_fail);
    return t_fail != 0;
}
