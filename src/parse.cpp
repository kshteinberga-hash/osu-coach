#include "parse.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

static std::string_view skipBom(std::string_view s) { return s.starts_with("\xEF\xBB\xBF") ? s.substr(3) : s; }

static double num(std::string_view s) {  // 0 when s is not a number, like strtod
    s = trim(s);
    if (s.starts_with('+')) s.remove_prefix(1);
    double v = 0;
    std::from_chars(s.data(), s.data() + s.size(), v);
    return v;
}

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
    text = skipBom(text);
    while (!text.empty()) {
        size_t p = text.find('\n');
        std::string_view line = trim(text.substr(0, p));
        text.remove_prefix(p == std::string_view::npos ? text.size() : p + 1);
        if (line.empty() || line.starts_with("//")) continue;
        if (line.front() == '[') { sec = line; continue; }
        if (sec == "[TimingPoints]") {
            std::string_view f[8];
            int n = split(line, ',', f, 8);
            double len = n >= 2 ? num(f[1]) : 0;  // before v6 there is no uninherited column: negative length = inherited
            if (n >= 2) tps.push_back({num(f[0]), len, n >= 7 ? trim(f[6]) == "1" : len > 0});
        } else if (sec == "[HitObjects]") {
            std::string_view f[8];
            int n = split(line, ',', f, 8);
            if (n < 4) continue;
            int type = (int)num(f[3]);
            HitObj o{(float)num(f[0]), (float)num(f[1]), (int)num(f[2]), (int)num(f[2]), 0, (type & 4) != 0};
            if (type & 2) {
                o.kind = 1;
                for (std::string_view c = n >= 6 ? f[5] : std::string_view{}; !c.empty();) {  // "B|x:y|x:y"
                    size_t q = c.find('|');
                    std::string_view tok = c.substr(0, q);
                    c.remove_prefix(q == std::string_view::npos ? c.size() : q + 1);
                    if (size_t k = tok.find(':'); k != std::string_view::npos) o.pts.push_back({(float)num(tok.substr(0, k)), (float)num(tok.substr(k + 1))});
                    else if (tok.size() == 1) o.curve = tok[0];
                }
                o.slides = n >= 7 ? std::max(1, (int)num(f[6])) : 1, o.length = n >= 8 ? (float)num(f[7]) : 0;
                sliders.push_back({b.objs.size(), n >= 8 ? num(f[7]) : 0, n >= 7 ? num(f[6]) : 1});
            }
            else if (type & 8) o.kind = 2, o.end = n >= 6 ? (int)num(f[5]) : o.t;
            else if (!(type & 1)) continue;
            b.objs.push_back(o);
        } else {
            size_t c = line.find(':');
            if (c == std::string_view::npos) continue;
            std::string_view k = trim(line.substr(0, c)), v = trim(line.substr(c + 1));
            if (k == "Mode" && num(v) != 0) return false;
            else if (k == "AudioFilename") b.audioFile = std::string(v);
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
    std::stable_sort(tps.begin(), tps.end(), [](const TP& a, const TP& c) { return a.t < c.t || (a.t == c.t && a.unin > c.unin); });  // tempo first: it resets SV
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

// ---- slider curves ----
namespace {
using Pt = std::pair<float, float>;
Pt lerp(Pt a, Pt b, float t) { return {a.first + (b.first - a.first) * t, a.second + (b.second - a.second) * t}; }
float dist(Pt a, Pt b) { return std::hypot(b.first - a.first, b.second - a.second); }
int stepsFor(const std::vector<Pt>& c) {  // ~4 px per step along the control polygon
    float l = 0;
    for (size_t i = 1; i < c.size(); i++) l += dist(c[i - 1], c[i]);
    return std::clamp((int)(l / 4), 8, 256);
}
void bezier(const std::vector<Pt>& c, std::vector<Pt>& out) {  // de Casteljau
    if (c.size() < 3) { out.insert(out.end(), c.begin(), c.end()); return; }
    int steps = stepsFor(c);
    std::vector<Pt> w(c.size());
    for (int s = 0; s <= steps; s++) {
        w = c;
        for (size_t k = c.size() - 1; k > 0; k--)
            for (size_t i = 0; i < k; i++) w[i] = lerp(w[i], w[i + 1], (float)s / steps);
        out.push_back(w[0]);
    }
}
void catmull(const std::vector<Pt>& c, std::vector<Pt>& out) {
    for (size_t i = 0; i + 1 < c.size(); i++) {
        Pt p0 = c[i ? i - 1 : 0], p1 = c[i], p2 = c[i + 1], p3 = c[std::min(i + 2, c.size() - 1)];
        int steps = std::clamp((int)(dist(p1, p2) / 4), 4, 64);
        for (int s = i ? 1 : 0; s <= steps; s++) {
            float t = (float)s / steps, t2 = t * t, t3 = t2 * t;
            auto f = [&](float a, float b, float c2, float d) { return 0.5f * (2 * b + (-a + c2) * t + (2 * a - 5 * b + 4 * c2 - d) * t2 + (-a + 3 * b - 3 * c2 + d) * t3); };
            out.push_back({f(p0.first, p1.first, p2.first, p3.first), f(p0.second, p1.second, p2.second, p3.second)});
        }
    }
}
bool arc(const std::vector<Pt>& c, std::vector<Pt>& out) {  // circle through 3 points; false if collinear
    if (c.size() != 3) return false;
    auto [ax, ay] = c[0]; auto [bx, by] = c[1]; auto [cx, cy] = c[2];
    float d = 2 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
    if (std::fabs(d) < 1e-3f) return false;
    float a2 = ax * ax + ay * ay, b2 = bx * bx + by * by, c2 = cx * cx + cy * cy;
    float ux = (a2 * (by - cy) + b2 * (cy - ay) + c2 * (ay - by)) / d, uy = (a2 * (cx - bx) + b2 * (ax - cx) + c2 * (bx - ax)) / d;
    float r = std::hypot(ax - ux, ay - uy), ta = std::atan2(ay - uy, ax - ux), tb = std::atan2(by - uy, bx - ux), tc = std::atan2(cy - uy, cx - ux);
    const float TAU = 6.2831853f;
    auto ahead = [&](float from, float to, float dir) { float x = (to - from) * dir; while (x < 0) x += TAU; while (x >= TAU) x -= TAU; return x; };
    float dir = ahead(ta, tb, 1) < ahead(ta, tc, 1) ? 1.f : -1.f;  // go the way that passes the middle point
    float sweep = ahead(ta, tc, dir);
    int steps = std::clamp((int)(r * sweep / 4), 8, 256);
    for (int s = 0; s <= steps; s++) {
        float t = ta + dir * sweep * s / steps;
        out.push_back({ux + r * std::cos(t), uy + r * std::sin(t)});
    }
    return true;
}
}

std::vector<std::pair<float, float>> sliderPath(const HitObj& o) {
    std::vector<Pt> c{{o.x, o.y}}, raw;
    c.insert(c.end(), o.pts.begin(), o.pts.end());
    if (o.curve == 'L') raw = c;
    else if (o.curve == 'C') catmull(c, raw);
    else if (o.curve != 'P' || !arc(c, raw)) {  // B, unknown letters and degenerate arcs: bezier, split at repeated (red) points
        std::vector<Pt> seg{c[0]};
        for (size_t i = 1; i < c.size(); i++) {
            if (c[i] == c[i - 1]) { bezier(seg, raw), seg = {c[i]}; continue; }
            seg.push_back(c[i]);
        }
        bezier(seg, raw);
    }
    // resample every 4 px along the raw path, cut or extended (along the last direction) to o.length
    std::vector<float> cum{0};
    for (size_t i = 1; i < raw.size(); i++) cum.push_back(cum.back() + dist(raw[i - 1], raw[i]));
    float total = cum.back(), len = o.length > 0 ? o.length : total;
    if (raw.size() < 2 || total <= 0) return {c[0]};
    size_t last = raw.size() - 1;
    while (last > 0 && dist(raw[last - 1], raw[last]) <= 0) last--;
    auto at = [&](float d) -> Pt {
        if (d >= total) {
            Pt a = raw[last ? last - 1 : 0], b = raw[last];
            float sl = std::max(dist(a, b), 1e-6f);
            return {b.first + (b.first - a.first) / sl * (d - total), b.second + (b.second - a.second) / sl * (d - total)};
        }
        size_t i = std::upper_bound(cum.begin(), cum.end(), d) - cum.begin();
        float sl = cum[i] - cum[i - 1];
        return lerp(raw[i - 1], raw[i], sl > 0 ? (d - cum[i - 1]) / sl : 0);
    };
    std::vector<Pt> out;
    for (float d = 0; d < len; d += 4) out.push_back(at(d));
    out.push_back(at(len));
    return out;
}

std::pair<float, float> sliderBall(const HitObj& o, const std::vector<std::pair<float, float>>& path, int t) {
    if (path.size() < 2) return path.empty() ? Pt{o.x, o.y} : path[0];
    float dur = (float)(o.end - o.t) / o.slides;
    if (dur <= 0) return path[0];
    float p = std::clamp((t - o.t) / dur, 0.f, (float)o.slides);
    int k = std::min((int)p, o.slides - 1);
    float f = p - k;
    if (k % 2) f = 1 - f;
    float idx = f * (path.size() - 1);
    size_t i = std::min((size_t)idx, path.size() - 2);
    return lerp(path[i], path[i + 1], idx - i);
}

bool loadOsu(const std::wstring& path, Beatmap& out) {
    std::string s;
    return readFile(path, s) && parseOsu(s, out);
}

// ---- binary readers (.osr, osu!.db) ----
#include <windows.h>
extern "C" {
#include "LzmaDec.h"
}

namespace {
struct Reader {
    const uint8_t *p, *e;
    bool ok = true;
    Reader(std::string_view s) : p((const uint8_t*)s.data()), e(p + s.size()) {}
    bool need(size_t n) { if ((size_t)(e - p) < n) ok = false; return ok; }
    template <class T> T get() { T v{}; if (need(sizeof v)) memcpy(&v, p, sizeof v), p += sizeof v; return v; }
    void skip(size_t n) { if (need(n)) p += n; }
    uint64_t uleb() { uint64_t r = 0; for (int s = 0; need(1) && s < 64; s += 7) { uint8_t c = *p++; r |= uint64_t(c & 0x7f) << s; if (!(c & 0x80)) break; } return r; }
    std::string str() {
        uint8_t m = get<uint8_t>();
        if (m != 0x0b) return {};
        size_t n = (size_t)uleb();
        if (!need(n)) return {};
        std::string s((const char*)p, n); p += n; return s;
    }
};
void* lzAlloc(ISzAllocPtr, size_t n) { return malloc(n); }
void lzFree(ISzAllocPtr, void* q) { free(q); }
const ISzAlloc kAlloc = {lzAlloc, lzFree};
}

std::wstring widen(std::string_view s) {
    std::wstring w(MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0), 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), (int)w.size());
    return w;
}

