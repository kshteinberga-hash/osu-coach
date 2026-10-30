@echo off
setlocal
cd /d %~dp0
where cl >nul 2>nul || call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
if not exist build mkdir build
set F=/nologo /O2 /MT /std:c++20 /EHsc /W3 /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_CRT_SECURE_NO_WARNINGS /Ithird_party\imgui /Ithird_party\lzma /Ithird_party\miniaudio /Ithird_party\stb /Ithird_party\fonts /Fobuild\
set SRC=src\parse.cpp src\analyze.cpp src\coach.cpp src\scan.cpp src\audio.cpp src\playback.cpp build\lzma.obj build\audio_impl.obj
if "%OUT%"=="" set OUT=osu-coach.exe
set LIBS=user32.lib shell32.lib ole32.lib advapi32.lib winmm.lib
if not exist build\lzma.obj cl /nologo /O2 /MT /c third_party\lzma\LzmaDec.c /Fobuild\lzma.obj >nul || exit /b 1
if not exist build\audio_impl.obj cl /nologo /O2 /MT /W0 /c /Ithird_party\miniaudio /Ithird_party\stb src\audio_impl.cpp /Fobuild\audio_impl.obj >build\audio-build.log || (type build\audio-build.log & exit /b 1)
if "%1"=="test" (
  cl %F% tests\*.cpp %SRC% /Fetests\test.exe /link %LIBS% >build\test-build.log || (type build\test-build.log & exit /b 1)
  tests\test.exe
  exit /b %errorlevel%
)
if not exist build\imgui.lib (
  cl %F% /c third_party\imgui\imgui*.cpp third_party\imgui\backends\imgui_impl_win32.cpp third_party\imgui\backends\imgui_impl_dx11.cpp >nul || exit /b 1
  lib /nologo /out:build\imgui.lib build\imgui*.obj >nul || exit /b 1
)
rc /nologo /fo build\ui_art.res src\ui_art.rc || exit /b 1
cl %F% src\main.cpp src\gui.cpp src\overview.cpp src\ui_art.cpp src\viewer.cpp %SRC% build\imgui.lib build\ui_art.res /Fe%OUT% /link /SUBSYSTEM:WINDOWS /ENTRY:wmainCRTStartup d3d11.lib dxgi.lib d3dcompiler.lib windowscodecs.lib %LIBS% >build\build.log || (type build\build.log & exit /b 1)
echo built %OUT%
