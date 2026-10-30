#include <d3d11.h>
#include <shobjidl.h>
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>
#include "gui.h"
#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

static float comfortableStreamBpm(const std::vector<PlayStats>& plays, size_t from) {
    std::vector<float> b;
    for (size_t i = from; i < plays.size(); i++)
        for (auto& s : plays[i].streams) if (s.miss == 0) b.push_back(s.bpm);
    if (b.empty()) return 0;
    std::nth_element(b.begin(), b.begin() + b.size() / 2, b.end());
    return b[b.size() / 2];
}

static Skill trainFor(int skill, const Report& r) { return skill >= 0 ? (Skill)skill : r.nTop ? r.top[0].s : JUMPS; }

void App::startScan() {
    if (busy || osuDir.empty()) return;
    busy = true, done = 0, total = 0;
    // the UI can change these while the worker runs: copy them
    std::thread([this, dir = osuDir, lo = lo, hi = hi, skill = trainSkill] {
        pRes = scan(dir, &done, &total);
        std::wstring hp = exeDir() + L"\\history.json";
        std::vector<Day> hist;
        loadHistory(hp, hist);
        pRep = coach(pRes.plays, hist, nowMs());
        saveHistory(hp, pRep.days);
        pBpm = comfortableStreamBpm(pRes.plays, pRep.recentFrom);
        pSugg = suggest(pRes.db, pRes.plays, trainFor(skill, pRep), lo, hi, pBpm, nowMs());
        scanReady = true;
        PostMessageW((HWND)hwnd, WM_APP, 0, 0);
    }).detach();
}

void App::startSuggest() {
    if (busy || !scanned) return;
    busy = true;
    std::thread([this, lo = lo, hi = hi, skill = trainSkill] {
        pSugg = suggest(res.db, res.plays, trainFor(skill, rep), lo, hi, streamBpm, nowMs());
        suggReady = true;
        PostMessageW((HWND)hwnd, WM_APP, 0, 0);
    }).detach();
}

void App::poll() {
    if (scanReady.exchange(false)) {
        res = std::move(pRes), rep = std::move(pRep), sugg = std::move(pSugg), streamBpm = pBpm;
        scanned = true, selPlay = -1, gen++, busy = false;
    }
    if (suggReady.exchange(false)) sugg = std::move(pSugg), busy = false;
}

void App::openViewer() {
    if (selPlay < 0 || selPlay >= (int)res.plays.size()) return;
    viewer = std::make_unique<Viewer>();
    viewer->open(osuDir, res.plays[selPlay], res.db);
    viewing = true;
}
bool viewerWantsFrames(const App& a) { return a.viewing && a.viewer && a.viewer->clock.running; }

const std::vector<ObjRes>& App::selectedObjs() {
    if (selObjsFor != selPlay || selObjsGen != gen) {
        selObjs = selPlay >= 0 && selPlay < (int)res.plays.size() ? reanalyze(osuDir, res.plays[selPlay], res.db) : std::vector<ObjRes>{};
        selObjsFor = selPlay, selObjsGen = gen;
    }
    return selObjs;
}

void App::pickFolder() {
    IFileOpenDialog* d = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&d)))) return;
    DWORD o;
    d->GetOptions(&o);
    d->SetOptions(o | FOS_PICKFOLDERS);
    d->SetTitle(L"Select your osu! folder (the one with osu!.db)");
    IShellItem* it;
    if (SUCCEEDED(d->Show((HWND)hwnd)) && SUCCEEDED(d->GetResult(&it))) {
        PWSTR p;
        if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
            osuDir = p, CoTaskMemFree(p);
            saveOsuDir(osuDir);
            startScan();
        }
        it->Release();
    }
    d->Release();
}

// ---- D3D11 / Win32 boilerplate (after the Dear ImGui example) ----
static ID3D11Device* gDev;
static ID3D11DeviceContext* gCtx;
static IDXGISwapChain* gSwap;
static ID3D11RenderTargetView* gRtv;
static UINT gResizeW, gResizeH;
static float gDpi = 1;
static void (*gRedraw)();  // draws one frame; called from WM_SIZE while the user drags the window edge
static bool gSizing;

static void createRtv() {
    ID3D11Texture2D* bb;
    gSwap->GetBuffer(0, IID_PPV_ARGS(&bb));
    gDev->CreateRenderTargetView(bb, nullptr, &gRtv);
    bb->Release();
}
static bool createDevice(HWND h) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2, sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM, sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = h, sd.SampleDesc.Count = 1, sd.Windowed = TRUE;
    D3D_FEATURE_LEVEL fl, want[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    // flip model (Windows 10+): no extra copy per frame and smoother resizing; the old blit model where it's unavailable
    HRESULT r = E_FAIL;
    for (auto fx : {DXGI_SWAP_EFFECT_FLIP_DISCARD, DXGI_SWAP_EFFECT_DISCARD}) {
        sd.SwapEffect = fx;
        r = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, want, 2, D3D11_SDK_VERSION, &sd, &gSwap, &gDev, &fl, &gCtx);
        if (r == DXGI_ERROR_UNSUPPORTED)
            r = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, want, 2, D3D11_SDK_VERSION, &sd, &gSwap, &gDev, &fl, &gCtx);
        if (SUCCEEDED(r)) break;
    }
    if (FAILED(r)) return false;
    createRtv();
    return true;
}

