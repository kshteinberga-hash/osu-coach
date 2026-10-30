#include "online.h"
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <cstdio>
#include <mutex>
#include "parse.h"

namespace {
// ---- a small JSON reader: enough for the API's answers ----
struct Json {
    enum Type { NUL, BOOL, NUM, STR, ARR, OBJ } type = NUL;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::vector<std::pair<std::string, Json>> o;
    const Json& operator[](std::string_view k) const {
        static const Json none;
        for (auto& [key, v] : o) if (key == k) return v;
        return none;
    }
    double num(std::string_view k) const { auto& v = (*this)[k]; return v.type == NUM ? v.n : 0; }
    std::string str(std::string_view k) const { auto& v = (*this)[k]; return v.type == STR ? v.s : std::string(); }
};

struct Reader {
    std::string_view t;
    size_t i = 0;
    int depth = 0;
    void ws() { while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\r' || t[i] == '\n')) i++; }
    bool lit(std::string_view w) { if (t.substr(i, w.size()) != w) return false; i += w.size(); return true; }
    static void utf8(std::string& out, unsigned c) {
        if (c < 0x80) out += (char)c;
        else if (c < 0x800) out += (char)(0xC0 | c >> 6), out += (char)(0x80 | (c & 63));
        else if (c < 0x10000) out += (char)(0xE0 | c >> 12), out += (char)(0x80 | (c >> 6 & 63)), out += (char)(0x80 | (c & 63));
        else out += (char)(0xF0 | c >> 18), out += (char)(0x80 | (c >> 12 & 63)), out += (char)(0x80 | (c >> 6 & 63)), out += (char)(0x80 | (c & 63));
    }
    bool hex4(unsigned& c) {
        if (i + 4 > t.size()) return false;
        c = 0;
        for (int k = 0; k < 4; k++) {
            char h = t[i++];
            int d = h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1;
            if (d < 0) return false;
            c = c * 16 + d;
        }
        return true;
    }
    bool string(std::string& out) {
        if (t[i++] != '"') return false;
        while (i < t.size() && t[i] != '"') {
            if (t[i] != '\\') { out += t[i++]; continue; }
            if (++i >= t.size()) return false;
            char e = t[i++];
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    unsigned c;
                    if (!hex4(c)) return false;
                    if (c >= 0xD800 && c < 0xDC00 && lit("\\u")) {  // surrogate pair
                        unsigned lo;
                        if (!hex4(lo)) return false;
                        c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    utf8(out, c);
                    break;
                }
                default: out += e;  // \" \\ \/
            }
        }
        return i++ < t.size();
    }
    bool value(Json& v) {
        if (++depth > 64) return false;
        ws();
        if (i >= t.size()) return false;
        char c = t[i];
        bool ok = true;
        if (c == '{') {
            v.type = Json::OBJ, i++, ws();
            if (i < t.size() && t[i] == '}') i++;
            else
                for (;;) {
                    ws();
                    std::pair<std::string, Json> kv;
                    if (i >= t.size() || t[i] != '"' || !string(kv.first)) return false;
                    ws();
                    if (i >= t.size() || t[i++] != ':' || !value(kv.second)) return false;
                    v.o.push_back(std::move(kv));
                    ws();
                    if (i < t.size() && t[i] == ',') { i++; continue; }
                    if (i < t.size() && t[i] == '}') { i++; break; }
                    return false;
                }
        } else if (c == '[') {
            v.type = Json::ARR, i++, ws();
            if (i < t.size() && t[i] == ']') i++;
            else
                for (;;) {
                    v.a.emplace_back();
                    if (!value(v.a.back())) return false;
                    ws();
                    if (i < t.size() && t[i] == ',') { i++; continue; }
                    if (i < t.size() && t[i] == ']') { i++; break; }
                    return false;
                }
        } else if (c == '"') v.type = Json::STR, ok = string(v.s);
        else if (lit("true")) v.type = Json::BOOL, v.n = 1;
        else if (lit("false")) v.type = Json::BOOL;
        else if (lit("null")) v.type = Json::NUL;
        else {
            char* end;
            std::string num(t.substr(i, std::min<size_t>(32, t.size() - i)));
            v.type = Json::NUM, v.n = strtod(num.c_str(), &end);
            if (end == num.c_str()) return false;
            i += end - num.c_str();
        }
        depth--;
        return ok;
    }
};

bool parseJson(std::string_view text, Json& out) {
    Reader r{text};
    if (!r.value(out)) return false;
    r.ws();
    return r.i == text.size();
}

std::string urlEncode(std::string_view s) {
    std::string out;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
        else { char b[4]; snprintf(b, sizeof b, "%%%02X", c); out += b; }
    }
    return out;
}