static bool decodeFrames(const uint8_t* src, size_t n, std::vector<Frame>& out) {
    if (n < 13) return false;
    uint64_t size;  // header: 5 bytes of properties, then the decoded size (all ones when unknown)
    memcpy(&size, src + 5, 8);
    bool known = size > 0 && size < (256u << 20);  // 0 would silently empty the replay: guess instead
    size_t cap = known ? (size_t)size : n * 10 + 4096;
    std::string txt;
    for (int tries = 0; tries < 6; tries++, cap *= 2) {  // a guessed size may be too small: retry larger
        txt.resize(cap);
        SizeT dl = cap, sl = n - 13;
        ELzmaStatus st;
        SRes r = LzmaDecode((Byte*)txt.data(), &dl, src + 13, &sl, src, 5, LZMA_FINISH_ANY, &st, &kAlloc);
        if (r != SZ_OK && r != SZ_ERROR_INPUT_EOF) return false;
        if (known || dl < cap || st == LZMA_STATUS_FINISHED_WITH_MARK) { txt.resize(dl); break; }
    }
    return parseFrames(txt, out);
}

bool parseFrames(std::string_view s, std::vector<Frame>& out) {
    out.clear();
    out.reserve(s.size() / 16);
    const char *q = s.data(), *e = q + s.size();
    int t = 0, n = 0;  // n counts frames read, the seed frame excluded
    while (q < e) {
        const char* end;
        long w = 0; end = std::from_chars(q, e, w).ptr; if (end == e || *end != '|') break; q = end + 1;
        float x = 0; end = std::from_chars(q, e, x).ptr; if (end == e || *end != '|') break; q = end + 1;
        float y = 0; end = std::from_chars(q, e, y).ptr; if (end == e || *end != '|') break; q = end + 1;
        long k = 0; q = std::from_chars(q, e, k).ptr;
        while (q < e && *q != ',') q++;
        if (q < e) q++;
        if (w == -12345) continue;
        t += w;  // skipped frames still move the clock, like osu!lazer's decoder
        bool placeholder = n++ < 2 && x == 256 && y == -500;
        if (placeholder || (!out.empty() && t < out.back().t)) continue;
        out.push_back({t, x, y, (int)k});
    }
    return true;
}

