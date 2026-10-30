#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <ctime>
#include <map>
#include "gui.h"
#include "imgui.h"

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

// ---- design system: 8 px grid (scaled by DPI), flat cards, 44 px targets ----
float gS = 1;
float S(float v) { return std::round(v * gS); }
enum { BODY, LABEL, DISPLAY, TITLE };  // Segoe UI 16, Semibold 13, Bahnschrift 40, Semibold 22
ImFont* font(int i) { auto& f = ImGui::GetIO().Fonts->Fonts; return f[std::min(i, f.Size - 1)]; }
constexpr ImU32 cBg = IM_COL32(14, 11, 22, 255), cCard = IM_COL32(26, 21, 38, 255), cRow = IM_COL32(33, 27, 48, 255),
                cLine = IM_COL32(44, 36, 66, 255), cText = IM_COL32(244, 240, 255, 255), cDim = IM_COL32(180, 172, 204, 255),
                cViolet = IM_COL32(155, 107, 255, 255), cPink = IM_COL32(240, 122, 200, 255), cGood = IM_COL32(74, 222, 128, 255),
                cBad = IM_COL32(251, 113, 133, 255);
ImVec4 v4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }
ImU32 alpha(ImU32 c, int a) { return (c & 0x00FFFFFF) | ((ImU32)a << 24); }
double pct(double a, double b) { return b > 0 ? 100 * a / b : 0; }

std::string sf(const char* f, ...) {
    char b[512];
    va_list a;
    va_start(a, f);
    vsnprintf(b, sizeof b, f, a);
    va_end(a);
    return b;
}
void txt(int f, ImU32 c, const std::string& s, bool wrap = false) {
    ImGui::PushFont(font(f));
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    if (wrap) ImGui::TextWrapped("%s", s.c_str());
    else ImGui::TextUnformatted(s.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

// Cards: flat surface, 1 px border, 16 px padding, 16 px gutters between cards.
void card(const char* id, float w, float h = 0) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {S(16), S(16)});
    ImGui::BeginChild(id, {w, h}, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | (h > 0 ? 0 : ImGuiChildFlags_AutoResizeY),
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
}
void endCard() { ImGui::EndChild(); }
float colW(int n, float avail = 0) { return std::floor(((avail ? avail : ImGui::GetContentRegionAvail().x) - S(16) * (n - 1)) / n); }
void nextCol() { ImGui::SameLine(0, S(16)); }
void rowGap() { ImGui::Dummy({0, 0}); }  // 8 px item spacing on each side: 16 px between card rows

void statCard(const char* id, float w, const char* label, const std::string& value, ImU32 vc, const std::string& sub = "", ImU32 sc = cDim) {
    card(id, w, S(160));
    txt(LABEL, cDim, label);
    txt(DISPLAY, vc, value);
    if (!sub.empty()) txt(BODY, sc, sub, true);
    endCard();
}

const char* trendWord(int t) { return t > 0 ? "\xE2\x86\x93 improving" : t < 0 ? "\xE2\x86\x91 regressing" : "\xE2\x86\x92 flat"; }
ImU32 trendCol(int t) { return t > 0 ? cGood : t < 0 ? cBad : cDim; }

// Bar chart filling the content width; mark = index of a reference line (e.g. 0 ms).
void bars(const float* v, int n, float h, int mark = -1) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x, mx = 0;
    ImGui::Dummy({w, h});
    for (int i = 0; i < n; i++) mx = std::max(mx, v[i]);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine({p.x, p.y + h}, {p.x + w, p.y + h}, cLine);
    if (mx <= 0) return;
    float bw = w / n, g = bw >= S(8) ? S(2) : 0;
    for (int i = 0; i < n; i++) {
        float bh = v[i] / mx * h, x = p.x + i * bw + g / 2;
        if (bh >= 1) dl->AddRectFilledMultiColor({x, p.y + h - bh}, {x + bw - g, p.y + h}, cPink, cPink, cViolet, cViolet);
    }
    if (mark >= 0) dl->AddLine({p.x + mark * bw, p.y}, {p.x + mark * bw, p.y + h}, cDim, S(1));
}

// Area chart: violet fill fading to transparent under a pink line, min/max labels on the left.
void area(const std::vector<float>& v, float h, const char* unit) {
    if (v.size() < 2) { txt(BODY, cDim, "Needs at least two days of history."); return; }
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x, mn = *std::min_element(v.begin(), v.end()), mx = *std::max_element(v.begin(), v.end());
    ImGui::Dummy({w, h});
    if (mx - mn < 1e-3f) mx = mn + 1;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float lx = S(48), cw = w - lx, top = p.y + S(8), bot = p.y + h;
    std::vector<ImVec2> pts(v.size());
    for (size_t i = 0; i < v.size(); i++) pts[i] = {p.x + lx + cw * i / (v.size() - 1), bot - (v[i] - mn) / (mx - mn) * (bot - top)};
    ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    ImU32 hiC = alpha(cViolet, 120), loC = alpha(cViolet, 0);
    dl->PrimReserve((int)(pts.size() - 1) * 6, (int)(pts.size() - 1) * 4);
    for (size_t i = 0; i + 1 < pts.size(); i++) {
        ImDrawIdx k = (ImDrawIdx)dl->_VtxCurrentIdx;
        dl->PrimWriteVtx(pts[i], uv, hiC), dl->PrimWriteVtx(pts[i + 1], uv, hiC);
        dl->PrimWriteVtx({pts[i + 1].x, bot}, uv, loC), dl->PrimWriteVtx({pts[i].x, bot}, uv, loC);
        for (int d : {0, 1, 2, 0, 2, 3}) dl->PrimWriteIdx((ImDrawIdx)(k + d));
    }
    dl->AddLine({p.x + lx, bot}, {p.x + w, bot}, cLine);
    dl->AddPolyline(pts.data(), (int)pts.size(), cPink, 0, S(2));
    ImFont* f = font(LABEL);
    dl->AddText(f, f->FontSize, {p.x, top - f->FontSize / 2}, cDim, sf("%.*f%s", *unit ? 1 : 0, mx, unit).c_str());
    dl->AddText(f, f->FontSize, {p.x, bot - f->FontSize}, cDim, sf("%.*f%s", *unit ? 1 : 0, mn, unit).c_str());
}

