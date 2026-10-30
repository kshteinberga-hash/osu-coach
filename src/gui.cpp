#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <ctime>
#include <map>
#include "gui.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "fonts.inl"

namespace {
// Recent-window totals for the detail tabs, rebuilt once per scan.
struct Band { int segs = 0, notes = 0, miss = 0, alt = 0, dbl = 0; double early = 0, late = 0, eN = 0, lN = 0; };
struct Recent {
    size_t plays = 0;
    int fcs = 0; double accSum = 0;
    Acc err, errKey[2], zone[4];
    int within[3]{}, keyHits[2]{}, bObj[4]{}, bMiss[4]{}, qObj[4]{}, qMiss[4]{}, under = 0, over = 0;
    int zPairs[4]{}, zAlt[4]{}, zOv[4]{}, brkObj = 0, brkMiss = 0, flowObj = 0, flowMiss = 0, arObj[2]{}, arMiss[2]{};
    double bDist[4]{};
    float hist[100]{}, jump[24]{};
    Band band[5];  // <150, 150-170, 170-190, 190-210, 210+ BPM
    int stamNotes = 0; float stamSec = 0, stamNps = 0;
};

Recent build(const std::vector<PlayStats>& ps, size_t from) {
    Recent r;
    for (size_t i = from; i < ps.size(); i++) {
        auto& p = ps[i];
        r.plays++, r.fcs += p.gmiss == 0, r.accSum += p.acc;
        r.err.add(p.err);
        for (int k = 0; k < 2; k++) r.errKey[k].add(p.errKey[k]), r.keyHits[k] += p.keyHits[k];
        for (int k = 0; k < 3; k++) r.within[k] += p.within[k];
        for (int k = 0; k < 4; k++) {
            r.zone[k].add(p.zone[k]), r.bObj[k] += p.bObj[k], r.bMiss[k] += p.bMiss[k], r.bDist[k] += p.bDist[k];
            r.qObj[k] += p.qObj[k], r.qMiss[k] += p.qMiss[k], r.zPairs[k] += p.zPairs[k], r.zAlt[k] += p.zAlt[k], r.zOv[k] += p.zOv[k];
        }
        for (int k = 0; k < 100; k++) r.hist[k] += (float)p.hist[k];
        for (int k = 0; k < 24; k++) r.jump[k] += (float)p.jumpHist[k];
        r.under += p.under, r.over += p.over;
        r.brkObj += p.brkObj, r.brkMiss += p.brkMiss, r.flowObj += p.flowObj, r.flowMiss += p.flowMiss;
        int hi = p.ar >= 8;
        r.arObj[hi] += p.nobj, r.arMiss[hi] += p.sMiss;
        for (auto& s : p.streams) {
            Band& b = r.band[s.bpm < 150 ? 0 : s.bpm < 170 ? 1 : s.bpm < 190 ? 2 : s.bpm < 210 ? 3 : 4];
            b.segs++, b.notes += s.n, b.miss += s.miss, b.alt += s.alt, b.dbl += s.dbl;
            b.early += s.errEarly * std::min(4, s.n), b.eN += std::min(4, s.n);
            if (s.n > 4) b.late += s.errLate * (s.n - 4), b.lN += s.n - 4;
        }
        if (p.stamNotes > r.stamNotes) r.stamNotes = p.stamNotes, r.stamSec = p.stamSec, r.stamNps = p.stamNps;
    }
    return r;
}

double pct(double a, double b) { return b > 0 ? 100 * a / b : 0; }
const char* trendWord(int t) { return t > 0 ? "\xE2\x86\x93 improving" : t < 0 ? "\xE2\x86\x91 getting worse" : "\xE2\x86\x92 about the same"; }
ImU32 trendCol(int t) { return t > 0 ? cGood : t < 0 ? cBad : cMuted; }
ImVec2 textSize(int f, const char* s) { ImFont* ft = font(f); return ft->CalcTextSizeA(ft->FontSize, FLT_MAX, 0, s); }
float fill(float minH) { return std::max(minH, ImGui::GetContentRegionAvail().y - S(6)); }  // the height left for a window, at least minH
void box(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 c, float t) {  // crisp outline drawn inside a..b
    dl->AddRectFilled(a, {b.x, a.y + t}, c), dl->AddRectFilled({a.x, b.y - t}, b, c);
    dl->AddRectFilled({a.x, a.y + t}, {a.x + t, b.y - t}, c), dl->AddRectFilled({b.x - t, a.y + t}, {b.x, b.y - t}, c);
}

void outline(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 c, float t, float r) {  // rounded outline drawn inside a..b
    dl->AddRect({a.x + t / 2, a.y + t / 2}, {b.x - t / 2, b.y - t / 2}, c, r, 0, t);
}

// Skill radar: rings at 25/50/75/100, the 50 ring (= your usual) darker; labels sit outside the outer ring.
void radar(const Report& r, float size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos(), c{p.x + size / 2, p.y + size / 2};
    float lh = font(LABEL)->FontSize + font(TITLE)->FontSize, R = std::max(S(48), size / 2 - lh - S(12));
    auto pt = [&](int i, float v) { float a = -3.14159265f / 2 + i * 2 * 3.14159265f / (float)NSKILL; return ImVec2{c.x + std::cos(a) * R * v, c.y + std::sin(a) * R * v}; };
    for (float ring : {0.25f, 0.5f, 0.75f, 1.f}) {
        ImVec2 q[NSKILL];
        for (int i = 0; i < NSKILL; i++) q[i] = pt(i, ring);
        dl->AddPolyline(q, NSKILL, ring == 0.5f ? cMuted : cRule, ImDrawFlags_Closed, ring == 0.5f ? S(1.5f) : S(1));
    }
    ImVec2 v[NSKILL];
    for (int i = 0; i < NSKILL; i++) dl->AddLine(c, pt(i, 1), cRule), v[i] = pt(i, std::max(0.03f, r.radar[i] / 100));
    for (int i = 0; i < NSKILL; i++) dl->AddTriangleFilled(c, v[i], v[(i + 1) % NSKILL], alpha(cYellow, 190));
    dl->AddPolyline(v, NSKILL, cInk, ImDrawFlags_Closed, S(2));
    for (int i = 0; i < NSKILL; i++) dl->AddRectFilled({v[i].x - S(3), v[i].y - S(3)}, {v[i].x + S(3), v[i].y + S(3)}, cInk);
    ImFont *fl = font(LABEL), *fv = font(TITLE);
    for (int i = 0; i < NSKILL; i++) {
        float a = -3.14159265f / 2 + i * 2 * 3.14159265f / (float)NSKILL, ca = std::cos(a), sa = std::sin(a);
        // colour plus an arrow, so better/worse than usual doesn't rely on colour alone
        std::string val = sf("%s%.0f", r.radar[i] < 45 ? "\xE2\x86\x93" : r.radar[i] > 55 ? "\xE2\x86\x91" : "", r.radar[i]);
        ImVec2 ls = textSize(LABEL, kSkillName[i]), vs = textSize(TITLE, val.c_str());
        float bw = std::max(ls.x, vs.x), bh = ls.y + vs.y;
        // push the label block out until its nearest edge clears the ring
        ImVec2 q{c.x + ca * (R + S(10)) + ca * bw / 2, c.y + sa * (R + S(8)) + sa * bh / 2};
        ImU32 col = r.radar[i] < 45 ? cBad : r.radar[i] > 55 ? cGood : cInk;
        float y = q.y - bh / 2;
        dl->AddText(fl, fl->FontSize, {q.x - ls.x / 2, y}, cMuted, kSkillName[i]);
        dl->AddText(fv, fv->FontSize, {q.x - vs.x / 2, y + ls.y}, col, val.c_str());
    }
    ImGui::Dummy({size, size});
}

// Bar chart filling the width; mark = index of a reference line.
void bars(const float* v, int n, float h, int mark = -1) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x, mx = 0;
    ImGui::Dummy({w, h});
    for (int i = 0; i < n; i++) mx = std::max(mx, v[i]);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (mx > 0) {
        float bw = w / n, g = bw >= S(8) ? S(2) : 0;
        for (int i = 0; i < n; i++) {
            float bh = v[i] / mx * (h - S(2)), x = std::floor(p.x + i * bw + g / 2);
            if (bh >= 1) dl->AddRectFilled({x, p.y + h - bh}, {std::floor(p.x + (i + 1) * bw - g / 2), p.y + h}, cMuted);
        }
        if (mark >= 0) dl->AddRectFilled({p.x + mark * bw - S(1), p.y}, {p.x + mark * bw + S(1), p.y + h}, cOrange);
    }
    dl->AddRectFilled({p.x, p.y + h - S(2)}, {p.x + w, p.y + h}, cInk);
}
void axis(const char* l, const char* m, const char* r) {  // three captions under a chart
    float w = ImGui::GetContentRegionAvail().x, x = ImGui::GetCursorPosX();
    txt(LABEL, cMuted, l);
    if (*m) { ImGui::SameLine(x + (w - textSize(LABEL, m).x) / 2); txt(LABEL, cMuted, m); }
    ImGui::SameLine(x + w - textSize(LABEL, r).x);
    txt(LABEL, cMuted, r);
}

