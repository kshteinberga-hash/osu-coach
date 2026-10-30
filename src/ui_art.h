#pragma once
#include "imgui.h"
#include <string>
struct ID3D11Device;
extern ImTextureID gMastheadArt, gPaperArt, gOverviewArt;
struct UiCover { ImTextureID texture = 0; float aspect = 1; };
UiCover coverForMap(const std::wstring& mapPath);
bool loadUiArt(ID3D11Device* device);
void releaseUiArt();