// Half-circle gauge with the value printed in the middle; frac 0..1 = how good.
void gauge(float frac, const std::string& value, const char* caption) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x, r = S(56), th = S(8);
    ImVec2 c{p.x + w / 2, p.y + r + th / 2};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float PI = 3.14159265f;
    dl->PathArcTo(c, r, PI, 2 * PI, 48);
    dl->PathStroke(cLine, 0, th);
    dl->PathArcTo(c, r, PI, PI + PI * std::clamp(frac, 0.02f, 1.f), 48);
    dl->PathStroke(cViolet, 0, th);
    ImFont* f = font(DISPLAY);
    ImVec2 ts = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0, value.c_str());
    dl->AddText(f, f->FontSize, {c.x - ts.x / 2, c.y - ts.y * 0.8f}, cText, value.c_str());
    ImGui::Dummy({w, r + th + S(8)});
    ImVec2 cs = ImGui::CalcTextSize(caption);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (w - cs.x) / 2);
    txt(BODY, cDim, caption);
}

void radar(const Report& r, float size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos(), c{p.x + size / 2, p.y + size / 2};
    float R = size * 0.34f;
    auto pt = [&](int i, float v) { float a = -3.14159265f / 2 + i * 2 * 3.14159265f / NSKILL; return ImVec2{c.x + std::cos(a) * R * v, c.y + std::sin(a) * R * v}; };
    for (float ring : {0.25f, 0.5f, 0.75f, 1.f}) {
        ImVec2 q[NSKILL];
        for (int i = 0; i < NSKILL; i++) q[i] = pt(i, ring);
        dl->AddPolyline(q, NSKILL, ring == 0.5f ? cDim : cLine, ImDrawFlags_Closed, ring == 0.5f ? S(1.5f) : S(1));
    }
    ImVec2 v[NSKILL];
    for (int i = 0; i < NSKILL; i++) {
        dl->AddLine(c, pt(i, 1), cLine);
        v[i] = pt(i, std::max(0.03f, r.radar[i] / 100));
    }
    for (int i = 0; i < NSKILL; i++) dl->AddTriangleFilled(c, v[i], v[(i + 1) % NSKILL], alpha(cViolet, 70));
    dl->AddPolyline(v, NSKILL, cPink, ImDrawFlags_Closed, S(2.5f));
    ImFont* fl = font(LABEL);
    ImFont* fv = font(TITLE);
    for (int i = 0; i < NSKILL; i++) {
        ImVec2 q = pt(i, 1.28f);
        std::string val = sf("%.0f", r.radar[i]);
        ImVec2 ls = fl->CalcTextSizeA(fl->FontSize, FLT_MAX, 0, kSkillName[i]), vs = fv->CalcTextSizeA(fv->FontSize, FLT_MAX, 0, val.c_str());
        ImU32 col = r.radar[i] < 45 ? cBad : r.radar[i] > 55 ? cGood : cText;
        dl->AddText(fl, fl->FontSize, {q.x - ls.x / 2, q.y - (ls.y + vs.y) / 2}, cDim, kSkillName[i]);
        dl->AddText(fv, fv->FontSize, {q.x - vs.x / 2, q.y - (ls.y + vs.y) / 2 + ls.y}, col, val.c_str());
    }
    ImGui::Dummy({size, size});
}

// Tables: flat rows, 32 px tall (8 px padding around 16 px text).
bool table(const char* id, int cols, float h = 0, ImGuiTableFlags extra = 0) {
    return ImGui::BeginTable(id, cols, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX | extra, {0, h});
}
void cell(ImU32 c, const std::string& s) { ImGui::TableNextColumn(); txt(BODY, c, s); }

