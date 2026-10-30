@echo off
rem Regenerates third_party\fonts\fonts.inl (compressed TTFs for ImGui::AddFontFromMemoryCompressedTTF).
setlocal
cd /d %~dp0..
where cl >nul 2>nul || call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
if not exist build mkdir build
cl /nologo /O2 /EHsc /D_CRT_SECURE_NO_WARNINGS third_party\imgui\misc\fonts\binary_to_compressed_c.cpp /Fobuild\ /Febuild\b2c.exe >nul || exit /b 1
set F=third_party\fonts
(
build\b2c.exe -u32 %F%\IBMPlexSans-Regular.ttf kFontPlexSans
build\b2c.exe -u32 %F%\IBMPlexSans-SemiBold.ttf kFontPlexSansSemi
build\b2c.exe -u32 %F%\IBMPlexMono-Regular.ttf kFontPlexMono
build\b2c.exe -u32 %F%\BricolageGrotesque-Bold.ttf kFontBricolageBold
) > %F%\fonts.inl || exit /b 1
echo wrote %F%\fonts.inl