bool parseOsr(std::string_view bytes, Replay& r, bool withFrames) {
    Reader R(bytes);
    r.mode = R.get<uint8_t>(); r.ver = R.get<int>();
    r.md5 = R.str(); r.player = R.str(); R.str();
    r.n300 = R.get<uint16_t>(); r.n100 = R.get<uint16_t>(); r.n50 = R.get<uint16_t>();
    r.geki = R.get<uint16_t>(); r.katu = R.get<uint16_t>(); r.miss = R.get<uint16_t>();
    r.score = R.get<int>(); r.combo = R.get<uint16_t>(); R.get<uint8_t>(); r.mods = R.get<int>();
    R.str();
    r.when = (R.get<int64_t>() - 621355968000000000LL) / 10000;
    int len = R.get<int>();
    if (!R.ok) return false;
    r.frames.clear();
    if (!withFrames) return true;
    return len > 0 && R.need(len) && decodeFrames(R.p, len, r.frames);
}

bool readOsr(const std::wstring& path, Replay& r, bool withFrames) {
    std::string s;
    if (withFrames) return readFile(path, s) && parseOsr(s, r, true);
    FILE* f = _wfopen(path.c_str(), L"rb");  // header only: 1 KB is usually enough, unless the life bar string is long
    if (!f) return false;
    s.resize(1024);
    s.resize(fread(s.data(), 1, s.size(), f));
    fclose(f);
    return parseOsr(s, r, false) || (s.size() == 1024 && readFile(path, s) && parseOsr(s, r, false));
}