std::string modStr(int m) {
    using namespace mod;
    static const std::pair<int, const char*> t[] = {{NF, "NF"}, {EZ, "EZ"}, {HD, "HD"}, {HR, "HR"}, {SD, "SD"}, {NC, "NC"}, {DT, "DT"}, {HT, "HT"}, {FL, "FL"}, {SO, "SO"}, {PF, "PF"}};
    std::string s;
    for (auto [b, n] : t) if ((m & b) && !(b == DT && (m & NC)) && !(b == SD && (m & PF))) s += n;
    return s.empty() ? "NM" : s;
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

// Secondary button: card-coloured with a border; primary buttons use the default (violet) style.
bool button2(const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Button, cCard), ImGui::PushStyleColor(ImGuiCol_ButtonHovered, cRow), ImGui::PushStyleColor(ImGuiCol_ButtonActive, cLine);
    ImGui::PushStyleColor(ImGuiCol_Text, cText);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, S(1));
    bool r = ImGui::Button(label);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    return r;
}

void tabOverview(App& a, const Recent& rc) {
    const Report& r = a.rep;
    float w = colW(4);
    card("verdict", w, S(160));
    txt(LABEL, cDim, "Coach verdict");
    txt(TITLE, cPink, r.verdict, true);
    const char* names[] = {"UR", "Jump misses", "Stream misses"};
    for (int i = 0; i < 3; i++) txt(LABEL, trendCol(r.trend[i]), sf("%s %s this week", names[i], trendWord(r.trend[i])));
    endCard();
    nextCol();
    card("ur", w, S(160));
    txt(LABEL, cDim, "Unstable rate");
    gauge(1 - ((float)rc.err.ur() - 80) / 220, sf("%.0f", rc.err.ur()), trendWord(r.trend[0]));
    endCard();
    nextCol();
    statCard("plays", w, "Recent plays", sf("%zu", rc.plays), cText, sf("of %zu analysed", a.res.plays.size()));
    nextCol();
    statCard("acc", w, "Average accuracy", sf("%.2f%%", 100 * rc.accSum / std::max<size_t>(1, rc.plays)), cText,
             sf("%.0f%% full combos", pct(rc.fcs, rc.plays)));
    rowGap();

    float h = ImGui::GetContentRegionAvail().y, lw = std::floor(ImGui::GetContentRegionAvail().x * 0.42f);
    card("radar", lw, h);
    txt(LABEL, cDim, "Skill radar");
    txt(BODY, cDim, r.needHistory ? "Needs older plays as a baseline: every skill shows 50 for now." : "50 = your usual miss rate at this star rating, +25 per halving.", true);
    float avail = ImGui::GetContentRegionAvail().x, rs = std::min(avail, ImGui::GetContentRegionAvail().y);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - rs) / 2);
    radar(r, rs);
    endCard();
    nextCol();
    ImGui::BeginChild("weak", {0, h}, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);

    if (!r.nTop) {
        card("none", 0);
        txt(BODY, cText, "No weakness costs measurable pp in your recent plays. Push the star rating up.", true);
        endCard();
    }
    for (int i = 0; i < r.nTop; i++) {
        auto& wk = r.top[i];
        ImGui::PushID(i);
        card("w", 0);
        txt(TITLE, cPink, sf("#%d  %s", i + 1, kSkillName[wk.s]));
        ImGui::SameLine(0, S(16));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + font(TITLE)->FontSize - font(LABEL)->FontSize - S(2));
        txt(LABEL, cDim, sf("~%.1f pp lost per play", wk.pp / std::max<size_t>(1, rc.plays)));
        txt(BODY, cText, wk.metric, true);
        txt(LABEL, cGood, "FIX");
        txt(BODY, cText, wk.fix, true);
        endCard();
        rowGap();
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void tabTiming(const Recent& rc) {
    double n = rc.err.n;
    float w = colW(4);
    int keys = rc.keyHits[0] + rc.keyHits[1];
    double m = rc.err.mean();
    statCard("ur", w, "Unstable rate", sf("%.0f", rc.err.ur()), cText, "10 \xC3\x97 spread of your hit errors");
    nextCol();
    statCard("mean", w, "Mean offset", sf("%+.1f ms", m), cText, m < -2 ? "you hit early" : m > 2 ? "you hit late" : "centred");
    nextCol();
    statCard("win", w, "Within \xC2\xB1""15 ms", sf("%.0f%%", pct(rc.within[0], n)), cText,
             sf("%.0f%% within \xC2\xB1""30  \xC2\xB7  %.0f%% within \xC2\xB1""50", pct(rc.within[1], n), pct(rc.within[2], n)));
    nextCol();
    statCard("keys", w, "Key 1 / key 2 share", sf("%.0f / %.0f", pct(rc.keyHits[0], keys), pct(rc.keyHits[1], keys)), cText,
             sf("UR %.0f / %.0f", rc.errKey[0].ur(), rc.errKey[1].ur()));
    rowGap();
    card("hist", 0);
    txt(LABEL, cDim, "Hit errors, -100 ms to +100 ms (line = 0 ms)");
    ImGui::Dummy({0, S(8)});
    bars(rc.hist, 100, S(240), 50);
    endCard();
}

