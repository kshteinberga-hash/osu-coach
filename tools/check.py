"""Compare osu-coach --dump output with the reference Python analyzer. usage: check.py dump.jsonl ref.jsonl"""
import json, sys, statistics as st

dump = {r["file"]: r for r in map(json.loads, open(sys.argv[1], encoding="utf-8"))}
ref = {r["file"]: r for r in map(json.loads, open(sys.argv[2], encoding="utf-8"))}
common = sorted(dump.keys() & ref.keys())
print(f"dump {len(dump)} plays, ref {len(ref)} plays, common {len(common)}")
if not common: sys.exit(1)

same = ur_ok = game_ok = 0
bad = []
for f in common:
    d, r = dump[f], ref[f]
    objs = r["objs"][: d["nobj"]]  # the reference doesn't cut objects after a quit
    rmiss = sum(o["j"] == 0 for o in objs)
    errs = [o["hit"]["err"] for o in objs if o["j"] and o["hit"]]
    rur = 10 * st.pstdev(errs) if len(errs) > 1 else 0
    same += d["smiss"] == rmiss
    ur_ok += abs(d["ur"] - rur) <= 1
    game_ok += abs(d["smissc"] - d["miss"]) <= 2  # stable doesn't count a missed slider head as a miss
    if d["smiss"] != rmiss or abs(d["ur"] - rur) > 1: bad.append((f, d["smiss"], rmiss, round(d["ur"], 1), round(rur, 1)))

n = len(common)
print(f"simulated misses equal: {100*same/n:.1f}%  (target >= 98)")
print(f"UR within 1:            {100*ur_ok/n:.1f}%")
print(f"|circle misses - game| <= 2: {100*game_ok/n:.1f}%  (target >= 95)")
for b in bad[:10]: print("  diff", *b)
sys.exit(0 if same / n >= 0.98 and game_ok / n >= 0.95 else 1)
