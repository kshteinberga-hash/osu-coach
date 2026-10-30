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
    int c = 0;
    for (size_t i = 0; i < map.objs.size(); i++) {
        auto& o = map.objs[i];
        c = o.nc || i == 0 ? 1 : c + 1;
        combo.push_back(c);
        paths.push_back(o.kind == 1 ? sliderPath(o) : std::vector<std::pair<float, float>>{});
        if (o.kind != 2 && objMap.size() < objs.size()) objMap.push_back((int)i);
        endT = std::max(endT, (double)o.end);
    }
    float ar = map.ar, cs = map.cs;
    if (p.mods & mod::HR) ar = std::min(10.f, ar * 1.4f), cs = std::min(10.f, cs * 1.3f);
    if (p.mods & mod::EZ) ar *= 0.5f, cs *= 0.5f;
    preempt = ar < 5 ? 1800 - 120 * ar : 1200 - 150 * (ar - 5);
    radius = circleRadius(cs);
    int prev = 0;
    for (auto& f : rep.frames) {
        for (int k = 0; k < 2; k++)
            if ((f.keys & (k ? 10 : 5)) && !(prev & (k ? 10 : 5))) presses.push_back({f.t, f.x, f.y, k});
        prev = f.keys;
    }
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

void drawPlayfield(Viewer& v, ImVec2 p, ImVec2 size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float sc = std::min(size.x / 512, size.y / 384) * 0.8f;  // margin: circles near the edge stay inside the panel
    ImVec2 o{p.x + (size.x - 512 * sc) / 2, p.y + (size.y - 384 * sc) / 2};
    bool hr = v.play.mods & mod::HR;
    auto obj = [&](float x, float y) { return ImVec2{o.x + x * sc, o.y + drawY(y, hr) * sc}; };  // map objects
    auto cur = [&](float x, float y) { return ImVec2{o.x + x * sc, o.y + y * sc}; };              // replay frames
    dl->AddRect({o.x - S(1), o.y - S(1)}, {o.x + 512 * sc + S(1), o.y + 384 * sc + S(1)}, cRule);
    double t = v.clock.t;
    float r = v.radius * sc;
    auto fade = [&](ImU32 c, float a) { return alpha(c, (int)(255 * std::clamp(a, 0.f, 1.f))); };
    ImFont* num = font(TITLE);

    // map objects, later ones first so earlier ones are drawn on top
    for (int i = (int)v.map.objs.size() - 1; i >= 0; i--) {
        auto& m = v.map.objs[i];
        if (t < m.t - v.preempt || t > m.end + 150) continue;
        float a = (float)(t - (m.t - v.preempt)) / 400;
        if (t > m.end) a = 1 - (float)(t - m.end) / 150;
        if (m.kind == 2) {
            dl->AddCircle(obj(256, 192), 150 * sc, fade(cRule, a), 64, S(4));
            std::string s = sf("spin %.1f s", std::max(0.0, (m.end - t) / 1000 / v.clock.rate));
            ImVec2 ts = num->CalcTextSizeA(num->FontSize, FLT_MAX, 0, s.c_str()), c = obj(256, 192);
            dl->AddText(num, num->FontSize, {c.x - ts.x / 2, c.y - ts.y / 2}, fade(cMuted, a), s.c_str());
            continue;
        }
        ImVec2 c = obj(m.x, m.y);
        if (m.kind == 1 && v.paths[i].size() > 1) {
            std::vector<ImVec2> pts;
            for (auto& q : v.paths[i]) pts.push_back(obj(q.first, q.second));
            dl->AddPolyline(pts.data(), (int)pts.size(), fade(cCream, a), 0, 2 * r);
            dl->AddPolyline(pts.data(), (int)pts.size(), fade(cRule, a), 0, 2 * r - S(6));
            dl->AddCircleFilled(pts.back(), r, fade(cCream, a));
            dl->AddCircleFilled(pts.back(), r - S(3), fade(cRule, a));
            if (m.slides > 1) {
                ImVec2 ts = num->CalcTextSizeA(num->FontSize, FLT_MAX, 0, "\xE2\x86\xBA");
                dl->AddText(num, num->FontSize, {pts.back().x - ts.x / 2, pts.back().y - ts.y / 2}, fade(cCream, a), "\xE2\x86\xBA");
            }
            if (t >= m.t && t <= m.end) {
                auto b = sliderBall(m, v.paths[i], (int)t);
                dl->AddCircle(obj(b.first, b.second), r * 0.9f, cPink, 32, S(3));
            }
        }
        if (t <= m.t + 150) {  // the circle (or slider head) disappears once its time has passed
            float ha = t > m.t ? 1 - (float)(t - m.t) / 150 : a;
            dl->AddCircleFilled(c, r, fade(cPanel, ha), 48);
            dl->AddCircle(c, r - S(1.5f), fade(cCream, ha), 48, S(3));
            std::string n = std::to_string(v.combo[i]);
            ImVec2 ts = num->CalcTextSizeA(num->FontSize, FLT_MAX, 0, n.c_str());
            dl->AddText(num, num->FontSize, {c.x - ts.x / 2, c.y - ts.y / 2}, fade(cCream, ha), n.c_str());
            if (t < m.t) dl->AddCircle(c, r * (1 + 2 * (float)(m.t - t) / v.preempt), fade(cPink, a), 48, S(2));
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
        dl->AddText(num, num->FontSize, {c.x - ts.x / 2, c.y - r - ts.y}, fade(res.judge ? cAmber : cRed, a), s);
    }
    // your clicks: a dot where each press landed (key 1 cream, key 2 green)
    for (auto& pr : v.presses)
        if (t >= pr.t && t <= pr.t + 600) {
            float a = 1 - (float)(t - pr.t) / 600;
            ImVec2 c = cur(pr.x, pr.y);
            dl->AddCircleFilled(c, S(6), fade(pr.key ? cGreen : cCream, a));
            dl->AddCircle(c, S(6), fade(cInk, a), 12, S(1.5f));
        }
    // the coach's stop: red ring on the note, and for aim misses a line from your click to the centre
    if (v.stoppedAt >= 0) {
        auto& res = v.objs[v.stoppedAt];
        auto& m = v.map.objs[v.objMap[v.stoppedAt]];
        ImVec2 c = obj(m.x, m.y);
        dl->AddCircle(c, r * 1.3f, cRed, 48, S(3));
        if (res.cause == AIM || res.cause == OFFTIME) {
            auto q = v.cursorAt(v.mapT(v.stoppedAt) + res.err * v.clock.rate);
            ImVec2 a = cur(q.first, q.second);
            dl->AddLine(a, c, cRed, S(2));
            dl->AddCircleFilled(a, S(6), cRed);
        }
    }
    // cursor with a 150 ms trail
    {
        std::vector<ImVec2> trail;
        for (double tt = t - 150; tt <= t; tt += 10) { auto q = v.cursorAt(tt); trail.push_back(cur(q.first, q.second)); }
        for (size_t i = 1; i < trail.size(); i++) dl->AddLine(trail[i - 1], trail[i], alpha(cPink, (int)(200 * i / trail.size())), S(3));
        auto q = v.cursorAt(t);
        dl->AddCircleFilled(cur(q.first, q.second), S(8), cPink);
        dl->AddCircle(cur(q.first, q.second), S(8), cInk, 16, S(2));
    }
}

void timeline(Viewer& v) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x, h = S(44);
    double dur = std::max(1.0, v.endT + 1000);
    ImGui::InvisibleButton("timeline", {w, h});
    if (ImGui::IsItemActive()) {
        if (ImGui::IsItemActivated()) v.setPlaying(false);
        v.seek((ImGui::GetIO().MousePos.x - p.x) / w * dur), v.stoppedAt = -1;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, {p.x + w, p.y + h}, cPanel);
    dl->AddRect(p, {p.x + w, p.y + h}, cRule);
    for (size_t k = 0; k < v.objs.size(); k++) {
        int j = v.objs[k].judge;
        if (j == 300) continue;
        float x = p.x + (float)(v.mapT((int)k) / dur) * w;
        dl->AddLine({x, j ? p.y + h / 2 : p.y}, {x, p.y + h}, j ? cAmber : cRed, S(2));
    }
    float x = p.x + (float)(v.clock.t / dur) * w;
    dl->AddRectFilled({x - S(1), p.y - S(4)}, {x + S(1), p.y + h + S(4)}, cCream);
}
}