void tabAim(const Recent& rc) {
    const char* names[] = {"Dense (\xE2\x89\xA4""2 r)", "Stream spacing (2-4 r)", "Jumps (4-7 r)", "Big jumps (>7 r)"};
    float w = colW(4);
    for (int b = 0; b < 4; b++) {
        if (b) nextCol();
        ImGui::PushID(b);
        statCard("b", w, names[b], sf("%.2f%%", pct(rc.bMiss[b], rc.bObj[b])), cText,
                 sf("%d notes  \xC2\xB7  aim error %.2f r", rc.bObj[b], rc.bObj[b] > rc.bMiss[b] ? rc.bDist[b] / (rc.bObj[b] - rc.bMiss[b]) : 0));
        ImGui::PopID();
    }
    rowGap();
    float half = colW(2);
    card("quad", half, S(256));
    txt(LABEL, cDim, "Miss rate by screen quadrant");
    ImGui::Dummy({0, S(8)});
    {
        static const char* qn[] = {"Top-left", "Top-right", "Bottom-left", "Bottom-right"};
        double mx = 0;
        for (int q = 0; q < 4; q++) mx = std::max(mx, pct(rc.qMiss[q], rc.qObj[q]));
        ImVec2 p = ImGui::GetCursorScreenPos();
        float tw = (ImGui::GetContentRegionAvail().x - S(8)) / 2, th = (ImGui::GetContentRegionAvail().y - S(8)) / 2;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int q = 0; q < 4; q++) {
            double m = pct(rc.qMiss[q], rc.qObj[q]);
            ImVec2 a{p.x + (q % 2) * (tw + S(8)), p.y + (q / 2) * (th + S(8))};
            dl->AddRectFilled(a, {a.x + tw, a.y + th}, cRow, S(8));
            dl->AddRectFilled(a, {a.x + tw, a.y + th}, alpha(cViolet, mx > 0 ? (int)(110 * m / mx) : 0), S(8));
            dl->AddText(font(LABEL), font(LABEL)->FontSize, {a.x + S(16), a.y + S(16)}, cText, qn[q]);
            dl->AddText(font(TITLE), font(TITLE)->FontSize, {a.x + S(16), a.y + S(40)}, cText, sf("%.2f%%", m).c_str());
        }
    }
    endCard();
    nextCol();
    card("over", half, S(256));
    int am = rc.under + rc.over;
    txt(LABEL, cDim, "Aim misses along the jump");
    txt(DISPLAY, cText, sf("%.0f%% short", pct(rc.under, am)));
    txt(BODY, cDim, sf("%d undershoots, %d overshoots", rc.under, rc.over));
    ImGui::Dummy({0, S(8)});
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float bw = ImGui::GetContentRegionAvail().x, f = am ? (float)rc.under / am : 0.5f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, {p.x + bw * f, p.y + S(16)}, cViolet, S(8), ImDrawFlags_RoundCornersLeft);
        dl->AddRectFilled({p.x + bw * f, p.y}, {p.x + bw, p.y + S(16)}, cPink, S(8), ImDrawFlags_RoundCornersRight);
        ImGui::Dummy({bw, S(16)});
        txt(LABEL, cDim, "violet = stopped short of the note, pink = went past it");
    }
    endCard();
    rowGap();
    card("jump", 0);
    txt(LABEL, cDim, "Distance between consecutive notes, 0 to 12 radii");
    ImGui::Dummy({0, S(8)});
    bars(rc.jump, 24, std::max(S(96), ImGui::GetContentRegionAvail().y - S(16)));
    endCard();
}

void tabSpeed(const Recent& rc, float streamBpm) {
    float w = colW(4);
    double ov = pct(rc.zOv[0], rc.zPairs[0]);
    statCard("bpm", w, "Comfortable stream BPM", streamBpm > 0 ? sf("%.0f", streamBpm) : "-", cText, "median of streams with no misses");
    nextCol();
    statCard("lock", w, "Finger lock on fast notes", sf("%.0f%%", ov), ov > 20 ? cBad : cText, "presses held into the next one");
    nextCol();
    statCard("switch", w, "Switch zone UR", sf("%.0f", rc.zone[1].ur()), cText, "gaps of 100-130 ms");
    nextCol();
    statCard("stam", w, "Longest run", sf("%d notes", rc.stamNotes), cText, sf("%.0f s at %.1f notes/s", rc.stamSec, rc.stamNps));
    rowGap();
    card("streams", 0);
    txt(LABEL, cDim, "Streams: 4+ evenly spaced 1/4 notes within 3 radii");
    const char* bands[] = {"< 150 BPM", "150-170", "170-190", "190-210", "210+"};
    if (table("st", 7)) {
        for (auto h : {"BPM", "Streams", "Notes", "Miss %", "Alternate %", "Notes 1-4 mean", "Notes 5+ mean"}) ImGui::TableSetupColumn(h);
        ImGui::TableHeadersRow();
        for (int i = 0; i < 5; i++) {
            auto& b = rc.band[i];
            if (!b.segs) continue;
            double late = b.lN ? b.late / b.lN : 0;
            ImGui::TableNextRow();
            cell(cText, bands[i]), cell(cText, sf("%d", b.segs)), cell(cText, sf("%d", b.notes)), cell(cText, sf("%.1f%%", pct(b.miss, b.notes)));
            cell(cText, sf("%.0f%%", pct(b.alt, b.alt + b.dbl))), cell(cText, sf("%+.1f ms", b.eN ? b.early / b.eN : 0));
            cell(late < -8 ? cBad : cText, sf("%+.1f ms%s", late, late < -8 ? "  rushing" : ""));
        }
        ImGui::EndTable();
    }
    endCard();
    rowGap();
    card("zones", 0);
    txt(LABEL, cDim, "Tapping by gap between notes (overlap = key still held when the next press comes)");
    const char* zones[] = {"Fast (< 100 ms)", "Switch zone (100-130 ms)", "1/2 (130-260 ms)", "Slow (260+ ms)"};
    if (table("zt", 5)) {
        for (auto h : {"Gap", "Hits", "UR", "Alternate %", "Overlap %"}) ImGui::TableSetupColumn(h);
        ImGui::TableHeadersRow();
        for (int z = 0; z < 4; z++) {
            double o = pct(rc.zOv[z], rc.zPairs[z]);
            ImGui::TableNextRow();
            cell(cText, zones[z]), cell(cText, sf("%.0f", rc.zone[z].n)), cell(cText, sf("%.0f", rc.zone[z].ur()));
            cell(cText, sf("%.0f%%", pct(rc.zAlt[z], rc.zPairs[z]))), cell(o > 20 ? cBad : cText, sf("%.0f%%%s", o, o > 20 ? "  finger lock" : ""));
        }
        ImGui::EndTable();
    }
    endCard();
}