// Line chart: 2 px ink line over a flat yellow fill, min/max on the left.
void line(const std::vector<float>& v, float h, const char* unit) {
    if (v.size() < 2) { txt(BODY, cMuted, "Needs at least two days of history."); return; }
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x, mn = *std::min_element(v.begin(), v.end()), mx = *std::max_element(v.begin(), v.end());
    ImGui::Dummy({w, h});
    if (mx - mn < 1e-3f) mx = mn + 1;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float lx = S(56), cw = w - lx, top = p.y + S(8), bot = p.y + h;
    std::vector<ImVec2> pts(v.size());
    for (size_t i = 0; i < v.size(); i++) pts[i] = {p.x + lx + cw * i / (v.size() - 1), bot - (v[i] - mn) / (mx - mn) * (bot - top)};
    for (size_t i = 0; i + 1 < pts.size(); i++)
        dl->AddQuadFilled(pts[i], pts[i + 1], {pts[i + 1].x, bot}, {pts[i].x, bot}, alpha(cYellow, 150));
    dl->AddRectFilled({p.x + lx, bot - S(2)}, {p.x + w, bot}, cInk);
    dl->AddPolyline(pts.data(), (int)pts.size(), cInk, 0, S(2));
    ImFont* f = font(LABEL);
    dl->AddText(f, f->FontSize, {p.x, top - f->FontSize / 2}, cMuted, sf("%.*f%s", *unit ? 1 : 0, mx, unit).c_str());
    dl->AddText(f, f->FontSize, {p.x, bot - f->FontSize}, cMuted, sf("%.*f%s", *unit ? 1 : 0, mn, unit).c_str());
}

// Headline block: label, then one big number on a sticker (yellow, orange when it's bad news), then a note.
void headline(const char* label, const std::string& value, bool bad, const std::string& note) {
    txt(LABEL, cMuted, label);
    ImGui::Dummy({0, S(4)});
    ImVec2 p = ImGui::GetCursorScreenPos(), ts = textSize(HEAD, value.c_str());
    ImVec2 b{p.x + ts.x + S(24), p.y + ts.y + S(8)};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled({p.x + S(4), p.y + S(4)}, {b.x + S(4), b.y + S(4)}, cInk, S(4));
    dl->AddRectFilled(p, b, bad ? cOrange : cYellow, S(4));
    outline(dl, p, b, cInk, S(2), S(4));
    dl->AddText(font(HEAD), font(HEAD)->FontSize, {p.x + S(12), p.y + S(4)}, cInk, value.c_str());
    ImGui::Dummy({b.x - p.x + S(4), b.y - p.y + S(4)});
    if (!note.empty()) ImGui::Dummy({0, S(4)}), txt(BODY, cMuted, note, true);
}
// One fact row: label on the left, value (Bricolage) from 55% of the width, an optional note after it; texts share a baseline.
// When the label doesn't fit in front of the value, the value goes on the next line.
void fact(const char* label, const std::string& value, ImU32 vc = cInk, const std::string& note = "") {
    float w = ImGui::GetContentRegionAvail().x, x = ImGui::GetCursorPosX(), y = ImGui::GetCursorPosY();
    float big = font(TITLE)->FontSize, vx = std::floor(w * 0.55f);
    bool stack = textSize(BODY, label).x + S(16) > vx;
    if (stack) {
        txt(BODY, cInk, label, true);
        x = ImGui::GetCursorPosX(), y = ImGui::GetCursorPosY(), vx = 0;
    }
    float base = y + big * 0.8f;  // approximate baseline of the value text
    auto at = [&](float px, int f, ImU32 c, const std::string& s) { ImGui::SetCursorPos({px, base - font(f)->FontSize * 0.8f}); txt(f, c, s); };
    if (!stack) at(x, BODY, cInk, label);
    at(x + vx, TITLE, vc, value);
    float after = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + S(16);
    if (!note.empty() && after + textSize(LABEL, note.c_str()).x <= x + w) at(after, LABEL, cMuted, note);
    else if (!note.empty()) ImGui::SetCursorPos({x, y + big + S(2)}), txt(LABEL, cMuted, note, true), y = ImGui::GetCursorPosY() - big - S(8);
    ImGui::SetCursorPos({x, y + big + S(8)});
    rule();
}
// Top block of a tab, in a titled window: headline on the left (34%), facts on the right; one column when narrow.
bool gTopStack = false;
void top(const char* id, const char* title) {
    window(id, title, cOrange, 0, 0, true);
    gTopStack = ImGui::GetContentRegionAvail().x < S(720);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {S(12), 0});
    ImGui::BeginTable("top", gTopStack ? 1 : 2);
    if (!gTopStack) {
        ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthStretch, 0.34f);
        ImGui::TableSetupColumn("r", ImGuiTableColumnFlags_WidthStretch, 0.66f);
    }
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
}
void split() {
    if (gTopStack) ImGui::TableNextRow(0, 0), ImGui::TableNextColumn(), ImGui::Dummy({0, S(8)});
    else ImGui::TableNextColumn();
}
void endTop() { ImGui::EndTable(); ImGui::PopStyleVar(); endWindow(); gap(); }

// A small numbered square, like a key cap, used for ranked lists.
void badge(int n, int f = TITLE) {
    std::string s = std::to_string(n);
    float side = font(f)->FontSize + S(8);
    ImVec2 p = ImGui::GetCursorScreenPos(), ts = textSize(f, s.c_str());
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled({p.x + S(3), p.y + S(3)}, {p.x + side + S(3), p.y + side + S(3)}, cInk, S(4));
    dl->AddRectFilled(p, {p.x + side, p.y + side}, cYellow, S(4));
    outline(dl, p, {p.x + side, p.y + side}, cInk, S(2), S(4));
    dl->AddText(font(f), font(f)->FontSize, {p.x + (side - ts.x) / 2, p.y + (side - ts.y) / 2}, cInk, s.c_str());
    ImGui::Dummy({side + S(3), side + S(3)});
}

