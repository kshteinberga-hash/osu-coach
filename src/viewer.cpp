#include "viewer.h"
#include <algorithm>
#include <cmath>
#include "coach.h"
#include "gui.h"

bool Viewer::open(const std::wstring& osuDir, const PlayStats& p, const std::vector<DbMap>& db) {
    play = p;
    auto m = std::find_if(db.begin(), db.end(), [&](const DbMap& d) { return d.md5 == p.md5; });
    if (m == db.end() || !readOsr(osuDir + L"\\Data\\r\\" + p.file, rep, true) || !loadOsu(m->path, map)) {
        error = "This replay or its map is no longer on your PC.";
        return false;
    }
    objs = analyze(rep, map, p.stars).objs;
    int c = 0, col = 0;
    for (size_t i = 0; i < map.objs.size(); i++) {
        auto& o = map.objs[i];
        if (i && o.nc) col++;
        c = o.nc || i == 0 ? 1 : c + 1;
        combo.push_back(c), colour.push_back(col);
        paths.push_back(o.kind == 1 ? sliderPath(o) : std::vector<std::pair<float, float>>{});
        objOf.push_back(o.kind != 2 && objMap.size() < objs.size() ? (int)objMap.size() : -1);
        if (o.kind != 2 && objMap.size() < objs.size()) objMap.push_back((int)i);
        endT = std::max(endT, (double)o.end);
    }
    std::array<int, 4> run{};
    for (auto& o : objs) {
        run[o.judge == 300 ? 0 : o.judge == 100 ? 1 : o.judge == 50 ? 2 : 3]++;
        judged.push_back(run);
        if (o.judge) hitErr.add(o.err);
    }
    float ar = map.ar, cs = map.cs;
    if (p.mods & mod::HR) ar = std::min(10.f, ar * 1.4f), cs = std::min(10.f, cs * 1.3f);
    if (p.mods & mod::EZ) ar *= 0.5f, cs *= 0.5f;
    preempt = ar < 5 ? 1800 - 120 * ar : 1200 - 150 * (ar - 5);
    radius = circleRadius(cs);
    int prev = 0, open[4] = {-1, -1, -1, -1};
    for (auto& f : rep.frames) {
        for (int k = 0; k < 2; k++)
            if ((f.keys & (k ? 10 : 5)) && !(prev & (k ? 10 : 5))) presses.push_back({f.t, f.x, f.y, k});
        int now = laneBits(f.keys), was = laneBits(prev);
        for (int l = 0; l < 4; l++) {
            if ((now >> l & 1) && !(was >> l & 1)) open[l] = (int)holds.size(), holds.push_back({f.t, f.t, l});
            if (!(now >> l & 1) && (was >> l & 1) && open[l] >= 0) holds[open[l]].t1 = f.t, open[l] = -1;
        }
        prev = f.keys;
    }
    for (int l = 0; l < 4; l++) if (open[l] >= 0 && !rep.frames.empty()) holds[open[l]].t1 = rep.frames.back().t;
    int used = 0;
    for (auto& h : holds) longestHold = std::max(longestHold, h.t1 - h.t0), used |= 1 << h.lane;
    for (int l = 0; l < 4; l++) if (used >> l & 1 || (!used && l < 2)) lanes.push_back(l);
    std::wstring folder = m->path.substr(0, m->path.find_last_of(L'\\') + 1);
    if (map.audioFile.empty() || !audio.load(folder + widen(map.audioFile))) audioNote = "Music file not found, playing silently.";
    clock.rate = p.rate;
    setSpeed(1);
    applyVolume();
    rebuildStops();
    seek(map.objs.empty() ? 0 : std::max(0, map.objs[0].t - 1500));
    return ok = true;
}

void Viewer::rebuildStops() {
    miss = mistakes(objs, stop50);
    stops.times.clear();
    for (int i : miss) stops.times.push_back(mapT(i));
    stops.seek(clock.t);
}

void Viewer::setPlaying(bool on) {
    if (on && clock.t >= endT + 1000) seek(0);
    clock.running = on;
    if (on) stoppedAt = -1, audio.seekMs(clock.t), audio.play();
    else audio.pause();
}

void Viewer::seek(double t) {
    clock.t = std::clamp(t, 0.0, endT + 2000);
    stops.seek(clock.t);
    audio.seekMs(clock.t);
}

void Viewer::jumpTo(int k) {
    if (k < 0 || k >= (int)miss.size()) return;
    setPlaying(false);
    seek(mapT(miss[k]));
    stoppedAt = miss[k];
}

