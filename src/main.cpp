#include <cstdio>
#include <string>
#include <windows.h>
#include "scan.h"

static int dumpMode(int argc, wchar_t** argv) {
    if (AttachConsole(ATTACH_PARENT_PROCESS)) freopen("CONOUT$", "w", stdout);
    std::wstring out, dir;
    for (int i = 1; i + 1 < argc; i++) {
        if (!wcscmp(argv[i], L"--dump")) out = argv[i + 1];
        if (!wcscmp(argv[i], L"--osu")) dir = argv[i + 1];
    }
    if (dir.empty()) dir = findOsuDir();
    if (dir.empty()) { printf("osu! folder not found, pass --osu PATH\n"); return 1; }
    ScanResult r = scan(dir);
    dumpJsonl(r, out);
    printf("analysed %zu plays; skipped: %d map deleted, %d corrupt, %d other mode, %d relax/autopilot/auto; db %s\n",
           r.plays.size(), r.deleted, r.corrupt, r.otherMode, r.modSkip, r.dbOk ? "ok" : "unreadable");
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    for (int i = 1; i < argc; i++)
        if (!wcscmp(argv[i], L"--dump")) return dumpMode(argc, argv);
    return 0;
}
