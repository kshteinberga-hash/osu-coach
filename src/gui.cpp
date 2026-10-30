#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <ctime>
#include <map>
#include "gui.h"
#include "imgui.h"
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
ImU32 trendCol(int t) { return t > 0 ? cGreen : t < 0 ? cRed : cMuted; }

void radar(const Report& r, float size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos(), c{p.x + size / 2, p.y + size / 2};
    float R = size * 0.32f;
    auto pt = [&](int i, float v) { float a = -3.14159265f / 2 + i * 2 * 3.14159265f / NSKILL; return ImVec2{c.x + std::cos(a) * R * v, c.y + std::sin(a) * R * v}; };
    for (float ring : {0.25f, 0.5f, 0.75f, 1.f}) {
        ImVec2 q[NSKILL];
        for (int i = 0; i < NSKILL; i++) q[i] = pt(i, ring);
        dl->AddPolyline(q, NSKILL, ring == 0.5f ? cMuted : cRule, ImDrawFlags_Closed, ring == 0.5f ? S(1.5f) : S(1));
    }
    ImVec2 v[NSKILL];
    for (int i = 0; i < NSKILL; i++) dl->AddLine(c, pt(i, 1), cRule), v[i] = pt(i, std::max(0.03f, r.radar[i] / 100));
    for (int i = 0; i < NSKILL; i++) dl->AddTriangleFilled(c, v[i], v[(i + 1) % NSKILL], alpha(cPink, 56));
    dl->AddPolyline(v, NSKILL, cPink, ImDrawFlags_Closed, S(2.5f));
    ImFont *fl = font(LABEL), *fv = font(TITLE);
    for (int i = 0; i < NSKILL; i++) {
        ImVec2 q = pt(i, 1.3f);
        std::string val = sf("%.0f", r.radar[i]);
        ImVec2 ls = fl->CalcTextSizeA(fl->FontSize, FLT_MAX, 0, kSkillName[i]), vs = fv->CalcTextSizeA(fv->FontSize, FLT_MAX, 0, val.c_str());
        ImU32 col = r.radar[i] < 45 ? cRed : r.radar[i] > 55 ? cGreen : cCream;
        float y = q.y - (ls.y + vs.y) / 2;
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
    dl->AddLine({p.x, p.y + h}, {p.x + w, p.y + h}, cRule);
    if (mx <= 0) return;
    float bw = w / n, g = bw >= S(8) ? S(2) : 0;
    for (int i = 0; i < n; i++) {
        float bh = v[i] / mx * h, x = p.x + i * bw + g / 2;
        if (bh >= 1) dl->AddRectFilled({x, p.y + h - bh}, {x + bw - g, p.y + h}, cPink);
    }
    if (mark >= 0) dl->AddLine({p.x + mark * bw, p.y}, {p.x + mark * bw, p.y + h}, cCream, S(1));
}
void axis(const char* l, const char* m, const char* r) {  // three captions under a chart
    float w = ImGui::GetContentRegionAvail().x, x = ImGui::GetCursorPosX();
    txt(LABEL, cMuted, l);
    if (*m) { ImGui::SameLine(x + (w - ImGui::CalcTextSize(m).x) / 2); txt(LABEL, cMuted, m); }
    ImGui::SameLine(x + w - ImGui::CalcTextSize(r).x);
    txt(LABEL, cMuted, r);
}

// Line chart: 2 px pink line over a flat translucent fill (no gradient), min/max on the left.
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
        dl->AddQuadFilled(pts[i], pts[i + 1], {pts[i + 1].x, bot}, {pts[i].x, bot}, alpha(cPink, 40));
    dl->AddLine({p.x + lx, bot}, {p.x + w, bot}, cRule);
    dl->AddPolyline(pts.data(), (int)pts.size(), cPink, 0, S(2));
    ImFont* f = font(LABEL);
    dl->AddText(f, f->FontSize, {p.x, top - f->FontSize / 2}, cMuted, sf("%.*f%s", *unit ? 1 : 0, mx, unit).c_str());
    dl->AddText(f, f->FontSize, {p.x, bot - f->FontSize}, cMuted, sf("%.*f%s", *unit ? 1 : 0, mn, unit).c_str());
}