void Viewer::setSpeed(float s) { clock.speed = s, audio.setSpeed(clock.rate * s); }
void Viewer::applyVolume() { audio.setVolume(muted ? 0 : volume); }

void Viewer::tick(double realMs) {
    if (!clock.running) return;
    double from = clock.t;
    bool song = audio.loaded() && audio.playing() && !audio.ended();
    if (song) clock.t = audio.positionMs();
    else clock.advance(realMs);
    if (coach && stops.cross(from, clock.t)) {
        int at = (int)stops.next - 1;
        setPlaying(false);
        clock.t = stops.times[at];
        audio.seekMs(clock.t);
        stoppedAt = miss[at];
    }
    if (clock.t > endT + 2000) setPlaying(false);
}

int Viewer::judgedBefore(double t) const {
    auto it = std::upper_bound(objMap.begin(), objMap.end(), t, [&](double tt, int i) { return tt < map.objs[i].t; });
    return (int)(it - objMap.begin());
}

std::pair<float, float> Viewer::cursorAt(double t, int* keys) const {
    auto& f = rep.frames;
    if (f.empty()) return {256, 192};
    auto it = std::upper_bound(f.begin(), f.end(), t, [](double v, const Frame& fr) { return v < fr.t; });
    if (it == f.begin()) { if (keys) *keys = f[0].keys; return {f[0].x, f[0].y}; }
    if (it == f.end()) { if (keys) *keys = f.back().keys; return {f.back().x, f.back().y}; }
    auto& a = *(it - 1);
    auto& b = *it;
    if (keys) *keys = a.keys;
    float k = b.t > a.t ? (float)((t - a.t) / (b.t - a.t)) : 0;
    return {a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k};
}

namespace {
std::string clockStr(double ms) {
    int t = std::max(0, (int)ms);
    return sf("%d:%02d.%03d", t / 60000, t / 1000 % 60, t % 1000);
}

ImU32 mix(ImU32 a, ImU32 b, float k) {  // a -> b by k, opaque
    auto ch = [&](int sh) { return (ImU32)std::lround(((a >> sh) & 255) * (1 - k) + ((b >> sh) & 255) * k) << sh; };
    return ch(0) | ch(8) | ch(16) | 0xFF000000u;
}
// Combo colours on the playfield screen; new combos cycle through them like a default osu! skin.
constexpr ImU32 kCombo[] = {IM_COL32(246, 134, 92, 255), IM_COL32(245, 187, 64, 255), IM_COL32(246, 150, 180, 255), IM_COL32(120, 170, 232, 255)};

// A hit circle: combo-coloured face, white rim, dark number; a = opacity, grow = scale for the hit burst.
void hitCircle(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float a, const std::string& n, float grow = 1) {
    auto fade = [&](ImU32 u, float k) { return alpha(u, (int)(255 * std::clamp(k, 0.f, 1.f))); };
    float R = r * grow, rim = std::max(S(2), r * 0.11f);
    dl->AddCircleFilled(c, R + S(1.5f), fade(cScreen, a * 0.9f), 48);
    dl->AddCircleFilled(c, R, fade(col, a), 48);
    dl->AddCircle(c, R - rim / 2, fade(cWin, a), 48, rim);
    if (n.empty() || grow > 1) return;
    ImFont* f = font(r >= S(30) ? TITLE : BAR);
    ImVec2 ts = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0, n.c_str());
    dl->AddText(f, f->FontSize, {c.x - ts.x / 2, c.y - ts.y / 2}, fade(cScreen, a), n.c_str());
}

// Chevron pointing from `at` towards `to`: a slider's reverse arrow.
void reverseArrow(ImDrawList* dl, ImVec2 at, ImVec2 to, float r, ImU32 col) {
    float dx = to.x - at.x, dy = to.y - at.y, l = std::max(1e-3f, std::hypot(dx, dy));
    dx /= l, dy /= l;
    float s = r * 0.45f;
    ImVec2 tip{at.x + dx * s, at.y + dy * s}, back{at.x - dx * s * 0.4f, at.y - dy * s * 0.4f};
    ImVec2 w1{back.x - dy * s, back.y + dx * s}, w2{back.x + dy * s, back.y - dx * s};
    ImVec2 pts[] = {w1, tip, w2};
    dl->AddPolyline(pts, 3, col, 0, std::max(S(3), r * 0.14f));
}

