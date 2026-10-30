#include "../src/online.h"
#include "t.h"

// trimmed from a real /api/v2/beatmapsets/search answer
static const char* kSearch = R"({"beatmapsets":[
 {"artist":"Zekk","title":"Vespera","creator":"Takecolite","genre_id":2,"id":2585933,"play_count":123456,"status":"ranked","bpm":150,
  "beatmaps":[{"id":1,"mode":"osu","difficulty_rating":2.5,"version":"Normal","total_length":150,"ar":5,"accuracy":4,"bpm":150},
              {"id":2,"mode":"osu","difficulty_rating":4.2,"version":"Hard","total_length":150,"ar":8,"accuracy":7,"bpm":150},
              {"id":3,"mode":"osu","difficulty_rating":4.4,"version":"Hard+","total_length":150,"ar":8.5,"accuracy":7.5,"bpm":150},
              {"id":4,"mode":"mania","difficulty_rating":4.3,"version":"4K","total_length":150,"ar":8,"accuracy":8,"bpm":150}]},
 {"artist":"Anime","title":"Op","creator":"x","genre_id":3,"id":7,"play_count":5,"status":"loved",
  "beatmaps":[{"id":70,"mode":"osu","difficulty_rating":4.1,"version":"I","total_length":90,"ar":8,"accuracy":7,"bpm":180}]},
 {"artist":"Fujikawa Chiai","title":"Yuzurenai ゆず 🎵 \"q\"","creator":"Kibitz","genre_id":5,"id":9,"play_count":1,"status":"graveyard","bpm":90,
  "beatmaps":[{"id":90,"mode":"osu","difficulty_rating":4.0,"version":"Long","total_length":200,"ar":7,"accuracy":6,"bpm":0}]},
 {"artist":"none","title":"in range","creator":"y","id":10,"beatmaps":[{"id":100,"mode":"osu","difficulty_rating":6.1}]}
],"cursor_string":"eyJhIjoxfQ","total":4})";

static void test_parse_search() {
    OnlineQuery q;
    q.lo = 4, q.hi = 4.5f, q.noAnime = true;
    OnlinePage p = parseOnlineSearch(kSearch, q);
    CHECK(p.error.empty());
    CHECK(p.cursor == "eyJhIjoxfQ");
    CHECK(p.maps.size() == 2);  // anime skipped, the 6.1* set has nothing in range
    if (p.maps.size() == 2) {
        CHECK(p.maps[0].sid == 2585933 && p.maps[0].bid == 3);  // hardest osu! difficulty in range, mania ignored
        CHECK(p.maps[0].version == "Hard+" && p.maps[0].creator == "Takecolite" && p.maps[0].status == "ranked");
        NEAR(p.maps[0].ar, 8.5, 1e-6);
        NEAR(p.maps[0].od, 7.5, 1e-6);
        CHECK(p.maps[0].lengthS == 150 && p.maps[0].plays == 123456);
        CHECK(p.maps[1].title == "Yuzurenai \xE3\x82\x86\xE3\x81\x9A \xF0\x9F\x8E\xB5 \"q\"");  // \u escapes and a surrogate pair
        NEAR(p.maps[1].bpm, 90, 1e-6);  // the difficulty's own BPM is 0: the set's
    }
    q.noAnime = false, q.shortOnly = true;
    p = parseOnlineSearch(kSearch, q);
    CHECK(p.maps.size() == 1 && p.maps[0].sid == 7);  // only the 90 s map is short enough
    CHECK(!parseOnlineSearch("{\"beatmapsets\":[", q).error.empty());
    CHECK(!parseOnlineSearch("<html>busy</html>", q).error.empty());
}

static void test_search_path() {
    OnlineQuery q;
    q.lo = 3.5f, q.hi = 4.25f, q.status = 1;
    CHECK(onlineSearchPath(q, "") == "/api/v2/beatmapsets/search?m=0&q=stars%3E%3D3.50%20stars%3C%3D4.25&s=leaderboard&sort=plays_desc");
    q.text = "zekk & co", q.status = 2, q.shortOnly = true;
    CHECK(onlineSearchPath(q, "a+b=") ==
          "/api/v2/beatmapsets/search?m=0&q=zekk%20%26%20co%20stars%3E%3D3.50%20stars%3C%3D4.25%20length%3C%3D120&s=ranked&cursor_string=a%2Bb%3D");
}

void onlineTests() {
    test_parse_search();
    test_search_path();
}