// Scoresheet block: one headline number on the left, fact rows separated by rules on the right.
void headline(const char* label, const std::string& value, ImU32 vc, const std::string& note) {
    txt(LABEL, cMuted, label);
    txt(HEAD, vc, value);
    if (!note.empty()) txt(BODY, cMuted, note, true);
}
// One fact row: label on the left, value (Barlow) from 55% of the width, an optional note after it; texts share a baseline.
void fact(const char* label, const std::string& value, ImU32 vc = cCream, const std::string& note = "") {
    float w = ImGui::GetContentRegionAvail().x, x = ImGui::GetCursorPosX(), y = ImGui::GetCursorPosY();
    float big = font(TITLE)->FontSize, base = y + big * 0.8f;  // approximate baseline of the value text
    auto at = [&](float px, int f, ImU32 c, const std::string& s) { ImGui::SetCursorPos({px, base - font(f)->FontSize * 0.8f}); txt(f, c, s); };
    at(x, BODY, cCream, label);
    at(x + std::floor(w * 0.55f), TITLE, vc, value);
    if (!note.empty()) at(ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + S(16), LABEL, cMuted, note);
    ImGui::SetCursorPos({x, y + big + S(8)});
    rule();
}
// Two-column top block of a tab: headline on the left (34%), facts on the right; sized to its content.
void top(const char* id) {
    section(id, 0, 0, true);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {S(12), 0});
    ImGui::BeginTable("top", 2);
    ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthStretch, 0.34f);
    ImGui::TableSetupColumn("r", ImGuiTableColumnFlags_WidthStretch, 0.66f);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
}
void split() { ImGui::TableNextColumn(); }
void endTop() { ImGui::EndTable(); ImGui::PopStyleVar(); endSection(); gap(); }

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

void tabOverview(App& a, const Recent& rc) {
    const Report& r = a.rep;
    top("top");
    txt(LABEL, cMuted, "Coach verdict");
    txt(TITLE, cPink, r.verdict, true);
    ImGui::Dummy({0, S(8)});
    txt(BODY, cMuted, sf("Based on your last %zu plays. Older plays are your baseline.", rc.plays), true);
    split();
    fact("Timing steadiness (UR, lower is steadier)", sf("%.0f", rc.err.ur()), cCream, trendWord(r.trend[0]));
    fact("Average accuracy", sf("%.2f%%", 100 * rc.accSum / std::max<size_t>(1, rc.plays)), cCream, sf("%.0f%% full combos", pct(rc.fcs, rc.plays)));
    fact("Jump misses this week", trendWord(r.trend[1]) + 4, trendCol(r.trend[1]));
    fact("Stream misses this week", trendWord(r.trend[2]) + 4, trendCol(r.trend[2]));
    endTop();

    float h = ImGui::GetContentRegionAvail().y, lw = std::floor(ImGui::GetContentRegionAvail().x * 0.40f);
    section("radar", lw, h);
    txt(TITLE, cCream, "Your skills");
    txt(BODY, cMuted, r.needHistory ? "Needs older plays to compare with, so every skill shows 50 for now."
                                    : "Compared with your usual: 50 = normal for you, higher = better than usual.", true);
    float avail = ImGui::GetContentRegionAvail().x, rs = std::min(avail, ImGui::GetContentRegionAvail().y);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - rs) / 2);
    radar(r, rs);
    endSection();
    ImGui::SameLine(0, S(16));
    section("work", 0, h, true);
    txt(TITLE, cCream, "What to work on");
    if (!r.nTop) txt(BODY, cCream, "Nothing is costing you much right now. Try slightly harder maps.", true);
    for (int i = 0; i < r.nTop; i++) {
        auto& wk = r.top[i];
        rule();
        txt(TITLE, cPink, sf("%d. %s", i + 1, kSkillName[wk.s]));
        ImGui::SameLine(0, S(16));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + font(TITLE)->FontSize - font(LABEL)->FontSize - S(4));
        txt(LABEL, cMuted, sf("costs you about %.1f pp per play", wk.pp / std::max<size_t>(1, rc.plays)));
        txt(BODY, cCream, wk.metric, true);
        txt(LABEL, cGreen, "WHAT TO DO");
        txt(BODY, cCream, wk.fix, true);
    }
    endSection();
}

void tabTiming(const Recent& rc) {
    double n = rc.err.n, m = rc.err.mean();
    int keys = rc.keyHits[0] + rc.keyHits[1];
    top("top");
    headline("Timing steadiness", sf("%.0f", rc.err.ur()), cPink, "UR: how spread out your clicks are. Lower is steadier; top players are under 100.");
    split();
    fact("On average you click", std::fabs(m) < 2 ? "right on time" : sf("%.0f ms %s", std::fabs(m), m < 0 ? "early" : "late"));
    fact("Clicks within 15 ms of perfect", sf("%.0f%%", pct(rc.within[0], n)));
    fact("Clicks within 30 ms", sf("%.0f%%", pct(rc.within[1], n)));
    fact("Clicks within 50 ms", sf("%.0f%%", pct(rc.within[2], n)));
    fact("Clicks with key 1 / key 2", sf("%.0f%% / %.0f%%", pct(rc.keyHits[0], keys), pct(rc.keyHits[1], keys)), cCream,
         sf("steadiness %.0f / %.0f", rc.errKey[0].ur(), rc.errKey[1].ur()));
    endTop();
    section("hist", 0, 0, true);
    txt(TITLE, cCream, "When your clicks land");
    txt(BODY, cMuted, "Each bar is 2 ms. The white line is perfect timing; a narrow, tall pile means steady timing.", true);
    ImGui::Dummy({0, S(8)});
    bars(rc.hist, 100, std::max(S(120), ImGui::GetContentRegionAvail().y - S(48)), 50);
    axis("100 ms early", "perfect", "100 ms late");
    endSection();
}