void drawPlayfield(Viewer& v, ImVec2 p, ImVec2 size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float sc = std::min(size.x / 512, size.y / 384) * 0.8f;  // margin: circles near the edge stay inside the panel
    ImVec2 o{p.x + (size.x - 512 * sc) / 2, p.y + (size.y - 384 * sc) / 2};
    bool hr = v.play.mods & mod::HR;
    auto obj = [&](float x, float y) { return ImVec2{o.x + x * sc, o.y + drawY(y, hr) * sc}; };  // map objects
    auto cur = [&](float x, float y) { return ImVec2{o.x + x * sc, o.y + y * sc}; };              // replay frames
    dl->AddRect({o.x - S(1), o.y - S(1)}, {o.x + 512 * sc + S(1), o.y + 384 * sc + S(1)}, cScreenRule);
    double t = v.clock.t;
    float r = v.radius * sc;
    auto fade = [&](ImU32 c, float a) { return alpha(c, (int)(255 * std::clamp(a, 0.f, 1.f))); };
    ImFont* num = font(TITLE);
    int heldNow = 0;
    v.cursorAt(t, &heldNow);

    // map objects, later ones first so earlier ones are drawn on top
    for (int i = (int)v.map.objs.size() - 1; i >= 0; i--) {
        auto& m = v.map.objs[i];
        if (t < m.t - v.preempt || t > m.end + 200) continue;
        float a = (float)(t - (m.t - v.preempt)) / 400;
        if (t > m.end) a = 1 - (float)(t - m.end) / 200;
        a = std::clamp(a, 0.f, 1.f);
        if (m.kind == 2) {
            dl->AddCircle(obj(256, 192), 150 * sc, fade(cScreenRule, a), 64, S(4));
            std::string s = sf("spin %.1f s", std::max(0.0, (m.end - t) / 1000 / v.clock.rate));
            ImVec2 ts = num->CalcTextSizeA(num->FontSize, FLT_MAX, 0, s.c_str()), c = obj(256, 192);
            dl->AddText(num, num->FontSize, {c.x - ts.x / 2, c.y - ts.y / 2}, fade(cScreenMuted, a), s.c_str());
            continue;
        }
        ImU32 col = kCombo[v.colour[i] % 4];
        ImVec2 c = obj(m.x, m.y);
        if (m.kind == 1 && v.paths[i].size() > 1) {
            // body: stamped circles give round joins at any curve; colours are pre-blended with the screen so the
            // overlapping stamps stay even while the slider fades
            auto& path = v.paths[i];
            float rim = std::max(S(2), r * 0.11f);
            int step = std::max(1, (int)(r * 0.3f / (4 * sc)));  // path points are ~4 osu! px apart
            ImU32 edge = mix(cScreen, cWin, a), body = mix(cScreen, mix(col, cScreen, 0.62f), a);
            for (int pass = 0; pass < 2; pass++)
                for (size_t k = 0;; k = std::min(k + step, path.size() - 1)) {  // every step-th point, always the last
                    dl->AddCircleFilled(obj(path[k].first, path[k].second), pass ? r - rim : r, pass ? body : edge, 32);
                    if (k == path.size() - 1) break;
                }
            ImVec2 end = obj(path.back().first, path.back().second), head = c;
            // reverse arrows while repeats are left: at the end for the slides still to come back, at the head after that
            int slideMs = m.slides > 0 ? (m.end - m.t) / m.slides : m.end - m.t, done = t > m.t ? (int)((t - m.t) / std::max(1, slideMs)) : 0;
            if (m.slides - done > 1) {
                bool atEnd = done % 2 == 0;
                ImVec2 from = atEnd ? end : head;
                auto& nb = atEnd ? path[path.size() > 4 ? path.size() - 5 : 0] : path[std::min<size_t>(4, path.size() - 1)];
                reverseArrow(dl, from, obj(nb.first, nb.second), r, fade(cWin, a));
            }
            if (t >= m.t && t <= m.end) {  // ball, and the follow circle while a key is held
                auto b = sliderBall(m, path, (int)t);
                ImVec2 bc = obj(b.first, b.second);
                if (heldNow & 15) dl->AddCircle(bc, r * 2.1f, fade(cWin, 0.75f), 48, S(2.5f));
                hitCircle(dl, bc, r * 0.82f, col, 1, "");
            }
        }
        // the circle (or slider head): fades in, then on a hit grows and fades out over 200 ms; a miss just fades
        int k = v.objOf[i];
        bool hit = k >= 0 && v.objs[k].judge > 0;
        if (t < m.t) {
            hitCircle(dl, c, r, col, a, std::to_string(v.combo[i]));
            float ap = 1 + 2.2f * (float)(m.t - t) / v.preempt;
            dl->AddCircle(c, r * ap, fade(col, a), 48, std::max(S(2), r * 0.08f));
        } else if (t <= m.t + 200) {
            float k01 = (float)(t - m.t) / 200;
            if (hit) hitCircle(dl, c, r, col, 1 - k01, "", 1 + 0.35f * k01);
            else hitCircle(dl, c, r, col, 1 - k01 * 1.5f, std::to_string(v.combo[i]));
        }
    }
    // results: 100 / 50 in amber, misses as a red cross
    for (size_t k = 0; k < v.objs.size(); k++) {
        auto& res = v.objs[k];
        int mt = v.mapT((int)k);
        if (res.judge == 300 || t < mt || t > mt + 600) continue;
        auto& m = v.map.objs[v.objMap[k]];
        float a = 1 - (float)(t - mt) / 600;
        const char* s = res.judge == 100 ? "100" : res.judge == 50 ? "50" : "\xE2\x9C\x95";
        ImVec2 c = obj(m.x, m.y), ts = num->CalcTextSizeA(num->FontSize, FLT_MAX, 0, s);
        dl->AddText(num, num->FontSize, {c.x - ts.x / 2, c.y - r - ts.y}, fade(res.judge ? cHotAmber : cHotRed, a), s);
    }
    // your clicks: a dot where each press landed (key 1 cream, key 2 lime); presses are in time order
    auto pr0 = std::lower_bound(v.presses.begin(), v.presses.end(), t - 600, [](const Viewer::Press& q, double tt) { return q.t < tt; });
    for (auto it = pr0; it != v.presses.end() && it->t <= t; ++it) {
        auto& pr = *it;
        float a = 1 - (float)(t - pr.t) / 600;
        ImVec2 c = cur(pr.x, pr.y);
        dl->AddCircleFilled(c, S(6), fade(pr.key ? cLime : cWin, a));
        dl->AddCircle(c, S(6), fade(cScreen, a), 12, S(1.5f));
    }
    // the coach's stop: red ring on the note, and for aim misses a line from your click to the centre
    if (v.stoppedAt >= 0) {
        auto& res = v.objs[v.stoppedAt];
        auto& m = v.map.objs[v.objMap[v.stoppedAt]];
        ImVec2 c = obj(m.x, m.y);
        dl->AddCircle(c, r * 1.3f, cHotRed, 48, S(3));
        if (res.cause == AIM || res.cause == OFFTIME) {
            auto q = v.cursorAt(v.mapT(v.stoppedAt) + res.err * v.clock.rate);
            ImVec2 a = cur(q.first, q.second);
            dl->AddLine(a, c, cHotRed, S(2));
            dl->AddCircleFilled(a, S(6), cHotRed);
        }
    }
    // cursor path: the last 300 ms through the real replay frames (a dot on each), fading with age
    {
        auto& f = v.rep.frames;
        auto it = std::lower_bound(f.begin(), f.end(), t - 300, [](const Frame& fr, double tt) { return fr.t < tt; });
        ImVec2 prev{-1, -1};
        for (; it != f.end() && it->t <= t; ++it) {
            ImVec2 q = cur(it->x, it->y);
            float a = 1 - (float)(t - it->t) / 300;
            if (prev.x >= 0) dl->AddLine(prev, q, fade(cWin, 0.55f * a), S(1.5f));
            dl->AddCircleFilled(q, S(2.5f), fade(cWin, 0.9f * a), 8);
            prev = q;
        }
        auto q = v.cursorAt(t);
        ImVec2 cq = cur(q.first, q.second);
        if (prev.x >= 0) dl->AddLine(prev, cq, fade(cWin, 0.55f), S(1.5f));
        dl->AddCircleFilled(cq, S(8), cYellow);
        dl->AddCircle(cq, S(8), cScreen, 16, S(2));
    }
    // overlay: time and frame top left, keys top right, judgements so far bottom left
    {
        dl->PushClipRect(p, {p.x + size.x, p.y + size.y}, true);
        ImFont* fl = font(LABEL);
        auto& f = v.rep.frames;
        int fi = (int)(std::upper_bound(f.begin(), f.end(), t, [](double tt, const Frame& fr) { return tt < fr.t; }) - f.begin());
        dl->AddText(fl, fl->FontSize, {p.x + S(12), p.y + S(10)}, cScreenMuted, sf("%s   frame %d / %zu", clockStr(t).c_str(), fi, f.size()).c_str());
        static const char* kKey[] = {"K1", "K2", "M1", "M2"};
        int lanes = laneBits(heldNow);
        float ks = S(26), x = p.x + size.x - S(12) - 4 * ks - 3 * S(6);
        for (int l = 0; l < 4; l++, x += ks + S(6)) {
            bool on = lanes >> l & 1;
            ImVec2 a{x, p.y + S(10)}, b{x + ks, p.y + S(10) + ks};
            dl->AddRectFilled(a, b, on ? (l % 2 ? cOrange : cYellow) : cScreen, S(4));
            dl->AddRect(a, b, on ? cWin : cScreenRule, S(4), 0, S(1.5f));
            ImVec2 ts = fl->CalcTextSizeA(fl->FontSize, FLT_MAX, 0, kKey[l]);
            dl->AddText(fl, fl->FontSize, {a.x + (ks - ts.x) / 2, a.y + (ks - ts.y) / 2}, on ? cScreen : cScreenMuted, kKey[l]);
        }
        int n = v.judgedBefore(t);
        std::array<int, 4> j = n ? v.judged[n - 1] : std::array<int, 4>{};
        static const char* kJ[] = {"300", "100", "50", "miss"};
        const ImU32 jc[] = {cWin, cHotAmber, cHotAmber, cHotRed};
        float jx = p.x + S(12), jy = p.y + size.y - S(10) - fl->FontSize;
        for (int q = 0; q < 4; q++) {
            dl->AddText(fl, fl->FontSize, {jx, jy}, cScreenMuted, kJ[q]);
            jx += fl->CalcTextSizeA(fl->FontSize, FLT_MAX, 0, kJ[q]).x + S(6);
            std::string c = std::to_string(j[q]);
            dl->AddText(fl, fl->FontSize, {jx, jy}, jc[q], c.c_str());
            jx += fl->CalcTextSizeA(fl->FontSize, FLT_MAX, 0, c.c_str()).x + S(16);
        }
        std::string e = sf("UR %.0f   mean %+.1f ms", v.hitErr.ur(), v.hitErr.mean());
        if (n) {  // the last note so far: its result and timing
            auto& o = v.objs[n - 1];
            e += o.judge ? sf("   note %d: %d, %+.0f ms", n, o.judge, o.err) : sf("   note %d: miss", n);
        }
        dl->AddText(fl, fl->FontSize, {jx + S(8), jy}, cScreenMuted, e.c_str());
        dl->PopClipRect();
    }
}

