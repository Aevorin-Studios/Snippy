@echo off
setlocal
cd /d "%~dp0"

echo Closing any running Snippy.exe...
taskkill /IM Snippy.exe /T /F >nul 2>&1

:: Load Visual Studio environment if needed
cl >nul 2>&1
if errorlevel 1 (
    call "E:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64
    if errorlevel 1 goto build_failed
)

mkdir build 2>nul
cd /d "%~dp0build"
if errorlevel 1 goto build_failed

set LIBS=d2d1.lib dwrite.lib dxgi.lib d3d11.lib dcomp.lib windowscodecs.lib ole32.lib user32.lib gdi32.lib shell32.lib shlwapi.lib dwmapi.lib ntdll.lib shcore.lib comdlg32.lib dxguid.lib WindowsApp.lib wininet.lib
set CXXFLAGS=/utf-8 /std:c++20 /O2 /GL /arch:AVX2 /fp:fast /GS- /Gy /W4 /wd4100 /wd4505 /MT /EHsc /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00 /D_NDEBUG
set LDFLAGS=/LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS

echo Compiling Resources...
rc.exe /nologo /fo app.res ..\src\resources\app.rc
if errorlevel 1 goto build_failed

echo Compiling C++...
cl.exe /nologo %CXXFLAGS% ..\src\main.cpp ..\src\app.cpp ..\src\render\d2d_context.cpp ..\src\render\dwrite_context.cpp ..\src\capture\dxgi_capture.cpp ..\src\overlay\overlay_window.cpp ..\src\overlay\ocr_extract.cpp ..\src\clipboard\clipboard.cpp ..\src\hotkey\hotkey_manager.cpp ..\src\overlay\tools\rect_tool.cpp ..\src\overlay\tools\arrow_tool.cpp ..\src\overlay\tools\text_tool.cpp ..\src\overlay\tools\blur_tool.cpp ..\src\tray\tray_icon.cpp ..\src\settings\settings_manager.cpp ..\src\settings\settings_window.cpp ..\src\theme\theme_manager.cpp ..\src\utils\logger.cpp ..\src\update\update_checker.cpp /FeSnippy.exe /link %LDFLAGS% app.res %LIBS%
if errorlevel 1 goto build_failed

echo.
echo Build successful!
set "BUILD_RESULT=0"
goto build_finished

:build_failed
echo.
echo Build failed. Review the errors above.
set "BUILD_RESULT=1"

:build_finished
echo.
if "%NOPAUSE%"=="" pause
exit /b %BUILD_RESULT%