std::string modStr(int m) {
    using namespace mod;
    static const std::pair<int, const char*> t[] = {{NF, "NF"}, {EZ, "EZ"}, {HD, "HD"}, {HR, "HR"}, {SD, "SD"}, {NC, "NC"}, {DT, "DT"}, {HT, "HT"}, {FL, "FL"}, {SO, "SO"}, {PF, "PF"}};
    std::string s;
    for (auto [b, n] : t) if ((m & b) && !(b == DT && (m & NC)) && !(b == SD && (m & PF))) s += n;
    return s.empty() ? "no mods" : s;
}
std::string dateStr(int64_t ms) {
    time_t t = (time_t)(ms / 1000);
    tm l{};
    localtime_s(&l, &t);
    char b[32];
    strftime(b, sizeof b, "%Y-%m-%d %H:%M", &l);
    return b;
}
void open(const std::wstring& what, const std::wstring& args = L"") { ShellExecuteW(nullptr, L"open", what.c_str(), args.empty() ? nullptr : args.c_str(), nullptr, SW_SHOWNORMAL); }

void workItem(int i, const Weakness& wk, size_t plays) {
    ImGui::BeginGroup();
    badge(i + 1);
    ImGui::EndGroup();
    ImGui::SameLine(0, S(12));
    ImGui::BeginGroup();
    txt(TITLE, cInk, kSkillName[wk.s]);
    txt(LABEL, cMuted, sf("costs you about %.1f pp per play", wk.pp / std::max<size_t>(1, plays)));
    txt(BODY, cInk, wk.metric, true);
    ImGui::Dummy({0, S(4)});
    txt(LABEL, cGood, "What to do");
    txt(BODY, cInk, wk.fix, true);
    ImGui::EndGroup();
}

void tabOverview(App& a, const Recent& rc) {
    const Report& r = a.rep;
    top("top", "Coach verdict");
    txt(TITLE, cInk, r.verdict, true);
    ImGui::Dummy({0, S(8)});
    txt(BODY, cMuted, sf("Based on your last %zu plays. Older plays are your baseline.", rc.plays), true);
    split();
    fact("Timing steadiness (UR, lower is steadier)", sf("%.0f", rc.err.ur()), cInk, trendWord(r.trend[0]));
    fact("Average accuracy", sf("%.2f%%", 100 * rc.accSum / std::max<size_t>(1, rc.plays)), cInk, sf("%.0f%% full combos", pct(rc.fcs, rc.plays)));
    fact("Jump misses this week", trendWord(r.trend[1]) + 4, trendCol(r.trend[1]));
    fact("Stream misses this week", trendWord(r.trend[2]) + 4, trendCol(r.trend[2]));
    endTop();

    auto skills = [&](float w, float h) {
        window("radar", "Your skills", cWell, w, h);
        txt(BODY, cMuted, r.needHistory ? "Needs older plays to compare with, so every skill shows 50 for now."
                                        : "Compared with your usual: 50 = normal for you, higher = better than usual.", true);
        float avail = ImGui::GetContentRegionAvail().x, rs = std::min(avail, ImGui::GetContentRegionAvail().y);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - rs) / 2);
        radar(r, rs);
        endWindow();
    };
    auto work = [&](float h) {
        window("work", "What to work on", cWell, 0, h, h <= 0);
        if (!r.nTop) txt(BODY, cInk, "Nothing is costing you much right now. Try slightly harder maps.", true);
        for (int i = 0; i < r.nTop; i++) {
            if (i) ImGui::Dummy({0, S(4)}), rule(), ImGui::Dummy({0, S(4)});
            workItem(i, r.top[i], rc.plays);
        }
        endWindow();
    };
    if (ImGui::GetContentRegionAvail().x < S(900)) {  // narrow: skills on top, the list under it, and the page scrolls
        skills(0, S(460));
        gap();
        work(0);
        return;
    }
    float h = fill(S(400)), lw = std::floor(std::max(S(400), ImGui::GetContentRegionAvail().x * 0.40f));
    skills(lw, h);
    ImGui::SameLine(0, S(16));
    work(h);
}

void tabTiming(const Recent& rc) {
    double n = rc.err.n, m = rc.err.mean();
    int keys = rc.keyHits[0] + rc.keyHits[1];
    top("top", "Timing");
    headline("Timing steadiness", sf("%.0f", rc.err.ur()), false, "UR: how spread out your clicks are. Lower is steadier; top players are under 100.");
    split();
    fact("On average you click", std::fabs(m) < 2 ? "right on time" : sf("%.0f ms %s", std::fabs(m), m < 0 ? "early" : "late"));
    fact("Clicks within 15 ms of perfect", sf("%.0f%%", pct(rc.within[0], n)));
    fact("Clicks within 30 ms", sf("%.0f%%", pct(rc.within[1], n)));
    fact("Clicks within 50 ms", sf("%.0f%%", pct(rc.within[2], n)));
    fact("Clicks with key 1 / key 2", sf("%.0f%% / %.0f%%", pct(rc.keyHits[0], keys), pct(rc.keyHits[1], keys)), cInk,
         sf("steadiness %.0f / %.0f", rc.errKey[0].ur(), rc.errKey[1].ur()));
    endTop();
    window("hist", "When your clicks land", cWell, 0, fill(S(240)));
    txt(BODY, cMuted, "Each bar is 2 ms. The orange line is perfect timing; a narrow, tall pile means steady timing.", true);
    ImGui::Dummy({0, S(8)});
    bars(rc.hist, 100, std::max(S(120), ImGui::GetContentRegionAvail().y - S(32)), 50);
    axis("100 ms early", "perfect", "100 ms late");
    endWindow();
}

void tabAim(const Recent& rc) {
    int am = rc.under + rc.over;
    static const char* names[] = {"Close notes (under 1 circle width apart)", "Short moves (1 to 2 widths)", "Jumps (2 to 3.5 widths)", "Big jumps (over 3.5 widths)"};
    top("top", "Aim");
    headline("Aim misses that stop short", sf("%.0f%%", pct(rc.under, am)), false,
             sf("%d times you stopped short of the circle, %d times you went past it.", rc.under, rc.over));
    split();
    for (int b = 0; b < 4; b++)
        fact(names[b], sf("%.2f%% missed", pct(rc.bMiss[b], rc.bObj[b])), cInk, sf("of %d notes", rc.bObj[b]));
    endTop();
    float h = fill(S(300)), half = std::floor(std::max(S(300), (ImGui::GetContentRegionAvail().x - S(16)) * 0.4f));
    window("quad", "Misses by screen area", cWell, half, h);
    {
        static const char* qn[] = {"Top left", "Top right", "Bottom left", "Bottom right"};
        double mx = 0;
        for (int q = 0; q < 4; q++) mx = std::max(mx, pct(rc.qMiss[q], rc.qObj[q]));
        ImVec2 p = ImGui::GetCursorScreenPos();
        float tw = std::floor((ImGui::GetContentRegionAvail().x - S(8)) / 2), th = std::floor((ImGui::GetContentRegionAvail().y - S(8)) / 2);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int q = 0; q < 4; q++) {
            double m = pct(rc.qMiss[q], rc.qObj[q]);
            ImVec2 a{p.x + (q % 2) * (tw + S(8)), p.y + (q / 2) * (th + S(8))}, b{a.x + tw, a.y + th};
            dl->AddRectFilled(a, b, cWell);
            dl->AddRectFilled(a, b, alpha(cOrange, mx > 0 ? (int)(200 * m / mx) : 0));
            box(dl, a, b, cInk, S(2));
            dl->AddText(font(LABEL), font(LABEL)->FontSize, {a.x + S(12), a.y + S(10)}, cInk, qn[q]);
            dl->AddText(font(TITLE), font(TITLE)->FontSize, {a.x + S(12), a.y + S(30)}, cInk, sf("%.2f%%", m).c_str());
        }
    }
    endWindow();
    ImGui::SameLine(0, S(16));
    window("jump", "How far you move between notes", cWell, 0, h);
    txt(BODY, cMuted, "Most of your notes fall in the tall bars; misses usually come from the long moves on the right.", true);
    ImGui::Dummy({0, S(8)});
    bars(rc.jump, 24, std::max(S(96), ImGui::GetContentRegionAvail().y - S(32)));
    axis("0", "3 circle widths", "6 widths");
    endWindow();
}

