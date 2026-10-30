"""Build a separate sample-data renderer without modifying production sources.

python tools/preview_ui.py [width height]
Screenshots are written under build/art-preview-WIDTH.
"""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parent.parent
width, height = (map(int, sys.argv[1:3]) if len(sys.argv) == 3 else (1280, 900))
build = root / 'build'
build.mkdir(exist_ok=True)
source = (root / 'src/main.cpp').read_text(encoding='utf-8')
needle = '    app.osuDir = findOsuDir();\n    app.startScan();'
assert needle in source, 'Preview injection point changed'
fixture = '''    app.osuDir = L"sample-data-only"; app.scanned = true; app.res.dbOk = true; app.res.plays.resize(42);
    for (auto &p : app.res.plays) {
        p.title = "Sample artist - Sample song [Hard]"; p.acc = .9462f; p.stars = 3.5f;
        p.err.n=100; p.err.ss=42849; p.gmiss=2; p.when=1793318400000;
        for(int k=0;k<100;k++) p.hist[k]=(int)(100*std::exp(-.008*(k-50)*(k-50)));
    }
    app.rep.verdict = "Focus on stamina"; app.rep.recentFrom = 0; app.rep.needHistory = false;
    float scores[] = {55,46,41,49,49,62,46};
    for (int i = 0; i < NSKILL; ++i) app.rep.radar[i] = scores[i];
    app.rep.trend[0] = 1; app.rep.trend[1] = -1; app.rep.trend[2] = 1; app.rep.nTop = 3;
    app.rep.top[0] = {STAMINA, 273, "You miss 12.7% of notes near the end of long runs.", "Play longer stream maps at a comfortable speed. Relax your hand between bursts."};
    app.rep.top[1] = {STREAMS, 248, "You miss 12.6% of stream notes.", "Practise streams below your limit until you can full-combo them. Then add 5 BPM."};
    for(int d=0;d<14;d++) {
        Day day{}; day.date=20261015+d; day.plays=3; day.en=300; day.ess=120000;
        for(int k=0;k<NSKILL;k++) { day.obj[k]=250; day.miss[k]=30-d+(k*3+d*7)%12; }
        app.rep.days.push_back(day);
    }
    app.rep.top[2] = {BIGJUMPS, 125, "Big jumps are less consistent than your usual.", "Slow down at the end of each movement. Land on the circle before you tap."};
'''
source = source.replace(needle, fixture)
source = source.replace('(int)(1280 * dpi), (int)(820 * dpi)', f'(int)({width} * dpi), (int)({height} * dpi)')
source = source.replace('if (tab == 9) break;', 'if (tab == 8) break;')
# Do not load nonexistent sample replay files on the Plays tab.
source = source.replace('if (tab == 5 && shot % 4 == 0)', 'if (false)')
(build / 'preview-main.cpp').write_text(source, encoding='utf-8')
batch = (root / 'build.bat').read_text().replace('cd /d %~dp0', 'cd /d "%~dp0.."')
batch = batch.replace('src\\main.cpp', 'build\\preview-main.cpp').replace('set F=', 'set F=/Isrc ')
(build / 'preview-build.bat').write_text(batch)
env = os.environ.copy()
env['OUT'] = 'build\\art-preview.exe'
subprocess.run(['cmd', '/c', str(build / 'preview-build.bat')], cwd=root, env=env, check=True)
output = build / f'art-preview-{width}'
output.mkdir(exist_ok=True)
subprocess.run([str(build / 'art-preview.exe'), '--screenshot', str(output)], cwd=root, check=True, timeout=60)
print(output)