void timeline(Viewer& v) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x, h = S(44);
    double dur = std::max(1.0, v.endT + 1000);
    ImGui::InvisibleButton("timeline", {w, h});
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip("%s", clockStr(std::clamp((ImGui::GetIO().MousePos.x - p.x) / w, 0.f, 1.f) * dur).c_str());
    }
    if (ImGui::IsItemActive()) {
        if (ImGui::IsItemActivated()) v.setPlaying(false);
        v.seek(std::clamp((ImGui::GetIO().MousePos.x - p.x) / w, 0.f, 1.f) * dur), v.stoppedAt = -1;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float t = S(2), y0 = p.y + S(6), y1 = p.y + h - S(6);  // the track; the knob overhangs it
    dl->AddRectFilled({p.x, y0}, {p.x + w, y1}, cWell);
    for (size_t k = 0; k < v.objs.size(); k++) {
        int j = v.objs[k].judge;
        if (j == 300) continue;
        float x = std::floor(p.x + (float)(v.mapT((int)k) / dur) * w);
        dl->AddRectFilled({x - S(1), j ? (y0 + y1) / 2 : y0}, {x + S(1), y1}, j ? cWarn : cBad);
    }
    dl->AddRectFilled({p.x, y0}, {p.x + w, y0 + t}, cInk), dl->AddRectFilled({p.x, y1 - t}, {p.x + w, y1}, cInk);
    dl->AddRectFilled({p.x, y0}, {p.x + t, y1}, cInk), dl->AddRectFilled({p.x + w - t, y0}, {p.x + w, y1}, cInk);
    float x = std::floor(p.x + (float)(std::min(v.clock.t, dur) / dur) * w), k = S(7);
    dl->AddRectFilled({x - k + S(3), p.y + S(3)}, {x + k + S(3), p.y + h + S(3)}, cInk);
    dl->AddRectFilled({x - k, p.y}, {x + k, p.y + h}, cYellow);
    dl->AddRect({x - k, p.y}, {x + k, p.y + h}, cInk, 0, 0, S(2));
}