void tabSpeed(const Recent& rc, float streamBpm) {
    double ov = pct(rc.zOv[0], rc.zPairs[0]);
    top("top", "Speed");
    headline("Finger lock on fast notes", sf("%.0f%%", ov), ov > 20, "How often you still hold the last key when you press the next one. Under 10% is good.");
    split();
    fact("Comfortable stream speed", streamBpm > 0 ? sf("%.0f BPM", streamBpm) : std::string("not enough streams"), cInk, "streams you hit without a miss");
    fact("Steadiness on notes 100\xE2\x80\x93""130 ms apart", sf("%.0f", rc.zone[1].ur()), cInk, "usually the hardest speed");
    fact("Longest run without a pause", sf("%d notes", rc.stamNotes), cInk, sf("%.0f s at %.1f notes per second", rc.stamSec, rc.stamNps));
    endTop();
    window("streams", "Streams (4 or more quick notes in a row)", cWell, 0, 0, true);
    const char* bands[] = {"under 150 BPM", "150\xE2\x80\x93""170 BPM", "170\xE2\x80\x93""190 BPM", "190\xE2\x80\x93""210 BPM", "over 210 BPM"};
    if (table("st", 7)) {
        for (auto h : {"Speed", "Streams", "Notes", "Missed", "Switching fingers", "First 4 notes", "Later notes"}) ImGui::TableSetupColumn(h);
        headerRow();
        for (int i = 0; i < 5; i++) {
            auto& b = rc.band[i];
            if (!b.segs) continue;
            double early = b.eN ? b.early / b.eN : 0, late = b.lN ? b.late / b.lN : 0;
            auto when = [](double ms) { return std::fabs(ms) < 2 ? std::string("on time") : sf("%.0f ms %s", std::fabs(ms), ms < 0 ? "early" : "late"); };
            ImGui::TableNextRow();
            cell(cInk, bands[i]), cell(cInk, sf("%d", b.segs)), cell(cInk, sf("%d", b.notes)), cell(cInk, sf("%.1f%%", pct(b.miss, b.notes)));
            cell(cInk, sf("%.0f%%", pct(b.alt, b.alt + b.dbl))), cell(cInk, when(early));
            cell(late < -8 ? cBad : cInk, when(late) + (late < -8 ? ", speeding up" : ""));
        }
        ImGui::EndTable();
    }
    endWindow();
    gap();
    window("zones", "Tapping at different speeds", cWell, 0, 0, true);
    const char* zones[] = {"Very fast (under 100 ms apart)", "Fast (100\xE2\x80\x93""130 ms)", "Normal (130\xE2\x80\x93""260 ms)", "Slow (over 260 ms)"};
    if (table("zt", 5)) {
        for (auto h : {"Notes apart", "Clicks", "Steadiness", "Switching fingers", "Holding a key too long"}) ImGui::TableSetupColumn(h);
        headerRow();
        for (int z = 0; z < 4; z++) {
            double o = pct(rc.zOv[z], rc.zPairs[z]);
            ImGui::TableNextRow();
            cell(cInk, zones[z]), cell(cInk, sf("%.0f", rc.zone[z].n)), cell(cInk, sf("%.0f", rc.zone[z].ur()));
            cell(cInk, sf("%.0f%%", pct(rc.zAlt[z], rc.zPairs[z]))), cell(o > 20 ? cBad : cInk, sf("%.0f%%%s", o, o > 20 ? ", finger lock" : ""));
        }
        ImGui::EndTable();
    }
    endWindow();
}

void tabReading(const Recent& rc) {
    top("top", "Reading");
    headline("First note after a pause", sf("%.2f%%", pct(rc.brkMiss, rc.brkObj)), false,
             sf("missed (%d of %d). A pause is more than half a second without notes.", rc.brkMiss, rc.brkObj));
    split();
    fact("Notes in the middle of the music", sf("%.2f%% missed", pct(rc.flowMiss, rc.flowObj)), cInk, sf("%d of %d", rc.flowMiss, rc.flowObj));
    fact("Circles that appear slowly (AR below 8)", sf("%.2f%% missed", pct(rc.arMiss[0], rc.arObj[0])), cInk, sf("%d notes", rc.arObj[0]));
    fact("Circles that appear quickly (AR 8 or more)", sf("%.2f%% missed", pct(rc.arMiss[1], rc.arObj[1])), cInk, sf("%d notes", rc.arObj[1]));
    endTop();
    window("note", "How to read this", cWell, 0, 0, true);
    txt(BODY, cInk, "Reading means seeing where the next circle is in time. If you miss more right after pauses, or more when circles appear "
                    "quickly, the problem is reading, not aim. Mods count: HR and DT make circles appear faster, EZ and HT slower.", true);
    endWindow();
}

std::string whereStr(const ObjRes& o, float beat) {
    float frac = o.gap / beat;
    std::string s = o.afterBreak ? "after a pause" : frac < 0.3f ? "fast notes" : frac < 0.6f ? "normal notes" : "slow notes";
    if (o.inStream) s += ", in a stream";
    if (o.spacing > 4) s += sf(", jump of %.1f widths", o.spacing / 2);
    if (o.slider) s += ", slider start";
    return s;
}

