#include <windows.h>
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include "parse.h"
#include "ui_art.h"
using Microsoft::WRL::ComPtr;
ImTextureID gMastheadArt = 0, gPaperArt = 0, gOverviewArt = 0;
static ComPtr<ID3D11ShaderResourceView> masthead, paper, overview, cover;
static ComPtr<ID3D11Device> artDevice;
static std::wstring cachedMap;
static float coverAspect = 1;
static bool load(ID3D11Device* device, int id, ComPtr<ID3D11ShaderResourceView>& view) {
    auto module = GetModuleHandleW(nullptr);
    auto resource = FindResourceW(module, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));
    if (!resource) return false;
    auto data = LoadResource(module, resource);
    auto size = SizeofResource(module, resource);
    auto bytes = static_cast<BYTE*>(LockResource(data));
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateStream(&stream)) || FAILED(stream->InitializeFromMemory(bytes, size)) ||
        FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) || FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return false;
    UINT w = 0, h = 0;
    if (FAILED(converter->GetSize(&w, &h)) || !w || !h || w > 8192 || h > 8192) return false;
    std::vector<BYTE> pixels(static_cast<size_t>(w) * h * 4);
    if (FAILED(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(pixels.size()), pixels.data()))) return false;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{pixels.data(), w * 4, 0};
    ComPtr<ID3D11Texture2D> texture;
    return SUCCEEDED(device->CreateTexture2D(&desc, &initial, &texture)) &&
           SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &view));
}
bool loadUiArt(ID3D11Device* device) {
    artDevice = device;
    bool ok = load(device, 201, masthead) && load(device, 202, paper) && load(device, 203, overview);
    gMastheadArt = (ImTextureID)(uintptr_t)masthead.Get();
    gPaperArt = (ImTextureID)(uintptr_t)paper.Get();
    gOverviewArt = (ImTextureID)(uintptr_t)overview.Get();
    return ok;
}
UiCover coverForMap(const std::wstring& mapPath) {
    if (mapPath == cachedMap) return {(ImTextureID)(uintptr_t)cover.Get(), coverAspect};
    cachedMap = mapPath; cover.Reset(); coverAspect = 1;
    if (mapPath.empty() || !artDevice) return {};
    std::ifstream file{std::filesystem::path(mapPath)};
    std::string line, filename;
    bool events = false;
    while (std::getline(file, line)) {
        if (line.find("[Events]") == 0) { events = true; continue; }
        if (events && !line.empty() && line[0] == '[') break;
        if (!events || (line.find("0,0,") != 0 && line.find("Background,") != 0)) continue;
        auto begin = line.find('"'), end = line.find('"', begin + 1);
        if (begin != std::string::npos && end != std::string::npos) filename = line.substr(begin + 1, end - begin - 1);
        break;
    }
    if (filename.empty()) return {};
    std::filesystem::path relative = widen(filename);
    if (relative.is_absolute()) return {};
    for (const auto& part : relative) if (part == L"..") return {};
    auto path = std::filesystem::path(mapPath).parent_path() / relative;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame))) return {};
    UINT w = 0, h = 0;
    if (FAILED(frame->GetSize(&w, &h)) || !w || !h || w > 32768 || h > 32768) return {};
    coverAspect = (float)w / h;
    float scale = std::min(1.f, 1024.f / std::max(w, h));
    w = std::max(1u, (UINT)(w * scale)); h = std::max(1u, (UINT)(h * scale));
    if (FAILED(factory->CreateBitmapScaler(&scaler)) || FAILED(scaler->Initialize(frame.Get(), w, h, WICBitmapInterpolationModeFant)) ||
        FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return {};
    std::vector<BYTE> pixels((size_t)w * h * 4);
    if (FAILED(converter->CopyPixels(nullptr, w * 4, (UINT)pixels.size(), pixels.data()))) return {};
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{pixels.data(), w * 4, 0};
    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(artDevice->CreateTexture2D(&desc, &initial, &texture)) ||
        FAILED(artDevice->CreateShaderResourceView(texture.Get(), nullptr, &cover))) return {};
    return {(ImTextureID)(uintptr_t)cover.Get(), coverAspect};
}
void releaseUiArt() {
    gMastheadArt = gPaperArt = gOverviewArt = 0;
    masthead.Reset(); paper.Reset(); overview.Reset(); cover.Reset(); artDevice.Reset(); cachedMap.clear();
}