void tabAim(const Recent& rc) {
    int am = rc.under + rc.over;
    static const char* names[] = {"Close notes (under 1 circle width apart)", "Short moves (1 to 2 widths)", "Jumps (2 to 3.5 widths)", "Big jumps (over 3.5 widths)"};
    top("top");
    headline("Aim misses that stop short", sf("%.0f%%", pct(rc.under, am)), cPink,
             sf("%d times you stopped short of the circle, %d times you went past it.", rc.under, rc.over));
    split();
    for (int b = 0; b < 4; b++)
        fact(names[b], sf("%.2f%% missed", pct(rc.bMiss[b], rc.bObj[b])), cCream,
             sf("of %d notes", rc.bObj[b]));
    endTop();
    float h = ImGui::GetContentRegionAvail().y, half = std::floor((ImGui::GetContentRegionAvail().x - S(16)) * 0.4f);
    section("quad", half, h);
    txt(TITLE, cCream, "Misses by screen area");
    ImGui::Dummy({0, S(8)});
    {
        static const char* qn[] = {"Top left", "Top right", "Bottom left", "Bottom right"};
        double mx = 0;
        for (int q = 0; q < 4; q++) mx = std::max(mx, pct(rc.qMiss[q], rc.qObj[q]));
        ImVec2 p = ImGui::GetCursorScreenPos();
        float tw = (ImGui::GetContentRegionAvail().x - S(8)) / 2, th = (ImGui::GetContentRegionAvail().y - S(8)) / 2;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int q = 0; q < 4; q++) {
            double m = pct(rc.qMiss[q], rc.qObj[q]);
            ImVec2 a{p.x + (q % 2) * (tw + S(8)), p.y + (q / 2) * (th + S(8))};
            dl->AddRectFilled(a, {a.x + tw, a.y + th}, cInk);
            dl->AddRectFilled(a, {a.x + tw, a.y + th}, alpha(cPink, mx > 0 ? (int)(90 * m / mx) : 0));
            dl->AddRect(a, {a.x + tw, a.y + th}, cRule);
            dl->AddText(font(LABEL), font(LABEL)->FontSize, {a.x + S(16), a.y + S(16)}, cCream, qn[q]);
            dl->AddText(font(TITLE), font(TITLE)->FontSize, {a.x + S(16), a.y + S(40)}, cCream, sf("%.2f%%", m).c_str());
        }
    }
    endSection();
    ImGui::SameLine(0, S(16));
    section("jump", 0, h);
    txt(TITLE, cCream, "How far you move between notes");
    txt(BODY, cMuted, "Most of your notes fall in the tall bars; misses usually come from the long moves on the right.", true);
    ImGui::Dummy({0, S(8)});
    bars(rc.jump, 24, std::max(S(96), ImGui::GetContentRegionAvail().y - S(32)));
    axis("0", "3 circle widths", "6 widths");
    endSection();
}