void missList(const PlayStats& p, const std::vector<ObjRes>& objs, float h, int64_t key) {
    if (objs.empty()) { txt(BODY, cMuted, "The replay or its map is no longer on your PC, so the note-by-note list is unavailable."); return; }
    float beat = p.bpm > 0 ? 60000 / p.bpm : 500;
    static std::vector<int> miss;  // indexes of the misses; only the visible rows get explained each frame
    static int64_t missFor = -1;
    if (missFor != key) {
        miss.clear();
        for (size_t i = 0; i < objs.size(); i++) if (!objs[i].judge) miss.push_back((int)i);
        missFor = key;
    }
    if (table("misses", 4, h, ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, S(104));
        ImGui::TableSetupColumn("Note", ImGuiTableColumnFlags_WidthFixed, S(72));
        ImGui::TableSetupColumn("What happened", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Where", ImGuiTableColumnFlags_WidthStretch, 1);
        headerRow();
        ImGuiListClipper clip;
        clip.Begin((int)miss.size());
        while (clip.Step())
            for (int row = clip.DisplayStart; row < clip.DisplayEnd; row++) {
                auto& o = objs[miss[row]];
                int mt = (int)(o.t * p.rate);
                ImGui::TableNextRow();
                cell(cInk, sf("%d:%02d.%03d", mt / 60000, mt / 1000 % 60, mt % 1000)), cell(cMuted, sf("#%d", miss[row] + 1));
                cell(cInk, explainMiss(o, beat).title), cell(cMuted, whereStr(o, beat));
            }
        ImGui::EndTable();
    }
}

void tabPlays(App& a) {
    auto& ps = a.res.plays;
    static const char* cols[] = {"Date", "Map", "Stars", "Mods", "Accuracy", "Misses", "Steadiness"};
    static std::vector<int> order;
    static int orderFor = -1, sortCol = 0, sortedCol = -1;
    static bool asc = false, sortedAsc = true;
    bool sel = a.selPlay >= 0 && a.selPlay < (int)ps.size();
    float total = std::max(ImGui::GetContentRegionAvail().y, sel ? S(680) : S(300)), h = sel ? std::max(S(250), std::floor(total * 0.40f)) : total - S(6);
    window("list", "Your plays", cWell, 0, h, false, sf("%zu plays", ps.size()));
    ImGui::AlignTextToFramePadding();
    txt(LABEL, cMuted, "Sort by");
    ImGui::SameLine(0, S(8));
    ImGui::SetNextItemWidth(S(176));
    ImGui::Combo("##sort", &sortCol, cols, 7);
    ImGui::SameLine(0, S(8));
    if (button2(asc ? "Lowest first \xE2\x86\x91" : "Highest first \xE2\x86\x93")) asc = !asc;
    if (orderFor != a.gen || sortedCol != sortCol || sortedAsc != asc) {
        order.resize(ps.size());
        for (size_t i = 0; i < ps.size(); i++) order[i] = (int)i;
        auto key = [&](int i) -> double {
            auto& p = ps[i];
            switch (sortCol) { case 2: return p.stars; case 3: return p.mods; case 4: return p.acc; case 5: return p.gmiss; case 6: return p.err.ur(); default: return (double)p.when; }
        };
        if (sortCol == 1) std::stable_sort(order.begin(), order.end(), [&](int x, int y) { return asc ? ps[x].title < ps[y].title : ps[x].title > ps[y].title; });
        else std::stable_sort(order.begin(), order.end(), [&](int x, int y) { return asc ? key(x) < key(y) : key(x) > key(y); });
        orderFor = a.gen, sortedCol = sortCol, sortedAsc = asc;
    }
    ImGui::Dummy({0, S(4)});
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {S(8), 0});  // 44 px rows sized explicitly
    // narrow windows drop the least important columns instead of squeezing the map title
    float tw = ImGui::GetContentRegionAvail().x;
    bool showMods = tw > S(820), showUr = tw > S(720);
    if (table("plays", 7, ImGui::GetContentRegionAvail().y, ImGuiTableFlags_ScrollY | ImGuiTableFlags_Hideable)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(cols[0], ImGuiTableColumnFlags_WidthFixed, S(152));
        ImGui::TableSetupColumn(cols[1], ImGuiTableColumnFlags_WidthStretch);
        for (int c = 2; c < 7; c++) ImGui::TableSetupColumn(cols[c], ImGuiTableColumnFlags_WidthFixed, S(c == 3 ? 96 : c == 2 || c == 5 ? 80 : 104));
        ImGui::TableSetColumnEnabled(3, showMods);
        ImGui::TableSetColumnEnabled(6, showUr);
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers, S(40));
        for (int c = 0; c < 7; c++) {
            if (!ImGui::TableSetColumnIndex(c)) continue;
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (S(40) - font(LABEL)->FontSize) / 2);
            txt(LABEL, c == sortCol ? cInk : cMuted, c == sortCol ? sf("%s %s", cols[c], asc ? "\xE2\x86\x91" : "\xE2\x86\x93") : cols[c]);
        }
        ImGuiListClipper clip;
        clip.Begin((int)order.size(), S(44));
        while (clip.Step())
            for (int row = clip.DisplayStart; row < clip.DisplayEnd; row++) {
                int i = order[row];
                auto& p = ps[i];
                ImGui::TableNextRow(0, S(44));
                ImGui::TableNextColumn();
                bool isSel = a.selPlay == i;
                if (ImGui::Selectable((dateStr(p.when) + "##r" + std::to_string(i)).c_str(), isSel, ImGuiSelectableFlags_SpanAllColumns, {0, S(44)})) a.selPlay = i;
                if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                auto col = [](int f, ImU32 c, const std::string& s) { if (ImGui::TableNextColumn()) ImGui::AlignTextToFramePadding(), txt(f, c, s); };
                col(BODY, cInk, p.title), col(BODY, p.stars > 0 ? cInk : cMuted, p.stars > 0 ? sf("%.2f", p.stars) : "?"), col(BODY, cMuted, modStr(p.mods));
                col(BODY, cInk, sf("%.2f%%", 100 * p.acc)), col(BODY, p.gmiss ? cInk : cGood, p.gmiss ? sf("%d", p.gmiss) : "FC"), col(BODY, cInk, sf("%.0f", p.err.ur()));
            }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    endWindow();
    if (!sel) return;
    gap();
    auto& p = ps[a.selPlay];
    window("detail", p.title.c_str(), cWell, 0, fill(S(360)));
    txt(BODY, cMuted, sf("%s  \xC2\xB7  %s  \xC2\xB7  %.2f%% accuracy  \xC2\xB7  %d misses  \xC2\xB7  steadiness %.0f  \xC2\xB7  best combo %d",
                         dateStr(p.when).c_str(), modStr(p.mods).c_str(), 100 * p.acc, p.gmiss, p.err.ur(), p.combo), true);
    ImGui::Dummy({0, S(8)});
    if (button1("Watch replay with the coach")) a.openViewer();
    const char* hint = "Plays it back with the music and stops at every mistake to explain it.";
    flow(textSize(BODY, hint).x, S(16));
    ImGui::AlignTextToFramePadding();
    txt(BODY, cMuted, hint);
    ImGui::Dummy({0, S(4)});
    float rest = ImGui::GetContentRegionAvail().y;
    missList(p, a.selectedObjs(), std::max(S(132), rest - S(48) - S(8) - font(LABEL)->FontSize - 3 * S(8) - S(4)), (int64_t)a.gen << 32 | (uint32_t)a.selPlay);
    ImGui::Dummy({0, S(4)});
    txt(LABEL, cMuted, "When your clicks landed in this play (orange line = perfect)");
    float hist[100];
    for (int k = 0; k < 100; k++) hist[k] = (float)p.hist[k];
    bars(hist, 100, S(48), 50);
    endWindow();
}

void tabMaps(App& a) {
    window("ctl", "Practice maps from your Songs folder", cOrange, 0, 0, true);
    txt(BODY, cMuted, "Five maps that train the skill you pick. Maps you haven't played much lately come first.", true);
    ImGui::Dummy({0, S(8)});
    ImGui::BeginGroup();
    ImGui::AlignTextToFramePadding();
    txt(LABEL, cMuted, "Stars");
    ImGui::SameLine(0, S(8));
    ImGui::SetNextItemWidth(S(280));
    ImGui::DragFloatRange2("##stars", &a.lo, &a.hi, 0.05f, 1, 10, "%.2f\xE2\x98\x85");
    ImGui::EndGroup();
    flow(textSize(LABEL, "Train").x + S(8 + 216), S(24));
    ImGui::BeginGroup();
    ImGui::AlignTextToFramePadding();
    txt(LABEL, cMuted, "Train");
    ImGui::SameLine(0, S(8));
    const char* items[NSKILL + 1] = {"My top weakness"};
    for (int i = 0; i < NSKILL; i++) items[i + 1] = kSkillName[i];
    int cur = a.trainSkill + 1;
    ImGui::SetNextItemWidth(S(216));
    if (ImGui::Combo("##train", &cur, items, NSKILL + 1)) a.trainSkill = cur - 1;
    ImGui::EndGroup();
    flow(buttonW("Find maps"), S(24));
    ImGui::BeginDisabled(a.busy);
    if (button1("Find maps")) a.startSuggest();
    ImGui::EndDisabled();
    if (a.busy) { ImGui::SameLine(0, S(16)); ImGui::AlignTextToFramePadding(); txt(BODY, cMuted, "searching..."); }
    endWindow();
    gap();
    window("list", "Suggestions", cWell, 0, 0, true);
    if (a.sugg.empty()) txt(BODY, cInk, "No maps in this star range. Widen the range or download a few maps.", true);
    for (size_t i = 0; i < a.sugg.size(); i++) {
        auto& s = a.sugg[i];
        ImGui::PushID((int)i);
        if (i) ImGui::Dummy({0, S(4)}), rule(), ImGui::Dummy({0, S(4)});
        ImGui::BeginGroup();
        badge((int)i + 1);
        ImGui::EndGroup();
        ImGui::SameLine(0, S(16));
        ImGui::BeginGroup();
        txt(TITLE, cInk, s.map->title);
        txt(BODY, cMuted, s.why, true);
        ImGui::Dummy({0, S(4)});
        if (s.map->bid > 0) {
            if (button1("Open map page")) open(L"https://osu.ppy.sh/b/" + std::to_wstring(s.map->bid));
            ImGui::SameLine(0, S(12));
        }
        if (button2("Show in folder")) open(L"explorer.exe", L"/select,\"" + s.map->path + L"\"");
        ImGui::EndGroup();
        ImGui::PopID();
    }
    endWindow();
}

