# Snippy

Snippy is a native Windows screenshot utility written in C++20 using the Win32 API, DirectX 11 (DXGI Desktop Duplication), and Direct2D. Built by Aevorin Studios, it was designed to provide an instant screen capture tool that stays resident in memory without the overhead of Electron or .NET runtimes.

![Snippy Demo](demo.gif)

## Features

- **Capture Pipeline**: Uses the Desktop Duplication API (`IDXGIOutputDuplication`) to acquire frames directly from the display pipeline into a pre-allocated staging surface.
- **Selection Modes**: Rectangular selection, full-screen monitor capture, and arbitrary polygonal lasso selection.
- **Drawing & Annotation**: Direct2D-rendered vector overlays supporting freehand pen, translucent highlighter (single-path geometry without joint overlapping artifacts), lines, arrows, rectangles, and counter badges.
- **In-Place Text Editor**: Interactive text annotation box supporting caret navigation, multi-line wrapping, font family selection from DirectWrite system fonts, and font size scaling.
- **Local OCR**: Extracts text from captured regions using the on-device WinRT OCR engine (`Windows.Media.Ocr.OcrEngine`). Includes character bounding-box selection, line grouping, and full-text copy without network requests.
- **Pixel Operations**: In-place region blurring (Gaussian/mosaic) and color inversion on the captured bitmap.
- **Clipboard Output**: Immediately writes raw `CF_DIB` (Device-Independent Bitmap) on the main thread for zero-latency pasting, followed by background asynchronous encoding for registered `CF_PNG` targets.
- **Voice Activation**: Hands-free capture triggered by saying your keyword (default *"Snippy snip it"*, customizable in Settings) into your microphone. Runs natively via on-device Windows speech recognition with real-time hypothesis matching.
- **Settings & Hotkeys**: Direct2D hardware-accelerated settings window to configure global hotkeys, toolbar positioning, background dimming opacity, voice activation keyword and toggle, and active annotation tools.

## Keyboard Shortcuts & Voice Commands

| Shortcut / Phrase | Context | Action |
| :--- | :--- | :--- |
| `"Snippy snip it"` (customizable) | Voice | Trigger screen capture overlay hands-free |
| `Alt + S` | Global | Trigger screen capture overlay (configurable) |
| `PrintScreen` | Global | Trigger screen capture overlay |
| `Enter` | Selection Active | Confirm current selection and copy to clipboard |
| `Escape` | Selection Active | Cancel capture or dismiss active editor |
| `Ctrl + Z` | Annotation Mode | Undo last annotation stroke or shape |
| `Ctrl + Y` / `Ctrl + Shift + Z` | Annotation Mode | Redo last undone action |
| `Shift + Enter` | Text Tool | Insert newline without committing text |
| `Enter` | Text Tool | Commit and finalize current text annotation |
| `Ctrl + A` | OCR Active | Select all recognized text blocks |
| `Ctrl + C` | OCR Active | Copy selected text to clipboard |

## Build from Source

### Prerequisites

- Windows 10 (version 1903+) or Windows 11
- Visual Studio 2022 / 2026 with C++ Desktop Development workload (MSVC v143+ supporting C++20)
- Windows 10/11 SDK (version 10.0.19041.0 or newer)
- Inno Setup 6 (optional, required only for building `Snippy_Setup.exe`)

### Quick Build (Command Line)

Run `build.bat` from the Developer Command Prompt:

```cmd
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64
build.bat
```

The compiled binary will be placed at `build\Snippy.exe`.

### CMake Build

Alternatively, build using CMake:

```cmd
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### Packaging Installer

To generate the standalone setup installer:

```cmd
make_installer.bat
```

The output installer is generated at `InstallerOutput\Snippy_Setup.exe`.

## Known Issues / Limitations

- **DXGI Output Duplication Requirements**: Desktop duplication requires hardware acceleration. It will fail on basic display adapter fallbacks, remote desktop sessions without GPU virtualization, or virtual machines lacking DirectX 11 3D acceleration.
- **Multi-Monitor Mixed DPI**: On setups where monitors have significantly different DPI scale factors (e.g., 200% alongside 100%), moving selections across monitor boundaries can produce coordinate rounding differences until mouse release.
- **OCR Language Packs**: The text recognition feature depends on Windows built-in OCR language packs. If recognition fails to return characters, verify that the corresponding OCR language component is installed under Windows Settings -> Time & Language -> Language & Region.

## License & Contributing

Snippy is released under a **Source-Available License** by [Aevorin Studios](https://github.com/Aevorin-Studios):

- **Personal & Local Use**: You are free to view the source code, inspect it, and compile it locally for personal use.
- **Contributions**: Contributions and Pull Requests back to the official upstream repository are welcome. By submitting a contribution, you grant Aevorin Studios rights to include and distribute your work as part of the official project.
- **Redistribution Restrictions**: You may **not** distribute, publish, or host modified versions/forks of the software or its compiled binaries without prior written permission from Aevorin Studios.

See the full terms in the [LICENSE](LICENSE) file.