void tabSpeed(const Recent& rc, float streamBpm) {
    double ov = pct(rc.zOv[0], rc.zPairs[0]);
    top("top");
    headline("Finger lock on fast notes", sf("%.0f%%", ov), ov > 20 ? cRed : cPink,
             "How often you still hold the last key when you press the next one. Under 10% is good.");
    split();
    fact("Comfortable stream speed", streamBpm > 0 ? sf("%.0f BPM", streamBpm) : std::string("not enough streams"), cCream, "streams you hit without a miss");
    fact("Steadiness on notes 100\xE2\x80\x93""130 ms apart", sf("%.0f", rc.zone[1].ur()), cCream, "usually the hardest speed");
    fact("Longest run without a pause", sf("%d notes", rc.stamNotes), cCream, sf("%.0f s at %.1f notes per second", rc.stamSec, rc.stamNps));
    endTop();
    section("streams", 0, 0, true);
    txt(TITLE, cCream, "Streams (4 or more quick notes in a row)");
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
            cell(cCream, bands[i]), cell(cCream, sf("%d", b.segs)), cell(cCream, sf("%d", b.notes)), cell(cCream, sf("%.1f%%", pct(b.miss, b.notes)));
            cell(cCream, sf("%.0f%%", pct(b.alt, b.alt + b.dbl))), cell(cCream, when(early));
            cell(late < -8 ? cRed : cCream, when(late) + (late < -8 ? ", speeding up" : ""));
        }
        ImGui::EndTable();
    }
    endSection();
    gap();
    section("zones", 0, 0, true);
    txt(TITLE, cCream, "Tapping at different speeds");
    const char* zones[] = {"Very fast (under 100 ms apart)", "Fast (100\xE2\x80\x93""130 ms)", "Normal (130\xE2\x80\x93""260 ms)", "Slow (over 260 ms)"};
    if (table("zt", 5)) {
        for (auto h : {"Notes apart", "Clicks", "Steadiness", "Switching fingers", "Holding a key too long"}) ImGui::TableSetupColumn(h);
        headerRow();
        for (int z = 0; z < 4; z++) {
            double o = pct(rc.zOv[z], rc.zPairs[z]);
            ImGui::TableNextRow();
            cell(cCream, zones[z]), cell(cCream, sf("%.0f", rc.zone[z].n)), cell(cCream, sf("%.0f", rc.zone[z].ur()));
            cell(cCream, sf("%.0f%%", pct(rc.zAlt[z], rc.zPairs[z]))), cell(o > 20 ? cRed : cCream, sf("%.0f%%%s", o, o > 20 ? ", finger lock" : ""));
        }
        ImGui::EndTable();
    }
    endSection();
}

void tabReading(const Recent& rc) {
    top("top");
    headline("First note after a pause", sf("%.2f%%", pct(rc.brkMiss, rc.brkObj)), cPink,
             sf("missed (%d of %d). A pause is more than half a second without notes.", rc.brkMiss, rc.brkObj));
    split();
    fact("Notes in the middle of the music", sf("%.2f%% missed", pct(rc.flowMiss, rc.flowObj)), cCream, sf("%d of %d", rc.flowMiss, rc.flowObj));
    fact("Circles that appear slowly (AR below 8)", sf("%.2f%% missed", pct(rc.arMiss[0], rc.arObj[0])), cCream, sf("%d notes", rc.arObj[0]));
    fact("Circles that appear quickly (AR 8 or more)", sf("%.2f%% missed", pct(rc.arMiss[1], rc.arObj[1])), cCream, sf("%d notes", rc.arObj[1]));
    endTop();
    section("note", 0, 0, true);
    txt(TITLE, cCream, "How to read this");
    txt(BODY, cCream, "Reading means seeing where the next circle is in time. If you miss more right after pauses, or more when circles appear "
                      "quickly, the problem is reading, not aim. Mods count: HR and DT make circles appear faster, EZ and HT slower.", true);
    endSection();
}

std::string whereStr(const ObjRes& o, float beat) {
    float frac = o.gap / beat;
    std::string s = o.afterBreak ? "after a pause" : frac < 0.3f ? "fast notes" : frac < 0.6f ? "normal notes" : "slow notes";
    if (o.inStream) s += ", in a stream";
    if (o.spacing > 4) s += sf(", jump of %.1f widths", o.spacing / 2);
    if (o.slider) s += ", slider start";
    return s;
}

