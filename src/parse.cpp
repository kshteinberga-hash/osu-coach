#include "parse.h"
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>

static std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

static std::string_view skipBom(std::string_view s) { return s.starts_with("\xEF\xBB\xBF") ? s.substr(3) : s; }

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
    bool known = size < (256u << 20);
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
    out.clear();
    out.reserve(txt.size() / 16);
    const char* q = txt.c_str();
    int t = 0;
    while (*q) {
        char* end;
        long w = strtol(q, &end, 10); if (*end != '|') break; q = end + 1;
        float x = strtof(q, &end); if (*end != '|') break; q = end + 1;
        float y = strtof(q, &end); if (*end != '|') break; q = end + 1;
        long k = strtol(q, &end, 10); q = end;
        while (*q && *q != ',') q++;
        if (*q == ',') q++;
        if (w == -12345) continue;
        t += w;
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
    int key = mods & (2 | 16 | 64 | 256);
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
        std::string title = R.str(); R.str(); R.str();
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
        m.drainS = R.get<int>(); R.get<int>(); R.get<int>();
        R.skip((size_t)R.get<int>() * 17);
        m.bid = R.get<int>(); m.sid = R.get<int>(); R.get<int>();
        R.skip(4); R.get<uint16_t>(); R.get<float>();
        uint8_t mode = R.get<uint8_t>();
        R.str(); R.str(); R.get<uint16_t>(); R.str(); R.get<uint8_t>(); R.get<int64_t>(); R.get<uint8_t>();
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
