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
set CXXFLAGS=/utf-8 /std:c++20 /O2 /GL /arch:AVX2 /fp:fast /GS- /Gy /W4 /wd4100 /wd4505 /MT /EHsc /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00 /D_NDEBUG /I..\src /I..\src\core /I..\src\windows
set LDFLAGS=/LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS

echo Compiling Resources...
rc.exe /nologo /fo app.res ..\src\windows\resources\app.rc
if errorlevel 1 goto build_failed

echo Compiling C++...
set CORE_SRCS=..\src\core\version.cpp ..\src\core\settings.cpp
set WIN_SRCS=..\src\windows\main.cpp ..\src\windows\app.cpp ..\src\windows\render\d2d_context.cpp ..\src\windows\render\dwrite_context.cpp ..\src\windows\capture\dxgi_capture.cpp ..\src\windows\overlay\overlay_window.cpp ..\src\windows\overlay\ocr_extract.cpp ..\src\windows\clipboard\clipboard.cpp ..\src\windows\hotkey\hotkey_manager.cpp ..\src\windows\qr\qr_extract.cpp ..\src\windows\qr\quirc.c ..\src\windows\qr\identify.c ..\src\windows\qr\decode.c ..\src\windows\qr\version_db.c ..\src\windows\overlay\tools\rect_tool.cpp ..\src\windows\overlay\tools\arrow_tool.cpp ..\src\windows\overlay\tools\text_tool.cpp ..\src\windows\overlay\tools\blur_tool.cpp ..\src\windows\tray\tray_icon.cpp ..\src\windows\settings\settings_manager.cpp ..\src\windows\settings\settings_window.cpp ..\src\windows\theme\theme_manager.cpp ..\src\windows\utils\logger.cpp ..\src\windows\update\update_checker.cpp
cl.exe /nologo %CXXFLAGS% %CORE_SRCS% %WIN_SRCS% /FeSnippy.exe /link %LDFLAGS% app.res %LIBS%
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