void missList(const PlayStats& p, const std::vector<ObjRes>& objs, float h) {
    if (objs.empty()) { txt(BODY, cMuted, "The replay or its map is no longer on your PC, so the note-by-note list is unavailable."); return; }
    float beat = p.bpm > 0 ? 60000 / p.bpm : 500;
    if (table("misses", 4, h, ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, S(104));
        ImGui::TableSetupColumn("Note", ImGuiTableColumnFlags_WidthFixed, S(72));
        ImGui::TableSetupColumn("What happened", ImGuiTableColumnFlags_WidthFixed, S(296));
        ImGui::TableSetupColumn("Where", ImGuiTableColumnFlags_WidthStretch);
        headerRow();
        for (size_t i = 0; i < objs.size(); i++) {
            auto& o = objs[i];
            if (o.judge) continue;
            int mt = (int)(o.t * p.rate);
            ImGui::TableNextRow();
            cellMono(sf("%d:%02d.%03d", mt / 60000, mt / 1000 % 60, mt % 1000)), cell(cMuted, sf("#%zu", i + 1));
            cell(cCream, explainMiss(o, beat).title), cell(cMuted, whereStr(o, beat));
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
    float total = ImGui::GetContentRegionAvail().y, h = sel ? std::floor(total * 0.42f) : total;
    section("list", 0, h);
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
    ImGui::Dummy({0, S(8)});
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {S(8), 0});  // 44 px rows sized explicitly
    if (table("plays", 7, ImGui::GetContentRegionAvail().y, ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(cols[0], ImGuiTableColumnFlags_WidthFixed, S(152));
        ImGui::TableSetupColumn(cols[1], ImGuiTableColumnFlags_WidthStretch);
        for (int c = 2; c < 7; c++) ImGui::TableSetupColumn(cols[c], ImGuiTableColumnFlags_WidthFixed, S(c == 3 ? 96 : 104));
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers, S(44));
        for (int c = 0; c < 7; c++) {
            ImGui::TableSetColumnIndex(c);
            ImGui::AlignTextToFramePadding();
            txt(LABEL, c == sortCol ? cCream : cMuted, c == sortCol ? sf("%s %s", cols[c], asc ? "\xE2\x86\x91" : "\xE2\x86\x93") : cols[c]);
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
                ImGui::PushFont(font(MONO));
                if (ImGui::Selectable((dateStr(p.when) + "##r" + std::to_string(i)).c_str(), isSel, ImGuiSelectableFlags_SpanAllColumns, {0, S(44)})) a.selPlay = i;
                ImGui::PopFont();
                ImVec2 rmin = ImGui::GetItemRectMin();
                if (isSel) ImGui::GetWindowDrawList()->AddRectFilled(rmin, {rmin.x + S(4), rmin.y + S(44)}, cPink);
                auto col = [](int f, ImU32 c, const std::string& s) { ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); txt(f, c, s); };
                col(BODY, cCream, p.title), col(MONO, p.stars > 0 ? cCream : cMuted, p.stars > 0 ? sf("%.2f", p.stars) : "?"), col(BODY, cMuted, modStr(p.mods));
                col(MONO, cCream, sf("%.2f%%", 100 * p.acc)), col(MONO, p.gmiss ? cCream : cGreen, p.gmiss ? sf("%d", p.gmiss) : "FC"), col(MONO, cCream, sf("%.0f", p.err.ur()));
            }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    endSection();
    if (!sel) return;
    gap();
    auto& p = ps[a.selPlay];
    section("detail", 0, ImGui::GetContentRegionAvail().y);
    txt(TITLE, cCream, p.title);
    txt(BODY, cMuted, sf("%s  \xC2\xB7  %s  \xC2\xB7  %.2f%% accuracy  \xC2\xB7  %d misses  \xC2\xB7  steadiness %.0f  \xC2\xB7  best combo %d",
                         dateStr(p.when).c_str(), modStr(p.mods).c_str(), 100 * p.acc, p.gmiss, p.err.ur(), p.combo));
    ImGui::Dummy({0, S(8)});
    if (button1("Watch replay with the coach")) a.openViewer();
    ImGui::SameLine(0, S(16));
    ImGui::AlignTextToFramePadding();
    txt(BODY, cMuted, "Plays it back with the music and stops at every mistake to explain it.");
    float rest = ImGui::GetContentRegionAvail().y;
    missList(p, a.selectedObjs(), std::max(S(96), rest - S(96)));
    txt(LABEL, cMuted, "When your clicks landed in this play (white line = perfect)");
    float hist[100];
    for (int k = 0; k < 100; k++) hist[k] = (float)p.hist[k];
    bars(hist, 100, S(56), 50);
    endSection();
}

void tabMaps(App& a) {
    section("ctl", 0, 0, true);
    txt(TITLE, cCream, "Practice maps from your Songs folder");
    txt(BODY, cMuted, "Five maps that train the skill you pick. Maps you haven't played much lately come first.", true);
    ImGui::Dummy({0, S(8)});
    ImGui::AlignTextToFramePadding();
    txt(LABEL, cMuted, "Stars");
    ImGui::SameLine(0, S(8));
    ImGui::SetNextItemWidth(S(320));
    ImGui::DragFloatRange2("##stars", &a.lo, &a.hi, 0.05f, 1, 10, "%.2f\xE2\x98\x85");
    ImGui::SameLine(0, S(24));
    ImGui::AlignTextToFramePadding();
    txt(LABEL, cMuted, "Train");
    ImGui::SameLine(0, S(8));
    const char* items[NSKILL + 1] = {"My top weakness"};
    for (int i = 0; i < NSKILL; i++) items[i + 1] = kSkillName[i];
    int cur = a.trainSkill + 1;
    ImGui::SetNextItemWidth(S(216));
    if (ImGui::Combo("##train", &cur, items, NSKILL + 1)) a.trainSkill = cur - 1;
    ImGui::SameLine(0, S(24));
    ImGui::BeginDisabled(a.busy);
    if (button1("Find maps")) a.startSuggest();
    ImGui::EndDisabled();
    if (a.busy) { ImGui::SameLine(0, S(16)); ImGui::AlignTextToFramePadding(); txt(BODY, cMuted, "searching..."); }
    endSection();
    gap();
    section("list", 0, 0, true);
    if (a.sugg.empty()) txt(BODY, cCream, "No maps in this star range. Widen the range or download a few maps.", true);
    for (size_t i = 0; i < a.sugg.size(); i++) {
        auto& s = a.sugg[i];
        ImGui::PushID((int)i);
        if (i) rule();
        txt(HEAD, cPink, sf("%zu", i + 1));
        ImGui::SameLine(S(16) + S(56));  // fixed indent so every title lines up
        ImGui::BeginGroup();
        txt(TITLE, cCream, s.map->title);
        txt(BODY, cMuted, s.why, true);
        if (s.map->bid > 0) {
            if (button1("Open map page")) open(L"https://osu.ppy.sh/b/" + std::to_wstring(s.map->bid));
            ImGui::SameLine(0, S(8));
        }
        if (button2("Show in folder")) open(L"explorer.exe", L"/select,\"" + s.map->path + L"\"");
        ImGui::EndGroup();
        ImGui::PopID();
    }
    endSection();
}

void tabProgress(const Report& r) {
    std::map<int, Day> byDate;
    for (auto& d : r.days) {
        Day& t = byDate[d.date];
        t.date = d.date, t.plays += d.plays, t.es += d.es, t.ess += d.ess, t.en += d.en;
        for (int s = 0; s < NSKILL; s++) t.obj[s] += d.obj[s], t.miss[s] += d.miss[s];
    }
    std::vector<float> ur, jm, sm;
    for (auto& [k, d] : byDate) {
        Acc a; a.n = d.en, a.s = d.es, a.ss = d.ess;
        ur.push_back((float)a.ur());
        jm.push_back((float)pct(d.miss[JUMPS], d.obj[JUMPS]));
        sm.push_back((float)pct(d.miss[STREAMS], d.obj[STREAMS]));
    }
    section("v", 0, 0, true);
    txt(LABEL, cMuted, "Coach verdict");
    txt(TITLE, cPink, r.verdict);
    txt(BODY, cMuted, byDate.empty() ? std::string("No history yet.")
                                     : sf("%zu days of history, from %d to %d. Saved in history.json, so it stays even if you delete maps.", byDate.size(), byDate.begin()->first, byDate.rbegin()->first), true);
    endSection();
    gap();
    const char* names[] = {"Timing steadiness by day (lower is better)", "Jump misses by day", "Stream misses by day"};
    const char* units[] = {"", "%", "%"};
    std::vector<float>* series[] = {&ur, &jm, &sm};
    float h = std::floor((ImGui::GetContentRegionAvail().y - 2 * S(16)) / 3);
    for (int i = 0; i < 3; i++) {
        if (i) gap();
        ImGui::PushID(i);
        section("c", 0, h);
        txt(TITLE, cCream, names[i]);
        ImGui::SameLine(0, S(16));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + font(TITLE)->FontSize - font(LABEL)->FontSize - S(4));
        txt(LABEL, trendCol(r.trend[i]), sf("%s this week", trendWord(r.trend[i])));
        line(*series[i], std::max(S(48), ImGui::GetContentRegionAvail().y), units[i]);
        endSection();
        ImGui::PopID();
    }
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
void section(const char* id, float w, float h, bool autoH) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {S(16), S(16)});
    ImGui::BeginChild(id, {w, autoH ? 0 : h}, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | (autoH ? ImGuiChildFlags_AutoResizeY : 0));
    ImGui::PopStyleVar();
}
void endSection() { ImGui::EndChild(); }
void gap() { ImGui::Dummy({0, 0}); }  // with 8 px item spacing on both sides: 16 px between sections
void rule() {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine({p.x, p.y}, {p.x + ImGui::GetContentRegionAvail().x, p.y}, cRule);
    ImGui::Dummy({0, S(1)});
}
bool button1(const char* label, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button, cPink), ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 133, 189, 255)), ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(230, 80, 150, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, cInk);  // ink on pink: 6.8:1
    bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return r;
}
bool button2(const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Button, cPanel), ImGui::PushStyleColor(ImGuiCol_ButtonHovered, cRule), ImGui::PushStyleColor(ImGuiCol_ButtonActive, cRule);
    ImGui::PushStyleColor(ImGuiCol_Text, cCream);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, S(1));
    bool r = ImGui::Button(label);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    return r;
}
bool table(const char* id, int cols, float h, int extra) {
    return ImGui::BeginTable(id, cols, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_SizingStretchSame | extra, {0, h});
}
void headerRow() {
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    for (int c = 0; c < ImGui::TableGetColumnCount(); c++) {
        ImGui::TableSetColumnIndex(c);
        txt(LABEL, cMuted, ImGui::TableGetColumnName(c));
    }
}
void cell(ImU32 c, const std::string& s) { ImGui::TableNextColumn(); txt(BODY, c, s); }
void cellMono(const std::string& s) { ImGui::TableNextColumn(); txt(MONO, cCream, s); }

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
    const F fonts[] = {  // order = BODY, LABEL, TITLE, HEAD, MONO
        {kFontPlexSans_compressed_data, kFontPlexSans_compressed_size, 18, false},
        {kFontPlexSansSemi_compressed_data, kFontPlexSansSemi_compressed_size, 15, false},
        {kFontBarlowSemi_compressed_data, kFontBarlowSemi_compressed_size, 28, true},
        {kFontBarlowSemi_compressed_data, kFontBarlowSemi_compressed_size, 52, true},
        {kFontPlexMono_compressed_data, kFontPlexMono_compressed_size, 17, true},
    };
    ImFontConfig mc;
    mc.MergeMode = true;
    for (auto& f : fonts) {
        ImFontConfig fc;
        fc.FontDataOwnedByAtlas = true;
        io.Fonts->AddFontFromMemoryCompressedTTF(f.data, (int)f.size, S(f.px), &fc, ranges.Data);
        if (f.needCyr) io.Fonts->AddFontFromFileTTF(ui.c_str(), S(f.px), &mc, cyr);  // Barlow and Plex Mono have no Cyrillic
        io.Fonts->AddFontFromFileTTF(sym.c_str(), S(f.px), &mc, symbols);
    }

    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    st.WindowPadding = {32, 32}, st.FramePadding = {16, 13}, st.ItemSpacing = {8, 8}, st.ItemInnerSpacing = {8, 8};  // 18 px text + 2 x 13 = 44 px controls
    st.CellPadding = {8, 8}, st.ScrollbarSize = 16, st.GrabMinSize = 16;
    st.WindowRounding = st.ChildRounding = st.FrameRounding = st.GrabRounding = st.PopupRounding = st.ScrollbarRounding = st.TabRounding = 0;
    st.WindowBorderSize = 0, st.ChildBorderSize = 1, st.FrameBorderSize = 1, st.PopupBorderSize = 1;  // 1 px rule around every input
    st.SelectableTextAlign = {0, 0.5f};
    st.ScaleAllSizes(dpi);
    ImVec4* c = st.Colors;
    auto v4 = [](ImU32 u) { return ImGui::ColorConvertU32ToFloat4(u); };
    for (int i = 0; i < ImGuiCol_COUNT; i++) c[i] = v4(cCream);
    c[ImGuiCol_TextDisabled] = v4(cMuted);
    c[ImGuiCol_WindowBg] = v4(cInk), c[ImGuiCol_ChildBg] = v4(cPanel), c[ImGuiCol_PopupBg] = v4(cPanel);
    c[ImGuiCol_Border] = v4(cRule), c[ImGuiCol_BorderShadow] = v4(0), c[ImGuiCol_Separator] = v4(cRule);
    c[ImGuiCol_FrameBg] = v4(cInk), c[ImGuiCol_FrameBgHovered] = v4(cRule), c[ImGuiCol_FrameBgActive] = v4(cRule);
    c[ImGuiCol_Button] = v4(cRule), c[ImGuiCol_ButtonHovered] = v4(IM_COL32(80, 72, 61, 255)), c[ImGuiCol_ButtonActive] = v4(IM_COL32(96, 87, 74, 255));  // neutral: combo arrows etc.
    c[ImGuiCol_Header] = v4(cRule), c[ImGuiCol_HeaderHovered] = v4(alpha(cRule, 160)), c[ImGuiCol_HeaderActive] = v4(cRule);
    c[ImGuiCol_TableHeaderBg] = v4(cPanel), c[ImGuiCol_TableBorderLight] = v4(cRule), c[ImGuiCol_TableBorderStrong] = v4(cRule);
    c[ImGuiCol_TableRowBg] = v4(cPanel), c[ImGuiCol_TableRowBgAlt] = v4(cInk);
    c[ImGuiCol_ScrollbarBg] = v4(cPanel), c[ImGuiCol_ScrollbarGrab] = v4(cRule), c[ImGuiCol_ScrollbarGrabHovered] = v4(cMuted), c[ImGuiCol_ScrollbarGrabActive] = v4(cMuted);
    c[ImGuiCol_SliderGrab] = v4(cPink), c[ImGuiCol_SliderGrabActive] = v4(cPink), c[ImGuiCol_CheckMark] = v4(cPink);
    c[ImGuiCol_NavHighlight] = v4(cPink), c[ImGuiCol_ModalWindowDimBg] = v4(alpha(cInk, 160)), c[ImGuiCol_TextSelectedBg] = v4(alpha(cPink, 90));
}

