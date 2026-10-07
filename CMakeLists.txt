cmake_minimum_required(VERSION 3.25)

project(Snippy VERSION 1.0.0 LANGUAGES CXX RC)

# ── C++ standard ─────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Unicode ───────────────────────────────────────────────────────────────────
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
add_compile_definitions(WINVER=0x0A00 _WIN32_WINNT=0x0A00)   # Windows 10+

# ── Source files ──────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/app.cpp
    src/render/d2d_context.cpp
    src/render/dwrite_context.cpp
    src/capture/dxgi_capture.cpp
    src/overlay/overlay_window.cpp
    src/overlay/ocr_extract.cpp
    src/clipboard/clipboard.cpp
    src/hotkey/hotkey_manager.cpp
    src/overlay/tools/rect_tool.cpp
    src/overlay/tools/arrow_tool.cpp
    src/overlay/tools/text_tool.cpp
    src/overlay/tools/blur_tool.cpp
    src/tray/tray_icon.cpp
    src/settings/settings_manager.cpp
    src/settings/settings_window.cpp
    src/theme/theme_manager.cpp
    src/utils/logger.cpp
    src/update/update_checker.cpp
    src/resources/app.rc
)

add_executable(Snippy WIN32 ${SOURCES})

# ── Include dirs ──────────────────────────────────────────────────────────────
target_include_directories(Snippy PRIVATE src)

# ── Windows system libraries ──────────────────────────────────────────────────
target_link_libraries(Snippy PRIVATE
    d2d1.lib
    dwrite.lib
    dxgi.lib
    d3d11.lib
    dcomp.lib           # DirectComposition
    windowscodecs.lib   # WIC
    ole32.lib
    user32.lib
    gdi32.lib
    shell32.lib
    shlwapi.lib
    dwmapi.lib
    ntdll.lib
    shcore.lib          # GetDpiForMonitor
    wininet.lib         # HTTP Update Checker
    WindowsApp.lib      # WinRT OCR
)

# ── MSVC-specific optimizations ───────────────────────────────────────────────
if(MSVC)
    # Per-configuration compile options
    target_compile_options(Snippy PRIVATE
        $<$<CONFIG:Release>:/O2 /GL /arch:AVX2 /fp:fast /GS- /Gy>
        $<$<CONFIG:RelWithDebInfo>:/O2 /GL /arch:AVX2 /Zi>
        $<$<CONFIG:Debug>:/Od /Zi /RTC1>
    )
    target_compile_options(Snippy PRIVATE
        /W4
        /wd4100   # unreferenced formal parameter (common in Win32 callbacks)
        /wd4505   # unreferenced local function
    )

    # Per-configuration link options
    target_link_options(Snippy PRIVATE
        $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS>
        $<$<CONFIG:RelWithDebInfo>:/LTCG /OPT:REF /DEBUG /SUBSYSTEM:WINDOWS>
        $<$<CONFIG:Debug>:/DEBUG /SUBSYSTEM:WINDOWS>
    )

    # Static CRT: eliminates MSVCRT.dll dependency, reduces startup time
    set_property(TARGET Snippy PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ── Performance instrumentation build ─────────────────────────────────────────
option(SNIPPY_PERF_INSTRUMENTATION "Enable [PERF] timing output" OFF)
if(SNIPPY_PERF_INSTRUMENTATION)
    target_compile_definitions(Snippy PRIVATE SNIPPY_PERF=1)
endif()