// --screenshot: copy the back buffer to a 32-bit BMP
static void saveBackbuffer(const std::wstring& path) {
    ID3D11Texture2D *bb, *st;
    gSwap->GetBuffer(0, IID_PPV_ARGS(&bb));
    D3D11_TEXTURE2D_DESC d;
    bb->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING, d.BindFlags = 0, d.CPUAccessFlags = D3D11_CPU_ACCESS_READ, d.MiscFlags = 0;
    if (SUCCEEDED(gDev->CreateTexture2D(&d, nullptr, &st))) {
        gCtx->CopyResource(st, bb);
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(gCtx->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
            BITMAPINFOHEADER ih{sizeof ih, (LONG)d.Width, -(LONG)d.Height, 1, 32, BI_RGB};
            BITMAPFILEHEADER fh{0x4D42, (DWORD)(sizeof fh + sizeof ih + d.Width * d.Height * 4), 0, 0, sizeof fh + sizeof ih};
            if (FILE* f = _wfopen(path.c_str(), L"wb")) {
                fwrite(&fh, sizeof fh, 1, f), fwrite(&ih, sizeof ih, 1, f);
                std::vector<uint8_t> row(d.Width * 4);
                for (UINT y = 0; y < d.Height; y++) {
                    auto* src = (const uint8_t*)m.pData + y * m.RowPitch;
                    for (UINT x = 0; x < d.Width; x++) row[x * 4] = src[x * 4 + 2], row[x * 4 + 1] = src[x * 4 + 1], row[x * 4 + 2] = src[x * 4], row[x * 4 + 3] = 255;
                    fwrite(row.data(), 1, row.size(), f);
                }
                fclose(f);
            }
            gCtx->Unmap(st, 0);
        }
        st->Release();
    }
    bb->Release();
}

static void resizeBuffers() {
    if (!gResizeW) return;
    if (gRtv) gRtv->Release(), gRtv = nullptr;
    gSwap->ResizeBuffers(0, gResizeW, gResizeH, DXGI_FORMAT_UNKNOWN, 0);
    gResizeW = gResizeH = 0;
    createRtv();
}
static void renderFrame(App& app, const std::wstring& shotPath = L"") {
    resizeBuffers();
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    drawApp(app);
    ImGui::Render();
    const float clear[4] = {126 / 255.f, 178 / 255.f, 212 / 255.f, 1};  // cDesk
    gCtx->OMSetRenderTargets(1, &gRtv, nullptr);
    gCtx->ClearRenderTargetView(gRtv, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    if (!shotPath.empty()) saveBackbuffer(shotPath);
    gSwap->Present(1, 0);
}

static LRESULT WINAPI wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return true;
    if (m == WM_SIZE && w != SIZE_MINIMIZED) {
        gResizeW = LOWORD(l), gResizeH = HIWORD(l);
        if (gSizing && gRedraw) gRedraw();  // the main loop is blocked in the system's sizing loop: draw from here
        return 0;
    }
    if (m == WM_ENTERSIZEMOVE || m == WM_EXITSIZEMOVE) { gSizing = m == WM_ENTERSIZEMOVE; return 0; }
    if (m == WM_GETMINMAXINFO) {  // below this the layout stops fitting; the page scrolls between that and the default size
        auto* mm = (MINMAXINFO*)l;
        mm->ptMinTrackSize = {(LONG)(900 * gDpi), (LONG)(620 * gDpi)};
        return 0;
    }
    if (m == WM_SYSCOMMAND && (w & 0xfff0) == SC_KEYMENU) return 0;
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}


// GUI-subsystem exe: print to the parent console, unless stdout is already redirected to a file or pipe.
static void consoleOut() {
    if (GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) == FILE_TYPE_UNKNOWN && AttachConsole(ATTACH_PARENT_PROCESS)) freopen("CONOUT$", "w", stdout);
}