float starsFor(const DbMap& m, int mods) {
    int key = mods & (mod::EZ | mod::HR | mod::DT | mod::HT);
    float nm = -1;
    for (auto& [k, v] : m.stars) { if (k == key) return v; if (k == 0) nm = v; }
    return nm;
}

bool readOsuDb(const std::wstring& dir, std::vector<DbMap>& out) {
    std::string s;
    if (!readFile(dir + L"\\osu!.db", s)) return false;
    Reader R(s);
    int ver = R.get<int>();
    R.get<int>(); R.get<uint8_t>(); R.get<int64_t>(); R.str();
    int n = R.get<int>();
    if (ver < 20191106 || n < 0) return false;
    out.clear();
    out.reserve(std::min<size_t>(n, s.size() / 64));  // every entry takes well over 64 bytes; n may be garbage
    for (int i = 0; i < n && R.ok; i++) {
        DbMap m{};
        std::string artist = R.str(); R.str();
        std::string title = R.str(); R.str();
        m.creator = R.str();
        std::string diff = R.str(); R.str();
        m.md5 = R.str();
        std::string file = R.str();
        m.status = R.get<uint8_t>();
        m.nCircle = R.get<uint16_t>(); m.nSlider = R.get<uint16_t>(); m.nSpinner = R.get<uint16_t>();
        R.get<int64_t>();
        m.ar = R.get<float>(); m.cs = R.get<float>(); m.hp = R.get<float>(); m.od = R.get<float>();
        R.get<double>();
        for (int list = 0; list < 4; list++) {
            int k = R.get<int>();
            for (int j = 0; j < k && R.ok; j++) {
                R.get<uint8_t>(); int mods = R.get<int>(); R.get<uint8_t>();
                float v = ver >= 20250107 ? R.get<float>() : (float)R.get<double>();
                if (list == 0) m.stars.push_back({mods, v});
            }
        }
        m.drainS = R.get<int>(); m.totalMs = R.get<int>(); R.get<int>();
        R.skip((size_t)R.get<int>() * 17);
        m.bid = R.get<int>(); m.sid = R.get<int>(); R.get<int>();
        R.skip(4); R.get<uint16_t>(); R.get<float>();
        uint8_t mode = R.get<uint8_t>();
        m.source = R.str(); m.tags = R.str();
        R.get<uint16_t>(); R.str(); R.get<uint8_t>(); R.get<int64_t>(); R.get<uint8_t>();
        std::string folder = R.str();
        R.get<int64_t>(); R.skip(5); R.get<int>(); R.get<uint8_t>();
        if (!R.ok) break;
        if (mode != 0 || m.md5.empty()) continue;
        m.title = artist + " - " + title + " [" + diff + "]";
        m.path = dir + L"\\Songs\\" + widen(folder) + L"\\" + widen(file);
        out.push_back(std::move(m));
    }
    return R.ok;
}

