#include "parse.h"
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>

static std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\xEF' || s.front() == '\xBB' || s.front() == '\xBF')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

static double num(std::string_view s) { return std::strtod(std::string(trim(s)).c_str(), nullptr); }

// Splits s on c into at most n fields, returns count.
static int split(std::string_view s, char c, std::string_view* f, int n) {
    int k = 0;
    while (k < n) {
        size_t p = s.find(c);
        f[k++] = s.substr(0, p);
        if (p == std::string_view::npos) break;
        s.remove_prefix(p + 1);
    }
    return k;
}

bool readFile(const std::wstring& path, std::string& out) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? n : 0);
    bool ok = n <= 0 || std::fread(out.data(), 1, n, f) == (size_t)n;
    std::fclose(f);
    return ok;
}

bool parseOsu(std::string_view text, Beatmap& b) {
    b = Beatmap{5, 5, 5, 5, 0, 0, {}, {}};
    struct TP { double t, len; bool unin; };
    std::vector<TP> tps;
    struct Sl { size_t i; double len, slides; };
    std::vector<Sl> sliders;
    std::string_view sec, artist, title, version;
    double sm = 1.4;
    bool haveAr = false;
    while (!text.empty()) {
        size_t p = text.find('\n');
        std::string_view line = trim(text.substr(0, p));
        text.remove_prefix(p == std::string_view::npos ? text.size() : p + 1);
        if (line.empty() || line.starts_with("//")) continue;
        if (line.front() == '[') { sec = line; continue; }
        if (sec == "[TimingPoints]") {
            std::string_view f[8];
            int n = split(line, ',', f, 8);
            if (n >= 2) tps.push_back({num(f[0]), num(f[1]), n < 7 || trim(f[6]) == "1"});
        } else if (sec == "[HitObjects]") {
            std::string_view f[8];
            int n = split(line, ',', f, 8);
            if (n < 4) continue;
            int type = (int)num(f[3]);
            HitObj o{(float)num(f[0]), (float)num(f[1]), (int)num(f[2]), (int)num(f[2]), 0, (type & 4) != 0};
            if (type & 2) o.kind = 1, sliders.push_back({b.objs.size(), n >= 8 ? num(f[7]) : 0, n >= 7 ? num(f[6]) : 1});
            else if (type & 8) o.kind = 2, o.end = n >= 6 ? (int)num(f[5]) : o.t;
            else if (!(type & 1)) continue;
            b.objs.push_back(o);
        } else {
            size_t c = line.find(':');
            if (c == std::string_view::npos) continue;
            std::string_view k = trim(line.substr(0, c)), v = trim(line.substr(c + 1));
            if (k == "Mode" && num(v) != 0) return false;
            else if (k == "Title") title = v;
            else if (k == "Artist") artist = v;
            else if (k == "Version") version = v;
            else if (k == "BeatmapID") b.bid = (int)num(v);
            else if (k == "HPDrainRate") b.hp = (float)num(v);
            else if (k == "CircleSize") b.cs = (float)num(v);
            else if (k == "OverallDifficulty") b.od = (float)num(v);
            else if (k == "ApproachRate") b.ar = (float)num(v), haveAr = true;
            else if (k == "SliderMultiplier") sm = num(v);
        }
    }
    if (!haveAr) b.ar = b.od;
    b.title = std::string(artist) + " - " + std::string(title) + " [" + std::string(version) + "]";
    std::stable_sort(tps.begin(), tps.end(), [](const TP& a, const TP& c) { return a.t < c.t; });
    // main BPM: uninherited point that lasts longest (until the next uninherited point or the last object)
    double lastT = b.objs.empty() ? 0 : b.objs.back().t, best = -1;
    std::vector<const TP*> un;
    for (auto& t : tps) if (t.unin && t.len > 0) un.push_back(&t);
    for (size_t i = 0; i < un.size(); i++) {
        double d = (i + 1 < un.size() ? un[i + 1]->t : std::max(lastT, un[i]->t)) - un[i]->t;
        if (d > best) best = d, b.bpm = (float)(60000 / un[i]->len);
    }
    // slider end times: beat length + slider velocity in effect at the slider's start
    for (auto& s : sliders) {
        HitObj& o = b.objs[s.i];
        double beat = 500, sv = 1;
        for (auto& t : tps) {
            if (t.t > o.t) break;
            if (t.unin) beat = t.len, sv = 1;
            else if (t.len < 0) sv = std::clamp(-100 / t.len, 0.1, 10.0);
        }
        o.end = o.t + (int)(s.len / (sm * 100 * sv) * beat * s.slides + 0.5);
    }
    return true;
}

bool loadOsu(const std::wstring& path, Beatmap& out) {
    std::string s;
    return readFile(path, s) && parseOsu(s, out);
}
