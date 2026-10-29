#include "t.h"
#include "../src/parse.h"

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

int main() {
    test_osu_basic();
    test_osu_degenerate();
    test_osu_ar_default();
    std::printf("%d checks, %d failures\n", t_run, t_fail);
    return t_fail != 0;
}
