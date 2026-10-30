#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <map>
#include "gui.h"
#include "imgui.h"

namespace {
// Recent-window totals for the detail tabs, rebuilt once per scan.
struct Band { int segs = 0, notes = 0, miss = 0, alt = 0, dbl = 0; double early = 0, late = 0, eN = 0, lN = 0; };
struct Recent {
    size_t plays = 0;
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
        r.plays++;
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

const ImVec4 kGood{0.45f, 0.85f, 0.5f, 1}, kBad{0.95f, 0.45f, 0.4f, 1}, kDim{0.6f, 0.6f, 0.65f, 1}, kAccent{1, 0.55f, 0.75f, 1};
double pct(double a, double b) { return b > 0 ? 100 * a / b : 0; }

std::string modStr(int m) {
    static const std::pair<int, const char*> t[] = {{1, "NF"}, {2, "EZ"}, {8, "HD"}, {16, "HR"}, {32, "SD"}, {512, "NC"}, {64, "DT"}, {256, "HT"}, {1024, "FL"}, {4096, "SO"}, {16384, "PF"}};
    std::string s;
    for (auto [b, n] : t) if ((m & b) && !(b == 64 && (m & 512)) && !(b == 32 && (m & 16384))) s += n;
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

void radar(const Report& r, float size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos(), c{p.x + size / 2, p.y + size / 2};
    float R = size * 0.36f;
    auto pt = [&](int i, float v) { float a = -3.14159265f / 2 + i * 2 * 3.14159265f / NSKILL; return ImVec2{c.x + std::cos(a) * R * v, c.y + std::sin(a) * R * v}; };
    for (float ring : {0.25f, 0.5f, 0.75f, 1.f}) {
        ImVec2 q[NSKILL];
        for (int i = 0; i < NSKILL; i++) q[i] = pt(i, ring);
        dl->AddPolyline(q, NSKILL, ring == 0.5f ? IM_COL32(150, 150, 170, 160) : IM_COL32(90, 90, 105, 120), ImDrawFlags_Closed, ring == 0.5f ? 1.5f : 1);
    }
    ImVec2 v[NSKILL];
    for (int i = 0; i < NSKILL; i++) {
        dl->AddLine(c, pt(i, 1), IM_COL32(90, 90, 105, 120));
        v[i] = pt(i, std::max(0.03f, r.radar[i] / 100));
    }
    for (int i = 0; i < NSKILL; i++) dl->AddTriangleFilled(c, v[i], v[(i + 1) % NSKILL], IM_COL32(255, 120, 180, 60));
    dl->AddPolyline(v, NSKILL, IM_COL32(255, 120, 180, 255), ImDrawFlags_Closed, 2.5f);
    for (int i = 0; i < NSKILL; i++) {
        char t[48];
        snprintf(t, sizeof t, "%s %.0f", kSkillName[i], r.radar[i]);
        ImVec2 q = pt(i, 1.2f), ts = ImGui::CalcTextSize(t);
        dl->AddText({q.x - ts.x / 2, q.y - ts.y / 2}, r.radar[i] < 45 ? IM_COL32(242, 115, 102, 255) : r.radar[i] > 55 ? IM_COL32(115, 217, 128, 255) : IM_COL32(220, 220, 230, 255), t);
    }
    ImGui::Dummy({size, size});
}

const char* trendStr(int t) { return t > 0 ? "improving" : t < 0 ? "regressing" : "flat"; }
ImVec4 trendCol(int t) { return t > 0 ? kGood : t < 0 ? kBad : kDim; }

void tabOverview(App& a, const Recent& rc) {
    const Report& r = a.rep;
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[ImGui::GetIO().Fonts->Fonts.Size > 1 ? 1 : 0]);
    ImGui::TextColored(kAccent, "%s", r.verdict.c_str());
    ImGui::PopFont();
    ImGui::TextColored(kDim, "Recent window: %zu plays.  This week vs last week: ", rc.plays);
    const char* names[] = {"UR", "jump misses", "stream misses"};
    for (int i = 0; i < 3; i++) { ImGui::SameLine(); ImGui::TextColored(trendCol(r.trend[i]), "%s %s%s", names[i], trendStr(r.trend[i]), i < 2 ? "," : ""); }
    if (r.needHistory) ImGui::TextColored(kDim, "Radar needs older plays to compare against: every skill shows 50 until there is a baseline.");
    ImGui::Separator();
    float w = ImGui::GetContentRegionAvail().x, rs = std::min(w * 0.45f, ImGui::GetContentRegionAvail().y);
    ImGui::BeginChild("radar", {rs, rs});
    radar(r, rs);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("weak", {0, rs});
    ImGui::TextColored(kDim, "50 = you miss as often as you usually do at this star rating; +25 per halving of misses.");
    ImGui::Spacing();
    if (!r.nTop) ImGui::TextWrapped("No weakness costs measurable pp in your recent plays. Push the star rating up.");
    for (int i = 0; i < r.nTop; i++) {
        auto& wk = r.top[i];
        ImGui::PushID(i);
        ImGui::BeginChild("card", {0, 0}, ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
        ImGui::TextColored(kAccent, "#%d  %s", i + 1, kSkillName[wk.s]);
        ImGui::SameLine();
        ImGui::TextColored(kDim, "~%.1f pp lost per play (%zu recent plays)", wk.pp / std::max<size_t>(1, rc.plays), rc.plays);
        ImGui::TextWrapped("%s", wk.metric.c_str());
        ImGui::TextColored(kGood, "Fix:");
        ImGui::SameLine();
        ImGui::TextWrapped("%s", wk.fix.c_str());
        ImGui::EndChild();
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void tabTiming(const Recent& rc) {
    double n = rc.err.n;
    ImGui::Text("Unstable rate %.0f    mean %+.1f ms (%s)", rc.err.ur(), rc.err.mean(), rc.err.mean() < -2 ? "early" : rc.err.mean() > 2 ? "late" : "centred");
    ImGui::Text("Hits within +-15 ms %.0f%%    +-30 ms %.0f%%    +-50 ms %.0f%%", pct(rc.within[0], n), pct(rc.within[1], n), pct(rc.within[2], n));
    int keys = rc.keyHits[0] + rc.keyHits[1];
    ImGui::Text("Key 1: %.0f%% of hits, UR %.0f    Key 2: %.0f%% of hits, UR %.0f", pct(rc.keyHits[0], keys), rc.errKey[0].ur(), pct(rc.keyHits[1], keys), rc.errKey[1].ur());
    ImGui::Spacing();
    ImGui::TextColored(kDim, "Hit error histogram, -100 ms (left) to +100 ms (right), 2 ms per bar");
    ImGui::PlotHistogram("##hist", rc.hist, 100, 0, nullptr, 0, FLT_MAX, {ImGui::GetContentRegionAvail().x, 220});
}

void tabAim(const Recent& rc) {
    const char* names[] = {"Dense (<=2 r)", "Stream spacing (2-4 r)", "Jumps (4-7 r)", "Big jumps (>7 r)"};
    if (ImGui::BeginTable("buckets", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        for (auto h : {"Spacing", "Notes", "Miss %", "Mean aim error (radii)"}) ImGui::TableSetupColumn(h);
        ImGui::TableHeadersRow();
        for (int b = 0; b < 4; b++) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%s", names[b]);
            ImGui::TableNextColumn(); ImGui::Text("%d", rc.bObj[b]);
            ImGui::TableNextColumn(); ImGui::Text("%.2f%%", pct(rc.bMiss[b], rc.bObj[b]));
            ImGui::TableNextColumn(); ImGui::Text("%.2f", rc.bObj[b] > rc.bMiss[b] ? rc.bDist[b] / (rc.bObj[b] - rc.bMiss[b]) : 0);
        }
        ImGui::EndTable();
    }
    int am = rc.under + rc.over;
    ImGui::Text("Aim misses along the jump: %d undershoots, %d overshoots (%.0f%% undershoot)", rc.under, rc.over, pct(rc.under, am));
    ImGui::Spacing();
    ImGui::TextColored(kDim, "Miss rate by screen quadrant");
    for (int q = 0; q < 4; q++) {
        double m = pct(rc.qMiss[q], rc.qObj[q]);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f + std::min(1.0, m / 4) * 0.6f, 0.25f, 0.3f, 1));
        char t[64];
        static const char* qn[] = {"top-left", "top-right", "bottom-left", "bottom-right"};
        snprintf(t, sizeof t, "%s\n%.2f%% of %d", qn[q], m, rc.qObj[q]);
        ImGui::Button(t, {180, 64});
        ImGui::PopStyleColor();
        if (q % 2 == 0) ImGui::SameLine();
    }
    ImGui::Spacing();
    ImGui::TextColored(kDim, "Distance between consecutive notes, 0 to 12 radii (0.5 r per bar)");
    ImGui::PlotHistogram("##jump", rc.jump, 24, 0, nullptr, 0, FLT_MAX, {ImGui::GetContentRegionAvail().x, 150});
}

void tabSpeed(const Recent& rc, float streamBpm) {
    ImGui::Text("Streams (4+ evenly spaced notes within 3 radii). Comfortable stream BPM (no misses): %.0f", streamBpm);
    const char* bands[] = {"< 150 BPM", "150-170", "170-190", "190-210", "210+"};
    if (ImGui::BeginTable("streams", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        for (auto h : {"BPM", "Streams", "Notes", "Miss %", "Alternate %", "Notes 1-4 mean", "Notes 5+ mean"}) ImGui::TableSetupColumn(h);
        ImGui::TableHeadersRow();
        for (int i = 0; i < 5; i++) {
            auto& b = rc.band[i];
            if (!b.segs) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%s", bands[i]);
            ImGui::TableNextColumn(); ImGui::Text("%d", b.segs);
            ImGui::TableNextColumn(); ImGui::Text("%d", b.notes);
            ImGui::TableNextColumn(); ImGui::Text("%.1f%%", pct(b.miss, b.notes));
            ImGui::TableNextColumn(); ImGui::Text("%.0f%%", pct(b.alt, b.alt + b.dbl));
            ImGui::TableNextColumn(); ImGui::Text("%+.1f ms", b.eN ? b.early / b.eN : 0);
            ImGui::TableNextColumn(); ImGui::TextColored(b.lN && b.late / b.lN < -8 ? kBad : ImVec4(1, 1, 1, 1), "%+.1f ms", b.lN ? b.late / b.lN : 0);
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::Text("Tapping by gap between notes (overlap = key still held when the next press comes: finger lock)");
    const char* zones[] = {"Fast (< 100 ms)", "Switch zone (100-130 ms)", "1/2 (130-260 ms)", "Slow (260+ ms)"};
    if (ImGui::BeginTable("zones", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        for (auto h : {"Gap", "Hits", "UR", "Alternate %", "Overlap %"}) ImGui::TableSetupColumn(h);
        ImGui::TableHeadersRow();
        for (int z = 0; z < 4; z++) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%s", zones[z]);
            ImGui::TableNextColumn(); ImGui::Text("%.0f", rc.zone[z].n);
            ImGui::TableNextColumn(); ImGui::Text("%.0f", rc.zone[z].ur());
            ImGui::TableNextColumn(); ImGui::Text("%.0f%%", pct(rc.zAlt[z], rc.zPairs[z]));
            ImGui::TableNextColumn(); ImGui::TextColored(pct(rc.zOv[z], rc.zPairs[z]) > 20 ? kBad : ImVec4(1, 1, 1, 1), "%.0f%%", pct(rc.zOv[z], rc.zPairs[z]));
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::Text("Stamina: longest run without a break over 500 ms: %d notes in %.1f s (%.1f notes/s)", rc.stamNotes, rc.stamSec, rc.stamNps);
}

void tabReading(const Recent& rc) {
    ImGui::Text("First note after a break (> 500 ms): %.2f%% missed (%d of %d)", pct(rc.brkMiss, rc.brkObj), rc.brkMiss, rc.brkObj);
    ImGui::Text("Notes mid-flow:                      %.2f%% missed (%d of %d)", pct(rc.flowMiss, rc.flowObj), rc.flowMiss, rc.flowObj);
    ImGui::Spacing();
    ImGui::Text("Maps at AR below 8:  %.2f%% of notes missed (%d notes)", pct(rc.arMiss[0], rc.arObj[0]), rc.arObj[0]);
    ImGui::Text("Maps at AR 8+:       %.2f%% of notes missed (%d notes)", pct(rc.arMiss[1], rc.arObj[1]), rc.arObj[1]);
    ImGui::Spacing();
    ImGui::TextColored(kDim, "AR includes mods (HR, EZ, DT, HT). A big gap between the two rows usually means reading, not aim.");
}

void missList(const PlayStats& p, const std::vector<ObjRes>& objs) {
    if (objs.empty()) { ImGui::TextColored(kDim, "The replay or its map is gone, so the per-note list is unavailable."); return; }
    float beat = p.bpm > 0 ? 60000 / p.bpm : 500;
    if (ImGui::BeginTable("misses", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, {0, 260})) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Note", ImGuiTableColumnFlags_WidthFixed, 55);
        ImGui::TableSetupColumn("Context", ImGuiTableColumnFlags_WidthFixed, 230);
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
            char why[160];
            switch (o.cause) {
                case AIM: snprintf(why, sizeof why, "Aim: tapped on time (%+.0f ms) but %.1f radii away, %s", o.err, o.dist, o.along < 0 ? "undershot" : o.along > 0 ? "overshot" : "off to the side"); break;
                case TAP: snprintf(why, sizeof why, "Tap: your press was used by the previous note"); break;
                case OFFTIME: snprintf(why, sizeof why, "Timing: tapped %.0f ms early, outside the hit window", -o.err); break;
                default: snprintf(why, sizeof why, "No press near this note"); break;
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%d:%02d.%03d", mt / 60000, mt / 1000 % 60, mt % 1000);
            ImGui::TableNextColumn(); ImGui::Text("#%zu", i + 1);
            ImGui::TableNextColumn(); ImGui::Text("%s", ctx.c_str());
            ImGui::TableNextColumn(); ImGui::Text("%s", why);
        }
        ImGui::EndTable();
    }
}

void tabPlays(App& a) {
    auto& ps = a.res.plays;
    static std::vector<int> order;
    static int orderFor = -1;
    bool fresh = orderFor != a.gen;
    if (fresh) {
        order.resize(ps.size());
        for (size_t i = 0; i < ps.size(); i++) order[i] = (int)(ps.size() - 1 - i);
        orderFor = a.gen;
    }
    float h = a.selPlay >= 0 ? ImGui::GetContentRegionAvail().y * 0.45f : 0;
    if (ImGui::BeginTable("plays", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable, {0, h})) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_WidthFixed, 130);
        ImGui::TableSetupColumn("Map", ImGuiTableColumnFlags_WidthStretch);
        for (auto c : {"Stars", "Mods", "Acc", "Misses", "UR"}) ImGui::TableSetupColumn(c, ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableHeadersRow();
        if (auto* s = ImGui::TableGetSortSpecs(); s && (s->SpecsDirty || fresh) && s->SpecsCount) {  // re-apply the header's sort to a new scan
            int col = s->Specs[0].ColumnIndex;
            bool asc = s->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
            auto key = [&](int i) -> double {
                auto& p = ps[i];
                switch (col) { case 2: return p.stars; case 3: return p.mods; case 4: return p.acc; case 5: return p.gmiss; case 6: return p.err.ur(); default: return (double)p.when; }
            };
            if (col == 1) std::sort(order.begin(), order.end(), [&](int x, int y) { return asc ? ps[x].title < ps[y].title : ps[x].title > ps[y].title; });
            else std::sort(order.begin(), order.end(), [&](int x, int y) { return asc ? key(x) < key(y) : key(x) > key(y); });
            s->SpecsDirty = false;
        }
        ImGuiListClipper clip;
        clip.Begin((int)order.size());
        while (clip.Step())
            for (int row = clip.DisplayStart; row < clip.DisplayEnd; row++) {
                int i = order[row];
                auto& p = ps[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Selectable(dateStr(p.when).c_str(), a.selPlay == i, ImGuiSelectableFlags_SpanAllColumns)) a.selPlay = i;
                ImGui::TableNextColumn(); ImGui::TextUnformatted(p.title.c_str());
                ImGui::TableNextColumn(); p.stars > 0 ? ImGui::Text("%.2f", p.stars) : ImGui::TextColored(kDim, "?");
                ImGui::TableNextColumn(); ImGui::TextUnformatted(modStr(p.mods).c_str());
                ImGui::TableNextColumn(); ImGui::Text("%.2f%%", 100 * p.acc);
                ImGui::TableNextColumn(); ImGui::Text("%d", p.gmiss);
                ImGui::TableNextColumn(); ImGui::Text("%.0f", p.err.ur());
            }
        ImGui::EndTable();
    }
    if (a.selPlay >= 0 && a.selPlay < (int)ps.size()) {
        auto& p = ps[a.selPlay];
        ImGui::TextColored(kAccent, "%s", p.title.c_str());
        ImGui::Text("%s  %s  %.2f%%  %d misses (simulated %d incl. slider heads)  UR %.0f  mean %+.1f ms  max combo %d",
                    dateStr(p.when).c_str(), modStr(p.mods).c_str(), 100 * p.acc, p.gmiss, p.sMiss, p.err.ur(), p.err.mean(), p.combo);
        missList(p, a.selectedObjs());
        float hist[100];
        for (int k = 0; k < 100; k++) hist[k] = (float)p.hist[k];
        ImGui::PlotHistogram("##phist", hist, 100, 0, "hit errors -100..+100 ms", 0, FLT_MAX, {ImGui::GetContentRegionAvail().x, 90});
    }
}

void tabMaps(App& a) {
    ImGui::TextWrapped("Five maps from your Songs folder that train the selected skill. Maps you have played 0-3 times in the last 30 days come first.");
    ImGui::SetNextItemWidth(260);
    ImGui::DragFloatRange2("Star range", &a.lo, &a.hi, 0.05f, 1, 10, "%.2f\xE2\x98\x85");
    ImGui::SameLine();
    const char* items[NSKILL + 1] = {"Top weakness"};
    for (int i = 0; i < NSKILL; i++) items[i + 1] = kSkillName[i];
    int cur = a.trainSkill + 1;
    ImGui::SetNextItemWidth(170);
    if (ImGui::Combo("Train", &cur, items, NSKILL + 1)) a.trainSkill = cur - 1;
    ImGui::SameLine();
    ImGui::BeginDisabled(a.busy);
    if (ImGui::Button("Find maps")) a.startSuggest();
    ImGui::EndDisabled();
    if (a.busy) { ImGui::SameLine(); ImGui::TextColored(kDim, "searching..."); }
    ImGui::Separator();
    if (a.sugg.empty()) ImGui::TextColored(kDim, "No maps in this star range. Widen the range or download a few maps.");
    for (size_t i = 0; i < a.sugg.size(); i++) {
        auto& s = a.sugg[i];
        ImGui::PushID((int)i);
        ImGui::BeginChild("m", {0, 0}, ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
        ImGui::TextColored(kAccent, "%s", s.map->title.c_str());
        ImGui::TextColored(kDim, "%s", s.why.c_str());
        if (s.map->bid > 0 && ImGui::Button("Open page")) open(L"https://osu.ppy.sh/b/" + std::to_wstring(s.map->bid));
        if (s.map->bid > 0) ImGui::SameLine();
        if (ImGui::Button("Show in folder")) open(L"explorer.exe", L"/select,\"" + s.map->path + L"\"");
        ImGui::EndChild();
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
    ImGui::TextColored(kAccent, "%s", r.verdict.c_str());
    ImGui::Text("%zu days of history (kept in history.json, so it survives deleted maps).", byDate.size());
    if (!byDate.empty()) ImGui::TextColored(kDim, "From %d to %d, one point per day.", byDate.begin()->first, byDate.rbegin()->first);
    float w = ImGui::GetContentRegionAvail().x;
    const char* names[] = {"Unstable rate", "Jump miss %", "Stream miss %"};
    std::vector<float>* series[] = {&ur, &jm, &sm};
    for (int i = 0; i < 3; i++) {
        ImGui::Spacing();
        ImGui::Text("%s  ", names[i]);
        ImGui::SameLine();
        ImGui::TextColored(trendCol(r.trend[i]), "this week vs last: %s (lower is better)", trendStr(r.trend[i]));
        ImGui::PlotLines(("##p" + std::to_string(i)).c_str(), series[i]->data(), (int)series[i]->size(), 0, nullptr, 0, FLT_MAX, {w, 110});
    }
}
}

int gForceTab = -1;
static ImGuiTabItemFlags tabFlag(int i) { return gForceTab == i ? ImGuiTabItemFlags_SetSelected : 0; }

void drawApp(App& a) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("osu-coach", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    static Recent rc;
    static int builtFor = -1;
    if (a.scanned && builtFor != a.gen) rc = build(a.res.plays, a.rep.recentFrom), builtFor = a.gen;

    if (a.osuDir.empty()) {
        ImGui::TextWrapped("osu! folder not found. Pick the folder that contains osu!.db (for example C:\\Users\\you\\AppData\\Local\\osu!).");
        if (ImGui::Button("Choose osu! folder")) a.pickFolder();
        ImGui::End();
        return;
    }
    ImGui::BeginDisabled(a.busy);
    if (ImGui::Button("Refresh")) a.startScan();
    ImGui::SameLine();
    if (ImGui::Button("Change folder")) a.pickFolder();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (a.busy && (!a.scanned || a.done < a.total)) {
        int t = a.total, d = a.done;
        ImGui::ProgressBar(t ? (float)d / t : 0, {300, 0}, t ? (std::to_string(d) + " / " + std::to_string(t) + " replays").c_str() : "reading osu!.db...");
    } else if (a.scanned) {
        auto& s = a.res;
        ImGui::TextColored(kDim, "%zu plays analysed. Skipped: %d map deleted, %d other modes, %d relax/autopilot/auto, %d unreadable.",
                           s.plays.size(), s.deleted, s.otherMode, s.modSkip, s.corrupt);
    }
    if (a.scanned && !a.res.dbOk) ImGui::TextColored(kBad, "osu!.db could not be read: star ratings and map suggestions are off.");
    if (!a.scanned) { ImGui::End(); return; }
    if (a.res.plays.empty()) {
        ImGui::TextWrapped("No standard-mode replays with their maps still installed. Play a few maps in osu! (stable) and press Refresh.");
        ImGui::End();
        return;
    }
    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("Overview", nullptr, tabFlag(0))) { tabOverview(a, rc); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Timing", nullptr, tabFlag(1))) { tabTiming(rc); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Aim", nullptr, tabFlag(2))) { tabAim(rc); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Speed", nullptr, tabFlag(3))) { tabSpeed(rc, a.streamBpm); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Reading", nullptr, tabFlag(4))) { tabReading(rc); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Plays", nullptr, tabFlag(5))) { tabPlays(a); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Maps", nullptr, tabFlag(6))) { tabMaps(a); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Progress", nullptr, tabFlag(7))) { tabProgress(a.rep); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::End();
}