// One HTTPS request to osu.ppy.sh. false (with err) when the server could not be reached at all.
bool request(const wchar_t* verb, const std::string& path, const std::wstring& headers, const std::string& body, int& status,
             std::string& out, std::string& err) {
    status = 0, out.clear();
    HINTERNET ses = WinHttpOpen(L"osu-coach (github.com/kshteinberga-hash/osu-coach)", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET con = ses ? WinHttpConnect(ses, L"osu.ppy.sh", INTERNET_DEFAULT_HTTPS_PORT, 0) : nullptr;
    HINTERNET req = con ? WinHttpOpenRequest(con, verb, widen(path).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             WINHTTP_FLAG_SECURE) : nullptr;
    bool ok = false;
    if (req) {
        WinHttpSetTimeouts(req, 10000, 10000, 15000, 20000);
        if (WinHttpSendRequest(req, headers.c_str(), (DWORD)-1L, (void*)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0) &&
            WinHttpReceiveResponse(req, nullptr)) {
            DWORD code = 0, len = sizeof code;
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &len,
                                WINHTTP_NO_HEADER_INDEX);
            status = (int)code, ok = true;
            for (DWORD n = 0; WinHttpQueryDataAvailable(req, &n) && n;) {
                size_t at = out.size();
                out.resize(at + n);
                DWORD got = 0;
                if (!WinHttpReadData(req, out.data() + at, n, &got)) { ok = false; break; }
                out.resize(at + got);
                if (out.size() > (32u << 20)) { ok = false; break; }  // nothing the API sends is this big
            }
        }
    }
    if (!ok) err = "Couldn't reach osu.ppy.sh. Check your internet connection and try again.";
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    if (ses) WinHttpCloseHandle(ses);
    return ok;
}

// The app's access token, shared by all searches; it lasts a day, so it is fetched again when it is close to running out.
std::mutex gTokenMu;
std::string gToken, gTokenFor;
ULONGLONG gTokenUntil = 0;

bool token(const ApiKeys& k, bool fresh, std::string& tok, std::string& err) {
    std::lock_guard g(gTokenMu);
    std::string who = k.id + '\n' + k.secret;
    if (!fresh && gTokenFor == who && GetTickCount64() < gTokenUntil) return tok = gToken, true;
    std::string body = "client_id=" + urlEncode(k.id) + "&client_secret=" + urlEncode(k.secret) + "&grant_type=client_credentials&scope=public",
                out;
    int status;
    if (!request(L"POST", "/oauth/token", L"Content-Type: application/x-www-form-urlencoded\r\nAccept: application/json\r\n", body, status, out, err))
        return false;
    Json j;
    if (status == 400 || status == 401) {
        err = "osu! didn't accept the Client ID or secret. Check them with \"Change API keys\".";
        return false;
    }
    if (status != 200 || !parseJson(out, j) || j.str("access_token").empty()) {
        err = "osu.ppy.sh had a problem signing in (error " + std::to_string(status) + "). Try again in a minute.";
        return false;
    }
    gToken = j.str("access_token"), gTokenFor = who;
    gTokenUntil = GetTickCount64() + (ULONGLONG)std::max(60.0, j.num("expires_in") - 300) * 1000;
    tok = gToken;
    return true;
}
}  // namespace

static std::wstring keysPath() { return exeDir() + L"\\osu-api.ini"; }

