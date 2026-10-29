#include "analyze.h"
#include <algorithm>
#include <cmath>

double Acc::ur() const {
    if (n < 2) return 0;
    double m = s / n;
    return 10 * std::sqrt(std::max(0.0, ss / n - m * m));
}

int zoneOf(float gap) { return gap < 100 ? 0 : gap < 130 ? 1 : gap < 260 ? 2 : 3; }

namespace {
struct Press { int t; float x, y; uint8_t key; int rel; };
}

PlayStats analyze(const Replay& r, const Beatmap& b, float stars) {
    PlayStats s{};
    s.md5 = r.md5, s.title = b.title, s.bid = b.bid, s.when = r.when, s.mods = r.mods, s.stars = stars;
    s.g300 = r.n300, s.g100 = r.n100, s.g50 = r.n50, s.gmiss = r.miss, s.combo = r.combo;
    int tot = r.n300 + r.n100 + r.n50 + r.miss;
    s.acc = tot ? (300.f * r.n300 + 100.f * r.n100 + 50.f * r.n50) / (300.f * tot) : 0;
    s.bpm = b.bpm;

    float od = b.od, cs = b.cs, ar = b.ar;
    if (r.mods & 16) od = std::min(10.f, od * 1.4f), cs = std::min(10.f, cs * 1.3f), ar = std::min(10.f, ar * 1.4f);
    if (r.mods & 2) od *= 0.5f, cs *= 0.5f, ar *= 0.5f;
    float rate = r.mods & (64 | 512) ? 1.5f : r.mods & 256 ? 0.75f : 1.f;
    s.rate = rate, s.od = od, s.ar = ar, s.bpm *= rate;
    float w300 = 80 - 6 * od, w100 = 140 - 8 * od, w50 = 200 - 10 * od, rad = 54.4f - 4.48f * cs;
    bool flip = r.mods & 16;

    // key-down edges -> presses with release times
    std::vector<Press> ps;
    int open[2] = {-1, -1}, prev = 0;
    for (auto& f : r.frames) {
        for (int k = 0; k < 2; k++) {
            int mask = k ? 10 : 5;
            bool now = f.keys & mask, was = prev & mask;
            if (now && !was) open[k] = (int)ps.size(), ps.push_back({f.t, f.x, flip ? 384 - f.y : f.y, (uint8_t)k, -1});
            else if (was && !now && open[k] >= 0) ps[open[k]].rel = f.t, open[k] = -1;
        }
        prev = f.keys;
    }
    int lastT = r.frames.empty() ? -1000000 : r.frames.back().t;

    std::vector<char> used(ps.size());
    size_t pi = 0;
    const HitObj* po = nullptr;
    for (auto& o : b.objs) {
        if (o.kind == 2) continue;
        if (o.t > lastT + 1000) break;
        ObjRes e{};
        e.t = (int)(o.t / rate);
        int hitIdx = -1;
        bool early = false;
        while (pi < ps.size() && ps[pi].t < o.t - 400) pi++;
        for (size_t j = pi; j < ps.size() && ps[j].t <= o.t + w50; j++) {
            if (used[j]) continue;
            float err = (float)(ps[j].t - o.t);
            if (std::hypot(ps[j].x - o.x, ps[j].y - o.y) <= rad && std::fabs(err) <= 400) {
                used[j] = 1, hitIdx = (int)j, early = std::fabs(err) > w50, pi = j + 1;
                break;
            }
        }
        // nearest press in the 50 window (used or not), to explain misses
        int near = -1;
        for (size_t j = pi >= 5 ? pi - 5 : 0; j < std::min(ps.size(), pi + 5); j++) {
            float d = std::fabs((float)(ps[j].t - o.t));
            if (d <= w50 && (near < 0 || d < std::fabs((float)(ps[near].t - o.t)))) near = (int)j;
        }
        if (hitIdx >= 0 && !early) {
            const Press& p = ps[hitIdx];
            float a = std::fabs((float)(p.t - o.t));
            e.judge = a <= w300 ? 300 : a <= w100 ? 100 : 50;
            e.err = (p.t - o.t) / rate, e.key = p.key, e.hold = p.rel >= 0 ? (p.rel - p.t) / rate : -1;
            e.dist = std::hypot(p.x - o.x, p.y - o.y) / rad;
        } else {
            e.cause = early ? TIMING : near < 0 ? NOPRESS : std::hypot(ps[near].x - o.x, ps[near].y - o.y) > rad ? AIM : TAP;
            const Press* p = early ? &ps[hitIdx] : near >= 0 ? &ps[near] : nullptr;
            if (p) e.dist = std::hypot(p->x - o.x, p->y - o.y) / rad, e.err = (p->t - o.t) / rate, e.key = p->key;
            if (e.cause == AIM && po) {
                float vx = o.x - po->x, vy = o.y - po->y, len = std::hypot(vx, vy);
                if (len > 10) {
                    float along = ((p->x - o.x) * vx + (p->y - o.y) * vy) / len / rad;
                    e.along = along < -0.5f ? -1 : along > 0.5f ? 1 : 0;
                }
            }
        }
        e.spacing = po ? std::hypot(o.x - po->x, o.y - po->y) / rad : 0;
        e.gap = po ? (o.t - po->t) / rate : 1e9f;
        e.afterBreak = !po || (o.t - po->end) / rate > 500;
        e.bucket = e.spacing <= 2 ? DENSE : e.spacing <= 4 ? STREAM : e.spacing <= 7 ? JUMP : BIG;
        e.quad = (o.x >= 256) + 2 * (o.y >= 192);
        s.objs.push_back(e);
        po = &o;
    }

    // per-object counters
    const ObjRes* pr = nullptr;
    for (auto& e : s.objs) {
        bool hit = e.judge > 0, miss = !hit;
        s.nobj++, s.sMiss += miss;
        s.bObj[e.bucket]++, s.bMiss[e.bucket] += miss;
        s.qObj[e.quad]++, s.qMiss[e.quad] += miss;
        if (pr) s.jumpHist[std::min(23, (int)(e.spacing * 2))]++;
        (e.afterBreak ? s.brkObj : s.flowObj)++;
        (e.afterBreak ? s.brkMiss : s.flowMiss) += miss;
        if (e.cause == AIM) s.under += e.along < 0, s.over += e.along > 0;
        if (hit) {
            s.err.add(e.err), s.errKey[e.key].add(e.err), s.keyHits[e.key]++;
            s.bDist[e.bucket] += e.dist;
            float a = std::fabs(e.err);
            s.within[0] += a <= 15, s.within[1] += a <= 30, s.within[2] += a <= 50;
            s.hist[std::clamp((int)std::floor((e.err + 100) / 2), 0, 99)]++;
            if (pr) {
                int z = zoneOf(e.gap);
                s.zone[z].add(e.err);
                if (pr->judge > 0) {
                    s.zPairs[z]++, s.zAlt[z] += pr->key != e.key;
                    s.zOv[z] += pr->hold > e.gap;
                }
            }
        }
        pr = &e;
    }

    // streams: >= 4 objects, spacing <= 3r, even gaps (within 15% of the first), 1/4 at >= 75 bpm
    auto& v = s.objs;
    for (size_t i = 0; i + 1 < v.size();) {
        float g0 = v[i + 1].gap;
        size_t j = i + 1;
        if (g0 <= 200)
            while (j < v.size() && v[j].spacing <= 3 && std::fabs(v[j].gap - g0) <= 0.15f * g0) j++;
        if (j - i >= 4) {
            StreamSeg sg{15000 / g0, (int)(j - i), 0, 0, 0, 0, 0};
            Acc ea, la;
            for (size_t k = i; k < j; k++) {
                v[k].inStream = true;
                sg.miss += v[k].judge == 0;
                if (v[k].judge) (k - i < 4 ? ea : la).add(v[k].err);
                if (k > i && v[k].judge && v[k - 1].judge) (v[k].key != v[k - 1].key ? sg.alt : sg.dbl)++;
            }
            sg.errEarly = (float)ea.mean(), sg.errLate = (float)la.mean();
            s.streams.push_back(sg);
            i = j;
        } else i++;
    }

    // stamina: longest run with no gap over 500 ms; second half of 20+ runs is "tired"
    for (size_t i = 0; i < v.size();) {
        size_t j = i + 1;
        while (j < v.size() && v[j].gap <= 500) j++;
        int n = (int)(j - i);
        if (n >= 20)
            for (size_t k = i + n / 2; k < j; k++) v[k].tired = true;
        float sec = (v[j - 1].t - v[i].t) / 1000.f;
        if (n > s.stamNotes) s.stamNotes = n, s.stamSec = sec, s.stamNps = sec > 0 ? n / sec : 0;
        i = j;
    }
    return s;
}
