"""Parse osu! stable replays + beatmaps, simulate hits, dump per-object stats as JSON lines."""
import sys, os, struct, lzma, hashlib, json, math, glob, datetime

OSU = r"E:\Pirate\Igruhi\oshuska\osu!"
OUT = sys.argv[1] if len(sys.argv) > 1 else None

def rd_uleb(b, i):
    r = s = 0
    while True:
        c = b[i]; i += 1
        r |= (c & 0x7F) << s
        if not c & 0x80: return r, i
        s += 7

def rd_str(b, i):
    if b[i] == 0: return "", i + 1
    i += 1
    n, i = rd_uleb(b, i)
    return b[i:i+n].decode("utf-8", "replace"), i + n

def parse_osr(path):
    b = open(path, "rb").read()
    mode, ver = struct.unpack_from("<Bi", b, 0); i = 5
    md5, i = rd_str(b, i); player, i = rd_str(b, i); rmd5, i = rd_str(b, i)
    c300, c100, c50, geki, katu, miss = struct.unpack_from("<6H", b, i); i += 12
    score, combo, perfect, mods = struct.unpack_from("<iH?i", b, i); i += 11
    life, i = rd_str(b, i)
    ts, = struct.unpack_from("<q", b, i); i += 8
    ln, = struct.unpack_from("<i", b, i); i += 4
    raw = lzma.decompress(b[i:i+ln]).decode("ascii", "replace")
    frames, t = [], 0
    for f in raw.split(","):
        p = f.split("|")
        if len(p) < 4: continue
        w = int(p[0])
        if w == -12345: continue
        t += w
        frames.append((t, float(p[1]), float(p[2]), int(p[3])))
    when = datetime.datetime(1, 1, 1) + datetime.timedelta(microseconds=ts // 10)
    return dict(mode=mode, md5=md5, player=player, n300=c300, n100=c100, n50=c50, miss=miss,
                score=score, combo=combo, mods=mods, when=when.isoformat(), frames=frames)

def parse_osu(path):
    d, sec, objs, tps = {}, None, [], []
    for line in open(path, encoding="utf-8", errors="replace"):
        line = line.strip()
        if not line or line.startswith("//"): continue
        if line.startswith("["): sec = line[1:-1]; continue
        if sec in ("General", "Metadata", "Difficulty"):
            if ":" in line:
                k, v = line.split(":", 1); d[k.strip()] = v.strip()
        elif sec == "TimingPoints":
            p = line.split(",")
            try: tps.append((float(p[0]), float(p[1]), len(p) < 7 or p[6] == "1"))
            except: pass
        elif sec == "HitObjects":
            p = line.split(",")
            x, y, t, typ = int(float(p[0])), int(float(p[1])), int(float(p[2])), int(p[3])
            kind = "c" if typ & 1 else "s" if typ & 2 else "sp" if typ & 8 else None
            if kind: objs.append(dict(x=x, y=y, t=t, k=kind, nc=bool(typ & 4)))
    if d.get("Mode", "0") != "0": return None
    od = float(d.get("OverallDifficulty", 5)); cs = float(d.get("CircleSize", 5))
    ar = float(d.get("ApproachRate", od))
    bpm = [60000 / b for _, b, unin in tps if unin and b > 0]
    return dict(od=od, cs=cs, ar=ar, objs=objs, bid=d.get("BeatmapID", "0"), bpm=max(set(bpm), key=bpm.count) if bpm else 0,
                title=f'{d.get("Artist","")} - {d.get("Title","")} [{d.get("Version","")}]')

HR, EZ, DT, HT, NC, RX, AP, AUTO = 16, 2, 64, 256, 512, 128, 8192, 2048

def simulate(rep, bm):
    mods = rep["mods"]
    od, cs = bm["od"], bm["cs"]
    if mods & HR: od = min(10, od * 1.4); cs = min(10, cs * 1.3)
    if mods & EZ: od *= 0.5; cs *= 0.5
    rate = 1.5 if mods & (DT | NC) else 0.75 if mods & HT else 1.0
    w300, w100, w50 = 80 - 6 * od, 140 - 8 * od, 200 - 10 * od
    rad = 54.4 - 4.48 * cs
    flip = bool(mods & HR)
    frames = rep["frames"]
    # key press events (K1=4|1, K2=8|2) with release times
    presses = []  # [t, x, y, key, release_t]
    prev = 0; open_ = {}
    for t, x, y, k in frames:
        k1, k2 = bool(k & 5), bool(k & 10)
        for key, now, was in (("K1", k1, bool(prev & 5)), ("K2", k2, bool(prev & 10))):
            if now and not was:
                yy = 384 - y if flip else y
                open_[key] = len(presses); presses.append([t, x, yy, key, None])
            elif was and not now and key in open_:
                presses[open_.pop(key)][4] = t
        prev = k
    objs = [dict(o) for o in bm["objs"]]
    pi = 0; res = []
    used = [False] * len(presses)
    for idx, o in enumerate(objs):
        o["i"] = idx
        if o["k"] == "sp": continue
        hit = None
        # earliest press within window; stable notelock: presses before previous object's judgement are consumed
        while pi < len(presses) and presses[pi][0] < o["t"] - 400: pi += 1
        j = pi
        while j < len(presses) and presses[j][0] <= o["t"] + w50:
            if not used[j]:
                pt, px, py, key, rel = presses[j]
                dist = math.hypot(px - o["x"], py - o["y"])
                err = pt - o["t"]
                if dist <= rad * 1.0 and abs(err) <= 400:
                    used[j] = True
                    if abs(err) <= w50:
                        hit = dict(err=err / rate, key=key, hold=(rel - pt) / rate if rel else None, dist=dist / rad)
                    else:
                        hit = dict(err=err / rate, key=key, early_miss=True, dist=dist / rad)
                    pi = j + 1
                    break
            j += 1
        # nearest press around object time (for diagnosing misses)
        near = None
        for pt, px, py, key, rel in presses[max(0, pi - 5):pi + 5]:
            e = pt - o["t"]
            if abs(e) <= w50 and (near is None or abs(e) < abs(near[0])):
                near = (e / rate, math.hypot(px - o["x"], py - o["y"]) / rad)
        if hit and not hit.get("early_miss"):
            a = abs(hit["err"] * rate)
            j_ = 300 if a <= w300 else 100 if a <= w100 else 50
        else:
            j_ = 0
        res.append(dict(i=idx, t=o["t"], k=o["k"], x=o["x"], y=o["y"], nc=o["nc"], j=j_, hit=hit, near=near))
    # all presses for tapping analysis
    ps = [dict(t=p[0] / rate, key=p[3], hold=(p[4] - p[0]) / rate if p[4] else None) for p in presses]
    return dict(rate=rate, w=(w300, w100, w50), rad=rad, objs=res, presses=ps)

def main():
    md5map = {}
    for f in glob.glob(os.path.join(OSU, "Songs", "*", "*.osu")):
        md5map[hashlib.md5(open(f, "rb").read()).hexdigest()] = f
    files = glob.glob(os.path.join(OSU, "Data", "r", "*.osr")) + glob.glob(os.path.join(OSU, "Replays", "*.osr"))
    n = skipped = 0
    with open(OUT, "w") as out:
        for f in files:
            try:
                rep = parse_osr(f)
            except Exception as e:
                skipped += 1; continue
            if rep["mode"] != 0 or rep["md5"] not in md5map or rep["mods"] & (RX | AP | AUTO):
                skipped += 1; continue
            bm = parse_osu(md5map[rep["md5"]])
            if not bm: skipped += 1; continue
            sim = simulate(rep, bm)
            rec = dict(file=os.path.basename(f), map=bm["title"], bid=bm["bid"], bpm=bm["bpm"], od=bm["od"], cs=bm["cs"], ar=bm["ar"],
                       mods=rep["mods"], when=rep["when"], n300=rep["n300"], n100=rep["n100"], n50=rep["n50"],
                       miss=rep["miss"], combo=rep["combo"], nobj=len(bm["objs"]), **sim)
            out.write(json.dumps(rec) + "\n"); n += 1
    print("analyzed", n, "skipped", skipped, "maps", len(md5map))

if __name__ == "__main__":
    main()
