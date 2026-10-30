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

static Skill trainFor(const App& a, const Report& r) { return a.trainSkill >= 0 ? (Skill)a.trainSkill : r.nTop ? r.top[0].s : JUMPS; }

void App::startScan() {
    if (busy || osuDir.empty()) return;
    busy = true, done = 0, total = 0;
    std::thread([this] {
        pRes = scan(osuDir, &done, &total);
        std::wstring hp = exeDir() + L"\\history.json";
        std::vector<Day> hist;
        loadHistory(hp, hist);
        pRep = coach(pRes.plays, hist, nowMs());
        saveHistory(hp, pRep.days);
        pBpm = comfortableStreamBpm(pRes.plays, pRep.recentFrom);
        pSugg = suggest(pRes.db, pRes.plays, trainFor(*this, pRep), lo, hi, pBpm, nowMs());
        scanReady = true;
        PostMessageW((HWND)hwnd, WM_APP, 0, 0);
    }).detach();
}

void App::startSuggest() {
    if (busy || !scanned) return;
    busy = true;
    std::thread([this] {
        pSugg = suggest(res.db, res.plays, trainFor(*this, rep), lo, hi, streamBpm, nowMs());
        suggReady = true;
        PostMessageW((HWND)hwnd, WM_APP, 0, 0);
    }).detach();
}

void App::poll() {
    if (scanReady.exchange(false)) {
        res = std::move(pRes), rep = std::move(pRep), sugg = std::move(pSugg), streamBpm = pBpm;
        scanned = true, selPlay = -1, busy = false;
    }
    if (suggReady.exchange(false)) sugg = std::move(pSugg), busy = false;
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

static void createRtv() {
    ID3D11Texture2D* bb;
    gSwap->GetBuffer(0, IID_PPV_ARGS(&bb));
    gDev->CreateRenderTargetView(bb, nullptr, &gRtv);
    bb->Release();
}
static bool createDevice(HWND h) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2, sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM, sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = h, sd.SampleDesc.Count = 1, sd.Windowed = TRUE, sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL fl, want[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    HRESULT r = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, want, 2, D3D11_SDK_VERSION, &sd, &gSwap, &gDev, &fl, &gCtx);
    if (r == DXGI_ERROR_UNSUPPORTED)
        r = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, want, 2, D3D11_SDK_VERSION, &sd, &gSwap, &gDev, &fl, &gCtx);
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

static LRESULT WINAPI wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return true;
    if (m == WM_SIZE && w != SIZE_MINIMIZED) { gResizeW = LOWORD(l), gResizeH = HIWORD(l); return 0; }
    if (m == WM_SYSCOMMAND && (w & 0xfff0) == SC_KEYMENU) return 0;
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}

static void loadFonts(float dpi) {
    ImGuiIO& io = ImGui::GetIO();
    static ImVector<ImWchar> ranges;
    ImFontGlyphRangesBuilder b;
    b.AddRanges(io.Fonts->GetGlyphRangesDefault());
    b.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
    b.AddText("\xE2\x98\x85\xE2\x80\x94\xE2\x86\x91\xE2\x86\x93\xE2\x80\xA6");  // ★ — ↑ ↓ …
    b.BuildRanges(&ranges);
    wchar_t win[MAX_PATH];
    GetWindowsDirectoryW(win, MAX_PATH);
    std::string font = narrow(std::wstring(win) + L"\\Fonts\\segoeui.ttf"), sym = narrow(std::wstring(win) + L"\\Fonts\\seguisym.ttf");
    if (!io.Fonts->AddFontFromFileTTF(font.c_str(), 18 * dpi, nullptr, ranges.Data)) { io.Fonts->AddFontDefault(); return; }
    ImFontConfig mc;
    mc.MergeMode = true;
    static const ImWchar star[] = {0x2605, 0x2605, 0};
    io.Fonts->AddFontFromFileTTF(sym.c_str(), 18 * dpi, &mc, star);
    io.Fonts->AddFontFromFileTTF(font.c_str(), 30 * dpi, nullptr, ranges.Data);  // headline font
    io.Fonts->AddFontFromFileTTF(sym.c_str(), 30 * dpi, &mc, star);
}

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
    std::wstring shotDir;
    for (int i = 1; i < argc; i++) {
        if (!wcscmp(argv[i], L"--dump")) return dumpMode(argc, argv);
        if (!wcscmp(argv[i], L"--screenshot") && i + 1 < argc) shotDir = argv[i + 1];
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ImGui_ImplWin32_EnableDpiAwareness();
    WNDCLASSEXW wc{sizeof wc, CS_CLASSDC, wndProc, 0, 0, GetModuleHandleW(nullptr), LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION), nullptr, nullptr, nullptr, L"osu-coach", nullptr};
    RegisterClassExW(&wc);
    float dpi = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY));
    HWND hwnd = CreateWindowW(L"osu-coach", L"osu-coach", WS_OVERLAPPEDWINDOW, 100, 100, (int)(1280 * dpi), (int)(820 * dpi), nullptr, nullptr, wc.hInstance, nullptr);
    if (!createDevice(hwnd)) { MessageBoxW(nullptr, L"Direct3D 11 is not available.", L"osu-coach", MB_ICONERROR); return 1; }
    ShowWindow(hwnd, shotDir.empty() ? SW_SHOWDEFAULT : SW_HIDE);
    UpdateWindow(hwnd);

    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.FrameRounding = st.WindowRounding = st.TabRounding = 4, st.WindowPadding = {14, 12};
    st.ScaleAllSizes(dpi);
    loadFonts(dpi);
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(gDev, gCtx);

    static App app;
    app.hwnd = hwnd;
    app.osuDir = findOsuDir();
    app.startScan();

    int idle = 0, shot = 0;  // --screenshot: 4 frames per tab, saved on the last one
    for (bool run = true; run;) {
        if (!app.busy && idle > 2 && shotDir.empty()) WaitMessage();  // sleep until input or a worker posts WM_APP
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
        if (gResizeW) {
            if (gRtv) gRtv->Release(), gRtv = nullptr;
            gSwap->ResizeBuffers(0, gResizeW, gResizeH, DXGI_FORMAT_UNKNOWN, 0);
            gResizeW = gResizeH = 0;
            createRtv();
        }
        bool saveShot = false;
        if (!shotDir.empty() && app.scanned) {
            int tab = shot / 4;
            if (tab == 8) break;
            gForceTab = shot % 4 == 0 ? tab : -1;
            if (tab == 5 && shot % 4 == 0)
                for (int i = (int)app.res.plays.size() - 1; i >= 0 && app.selPlay < 0; i--)
                    if (app.res.plays[i].gmiss > 0) app.selPlay = i;
            saveShot = shot++ % 4 == 3;
        }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        drawApp(app);
        ImGui::Render();
        const float clear[4] = {0.07f, 0.07f, 0.09f, 1};
        gCtx->OMSetRenderTargets(1, &gRtv, nullptr);
        gCtx->ClearRenderTargetView(gRtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        if (saveShot) saveBackbuffer(shotDir + L"\\tab" + std::to_wstring(shot / 4 - 1) + L".bmp");
        gSwap->Present(1, 0);
    }
    while (app.busy) app.poll(), Sleep(20);  // only poll() clears busy: let a running scan finish and land
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    return 0;
}