void drawViewer(App& a) {
    Viewer& v = *a.viewer;
    ImGuiIO& io = ImGui::GetIO();
    v.tick(io.DeltaTime * 1000);

    if (!v.ok) {
        txt(TITLE, cCream, "Can't open this replay");
        txt(BODY, cMuted, v.error, true);
        if (button2("Back")) a.viewing = false;
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

    // header row
    if (button2("\xE2\x86\x90 Back to plays")) { v.setPlaying(false); a.viewing = false; return; }
    ImGui::SameLine(0, S(16));
    ImGui::AlignTextToFramePadding();
    txt(TITLE, cCream, v.play.title);
    ImGui::Dummy({0, S(8)});

    float controlsH = S(44) * 2 + S(32), avail = ImGui::GetContentRegionAvail().y - controlsH;
    float fieldW = std::floor(ImGui::GetContentRegionAvail().x * 0.68f);
    section("field", fieldW, avail);
    drawPlayfield(v, ImGui::GetCursorScreenPos(), ImGui::GetContentRegionAvail());
    endSection();
    ImGui::SameLine(0, S(16));
    section("coach", 0, avail);
    {
        int keys = 0;
        v.cursorAt(v.clock.t, &keys);
        for (int k = 0; k < 2; k++) {  // key indicators, 44 px squares
            if (k) ImGui::SameLine(0, S(8));
            ImVec2 p = ImGui::GetCursorScreenPos();
            bool held = keys & (k ? 10 : 5);
            ImGui::Dummy({S(44), S(44)});
            ImGui::GetWindowDrawList()->AddRectFilled(p, {p.x + S(44), p.y + S(44)}, held ? (k ? cGreen : cCream) : cInk);
            ImGui::GetWindowDrawList()->AddRect(p, {p.x + S(44), p.y + S(44)}, cRule);
            const char* l = k ? "K2" : "K1";
            ImVec2 ts = font(LABEL)->CalcTextSizeA(font(LABEL)->FontSize, FLT_MAX, 0, l);
            ImGui::GetWindowDrawList()->AddText(font(LABEL), font(LABEL)->FontSize, {p.x + (S(44) - ts.x) / 2, p.y + (S(44) - ts.y) / 2}, held ? cInk : cMuted, l);
        }
        ImGui::SameLine(0, S(16));
        ImGui::AlignTextToFramePadding();
        txt(MONO, cCream, clockStr(v.clock.t));
        if (!v.audioNote.empty()) txt(LABEL, cMuted, v.audioNote);
        rule();
        float beat = v.play.bpm > 0 ? 60000 / v.play.bpm : 500;
        if (v.stoppedAt >= 0) {
            Explain e = explainMiss(v.objs[v.stoppedAt], beat);
            txt(MONO, cMuted, clockStr(v.mapT(v.stoppedAt)));
            txt(TITLE, v.objs[v.stoppedAt].judge ? cAmber : cRed, e.title, true);
            txt(BODY, cCream, e.text, true);
            ImGui::Dummy({0, S(8)});
            txt(LABEL, cGreen, "WHAT TO DO");
            txt(BODY, cCream, e.tip, true);
            ImGui::Dummy({0, S(8)});
            if (button1("Continue  (Enter)")) v.setPlaying(true);
        } else {
            txt(TITLE, cCream, v.miss.empty() ? "Clean play: nothing to stop at" : sf("%zu mistakes in this play", v.miss.size()));
            txt(BODY, cMuted, v.miss.empty() ? "Press Play (or Space) to watch it." : "The coach stops at each one. Click one to jump there. Space plays and pauses, the arrow keys jump between mistakes.", true);
            ImGui::BeginChild("list", {0, 0}, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
            for (int k = 0; k < (int)v.miss.size(); k++) {
                int i = v.miss[k];
                std::string row = clockStr(v.mapT(i)) + "   " + explainMiss(v.objs[i], beat).title + "##m" + std::to_string(k);
                if (ImGui::Selectable(row.c_str(), false, 0, {0, S(44)})) v.jumpTo(k);
            }
            ImGui::EndChild();
        }
    }
    endSection();

    gap();
    timeline(v);
    ImGui::Dummy({0, S(8)});
    // controls row
    if (button1(v.clock.running ? "Pause" : "Play", {S(112), 0})) v.setPlaying(!v.clock.running);
    ImGui::SameLine(0, S(16));
    static const float speeds[] = {0.25f, 0.5f, 0.75f, 1.f};
    for (int i = 0; i < 4; i++) {
        if (i) ImGui::SameLine(0, 0);
        bool on = v.clock.speed == speeds[i];
        bool hit = on ? button1(sf("%g\xC3\x97", speeds[i]).c_str()) : button2(sf("%g\xC3\x97", speeds[i]).c_str());
        if (hit) v.setSpeed(speeds[i]);
    }
    ImGui::SameLine(0, S(16));
    if (button2("\xE2\x86\x90 Previous")) nextMistake(-1);
    ImGui::SameLine(0, S(8));
    if (button2("Next \xE2\x86\x92")) nextMistake(1);
    ImGui::SameLine(0, S(16));
    if (ImGui::Checkbox("Coach stops", &v.coach)) v.stops.seek(v.clock.t);
    ImGui::SameLine(0, S(16));
    if (ImGui::Checkbox("Also stop at 50s", &v.stop50)) v.rebuildStops();
    ImGui::SameLine(0, S(16));
    ImGui::SetNextItemWidth(S(152));
    int vol = (int)std::lround(v.volume * 100);
    if (ImGui::SliderInt("##vol", &vol, 0, 100, "music %d%%")) v.volume = vol / 100.f, v.applyVolume();
    ImGui::SameLine(0, S(8));
    if (button2(v.muted ? "Unmute" : "Mute")) v.muted = !v.muted, v.applyVolume();
}