void tabReading(const Recent& rc) {
    float w = colW(4);
    statCard("brk", w, "First note after a break", sf("%.2f%%", pct(rc.brkMiss, rc.brkObj)), cText, sf("%d of %d missed", rc.brkMiss, rc.brkObj));
    nextCol();
    statCard("flow", w, "Notes mid-flow", sf("%.2f%%", pct(rc.flowMiss, rc.flowObj)), cText, sf("%d of %d missed", rc.flowMiss, rc.flowObj));
    nextCol();
    statCard("arlo", w, "AR below 8", sf("%.2f%%", pct(rc.arMiss[0], rc.arObj[0])), cText, sf("of %d notes missed", rc.arObj[0]));
    nextCol();
    statCard("arhi", w, "AR 8 and above", sf("%.2f%%", pct(rc.arMiss[1], rc.arObj[1])), cText, sf("of %d notes missed", rc.arObj[1]));
    rowGap();
    card("note", 0);
    txt(LABEL, cDim, "How to read this");
    txt(BODY, cText, "AR includes mods (HR, EZ, DT, HT). If the first note after a break or the AR 8+ row is much worse than the other, "
                     "the problem is reading the approach circles, not aim.", true);
    endCard();
}

void missList(const PlayStats& p, const std::vector<ObjRes>& objs, float h) {
    if (objs.empty()) { txt(BODY, cDim, "The replay or its map is gone, so the per-note list is unavailable."); return; }
    float beat = p.bpm > 0 ? 60000 / p.bpm : 500;
    if (table("misses", 4, h, ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, S(88));
        ImGui::TableSetupColumn("Note", ImGuiTableColumnFlags_WidthFixed, S(64));
        ImGui::TableSetupColumn("Context", ImGuiTableColumnFlags_WidthFixed, S(240));
        ImGui::TableSetupColumn("Why", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < objs.size(); i++) {
            auto& o = objs[i];
            if (o.judge) continue;
            int mt = (int)(o.t * p.rate);
            float frac = o.gap / beat;
            std::string ctx = frac < 0.3f ? "1/4" : frac < 0.4f ? "1/3" : frac < 0.6f ? "1/2" : frac < 1.1f ? "1/1" : "after a gap";
            if (o.spacing > 4) ctx += ", jump " + std::to_string((int)std::round(o.spacing)) + " r";
            if (o.slider) ctx += ", slider head";
            if (o.inStream) ctx += ", in stream";
            std::string why = o.cause == AIM ? sf("Aim: tapped on time (%+.0f ms) but %.1f radii away, %s", o.err, o.dist, o.along < 0 ? "undershot" : o.along > 0 ? "overshot" : "off to the side")
                            : o.cause == TAP ? "Tap: your press was used by the previous note"
                            : o.cause == OFFTIME ? sf("Timing: tapped %.0f ms early, outside the hit window", -o.err)
                                                 : "No press near this note";
            ImGui::TableNextRow();
            cell(cText, sf("%d:%02d.%03d", mt / 60000, mt / 1000 % 60, mt % 1000)), cell(cDim, sf("#%zu", i + 1)), cell(cText, ctx), cell(cText, why);
        }
        ImGui::EndTable();
    }
}