void tabProgress(const Report& r, int gen) {
    static std::vector<float> ur, jm, sm;  // one point per day, rebuilt once per scan
    static int builtFor = -1, nDays = 0, first = 0, last = 0;
    if (builtFor != gen) {
        std::map<int, Day> byDate;
        for (auto& d : r.days) {
            Day& t = byDate[d.date];
            t.date = d.date, t.plays += d.plays, t.es += d.es, t.ess += d.ess, t.en += d.en;
            for (int s = 0; s < NSKILL; s++) t.obj[s] += d.obj[s], t.miss[s] += d.miss[s];
        }
        ur.clear(), jm.clear(), sm.clear();
        for (auto& [k, d] : byDate) {
            Acc a; a.n = d.en, a.s = d.es, a.ss = d.ess;
            ur.push_back((float)a.ur());
            jm.push_back((float)pct(d.miss[JUMPS], d.obj[JUMPS]));
            sm.push_back((float)pct(d.miss[STREAMS], d.obj[STREAMS]));
        }
        nDays = (int)byDate.size(), first = nDays ? byDate.begin()->first : 0, last = nDays ? byDate.rbegin()->first : 0;
        builtFor = gen;
    }
    window("v", "Coach verdict", cOrange, 0, 0, true);
    txt(TITLE, cInk, r.verdict, true);
    txt(BODY, cMuted, !nDays ? std::string("No history yet.")
                             : sf("%d days of history, from %d to %d. Saved in history.json, so it stays even if you delete maps.", nDays, first, last), true);
    endWindow();
    gap();
    const char* names[] = {"Timing steadiness by day (lower is better)", "Jump misses by day", "Stream misses by day"};
    const char* units[] = {"", "%", "%"};
    std::vector<float>* series[] = {&ur, &jm, &sm};
    float h = std::max(S(150), std::floor((ImGui::GetContentRegionAvail().y - 2 * S(16) - S(6)) / 3));
    for (int i = 0; i < 3; i++) {
        if (i) gap();
        ImGui::PushID(i);
        window("c", names[i], cWell, 0, h, false, sf("%s this week", trendWord(r.trend[i])));
        line(*series[i], std::max(S(48), ImGui::GetContentRegionAvail().y), units[i]);
        endWindow();
        ImGui::PopID();
    }
}

// The menu bar across the top: logo, tabs (the current one inverted), and the folder buttons on the right.
// When it all doesn't fit on one row the tabs move to a second row. Returns its height.
float menuBar(App& a, int& tab, bool ready) {
    static const char* kTabs[] = {"Overview", "Timing", "Aim", "Speed", "Reading", "Plays", "Practice maps", "Progress"};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    float W = ImGui::GetWindowWidth(), pad = S(24), padY = S(10), ctl = S(44), tabPad = S(14);
    float logoW = S(34) + textSize(BAR, "osu-coach").x, tabsW = 0;
    for (auto t : kTabs) tabsW += textSize(BODY, t).x + 2 * tabPad;
    bool viewing = a.viewing && a.viewer;
    float btnW = buttonW("Refresh") + S(12) + buttonW("Change folder");
    bool two = ready && !viewing && pad + logoW + S(32) + tabsW + S(24) + btnW + pad > W;
    float h = padY + ctl + padY + (two ? ctl + padY : 0);
    dl->AddRectFilled(wp, {wp.x + W, wp.y + h}, cWin);
    dl->AddRectFilled({wp.x, wp.y + h - S(2)}, {wp.x + W, wp.y + h}, cInk);
    // logo: a hit circle with its approach ring
    ImVec2 c{wp.x + pad + S(13), wp.y + padY + ctl / 2};
    dl->AddCircle(c, S(13), cOrange, 32, S(2.5f));
    dl->AddCircleFilled(c, S(8), cYellow, 24);
    dl->AddCircle(c, S(8), cInk, 24, S(2));
    ImGui::SetCursorPos({pad + S(34), padY + (ctl - font(BAR)->FontSize) / 2});
    txt(BAR, cInk, "osu-coach");

    if (viewing) {
        ImGui::SetCursorPos({pad + logoW + S(32), padY});
        if (button2("\xE2\x86\x90 Back to plays")) { a.viewer->setPlaying(false); a.viewing = false; }
        float x = ImGui::GetItemRectMax().x - wp.x + S(20);
        ImGui::SetCursorPos({x, padY + (ctl - font(BAR)->FontSize) / 2});
        ImGui::PushClipRect({wp.x + x, wp.y}, {wp.x + W - pad, wp.y + h}, true);
        txt(BAR, cInk, a.viewer->play.title);
        ImGui::PopClipRect();
        return h;
    }
    if (ready) {
        float x = two ? pad : pad + logoW + S(32), y = two ? padY + ctl + padY : padY;
        for (int i = 0; i < 8; i++) {
            float tw = textSize(BODY, kTabs[i]).x + 2 * tabPad;
            ImGui::SetCursorPos({x, y});
            ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton(kTabs[i], {tw, ctl})) tab = i;
            bool on = tab == i, hov = ImGui::IsItemHovered();
            if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (on || hov) dl->AddRectFilled(p, {p.x + tw, p.y + ctl}, on ? cInk : cWell, S(4));
            ImVec2 ts = textSize(BODY, kTabs[i]);
            dl->AddText(font(BODY), font(BODY)->FontSize, {p.x + tabPad, p.y + (ctl - ts.y) / 2}, on ? cWin : cInk, kTabs[i]);
            x += tw;
        }
    }
    ImGui::SetCursorPos({W - pad - btnW, padY});
    ImGui::BeginDisabled(a.busy);
    if (button2("Refresh")) a.startScan();
    ImGui::SameLine(0, S(12));
    if (button2("Change folder")) a.pickFolder();
    ImGui::EndDisabled();
    return h;
}

// The status bar along the bottom: what was scanned, or the scan's progress as a row of blocks.
void statusBar(App& a, float h) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    float W = ImGui::GetWindowWidth(), H = ImGui::GetWindowHeight(), y = wp.y + H - h, pad = S(24);
    dl->AddRectFilled({wp.x, y}, {wp.x + W, wp.y + H}, cWell);
    dl->AddRectFilled({wp.x, y}, {wp.x + W, y + S(2)}, cInk);
    std::string s;
    if (a.viewing && a.viewer) s = "Space plays and pauses  \xC2\xB7  \xE2\x86\x90 / \xE2\x86\x92 jump between mistakes  \xC2\xB7  Enter continues after a stop";
    else if (a.busy && (!a.scanned || a.done < a.total)) s = a.total ? sf("Reading your replays: %d of %d", (int)a.done, (int)a.total) : "Reading osu!.db...";
    else if (a.scanned) s = sf("%zu plays analysed. Skipped %d whose map is deleted, %d from other modes, %d with Relax/Autopilot/Auto.",
                               a.res.plays.size(), a.res.deleted, a.res.otherMode, a.res.modSkip);
    float right = wp.x + W - pad;
    if (a.busy && a.total > 0 && a.done < a.total) {  // 20 blocks, like an old progress bar
        float bw = S(10), bg = S(3), bh = S(16), n = 20, tw = n * (bw + bg) + bg + S(4);
        ImVec2 p{right - tw, y + (h - bh - S(8)) / 2 + S(1)}, q{right, p.y + bh + S(8)};
        dl->AddRectFilled(p, q, cWin);
        box(dl, p, q, cInk, S(2));
        int on = (int)(n * a.done / a.total);
        for (int i = 0; i < on; i++) {
            float x = p.x + S(2) + bg + i * (bw + bg);
            dl->AddRectFilled({x, p.y + S(6)}, {x + bw, q.y - S(6)}, cInk);
        }
        right = p.x - S(16);
    }
    ImFont* f = font(LABEL);
    dl->PushClipRect({wp.x + pad, y}, {right, wp.y + H}, true);
    dl->AddText(f, f->FontSize, {wp.x + pad, y + (h - f->FontSize) / 2 + S(1)}, cMuted, s.c_str());
    dl->PopClipRect();
}

