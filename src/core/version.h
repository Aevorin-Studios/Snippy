#pragma once

#include <string>

namespace snippy {

inline constexpr const wchar_t* kCurrentVersion   = L"1.0.0";
inline constexpr int            kCurrentBuild     = 7;
inline constexpr const wchar_t* kDefaultUpdateUrl = L"https://raw.githubusercontent.com/Aevorin-Studios/Snippy-for-Windows/main/version.json";

struct AppVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;
    int build = 0;

    static AppVersion Parse(const std::wstring& str, int explicitBuild = 0);
};

bool IsRemoteVersionNewer(const std::wstring& localVer, int localBuild,
                          const std::wstring& remoteVer, int remoteBuild) noexcept;

} // namespace snippy