static int dumpMode(int argc, wchar_t** argv) {
    consoleOut();
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

// --synctest: plays the newest replay's song for 10 s at 1x and 5 s at 0.5x (muted) and checks that the music
// advances at rate x speed against the wall clock, which is what keeps the replay in sync with it.
static int syncTest() {
    consoleOut();
    std::wstring dir = findOsuDir();
    ScanResult r = scan(dir);
    for (int i = (int)r.plays.size() - 1; i >= 0; i--) {
        Viewer v;
        if (!v.open(dir, r.plays[i], r.db) || !v.audio.loaded()) continue;
        v.volume = 0, v.applyVolume();
        printf("%s (rate %.2f)\n", narrow(r.plays[i].file).c_str(), v.clock.rate);
        bool ok = true;
        for (float sp : {1.f, 0.5f}) {
            v.setSpeed(sp), v.seek(20000), v.setPlaying(true);
            LARGE_INTEGER f, a, b;
            QueryPerformanceFrequency(&f), QueryPerformanceCounter(&a);
            double t0 = v.audio.positionMs(), maxJump = 0, last = t0;
            for (int k = 0; k < (sp == 1 ? 1000 : 500); k++) { Sleep(10); double p = v.audio.positionMs(); maxJump = std::max(maxJump, p - last); last = p; }
            QueryPerformanceCounter(&b);
            double wall = (b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart, expect = wall * v.clock.rate * sp, got = last - t0;
            printf("speed %.2fx: music advanced %.0f ms, expected %.0f ms (error %+.0f ms, %.2f%%), largest step %.0f ms\n",
                   sp, got, expect, got - expect, 100 * (got - expect) / expect, maxJump);
            ok = ok && std::fabs(got - expect) <= 15 + 0.003 * expect;
            v.setPlaying(false);
        }
        printf(ok ? "PASS\n" : "FAIL\n");
        return ok ? 0 : 1;
    }
    printf("no replay with its music found\n");
    return 1;
}

int wmain(int argc, wchar_t** argv) {
    std::wstring shotDir;
    for (int i = 1; i < argc; i++) {
        if (!wcscmp(argv[i], L"--dump")) return dumpMode(argc, argv);
        if (!wcscmp(argv[i], L"--synctest")) return syncTest();
        if (!wcscmp(argv[i], L"--screenshot") && i + 1 < argc) shotDir = argv[i + 1];
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ImGui_ImplWin32_EnableDpiAwareness();
    WNDCLASSEXW wc{sizeof wc, CS_CLASSDC, wndProc, 0, 0, GetModuleHandleW(nullptr), LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION), nullptr, nullptr, nullptr, L"osu-coach", nullptr};
    RegisterClassExW(&wc);
    float dpi = gDpi = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY));
    HWND hwnd = CreateWindowW(L"osu-coach", L"osu-coach", WS_OVERLAPPEDWINDOW, 100, 100, (int)(1280 * dpi), (int)(820 * dpi), nullptr, nullptr, wc.hInstance, nullptr);
    if (!createDevice(hwnd)) { MessageBoxW(nullptr, L"Direct3D 11 is not available.", L"osu-coach", MB_ICONERROR); return 1; }
    ShowWindow(hwnd, shotDir.empty() ? SW_SHOWDEFAULT : SW_HIDE);
    UpdateWindow(hwnd);

    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    setupStyle(dpi);
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(gDev, gCtx);

    static App app;
    app.hwnd = hwnd;
    gRedraw = [] { app.poll(); renderFrame(app); };
    app.osuDir = findOsuDir();
    app.startScan();

    int idle = 0, shot = 0;  // --screenshot: 4 frames per tab, saved on the last one
    for (bool run = true; run;) {
        if (!app.busy && idle > 2 && shotDir.empty() && !viewerWantsFrames(app)) WaitMessage();  // sleep until input or a worker posts WM_APP
        MSG msg;
        bool any = false;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg), DispatchMessageW(&msg), any = true;
            if (msg.message == WM_QUIT) run = false;
        }
        if (!run) break;
        idle = any ? 0 : idle + 1;
        if (app.busy && !any) Sleep(30);  // progress bar refresh while scanning
        app.poll();
        if (IsIconic(hwnd) && shotDir.empty()) {  // minimized: 0x0 client area, nothing to draw (layout would go negative)
            if (app.viewing && app.viewer) app.viewer->setPlaying(false);
            idle = 3;
            continue;
        }
        bool saveShot = false;
        if (!shotDir.empty() && app.scanned) {
            int tab = shot / 4;  // tabs 0-7, then 8 = the viewer stopped at the first mistake of the newest play with one
            if (tab == 9) break;
            if (tab == 8 && shot % 4 == 0) {
                for (int i = (int)app.res.plays.size() - 1; i >= 0; i--)
                    if (app.res.plays[i].gmiss > 0) { app.selPlay = i; break; }
                app.openViewer();
                if (app.viewer) app.viewer->jumpTo(0);
            }
            gForceTab = shot % 4 == 0 ? tab : -1;
            if (tab == 5 && shot % 4 == 0)
                for (int i = (int)app.res.plays.size() - 1; i >= 0 && app.selPlay < 0; i--)
                    if (app.res.plays[i].gmiss > 0) app.selPlay = i;
            saveShot = shot++ % 4 == 3;
        }
        renderFrame(app, !saveShot ? L"" : shotDir + (shot / 4 - 1 == 8 ? std::wstring(L"\\viewer.bmp") : L"\\tab" + std::to_wstring(shot / 4 - 1) + L".bmp"));
    }
    while (app.busy) app.poll(), Sleep(20);  // only poll() clears busy: let a running scan finish and land
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    return 0;
}