// Key lanes: a zoomed strip around the playhead with a time ruler, every note (dot by result, sliders as a bar) and one
// lane per key showing how long each press was held, in ms. Wheel zooms, dragging scrubs, a click jumps there.
float lanesH(const Viewer& v) { return S(20) + S(18) + (float)v.lanes.size() * S(18) + S(4); }
void keyLanes(Viewer& v) {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x, h = lanesH(v), lw = S(30), rulerH = S(20), noteH = S(18), laneH = S(18);
    ImGui::InvisibleButton("lanes", {w, h});
    float x0 = p.x + lw, cw = w - lw;
    double span = v.laneMs, t0 = v.clock.t - span / 2, msPx = span / cw;
    static float dragX;
    static double dragT;
    static bool moved;
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (io.MouseWheel != 0) v.laneMs = std::clamp(v.laneMs * std::pow(1.25f, -io.MouseWheel), 500.f, 12000.f);
        if (io.MousePos.x > x0) ImGui::SetTooltip("%s", clockStr(t0 + (io.MousePos.x - x0) * msPx).c_str());
    }
    if (ImGui::IsItemActivated()) v.setPlaying(false), dragX = io.MousePos.x, dragT = v.clock.t, moved = false;
    if (ImGui::IsItemActive()) {
        float dx = io.MousePos.x - dragX;
        if (std::fabs(dx) > S(3)) moved = true;
        if (moved) v.seek(dragT - dx * msPx), v.stoppedAt = -1;
    }
    if (ImGui::IsItemDeactivated() && !moved && dragX > x0) v.seek(dragT - span / 2 + (dragX - x0) * msPx), v.stoppedAt = -1;
    t0 = v.clock.t - span / 2;
    auto X = [&](double t) { return (float)(x0 + (t - t0) / span * cw); };
    double t1 = t0 + span;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* fl = font(LABEL);
    int nl = (int)v.lanes.size(), row[4] = {-1, -1, -1, -1};
    for (int i = 0; i < nl; i++) row[v.lanes[i]] = i;
    float yN = p.y + rulerH, yL = yN + noteH, yEnd = yL + nl * laneH;
    dl->AddRectFilled({x0, yN}, {p.x + w, yEnd}, cWell, S(4));
    dl->PushClipRect({x0, p.y}, {p.x + w, p.y + h}, true);
    // ruler: labels at least ~90 px apart, minor ticks in between
    double step = 50;
    for (double c : {50, 100, 250, 500, 1000, 2000, 5000}) if ((step = c) / msPx >= S(90)) break;
    for (double t = std::floor(t0 / (step / 5)) * (step / 5); t <= t1; t += step / 5) {
        bool major = std::fmod(std::fabs(t) + 0.5, step) < 1;
        float x = std::floor(X(t));
        dl->AddLine({x, yN - (major ? S(7) : S(4))}, {x, yN}, major ? cMuted : cRule, S(1));
        if (major && t >= 0) dl->AddText(fl, fl->FontSize, {x + S(3), p.y}, cMuted, clockStr(t).c_str());
    }
    for (int l = 1; l < nl; l++) dl->AddLine({x0, yL + l * laneH}, {p.x + w, yL + l * laneH}, cRule);
    dl->AddLine({x0, yL}, {p.x + w, yL}, cRule);
    // notes: sliders as a bar from head to end, then a dot per note coloured by its result
    int k0 = std::max(0, v.judgedBefore(t0 - 5000) - 1);
    for (int k = k0; k < (int)v.objs.size(); k++) {
        auto& m = v.map.objs[v.objMap[k]];
        if (m.t > t1) break;
        if (m.end < t0) continue;
        float cy = yN + noteH / 2;
        if (m.kind == 1) dl->AddRectFilled({X(m.t), cy - S(3)}, {X(m.end), cy + S(3)}, cRule, S(3));
        int j = v.objs[k].judge;
        float x = X(m.t), d = S(4);
        if (!j) {
            dl->AddLine({x - d, cy - d}, {x + d, cy + d}, cBad, S(2.5f)), dl->AddLine({x - d, cy + d}, {x + d, cy - d}, cBad, S(2.5f));
            continue;
        }
        dl->AddCircleFilled({x, cy}, d, j == 300 ? cInk : cWarn, 12);
        if (j == 50) dl->AddCircleFilled({x, cy}, d * 0.45f, cWell, 8);  // a 50 is a ring
    }
    // holds: one bar per press, with its length in ms when it fits
    static const ImU32 lc[] = {cYellow, cOrange, cYellow, cOrange};
    auto h0 = std::lower_bound(v.holds.begin(), v.holds.end(), t0 - v.longestHold, [](const Viewer::Hold& q, double tt) { return q.t0 < tt; });
    for (auto it = h0; it != v.holds.end() && it->t0 <= t1; ++it) {
        if (it->t1 < t0) continue;
        if (row[it->lane] < 0) continue;
        float a = X(it->t0), b = std::max(X(it->t1), a + S(3)), y = yL + row[it->lane] * laneH;
        ImVec2 mn{a, y + S(3)}, mx{b, y + laneH - S(3)};
        dl->AddRectFilled(mn, mx, lc[it->lane], S(2));
        dl->AddRect(mn, mx, cInk, S(2), 0, S(1));
        std::string ms = std::to_string(it->t1 - it->t0);
        ImVec2 ts = fl->CalcTextSizeA(fl->FontSize * 0.8f, FLT_MAX, 0, ms.c_str());
        if (b - a > ts.x + S(8)) dl->AddText(fl, fl->FontSize * 0.8f, {a + (b - a - ts.x) / 2, y + (laneH - ts.y) / 2}, cInk, ms.c_str());
    }
    // playhead in the middle
    float px = std::floor(x0 + cw / 2);
    dl->AddRectFilled({px - S(1), p.y + S(2)}, {px + S(1), yEnd}, cBad);
    dl->PopClipRect();
    dl->AddRect({x0, yN}, {p.x + w, yEnd}, cInk, S(4), 0, S(1.5f));
    static const char* kKey[] = {"K1", "K2", "M1", "M2"};
    for (int l = 0; l <= nl; l++) {
        const char* s = l ? kKey[v.lanes[l - 1]] : "notes";
        float y = l ? yL + (l - 1) * laneH : yN, hh = l ? laneH : noteH, fs = fl->FontSize * (l ? 1 : 0.8f);
        ImVec2 ts = fl->CalcTextSizeA(fs, FLT_MAX, 0, s);
        dl->AddText(fl, fs, {p.x, y + (hh - ts.y) / 2}, cMuted, s);
    }
}

