#pragma once
#include <string>
#include <string_view>
#include <vector>

// Searching osu.ppy.sh through the osu! API v2. osu! only searches for signed-in users, so this needs an OAuth app's
// Client ID and secret (the user makes one in their osu! account settings); they live in osu-api.ini next to the exe.
struct ApiKeys { std::string id, secret; };
bool loadApiKeys(ApiKeys& k);  // false when the file is missing or incomplete
bool saveApiKeys(const ApiKeys& k);

struct OnlineQuery {
    std::string text;         // passed to osu!'s search as typed
    float lo = 3.5f, hi = 4.5f;
    int status = 1;           // 0 any, 1 ranked/approved/qualified/loved, 2 ranked/approved
    bool noAnime = true, shortOnly = false;  // shortOnly: 2 minutes or less
};
struct OnlineMap {
    int sid = 0, bid = 0, lengthS = 0, plays = 0;
    float stars = 0, ar = 0, od = 0, bpm = 0;
    std::string artist, title, version, creator, status;
};
struct OnlinePage {
    std::vector<OnlineMap> maps;  // one per mapset: its hardest osu!standard difficulty in the star range
    std::string cursor;           // pass back for the next page; empty on the last one
    std::string error;            // plain-English reason when the search failed
};
std::string onlineSearchPath(const OnlineQuery& q, const std::string& cursor);  // /api/v2/beatmapsets/search?...
OnlinePage parseOnlineSearch(std::string_view json, const OnlineQuery& q);
OnlinePage searchOnline(const ApiKeys& k, const OnlineQuery& q, const std::string& cursor = "");  // blocking: call on a worker
