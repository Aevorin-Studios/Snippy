cmake_minimum_required(VERSION 3.25)

project(Snippy VERSION 1.0.0 LANGUAGES CXX RC)

# ── C++ standard ─────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Core shared sources ───────────────────────────────────────────────────────
set(CORE_SOURCES
    src/core/version.cpp
    src/core/settings.cpp
)

# ── Platform-specific sources & configuration ────────────────────────────────
if(WIN32)
    # Unicode and Windows version definitions
    add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_compile_definitions(WINVER=0x0A00 _WIN32_WINNT=0x0A00)   # Windows 10+

    set(PLATFORM_SOURCES
        src/windows/main.cpp
        src/windows/app.cpp
        src/windows/render/d2d_context.cpp
        src/windows/render/dwrite_context.cpp
        src/windows/capture/dxgi_capture.cpp
        src/windows/overlay/overlay_window.cpp
        src/windows/overlay/ocr_extract.cpp
        src/windows/clipboard/clipboard.cpp
        src/windows/hotkey/hotkey_manager.cpp
        src/windows/overlay/tools/rect_tool.cpp
        src/windows/overlay/tools/arrow_tool.cpp
        src/windows/overlay/tools/text_tool.cpp
        src/windows/overlay/tools/blur_tool.cpp
        src/windows/tray/tray_icon.cpp
        src/windows/settings/settings_manager.cpp
        src/windows/settings/settings_window.cpp
        src/windows/theme/theme_manager.cpp
        src/windows/voice/voice_activation.cpp
        src/windows/utils/logger.cpp
        src/windows/update/update_checker.cpp
        src/windows/resources/app.rc
    )

    add_executable(Snippy WIN32 ${CORE_SOURCES} ${PLATFORM_SOURCES})

    # Include dirs: root src, core universal code, and windows platform
    target_include_directories(Snippy PRIVATE src src/core src/windows)

    # Windows system libraries
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

    if(MSVC)
        target_compile_options(Snippy PRIVATE
            $<$<CONFIG:Release>:/O2 /GL /arch:AVX2 /fp:fast /GS- /Gy>
            $<$<CONFIG:RelWithDebInfo>:/O2 /GL /arch:AVX2 /Zi>
            $<$<CONFIG:Debug>:/Od /Zi /RTC1>
        )
        target_compile_options(Snippy PRIVATE
            /W4
            /wd4100   # unreferenced formal parameter
            /wd4505   # unreferenced local function
        )

        target_link_options(Snippy PRIVATE
            $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS>
            $<$<CONFIG:RelWithDebInfo>:/LTCG /OPT:REF /DEBUG /SUBSYSTEM:WINDOWS>
            $<$<CONFIG:Debug>:/DEBUG /SUBSYSTEM:WINDOWS>
        )

        # Static CRT
        set_property(TARGET Snippy PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
elseif(UNIX AND NOT APPLE)
    # Placeholder for Linux build configuration
    # set(PLATFORM_SOURCES src/linux/...)
    # add_executable(Snippy ${CORE_SOURCES} ${PLATFORM_SOURCES})
    # target_include_directories(Snippy PRIVATE src src/core src/linux)
endif()

# ── Performance instrumentation build ─────────────────────────────────────────
option(SNIPPY_PERF_INSTRUMENTATION "Enable [PERF] timing output" OFF)
if(SNIPPY_PERF_INSTRUMENTATION AND TARGET Snippy)
    target_compile_definitions(Snippy PRIVATE SNIPPY_PERF=1)
endif()