void tabPlays(App& a) {
    auto& ps = a.res.plays;
    static const char* cols[] = {"Date", "Map", "Stars", "Mods", "Acc", "Misses", "UR"};
    static std::vector<int> order;
    static int orderFor = -1, sortCol = 0, sortedCol = -1;
    static bool asc = false, sortedAsc = true;
    bool sel = a.selPlay >= 0 && a.selPlay < (int)ps.size();
    float total = ImGui::GetContentRegionAvail().y, top = sel ? std::floor(total * 0.42f) : total;
    card("list", 0, top);
    // sorting lives in 44 px controls: ImGui's sortable headers can't be both 44 px tall and vertically centred
    ImGui::AlignTextToFramePadding();
    txt(LABEL, cDim, "Sort by");
    ImGui::SameLine(0, S(8));
    ImGui::SetNextItemWidth(S(160));
    ImGui::Combo("##sort", &sortCol, cols, 7);
    ImGui::SameLine(0, S(8));
    if (button2(asc ? "Ascending \xE2\x86\x91" : "Descending \xE2\x86\x93")) asc = !asc;
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
    // 44 px rows: CellPadding.y 0, rows sized explicitly, text centred with AlignTextToFramePadding
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {S(8), 0});
    if (table("plays", 7, ImGui::GetContentRegionAvail().y, ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthFixed, S(144));
        ImGui::TableSetupColumn("Map", ImGuiTableColumnFlags_WidthStretch);
        for (int c = 2; c < 7; c++) ImGui::TableSetupColumn(cols[c], ImGuiTableColumnFlags_WidthFixed, S(80));
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers, S(44));
        for (int c = 0; c < 7; c++) {
            ImGui::TableSetColumnIndex(c);
            ImGui::AlignTextToFramePadding();
            txt(LABEL, c == sortCol ? cText : cDim, c == sortCol ? sf("%s %s", cols[c], asc ? "\xE2\x86\x91" : "\xE2\x86\x93") : cols[c]);
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
                ImVec2 rmin = ImGui::GetItemRectMin();
                if (isSel) ImGui::GetWindowDrawList()->AddRectFilled(rmin, {rmin.x + S(4), rmin.y + S(44)}, cViolet);
                auto col = [](ImU32 c, const std::string& s) { ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); txt(BODY, c, s); };
                col(cText, p.title), col(p.stars > 0 ? cText : cDim, p.stars > 0 ? sf("%.2f", p.stars) : "?"), col(cDim, modStr(p.mods));
                col(cText, sf("%.2f%%", 100 * p.acc)), col(p.gmiss ? cText : cGood, sf("%d", p.gmiss)), col(cText, sf("%.0f", p.err.ur()));
            }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    endCard();
    if (!sel) return;
    rowGap();
    auto& p = ps[a.selPlay];
    card("detail", 0, ImGui::GetContentRegionAvail().y);
    txt(TITLE, cText, p.title);
    txt(BODY, cDim, sf("%s  \xC2\xB7  %s  \xC2\xB7  %.2f%%  \xC2\xB7  %d misses (simulated %d incl. slider heads)  \xC2\xB7  UR %.0f  \xC2\xB7  mean %+.1f ms  \xC2\xB7  max combo %d",
                       dateStr(p.when).c_str(), modStr(p.mods).c_str(), 100 * p.acc, p.gmiss, p.sMiss, p.err.ur(), p.err.mean(), p.combo));
    ImGui::Dummy({0, S(8)});
    float rest = ImGui::GetContentRegionAvail().y;
    missList(p, a.selectedObjs(), std::max(S(96), rest - S(104)));
    float hist[100];
    for (int k = 0; k < 100; k++) hist[k] = (float)p.hist[k];
    txt(LABEL, cDim, "Hit errors of this play, -100 to +100 ms");
    bars(hist, 100, S(64), 50);
    endCard();
}

void tabMaps(App& a) {
    card("ctl", 0);
    txt(LABEL, cDim, "Training maps from your Songs folder");
    txt(BODY, cText, "Five maps that train the selected skill. Maps you have played 0-3 times in the last 30 days come first.", true);
    ImGui::Dummy({0, S(8)});
    ImGui::SetNextItemWidth(S(320));
    ImGui::DragFloatRange2("##stars", &a.lo, &a.hi, 0.05f, 1, 10, "%.2f\xE2\x98\x85");
    ImGui::SameLine(0, S(16));
    const char* items[NSKILL + 1] = {"Top weakness"};
    for (int i = 0; i < NSKILL; i++) items[i + 1] = kSkillName[i];
    int cur = a.trainSkill + 1;
    ImGui::SetNextItemWidth(S(200));
    if (ImGui::Combo("##train", &cur, items, NSKILL + 1)) a.trainSkill = cur - 1;
    ImGui::SameLine(0, S(16));
    ImGui::BeginDisabled(a.busy);
    if (ImGui::Button("Find maps")) a.startSuggest();
    ImGui::EndDisabled();
    if (a.busy) { ImGui::SameLine(0, S(16)); ImGui::AlignTextToFramePadding(); txt(BODY, cDim, "searching..."); }
    endCard();
    rowGap();
    if (a.sugg.empty()) {
        card("empty", 0);
        txt(BODY, cText, "No maps in this star range. Widen the range or download a few maps.");
        endCard();
    }
    float w = colW(2);
    for (size_t i = 0; i < a.sugg.size(); i++) {
        auto& s = a.sugg[i];
        if (i % 2) nextCol();
        else if (i) rowGap();
        ImGui::PushID((int)i);
        card("m", w, S(168));
        txt(LABEL, cPink, sf("#%zu", i + 1));
        txt(BODY, cText, s.map->title);
        txt(BODY, cDim, s.why, true);
        ImGui::SetCursorPosY(ImGui::GetWindowHeight() - S(16) - S(44));
        if (s.map->bid > 0) {
            if (ImGui::Button("Open page")) open(L"https://osu.ppy.sh/b/" + std::to_wstring(s.map->bid));
            ImGui::SameLine(0, S(8));
        }
        if (button2("Show in folder")) open(L"explorer.exe", L"/select,\"" + s.map->path + L"\"");
        endCard();
        ImGui::PopID();
    }
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
    card("v", 0);
    txt(LABEL, cDim, "Coach verdict");
    txt(TITLE, cPink, r.verdict);
    txt(BODY, cDim, byDate.empty() ? std::string("No history yet.")
                                   : sf("%zu days of history, %d to %d, kept in history.json so it survives deleted maps.", byDate.size(), byDate.begin()->first, byDate.rbegin()->first));
    endCard();
    rowGap();
    const char* names[] = {"Unstable rate", "Jump miss rate", "Stream miss rate"};
    const char* units[] = {"", "%", "%"};
    std::vector<float>* series[] = {&ur, &jm, &sm};
    float h = std::floor((ImGui::GetContentRegionAvail().y - 2 * S(16)) / 3);
    for (int i = 0; i < 3; i++) {
        if (i) rowGap();
        ImGui::PushID(i);
        card("c", 0, h);
        txt(LABEL, cDim, names[i]);
        ImGui::SameLine(0, S(16));
        txt(LABEL, trendCol(r.trend[i]), sf("%s this week (lower is better)", trendWord(r.trend[i])));
        if (!series[i]->empty()) txt(TITLE, cText, sf("%.*f%s", i ? 2 : 0, series[i]->back(), units[i]));
        area(*series[i], std::max(S(48), ImGui::GetContentRegionAvail().y), units[i]);
        endCard();
        ImGui::PopID();
    }
}
}