// The desk: flat blue with a faint 24 px grid.
void desk(ImVec2 a, ImVec2 b) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float g = S(24);
    ImU32 c = alpha(cWin, 46);
    for (float x = a.x + g; x < b.x; x += g) dl->AddLine({x, a.y}, {x, b.y}, c);
    for (float y = a.y + g; y < b.y; y += g) dl->AddLine({a.x, y}, {b.x, y}, c);
}
}

// ---- design system (shared with viewer.cpp through gui.h) ----
float gS = 1;
int gForceTab = -1;
float S(float v) { return std::round(v * gS); }
ImFont* font(int i) { auto& f = ImGui::GetIO().Fonts->Fonts; return f[std::min(i, f.Size - 1)]; }
ImU32 alpha(ImU32 c, int a) { return (c & 0x00FFFFFF) | ((ImU32)a << 24); }
std::string sf(const char* f, ...) {
    char b[1024];
    va_list a;
    va_start(a, f);
    vsnprintf(b, sizeof b, f, a);
    va_end(a);
    return b;
}
void txt(int f, ImU32 c, const std::string& s, bool wrap) {
    ImGui::PushFont(font(f));
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    if (wrap) ImGui::TextWrapped("%s", s.c_str());
    else ImGui::TextUnformatted(s.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

namespace {
struct WinFrame { ImVec2 min; const char* title; ImU32 bar; std::string note; float barH; };
std::vector<WinFrame> gFrames;  // open windows (window() .. endWindow())
}
void window(const char* id, const char* title, ImU32 bar, float w, float h, bool autoH, const std::string& note) {
    float sh = S(6), bt = S(2), barH = title ? S(40) : bt;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (w <= 0) w = avail.x + w - sh;
    if (h <= 0 && !autoH) h = avail.y + h - sh;
    ImGui::BeginGroup();
    ImVec2 p = ImGui::GetCursorScreenPos();
    gFrames.push_back({p, title, bar, note, barH});
    ImGui::Dummy({w, barH});
    ImGui::SetCursorScreenPos({p.x + bt, p.y + barH});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {S(16), S(16)});
    ImGui::BeginChild(id, {w - 2 * bt, autoH ? 0 : std::max(S(8), h - barH - bt)},
                      ImGuiChildFlags_AlwaysUseWindowPadding | (autoH ? ImGuiChildFlags_AutoResizeY : 0), ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
}
void endWindow() {
    ImGui::EndChild();
    WinFrame f = gFrames.back();
    gFrames.pop_back();
    float bt = S(2), r = S(8);
    bool primary = f.title && f.bar != cWell;
    ImVec2 a = f.min, b{ImGui::GetItemRectMax().x + bt, ImGui::GetItemRectMax().y + bt};
    ImGui::SetCursorScreenPos({a.x, b.y});
    ImGui::Dummy({0, 0});
    ImGui::EndGroup();
    // drawn into the parent, which renders under the child's content
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (primary) dl->AddRectFilled({a.x + S(6), a.y + S(6)}, {b.x + S(6), b.y + S(6)}, cInk, r);
    dl->AddRectFilled(a, b, cWin, r);
    if (f.title) {
        float by = a.y + f.barH;
        dl->AddRectFilled(a, {b.x, by}, f.bar, r, ImDrawFlags_RoundCornersTop);
        dl->AddRectFilled({a.x, by - bt}, {b.x, by}, cInk);
        ImFont* ft = font(BAR);
        ImU32 tc = cInk;
        float right = b.x - S(14);
        if (!f.note.empty()) {
            ImVec2 ns = textSize(LABEL, f.note.c_str());
            right -= ns.x;
            dl->AddText(font(LABEL), font(LABEL)->FontSize, {right, a.y + (f.barH - ns.y) / 2}, tc, f.note.c_str());
            right -= S(16);
        }
        dl->PushClipRect(a, {right, by}, true);
        dl->AddText(ft, ft->FontSize, {a.x + S(16), a.y + (f.barH - ft->FontSize) / 2}, tc, f.title);
        dl->PopClipRect();
    }
    outline(dl, a, b, cInk, bt, r);
}
void gap() { ImGui::Dummy({0, 0}); }  // with 8 px item spacing on both sides: 16 px between windows
void rule() {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine({p.x, p.y}, {p.x + ImGui::GetContentRegionAvail().x, p.y}, cRule);
    ImGui::Dummy({0, S(1)});
}
// Buttons: ink outline and a 3 px hard shadow; while held the face moves onto the shadow, like a key going down.
static bool pressButton(const char* label, ImVec2 size, ImU32 face, ImU32 hover) {
    ImVec2 ts = ImGui::CalcTextSize(label, nullptr, true), pad = ImGui::GetStyle().FramePadding;
    if (size.x <= 0) size.x = ts.x + 2 * pad.x;
    if (size.y <= 0) size.y = ts.y + 2 * pad.y;
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool hit = ImGui::InvisibleButton(label, size);
    bool hov = ImGui::IsItemHovered(), held = ImGui::IsItemActive() && hov;
    if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    float sh = S(3), d = held ? sh : 0;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a{p.x + d, p.y + d}, b{a.x + size.x, a.y + size.y};
    if (!held) dl->AddRectFilled({p.x + sh, p.y + sh}, {p.x + size.x + sh, p.y + size.y + sh}, ImGui::GetColorU32(cInk), S(4));
    dl->AddRectFilled(a, b, ImGui::GetColorU32(hov ? hover : face), S(4));
    outline(dl, a, b, ImGui::GetColorU32(cInk), S(2), S(4));
    ImGui::PushStyleColor(ImGuiCol_Text, cInk);
    ImGui::RenderTextClipped(a, b, label, nullptr, &ts, {0.5f, 0.5f});
    ImGui::PopStyleColor();
    return hit;
}
bool button1(const char* label, ImVec2 size) { return pressButton(label, size, cYellow, IM_COL32(250, 206, 112, 255)); }
bool button2(const char* label, ImVec2 size) { return pressButton(label, size, cWin, cWell); }
float buttonW(const char* label) { return ImGui::CalcTextSize(label, nullptr, true).x + 2 * ImGui::GetStyle().FramePadding.x; }
void flow(float w, float gap) {
    float right = ImGui::GetItemRectMax().x + gap, edge = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;  // cursor is at the start of the next line
    if (right + w <= edge) ImGui::SameLine(0, gap);
    else ImGui::Dummy({0, S(4)});  // new row: 8 + 4 + 8 px, so the shadows of the row above stay clear
}
bool table(const char* id, int cols, float h, int extra) {
    return ImGui::BeginTable(id, cols, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_PadOuterX |
                                           ImGuiTableFlags_SizingStretchSame | extra, {0, h});
}
void headerRow() {
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    for (int c = 0; c < ImGui::TableGetColumnCount(); c++) {
        ImGui::TableSetColumnIndex(c);
        txt(LABEL, cMuted, ImGui::TableGetColumnName(c));
    }
}
void cell(ImU32 c, const std::string& s) { ImGui::TableNextColumn(); txt(BODY, c, s); }

void setupStyle(float dpi) {
    gS = dpi;
    ImGuiIO& io = ImGui::GetIO();
    static ImVector<ImWchar> ranges;
    ImFontGlyphRangesBuilder b;
    b.AddRanges(io.Fonts->GetGlyphRangesDefault());
    b.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
    b.AddText("\xE2\x80\x94\xE2\x80\x93\xE2\x80\xA6\xC2\xB7\xC2\xB1\xC3\x97");  // — – … · ± ×
    b.BuildRanges(&ranges);
    wchar_t win[MAX_PATH];
    GetWindowsDirectoryW(win, MAX_PATH);
    std::string ui = narrow(std::wstring(win) + L"\\Fonts\\segoeui.ttf"), sym = narrow(std::wstring(win) + L"\\Fonts\\seguisym.ttf");
    static const ImWchar symbols[] = {0x2190, 0x2193, 0x21BA, 0x21BA, 0x2264, 0x2264, 0x2605, 0x2605, 0x2715, 0x2715, 0};  // ← ↑ → ↓ ↺ ≤ ★ ✕
    static const ImWchar cyr[] = {0x0400, 0x052F, 0};
    struct F { const unsigned int* data; unsigned int size; float px; bool needCyr; };
    const F fonts[] = {  // order = BODY, LABEL, TITLE, HEAD, BAR
        {kFontPlexSans_compressed_data, kFontPlexSans_compressed_size, 18, false},
        {kFontPlexSansSemi_compressed_data, kFontPlexSansSemi_compressed_size, 15, false},
        {kFontBricolageBold_compressed_data, kFontBricolageBold_compressed_size, 26, true},
        {kFontBricolageBold_compressed_data, kFontBricolageBold_compressed_size, 48, true},
        {kFontBricolageBold_compressed_data, kFontBricolageBold_compressed_size, 19, true},
    };
    ImFontConfig mc;
    mc.MergeMode = true;
    for (auto& f : fonts) {
        ImFontConfig fc;
        fc.FontDataOwnedByAtlas = true;
        io.Fonts->AddFontFromMemoryCompressedTTF(f.data, (int)f.size, S(f.px), &fc, ranges.Data);
        if (f.needCyr) io.Fonts->AddFontFromFileTTF(ui.c_str(), S(f.px), &mc, cyr);  // Bricolage has no Cyrillic
        io.Fonts->AddFontFromFileTTF(sym.c_str(), S(f.px), &mc, symbols);
    }

    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    st.WindowPadding = {0, 0}, st.FramePadding = {16, 13}, st.ItemSpacing = {8, 8}, st.ItemInnerSpacing = {8, 8};  // 18 px text + 2 x 13 = 44 px controls
    st.CellPadding = {8, 8}, st.ScrollbarSize = 14, st.GrabMinSize = 18;
    st.WindowRounding = st.ChildRounding = st.FrameRounding = st.GrabRounding = st.PopupRounding = st.ScrollbarRounding = st.TabRounding = 0;
    st.WindowBorderSize = 0, st.ChildBorderSize = 0, st.PopupBorderSize = 2, st.FrameBorderSize = 2;  // ink outline around every input
    st.SelectableTextAlign = {0, 0.5f};
    st.ScaleAllSizes(dpi);
    st.PopupBorderSize = st.FrameBorderSize = S(2);
    st.FrameRounding = st.PopupRounding = S(4), st.GrabRounding = S(2), st.ScrollbarRounding = S(7);
    ImVec4* c = st.Colors;
    auto v4 = [](ImU32 u) { return ImGui::ColorConvertU32ToFloat4(u); };
    for (int i = 0; i < ImGuiCol_COUNT; i++) c[i] = v4(cInk);
    c[ImGuiCol_Text] = v4(cInk), c[ImGuiCol_TextDisabled] = v4(cMuted);
    c[ImGuiCol_WindowBg] = v4(cDesk), c[ImGuiCol_ChildBg] = v4(0), c[ImGuiCol_PopupBg] = v4(cWin);
    c[ImGuiCol_Border] = v4(cInk), c[ImGuiCol_BorderShadow] = v4(0), c[ImGuiCol_Separator] = v4(cRule);
    c[ImGuiCol_FrameBg] = v4(cWin), c[ImGuiCol_FrameBgHovered] = v4(cWell), c[ImGuiCol_FrameBgActive] = v4(cWell);
    c[ImGuiCol_Button] = v4(cWell), c[ImGuiCol_ButtonHovered] = v4(cYellow), c[ImGuiCol_ButtonActive] = v4(cYellow);  // combo arrows etc.
    c[ImGuiCol_Header] = v4(cYellow), c[ImGuiCol_HeaderHovered] = v4(IM_COL32(250, 220, 150, 255)), c[ImGuiCol_HeaderActive] = v4(cYellow);
    c[ImGuiCol_TableHeaderBg] = v4(cWell), c[ImGuiCol_TableBorderLight] = v4(cRule), c[ImGuiCol_TableBorderStrong] = v4(cInk);
    c[ImGuiCol_TableRowBg] = v4(cWin), c[ImGuiCol_TableRowBgAlt] = v4(IM_COL32(242, 235, 221, 255));
    c[ImGuiCol_ScrollbarBg] = v4(cWell), c[ImGuiCol_ScrollbarGrab] = v4(cMuted), c[ImGuiCol_ScrollbarGrabHovered] = v4(cInk), c[ImGuiCol_ScrollbarGrabActive] = v4(cInk);
    c[ImGuiCol_SliderGrab] = v4(cYellow), c[ImGuiCol_SliderGrabActive] = v4(cOrange), c[ImGuiCol_CheckMark] = v4(cInk);
    c[ImGuiCol_NavCursor] = v4(cOrange), c[ImGuiCol_ModalWindowDimBg] = v4(alpha(cInk, 120)), c[ImGuiCol_TextSelectedBg] = v4(alpha(cYellow, 160));
}

void drawApp(App& a) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("osu-coach", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
    static Recent rc;
    static int builtFor = -1;
    if (a.scanned && builtFor != a.gen) rc = build(a.res.plays, a.rep.recentFrom), builtFor = a.gen;
    static int tab = 0;
    if (gForceTab >= 0) tab = gForceTab;
    bool ready = a.scanned && !a.res.plays.empty() && !a.osuDir.empty();

    float statusH = S(36), barH = menuBar(a, tab, ready);
    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    desk({wp.x, wp.y + barH}, {wp.x + ws.x, wp.y + ws.y - statusH});
    statusBar(a, statusH);
    // the page between the bars; it scrolls when the window is too small for its minimum layout
    ImGui::SetCursorPos({0, barH});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {S(32), S(24)});
    ImGui::BeginChild("page", {ws.x, ws.y - barH - statusH}, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
    if (a.viewing && a.viewer) drawViewer(a);
    else if (a.osuDir.empty()) {
        window("pick", "Where is osu! installed?", cOrange, std::min(S(640), ImGui::GetContentRegionAvail().x - S(6)), 0, true);
        txt(BODY, cMuted, "Pick the folder that has osu!.db in it (for example C:\\Users\\you\\AppData\\Local\\osu!).", true);
        ImGui::Dummy({0, S(8)});
        if (button1("Choose osu! folder")) a.pickFolder();
        endWindow();
    } else if (a.scanned) {
        if (!a.res.dbOk) {
            window("dberr", "Error", cWell, 0, 0, true);
            txt(BODY, cBad, "osu!.db could not be read, so star ratings and practice maps are switched off.", true);
            endWindow();
            gap();
        }
        if (a.res.plays.empty()) {
            window("noplays", "No plays yet", cOrange, 0, 0, true);
            txt(BODY, cInk, "No osu!standard replays with their maps still installed. Play a few maps in osu! (stable) and press Refresh.", true);
            endWindow();
        } else switch (tab) {
            case 0: tabOverview(a, rc); break;
            case 1: tabTiming(rc); break;
            case 2: tabAim(rc); break;
            case 3: tabSpeed(rc, a.streamBpm); break;
            case 4: tabReading(rc); break;
            case 5: tabPlays(a); break;
            case 6: tabMaps(a); break;
            default: tabProgress(a.rep, a.gen); break;
        }
    }
    ImGui::EndChild();
    ImGui::End();
}
