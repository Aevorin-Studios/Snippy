# Contributing to Snippy

Thank you for your interest in contributing to **Snippy**! We welcome community contributions, bug fixes, and feature improvements.

## Code of Conduct & Contribution Terms

Snippy is released under a **Source-Available License** (see [LICENSE](LICENSE)). By submitting a Pull Request or patch to this repository, you agree that:
1. Your contribution is your original work or you have the legal right to submit it.
2. You grant **Aevorin Studios** a perpetual, worldwide, non-exclusive, royalty-free, irrevocable license to use, modify, merge, compile, publish, distribute, and sublicense your contribution as part of Snippy or related products.
3. Third-party distribution of modified builds or forks outside of contributing to this upstream project is prohibited by the project license.

---

## Getting Started

### 1. Prerequisites
- **OS**: Windows 10 (version 1903+) or Windows 11
- **Toolchain**: Visual Studio 2022 / Community with the **Desktop development with C++** workload (MSVC v143+ supporting C++20)
- **Windows SDK**: Windows 10/11 SDK (`10.0.19041.0` or newer)

### 2. Building Locally
1. Clone the repository and open the **x64 Native Tools Command Prompt for VS 2022**:
   ```cmd
   git clone https://github.com/Aevorin-Studios/Snippy-for-Windows.git
   cd Snippy-for-Windows
   ```
2. Run the quick build script:
   ```cmd
   build.bat
   ```
3. Test the generated executable at `build\Snippy.exe`.

---

## Submitting Pull Requests

1. **Create a Topic Branch**:
   ```cmd
   git checkout -b fix/your-bug-fix
   ```
2. **Follow Coding Standards**:
   - Snippy uses modern **C++20** standard library features and native Win32/Direct2D/DirectWrite/DXGI APIs.
   - Maintain zero external runtime dependencies (avoid bringing in large third-party runtime frameworks).
   - Ensure clean resource cleanup (RAII, ComPtrs, no handle/GDI leaks).
3. **Test Your Changes**:
   - Verify multi-monitor setups and DPI scaling behavior.
   - Test both clipboard copy formats (`CF_DIB` and `CF_PNG`).
   - Check performance and memory footprint.
4. **Open a Pull Request**:
   - Describe the problem your PR solves and attach screenshots/GIFs if relevant.
   - Link any relevant issues.