int gForceTab = -1;

void setupStyle(float dpi) {
    gS = dpi;
    ImGuiIO& io = ImGui::GetIO();
    static ImVector<ImWchar> ranges;
    ImFontGlyphRangesBuilder b;
    b.AddRanges(io.Fonts->GetGlyphRangesDefault());
    b.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
    b.AddText("\xE2\x98\x85\xE2\x80\x94\xE2\x86\x91\xE2\x86\x93\xE2\x86\x92\xE2\x80\xA6\xC2\xB7\xC2\xB1\xC3\x97\xE2\x89\xA4");  // ★ — ↑ ↓ → … · ± × ≤
    b.BuildRanges(&ranges);
    wchar_t win[MAX_PATH];
    GetWindowsDirectoryW(win, MAX_PATH);
    auto path = [&](const wchar_t* f) { return narrow(std::wstring(win) + L"\\Fonts\\" + f); };
    std::string body = path(L"segoeui.ttf"), semi = path(L"seguisb.ttf"), disp = path(L"bahnschrift.ttf"), sym = path(L"seguisym.ttf");
    if (GetFileAttributesA(semi.c_str()) == INVALID_FILE_ATTRIBUTES) semi = body;
    if (GetFileAttributesA(disp.c_str()) == INVALID_FILE_ATTRIBUTES) disp = semi;
    static const ImWchar star[] = {0x2605, 0x2605, 0};
    ImFontConfig mc;
    mc.MergeMode = true;
    // order matches BODY, LABEL, DISPLAY, TITLE; ★ merged from Segoe UI Symbol into each
    const std::pair<std::string*, float> fonts[] = {{&body, 16}, {&semi, 13}, {&disp, 40}, {&semi, 22}};
    for (auto& [f, size] : fonts) {
        if (!io.Fonts->AddFontFromFileTTF(f->c_str(), S(size), nullptr, ranges.Data)) { io.Fonts->AddFontDefault(); continue; }
        io.Fonts->AddFontFromFileTTF(sym.c_str(), S(size), &mc, star);
    }

    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    st.WindowPadding = {24, 24}, st.FramePadding = {16, 14}, st.ItemSpacing = {8, 8}, st.ItemInnerSpacing = {8, 8};
    st.CellPadding = {8, 8}, st.ScrollbarSize = 16, st.GrabMinSize = 16;
    st.WindowRounding = 0, st.ChildRounding = 16, st.FrameRounding = 8, st.GrabRounding = 8, st.PopupRounding = 8, st.ScrollbarRounding = 8;
    st.WindowBorderSize = 0, st.ChildBorderSize = 1, st.FrameBorderSize = 0, st.PopupBorderSize = 1;
    st.SelectableTextAlign = {0, 0.5f};
    st.ScaleAllSizes(dpi);
    ImVec4* c = st.Colors;
    for (int i = 0; i < ImGuiCol_COUNT; i++) c[i] = v4(cText);
    c[ImGuiCol_TextDisabled] = v4(cDim);
    c[ImGuiCol_WindowBg] = v4(cBg), c[ImGuiCol_ChildBg] = v4(cCard), c[ImGuiCol_PopupBg] = v4(cCard);
    c[ImGuiCol_Border] = v4(cLine), c[ImGuiCol_BorderShadow] = v4(0), c[ImGuiCol_Separator] = v4(cLine);
    c[ImGuiCol_FrameBg] = v4(cRow), c[ImGuiCol_FrameBgHovered] = v4(cLine), c[ImGuiCol_FrameBgActive] = v4(cLine);
    c[ImGuiCol_Button] = v4(cViolet), c[ImGuiCol_ButtonHovered] = v4(IM_COL32(176, 140, 255, 255)), c[ImGuiCol_ButtonActive] = v4(IM_COL32(130, 84, 240, 255));
    c[ImGuiCol_Header] = v4(cLine), c[ImGuiCol_HeaderHovered] = v4(cRow), c[ImGuiCol_HeaderActive] = v4(cLine);
    c[ImGuiCol_TableHeaderBg] = v4(cCard), c[ImGuiCol_TableBorderLight] = v4(cLine), c[ImGuiCol_TableBorderStrong] = v4(cLine);
    c[ImGuiCol_TableRowBg] = v4(cCard), c[ImGuiCol_TableRowBgAlt] = v4(cRow);
    c[ImGuiCol_ScrollbarBg] = v4(cCard), c[ImGuiCol_ScrollbarGrab] = v4(cLine), c[ImGuiCol_ScrollbarGrabHovered] = v4(cDim), c[ImGuiCol_ScrollbarGrabActive] = v4(cDim);
    c[ImGuiCol_SliderGrab] = v4(cViolet), c[ImGuiCol_SliderGrabActive] = v4(cPink), c[ImGuiCol_CheckMark] = v4(cViolet);
    c[ImGuiCol_PlotHistogram] = v4(cViolet), c[ImGuiCol_PlotLines] = v4(cPink), c[ImGuiCol_NavHighlight] = v4(cPink);
    c[ImGuiCol_ModalWindowDimBg] = v4(alpha(cBg, 160)), c[ImGuiCol_TextSelectedBg] = v4(alpha(cViolet, 110));
}