void drawApp(App& a) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("osu-coach", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (a.viewing && a.viewer) { drawViewer(a); ImGui::End(); return; }
    static Recent rc;
    static int builtFor = -1;
    if (a.scanned && builtFor != a.gen) rc = build(a.res.plays, a.rep.recentFrom), builtFor = a.gen;

    // header: name and status, then square tabs with a pink underline on the current one
    txt(TITLE, cPink, "osu-coach");
    ImGui::SameLine(0, S(16));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + font(TITLE)->FontSize - font(BODY)->FontSize - S(2));
    if (a.busy && (!a.scanned || a.done < a.total)) {
        int t = a.total, d = a.done;
        txt(BODY, cMuted, t ? sf("reading your replays: %d of %d", d, t) : "reading osu!.db...");
    } else if (a.scanned) {
        auto& s = a.res;
        txt(BODY, cMuted, sf("%zu plays analysed. Skipped %d whose map is deleted, %d from other modes, %d with Relax/Autopilot/Auto.",
                             s.plays.size(), s.deleted, s.otherMode, s.modSkip));
    }
    if (a.busy && a.total > 0 && a.done < a.total) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        ImGui::GetWindowDrawList()->AddRectFilled(p, {p.x + w, p.y + S(4)}, cRule);
        ImGui::GetWindowDrawList()->AddRectFilled(p, {p.x + w * a.done / a.total, p.y + S(4)}, cPink);
        ImGui::Dummy({w, S(4)});
    }
    ImGui::Dummy({0, S(8)});

    if (a.osuDir.empty()) {
        section("pick", 0, 0, true);
        txt(TITLE, cCream, "Where is osu! installed?");
        txt(BODY, cMuted, "Pick the folder that has osu!.db in it (for example C:\\Users\\you\\AppData\\Local\\osu!).", true);
        ImGui::Dummy({0, S(8)});
        if (button1("Choose osu! folder")) a.pickFolder();
        endSection();
        ImGui::End();
        return;
    }

    static const char* kTabs[] = {"Overview", "Timing", "Aim", "Speed", "Reading", "Plays", "Practice maps", "Progress"};
    static int tab = 0;
    if (gForceTab >= 0) tab = gForceTab;
    bool ready = a.scanned && !a.res.plays.empty();
    float tabsY = ImGui::GetCursorScreenPos().y, fullW = ImGui::GetContentRegionAvail().x;
    if (ready)
        for (int i = 0; i < 8; i++) {
            if (i) ImGui::SameLine(0, 0);
            bool on = tab == i;
            ImGui::PushStyleColor(ImGuiCol_Button, 0u), ImGui::PushStyleColor(ImGuiCol_ButtonHovered, cPanel), ImGui::PushStyleColor(ImGuiCol_ButtonActive, cRule);
            ImGui::PushStyleColor(ImGuiCol_Text, on ? cCream : cMuted);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
            if (ImGui::Button(kTabs[i])) tab = i;
            ImGui::PopStyleVar();
            if (on) {
                ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddRectFilled({mn.x, mx.y - S(4)}, mx, cPink);
            }
            ImGui::PopStyleColor(4);
        }
    float bw = ImGui::CalcTextSize("Refresh").x + ImGui::CalcTextSize("Change folder").x + 4 * ImGui::GetStyle().FramePadding.x + S(8);
    if (ready) ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - bw));
    ImGui::BeginDisabled(a.busy);
    if (button2("Refresh")) a.startScan();
    ImGui::SameLine(0, S(8));
    if (button2("Change folder")) a.pickFolder();
    ImGui::EndDisabled();
    {   // full-width rule under the tab row
        float y = tabsY + S(44), x0 = ImGui::GetWindowPos().x + ImGui::GetStyle().WindowPadding.x;
        ImGui::GetWindowDrawList()->AddLine({x0, y}, {x0 + fullW, y}, cRule);
    }
    ImGui::Dummy({0, S(16)});

    if (a.scanned && !a.res.dbOk) {
        section("dberr", 0, 0, true);
        txt(BODY, cRed, "osu!.db could not be read, so star ratings and practice maps are switched off.", true);
        endSection();
        gap();
    }
    if (!a.scanned) { ImGui::End(); return; }
    if (a.res.plays.empty()) {
        section("noplays", 0, 0, true);
        txt(BODY, cCream, "No osu!standard replays with their maps still installed. Play a few maps in osu! (stable) and press Refresh.", true);
        endSection();
        ImGui::End();
        return;
    }
    switch (tab) {
        case 0: tabOverview(a, rc); break;
        case 1: tabTiming(rc); break;
        case 2: tabAim(rc); break;
        case 3: tabSpeed(rc, a.streamBpm); break;
        case 4: tabReading(rc); break;
        case 5: tabPlays(a); break;
        case 6: tabMaps(a); break;
        default: tabProgress(a.rep); break;
    }
    ImGui::End();
}
