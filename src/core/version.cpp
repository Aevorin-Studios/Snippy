#include "version.h"
#include <sstream>
#include <tuple>
#include <cwctype>

namespace snippy {

AppVersion AppVersion::Parse(const std::wstring& str, int explicitBuild) {
    AppVersion v{};
    v.build = explicitBuild;

    std::wstring s = str;
    if (!s.empty() && (s[0] == L'v' || s[0] == L'V')) s = s.substr(1);

    size_t bpos = s.find(L"build");
    if (bpos == std::wstring::npos) bpos = s.find(L"Build");
    if (bpos != std::wstring::npos) {
        std::wstring bstr = s.substr(bpos + 5);
        size_t dpos = 0;
        while (dpos < bstr.size() && !iswdigit(bstr[dpos])) dpos++;
        if (dpos < bstr.size()) {
            try {
                v.build = std::stoi(bstr.substr(dpos));
            } catch (...) {}
        }
        s = s.substr(0, bpos);
        while (!s.empty() && (s.back() == L'-' || s.back() == L'+' || s.back() == L' ' || s.back() == L'(')) {
            s.pop_back();
        }
    }

    std::wstringstream ss(s);
    std::wstring part;
    int idx = 0;
    while (std::getline(ss, part, L'.') && idx < 4) {
        try {
            int val = std::stoi(part);
            if (idx == 0) v.major = val;
            else if (idx == 1) v.minor = val;
            else if (idx == 2) v.patch = val;
            else if (idx == 3 && v.build == 0) v.build = val;
        } catch (...) {}
        idx++;
    }
    return v;
}

bool IsRemoteVersionNewer(const std::wstring& localVer, int localBuild,
                          const std::wstring& remoteVer, int remoteBuild) noexcept {
    AppVersion local  = AppVersion::Parse(localVer, localBuild);
    AppVersion remote = AppVersion::Parse(remoteVer, remoteBuild);

    if (local.major == 1 && local.minor >= 1) {
        if (remote.major == 1 && remote.minor == 0 && remote.build > 0) {
            return true;
        }
    }

    auto localSemVer  = std::tie(local.major, local.minor, local.patch);
    auto remoteSemVer = std::tie(remote.major, remote.minor, remote.patch);

    if (localSemVer < remoteSemVer) {
        return true;
    }
    if (remoteSemVer < localSemVer) {
        return false;
    }

    return local.build < remote.build;
}

} // namespace snippy