float checkW(const char* label) { return ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(label).x; }
// Rows the controls take when flowed into width w (same rule as flow()).
int rowsFor(const std::vector<float>& ws, float gap, float w) {
    int rows = 1;
    float x = 0;
    for (size_t i = 0; i < ws.size(); i++) {
        if (i && x + gap + ws[i] <= w) x += gap + ws[i];
        else if (i) rows++, x = ws[i];
        else x = ws[i];
    }
    return rows;
}
}

void drawViewer(App& a) {
    Viewer& v = *a.viewer;
    ImGuiIO& io = ImGui::GetIO();
    v.tick(io.DeltaTime * 1000);

    if (!v.ok) {
        window("err", "Can't open this replay", cOrange, std::min(S(640), ImGui::GetContentRegionAvail().x - S(6)), 0, true);
        txt(BODY, cMuted, v.error, true);
        ImGui::Dummy({0, S(8)});
        if (button2("Back to plays")) a.viewing = false;
        endWindow();
        return;
    }
    // keyboard: Space play/pause, arrows previous/next mistake, Enter continue
    auto nextMistake = [&](int dir) {
        int best = -1;
        for (int k = 0; k < (int)v.miss.size(); k++) {
            int mt = v.mapT(v.miss[k]);
            if (dir > 0 && mt > v.clock.t + 1 && best < 0) best = k;
            if (dir < 0 && mt < v.clock.t - 1) best = k;
        }
        v.jumpTo(best);
    };
    if (!io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) v.setPlaying(!v.clock.running);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) nextMistake(1);
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) nextMistake(-1);
        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && v.stoppedAt >= 0) v.setPlaying(true);
    }

    // the controls under the timeline wrap onto more rows in a narrow window, so size the windows above from that
    static const float speeds[] = {0.25f, 0.5f, 0.75f, 1.f};
    float speedW = 0;
    for (float s : speeds) speedW += buttonW(sf("%g\xC3\x97", s).c_str());
    std::vector<float> ws = {buttonW("\xE2\x88\x92" "5 s") + S(8) + S(112) + S(8) + buttonW("+5 s"), speedW, buttonW("\xE2\x86\x90 Previous"), buttonW("Next \xE2\x86\x92"), S(152), buttonW(v.muted ? "Unmute" : "Mute")};
    float inner = ImGui::GetContentRegionAvail().x - S(6) - 2 * S(2) - 2 * S(16);
    int rows = rowsFor(ws, S(16), inner);
    float transportH = 2 * S(2) + 2 * S(16) + S(44) + S(8) + lanesH(v) + S(8) + rows * S(44) + (rows - 1) * S(20) + S(6);
    float upper = std::max(S(260), ImGui::GetContentRegionAvail().y - transportH - S(16) - S(6));

    float availX = ImGui::GetContentRegionAvail().x, coachW = std::clamp(std::floor(availX * 0.32f), S(320), S(440));
    window("field", "Replay", cWell, availX - coachW - S(16) - S(6), upper, false,
           (v.audioNote.empty() ? "" : "no music file   \xC2\xB7   ") + clockStr(v.clock.t) + " / " + clockStr(v.endT));
    {
        ImVec2 p = ImGui::GetCursorScreenPos(), sz = ImGui::GetContentRegionAvail();
        ImGui::GetWindowDrawList()->AddRectFilled(p, {p.x + sz.x, p.y + sz.y}, cScreen);
        drawPlayfield(v, p, sz);
    }
    endWindow();
    ImGui::SameLine(0, S(16));
    window("coach", "Coach", cOrange, 0, upper);
    {
        // the coach's own settings
        if (ImGui::Checkbox("Coach stops", &v.coach)) v.stops.seek(v.clock.t);
        flow(checkW("Also stop at 50s"), S(16));
        if (ImGui::Checkbox("Also stop at 50s", &v.stop50)) v.rebuildStops();
        ImGui::Dummy({0, S(4)}), rule(), ImGui::Dummy({0, S(4)});
        float beat = v.play.bpm > 0 ? 60000 / v.play.bpm : 500;
        if (v.stoppedAt >= 0) {
            Explain e = explainMiss(v.objs[v.stoppedAt], beat);
            txt(LABEL, cMuted, clockStr(v.mapT(v.stoppedAt)));
            txt(TITLE, v.objs[v.stoppedAt].judge ? cWarn : cBad, e.title, true);
            txt(BODY, cInk, e.text, true);
            ImGui::Dummy({0, S(8)});
            txt(LABEL, cGood, "What to do");
            txt(BODY, cInk, e.tip, true);
            ImGui::Dummy({0, S(8)});
            if (button1("Continue  (Enter)")) v.setPlaying(true);
        } else {
            txt(TITLE, cInk, v.miss.empty() ? "Clean play: nothing to stop at" : sf("%zu mistakes in this play", v.miss.size()), true);
            txt(BODY, cMuted, v.miss.empty() ? "Press Play (or Space) to watch it." : "The coach stops at each one. Click one to jump there.", true);
            ImGui::BeginChild("list", {0, 0}, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
            ImGuiListClipper clip;  // only the visible rows are explained each frame
            clip.Begin((int)v.miss.size(), S(44) + ImGui::GetStyle().ItemSpacing.y);
            while (clip.Step())
                for (int k = clip.DisplayStart; k < clip.DisplayEnd; k++) {
                    int i = v.miss[k];
                    std::string row = clockStr(v.mapT(i)) + "   " + explainMiss(v.objs[i], beat).title + "##m" + std::to_string(k);
                    if (ImGui::Selectable(row.c_str(), false, 0, {0, S(44)})) v.jumpTo(k);
                    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                }
            ImGui::EndChild();
        }
    }
    endWindow();

    gap();
    window("transport", nullptr, 0, 0, 0, true);
    timeline(v);
    keyLanes(v);
    // controls; each group goes on the next row when it doesn't fit
    if (button2("\xE2\x88\x92" "5 s")) v.seek(v.clock.t - 5000), v.stoppedAt = -1;
    ImGui::SameLine(0, S(8));
    if (button1(v.clock.running ? "Pause" : "Play", {S(112), 0})) v.setPlaying(!v.clock.running);
    ImGui::SameLine(0, S(8));
    if (button2("+5 s")) v.seek(v.clock.t + 5000), v.stoppedAt = -1;
    flow(ws[1], S(16));
    for (int i = 0; i < 4; i++) {
        if (i) ImGui::SameLine(0, 0);
        bool on = v.clock.speed == speeds[i];
        bool hit = on ? button1(sf("%g\xC3\x97", speeds[i]).c_str()) : button2(sf("%g\xC3\x97", speeds[i]).c_str());
        if (hit) v.setSpeed(speeds[i]);
    }
    flow(ws[2], S(16));
    if (button2("\xE2\x86\x90 Previous")) nextMistake(-1);
    flow(ws[3], S(16));
    if (button2("Next \xE2\x86\x92")) nextMistake(1);
    flow(ws[4], S(16));
    ImGui::SetNextItemWidth(S(152));
    int vol = (int)std::lround(v.volume * 100);
    if (ImGui::SliderInt("##vol", &vol, 0, 100, "music %d%%")) v.volume = vol / 100.f, v.applyVolume();
    flow(ws[5], S(16));
    if (button2(v.muted ? "Unmute" : "Mute")) v.muted = !v.muted, v.applyVolume();
    endWindow();
}