std::wstring exeDir() {
    wchar_t b[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, b, MAX_PATH);
    std::wstring s(b, n);
    return s.substr(0, s.find_last_of(L'\\'));
}

static bool hasDb(const std::wstring& d) { return !d.empty() && GetFileAttributesW((d + L"\\osu!.db").c_str()) != INVALID_FILE_ATTRIBUTES; }

static std::wstring regDir(const wchar_t* key) {
    wchar_t b[1024];
    DWORD n = sizeof b;
    if (RegGetValueW(HKEY_CLASSES_ROOT, key, nullptr, RRF_RT_REG_SZ, nullptr, b, &n) != ERROR_SUCCESS) return {};
    std::wstring s = b;  // "C:\...\osu!.exe" "%1"
    size_t a = s.find(L'"'), z = s.find(L'"', a + 1);
    if (a == std::wstring::npos || z == std::wstring::npos) return {};
    s = s.substr(a + 1, z - a - 1);
    return s.substr(0, s.find_last_of(L'\\'));
}

std::wstring findOsuDir() {
    std::string ini;
    if (readFile(exeDir() + L"\\osu-coach.ini", ini)) {
        std::wstring d = widen(trim(skipBom(ini)));
        if (hasDb(d)) return d;
    }
    for (auto k : {L"osustable.File.osz\\shell\\open\\command", L"osu\\shell\\open\\command"})
        if (auto d = regDir(k); hasDb(d)) return d;
    wchar_t b[MAX_PATH];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", b, MAX_PATH) && hasDb(std::wstring(b) + L"\\osu!")) return std::wstring(b) + L"\\osu!";
    return {};
}

void saveOsuDir(const std::wstring& dir) {
    std::string u(WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), (int)dir.size(), nullptr, 0, nullptr, nullptr), 0);
    WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), (int)dir.size(), u.data(), (int)u.size(), nullptr, nullptr);
    if (FILE* f = _wfopen((exeDir() + L"\\osu-coach.ini").c_str(), L"wb")) fwrite(u.data(), 1, u.size(), f), fclose(f);
}