void drawApp(App& a) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("osu-coach", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    static Recent rc;
    static int builtFor = -1;
    if (a.scanned && builtFor != a.gen) rc = build(a.res.plays, a.rep.recentFrom), builtFor = a.gen;

    txt(TITLE, cText, "osu-coach");
    ImGui::SameLine(0, S(16));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + font(TITLE)->FontSize - font(BODY)->FontSize);
    if (a.busy && (!a.scanned || a.done < a.total)) {
        int t = a.total, d = a.done;
        txt(BODY, cDim, t ? sf("analysing replays  %d / %d", d, t) : "reading osu!.db...");
    } else if (a.scanned) {
        auto& s = a.res;
        txt(BODY, cDim, sf("%zu plays analysed  \xC2\xB7  skipped: %d map deleted, %d other modes, %d relax/autopilot/auto, %d unreadable",
                           s.plays.size(), s.deleted, s.otherMode, s.modSkip, s.corrupt));
    }
    if (a.busy && a.total > 0 && a.done < a.total) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        ImGui::GetWindowDrawList()->AddRectFilled(p, {p.x + w, p.y + S(4)}, cLine, S(2));
        ImGui::GetWindowDrawList()->AddRectFilled(p, {p.x + w * a.done / a.total, p.y + S(4)}, cViolet, S(2));
        ImGui::Dummy({w, S(4)});
    }
    ImGui::Dummy({0, S(8)});

    if (a.osuDir.empty()) {
        card("pick", 0);
        txt(TITLE, cText, "osu! folder not found");
        txt(BODY, cDim, "Pick the folder that contains osu!.db (for example C:\\Users\\you\\AppData\\Local\\osu!).", true);
        ImGui::Dummy({0, S(8)});
        if (ImGui::Button("Choose osu! folder")) a.pickFolder();
        endCard();
        ImGui::End();
        return;
    }

    // navigation: 44 px pills; the current tab is violet with dark text (5.4:1)
    static const char* kTabs[] = {"Overview", "Timing", "Aim", "Speed", "Reading", "Plays", "Maps", "Progress"};
    static int tab = 0;
    if (gForceTab >= 0) tab = gForceTab;
    bool ready = a.scanned && !a.res.plays.empty();
    if (ready)
        for (int i = 0; i < 8; i++) {
            if (i) ImGui::SameLine(0, S(8));
            bool on = tab == i;
            if (!on) ImGui::PushStyleColor(ImGuiCol_Button, cCard), ImGui::PushStyleColor(ImGuiCol_ButtonHovered, cRow), ImGui::PushStyleColor(ImGuiCol_ButtonActive, cLine);
            else ImGui::PushStyleColor(ImGuiCol_Text, cBg);
            if (ImGui::Button(kTabs[i])) tab = i;
            ImGui::PopStyleColor(on ? 1 : 3);
        }
    float bw = ImGui::CalcTextSize("Refresh").x + ImGui::CalcTextSize("Change folder").x + 4 * ImGui::GetStyle().FramePadding.x + S(8);
    if (ready) ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - bw));
    ImGui::BeginDisabled(a.busy);
    if (button2("Refresh")) a.startScan();
    ImGui::SameLine(0, S(8));
    if (button2("Change folder")) a.pickFolder();
    ImGui::EndDisabled();
    ImGui::Dummy({0, S(8)});

    if (a.scanned && !a.res.dbOk) {
        card("dberr", 0);
        txt(BODY, cBad, "osu!.db could not be read: star ratings and map suggestions are off.");
        endCard();
        rowGap();
    }
    if (!a.scanned) { ImGui::End(); return; }
    if (a.res.plays.empty()) {
        card("noplays", 0);
        txt(BODY, cText, "No standard-mode replays with their maps still installed. Play a few maps in osu! (stable) and press Refresh.", true);
        endCard();
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