bool loadApiKeys(ApiKeys& k) {
    k = {};
    FILE* f = _wfopen(keysPath().c_str(), L"rb");
    if (!f) return false;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        std::string s = line;
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
        if (!s.compare(0, 10, "client_id=")) k.id = s.substr(10);
        if (!s.compare(0, 14, "client_secret=")) k.secret = s.substr(14);
    }
    fclose(f);
    return !k.id.empty() && !k.secret.empty();
}

bool saveApiKeys(const ApiKeys& k) {
    FILE* f = _wfopen(keysPath().c_str(), L"wb");
    if (!f) return false;
    fprintf(f, "client_id=%s\nclient_secret=%s\n", k.id.c_str(), k.secret.c_str());
    return fclose(f) == 0;
}

std::string onlineSearchPath(const OnlineQuery& q, const std::string& cursor) {
    char filt[96];
    snprintf(filt, sizeof filt, "stars>=%.2f stars<=%.2f%s", q.lo, q.hi, q.shortOnly ? " length<=120" : "");
    std::string text = q.text.empty() ? filt : q.text + " " + filt;
    static const char* const kStatus[] = {"any", "leaderboard", "ranked"};
    std::string p = "/api/v2/beatmapsets/search?m=0&q=" + urlEncode(text) + "&s=" + kStatus[std::clamp(q.status, 0, 2)];
    if (q.text.empty()) p += "&sort=plays_desc";  // nothing typed: popular maps first, not just the newest
    if (!cursor.empty()) p += "&cursor_string=" + urlEncode(cursor);
    return p;
}

OnlinePage parseOnlineSearch(std::string_view json, const OnlineQuery& q) {
    OnlinePage page;
    Json j;
    if (!parseJson(json, j) || j["beatmapsets"].type != Json::ARR) {
        page.error = "osu.ppy.sh sent an answer this version doesn't understand.";
        return page;
    }
    page.cursor = j.str("cursor_string");
    for (auto& set : j["beatmapsets"].a) {
        if (q.noAnime && set.num("genre_id") == 3) continue;  // osu!'s genre 3 is Anime
        const Json* best = nullptr;
        for (auto& b : set["beatmaps"].a) {
            float st = (float)b.num("difficulty_rating");
            if (b.str("mode") != "osu" || st < q.lo || st > q.hi || (q.shortOnly && b.num("total_length") > 120)) continue;
            if (!best || st > best->num("difficulty_rating")) best = &b;
        }
        if (!best) continue;
        OnlineMap m;
        m.sid = (int)set.num("id"), m.bid = (int)best->num("id"), m.lengthS = (int)best->num("total_length");
        m.plays = (int)set.num("play_count");
        m.stars = (float)best->num("difficulty_rating"), m.ar = (float)best->num("ar"), m.od = (float)best->num("accuracy");
        m.bpm = (float)(best->num("bpm") > 0 ? best->num("bpm") : set.num("bpm"));
        m.artist = set.str("artist"), m.title = set.str("title"), m.version = best->str("version"), m.creator = set.str("creator");
        m.status = set.str("status");
        page.maps.push_back(std::move(m));
    }
    return page;
}

OnlinePage searchOnline(const ApiKeys& k, const OnlineQuery& q, const std::string& cursor) {
    OnlinePage page;
    std::string path = onlineSearchPath(q, cursor), out;
    for (int attempt = 0; attempt < 2; attempt++) {
        std::string tok;
        if (!token(k, attempt > 0, tok, page.error)) return page;
        int status;
        if (!request(L"GET", path, L"Accept: application/json\r\nAuthorization: Bearer " + widen(tok) + L"\r\n", "", status, out, page.error))
            return page;
        if (status == 401) continue;  // the token ran out early: get a new one once
        if (status == 429) { page.error = "osu.ppy.sh says too many searches. Wait a minute and try again."; return page; }
        if (status != 200) { page.error = "osu.ppy.sh had a problem with the search (error " + std::to_string(status) + "). Try again in a minute."; return page; }
        return parseOnlineSearch(out, q);
    }
    page.error = "osu! didn't accept the Client ID or secret. Check them with \"Change API keys\".";
    return page;
}
