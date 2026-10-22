#include "voice_activation.h"
#include "../utils/logger.h"

#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Media.SpeechRecognition.h>

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <cwctype>
#include <mutex>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Globalization;
using namespace Windows::Media::SpeechRecognition;

namespace snippy {

VoiceActivationManager g_voiceActivation;

static std::wstring Normalize(const std::wstring& s) {
    std::wstring res;
    res.reserve(s.size());
    for (wchar_t c : s) {
        if (::iswpunct(c)) continue;
        if (::iswspace(c)) {
            if (!res.empty() && res.back() != L' ') res.push_back(L' ');
            continue;
        }
        res.push_back(static_cast<wchar_t>(::towlower(c)));
    }
    while (!res.empty() && res.back() == L' ') res.pop_back();
    return res;
}

static size_t Levenshtein(const std::wstring& s1, const std::wstring& s2) {
    const size_t len1 = s1.size(), len2 = s2.size();
    std::vector<size_t> col(len2 + 1), prev(len2 + 1);
    for (size_t j = 0; j <= len2; ++j) prev[j] = j;
    for (size_t i = 0; i < len1; ++i) {
        col[0] = i + 1;
        for (size_t j = 0; j < len2; ++j) {
            col[j + 1] = std::min({
                prev[1 + j] + 1,
                col[j] + 1,
                prev[j] + (s1[i] == s2[j] ? 0 : 1)
            });
        }
        prev = col;
    }
    return prev[len2];
}

static std::vector<hstring> BuildConstraintPhrases(const std::wstring& kw) {
    std::vector<std::wstring> raw;
    std::wstring clean = Normalize(kw);
    if (clean.empty()) clean = L"snippy snip it";

    raw.push_back(clean);
    raw.push_back(kw);

    std::vector<std::wstring> words;
    size_t start = 0;
    while (start < clean.size()) {
        size_t sp = clean.find(L' ', start);
        if (sp == std::wstring::npos) {
            words.push_back(clean.substr(start));
            break;
        }
        words.push_back(clean.substr(start, sp - start));
        start = sp + 1;
    }

    if (words.size() > 1) {
        std::wstring acc;
        for (const auto& w : words) {
            if (!acc.empty()) acc += L" ";
            acc += w;
            if (acc != clean && acc.size() >= 3) raw.push_back(acc);
        }
        if (words.size() >= 3) {
            raw.push_back(words[words.size() - 2] + L" " + words.back());
        }
    }

    raw.push_back(L"hey " + clean);
    raw.push_back(L"ok " + clean);
    raw.push_back(L"please " + clean);
    raw.push_back(clean + L" please");
    raw.push_back(clean + L" now");

    if (clean.find(L"snip") != std::wstring::npos) {
        raw.push_back(L"snip it");
        raw.push_back(L"snippy snip");
        raw.push_back(L"snippy snip it");
        raw.push_back(L"snappy snip it");
        raw.push_back(L"snappy");
        raw.push_back(L"take a snip");
        raw.push_back(L"screen snip");
    }
    if (clean.find(L"screenshot") != std::wstring::npos || clean.find(L"screen shot") != std::wstring::npos) {
        raw.push_back(L"take screenshot");
        raw.push_back(L"take a screenshot");
        raw.push_back(L"screen shot");
        raw.push_back(L"screenshot");
    }

    std::vector<hstring> out;
    std::vector<std::wstring> seen;
    for (const auto& p : raw) {
        if (p.empty()) continue;
        std::wstring lower = p;
        for (auto& c : lower) c = static_cast<wchar_t>(::towlower(c));
        if (std::find(seen.begin(), seen.end(), lower) == seen.end()) {
            seen.push_back(lower);
            out.emplace_back(p);
        }
    }
    return out;
}

static bool MatchesActivationPhrase(const std::wstring& recognized, const std::wstring& targetKw) noexcept {
    if (recognized.empty() || targetKw.empty()) return false;

    std::wstring rec = Normalize(recognized);
    std::wstring target = Normalize(targetKw);
    if (rec.empty() || target.empty()) return false;

    if (rec == target) return true;
    if (rec.find(target) != std::wstring::npos) return true;
    if (target.find(rec) != std::wstring::npos && rec.size() >= 4) return true;

    std::vector<std::wstring> tWords, rWords;
    {
        size_t s = 0;
        while (s < target.size()) {
            size_t sp = target.find(L' ', s);
            if (sp == std::wstring::npos) { tWords.push_back(target.substr(s)); break; }
            tWords.push_back(target.substr(s, sp - s));
            s = sp + 1;
        }
    }
    {
        size_t s = 0;
        while (s < rec.size()) {
            size_t sp = rec.find(L' ', s);
            if (sp == std::wstring::npos) { rWords.push_back(rec.substr(s)); break; }
            rWords.push_back(rec.substr(s, sp - s));
            s = sp + 1;
        }
    }

    if (tWords.size() == 1) {
        const auto& tw = tWords[0];
        for (const auto& rw : rWords) {
            if (rw == tw) return true;
            if (rw.size() >= 4 && Levenshtein(rw, tw) <= 1) return true;
        }
    } else {
        size_t matched = 0;
        for (const auto& tw : tWords) {
            for (const auto& rw : rWords) {
                if (rw == tw || (rw.size() >= 4 && Levenshtein(rw, tw) <= 1)) {
                    ++matched;
                    break;
                }
            }
        }
        if (matched >= 2 || matched * 3 >= tWords.size() * 2) {
            return true;
        }
    }

    if (rec.size() >= 4 && target.size() >= 4) {
        size_t d = Levenshtein(rec, target);
        if (d <= 2 && d <= target.size() / 3) return true;
    }

    if (target.find(L"snip") != std::wstring::npos) {
        if (rec.find(L"snip") != std::wstring::npos && (rec.find(L"snippy") != std::wstring::npos || rec.find(L"it") != std::wstring::npos)) {
            return true;
        }
        if (rec.find(L"snappy") != std::wstring::npos) {
            return true;
        }
    }

    return false;
}

struct VoiceActivationManager::Impl {
    std::function<void()> onTrigger;
    std::thread workerThread;
    std::atomic<bool> running{ false };
    std::atomic<bool> supported{ true };
    std::atomic<bool> stopRequested{ false };
    std::atomic<bool> reloadRequested{ false };
    std::atomic<bool> needsRestart{ false };

    std::mutex keywordMutex;
    std::wstring currentKeyword{ L"Snippy snip it" };

    std::mutex triggerMutex;
    std::chrono::steady_clock::time_point lastTriggerTime{};

    SpeechRecognizer recognizer{ nullptr };
    SpeechContinuousRecognitionSession session{ nullptr };
    event_token hypothesisToken{};
    event_token resultToken{};
    event_token completedToken{};

    void FireTrigger(const std::wstring& matchedText) noexcept {
        auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(triggerMutex);
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTriggerTime).count();
        if (elapsed < 1500) {
            return;
        }
        lastTriggerTime = now;

        std::string narrowText;
        for (wchar_t c : matchedText) narrowText.push_back(static_cast<char>(c < 128 ? c : '?'));
        Logger::LogEvent("Voice activation triggered by phrase: '" + narrowText + "'");

        if (onTrigger) {
            onTrigger();
        }
    }

    void CheckMatchAndTrigger(const std::wstring& text) noexcept {
        if (text.empty()) return;
        std::wstring kw;
        {
            std::lock_guard<std::mutex> lock(keywordMutex);
            kw = currentKeyword;
        }
        if (MatchesActivationPhrase(text, kw)) {
            FireTrigger(text);
        }
    }

    bool BuildAndStartSession() noexcept {
        try {
            recognizer = SpeechRecognizer();

            recognizer.Timeouts().InitialSilenceTimeout(std::chrono::hours(24));
            recognizer.Timeouts().BabbleTimeout(std::chrono::hours(24));
            recognizer.Timeouts().EndSilenceTimeout(std::chrono::milliseconds(150));

            std::wstring kw;
            {
                std::lock_guard<std::mutex> lock(keywordMutex);
                kw = currentKeyword;
            }

            auto phrases = BuildConstraintPhrases(kw);
            recognizer.Constraints().Append(
                SpeechRecognitionListConstraint(winrt::single_threaded_vector(std::move(phrases)))
            );

            auto compRes = recognizer.CompileConstraintsAsync().get();
            if (compRes.Status() != SpeechRecognitionResultStatus::Success) {
                Logger::LogError("Speech recognizer CompileConstraintsAsync failed");
                supported.store(false);
                return false;
            }

            hypothesisToken = recognizer.HypothesisGenerated([this](SpeechRecognizer const&,
                                                                    SpeechRecognitionHypothesisGeneratedEventArgs const& args) {
                try {
                    CheckMatchAndTrigger(args.Hypothesis().Text().c_str());
                } catch (...) {}
            });

            session = recognizer.ContinuousRecognitionSession();

            resultToken = session.ResultGenerated([this](SpeechContinuousRecognitionSession const&,
                                                         SpeechContinuousRecognitionResultGeneratedEventArgs const& args) {
                try {
                    CheckMatchAndTrigger(args.Result().Text().c_str());
                } catch (...) {}
            });

            completedToken = session.Completed([this](SpeechContinuousRecognitionSession const&,
                                                      SpeechContinuousRecognitionCompletedEventArgs const&) {
                if (running.load() && !stopRequested.load()) {
                    needsRestart.store(true);
                }
            });

            session.StartAsync().get();
            Logger::LogInfo("Voice activation continuous listener active");
            return true;
        } catch (const std::exception& ex) {
            Logger::LogError(std::string("Voice activation start error: ") + ex.what());
            return false;
        } catch (...) {
            Logger::LogError("Voice activation start unknown error");
            return false;
        }
    }

    void TeardownSession() noexcept {
        try {
            if (session) {
                try { session.StopAsync().get(); } catch (...) {}
                session = nullptr;
            }
            recognizer = nullptr;
        } catch (...) {}
    }

    void WorkerLoop() noexcept {
        try {
            init_apartment(apartment_type::multi_threaded);

            while (running.load() && !stopRequested.load()) {
                if (!BuildAndStartSession()) {
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    continue;
                }

                while (running.load() && !stopRequested.load() && !needsRestart.load() && !reloadRequested.load()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }

                TeardownSession();

                if (reloadRequested.exchange(false)) {
                    continue;
                }

                if (needsRestart.exchange(false)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
            }

            TeardownSession();
        } catch (...) {
            supported.store(false);
            running.store(false);
        }
    }
};

VoiceActivationManager::VoiceActivationManager() noexcept
    : m_impl(std::make_unique<Impl>()) {}

VoiceActivationManager::~VoiceActivationManager() noexcept {
    Shutdown();
}

bool VoiceActivationManager::Initialize(std::function<void()> onTrigger) noexcept {
    m_impl->onTrigger = std::move(onTrigger);
    return true;
}

void VoiceActivationManager::SetKeyword(const std::wstring& keyword) noexcept {
    std::wstring clean = keyword;
    while (!clean.empty() && (clean.front() == L' ' || clean.front() == L'\t')) clean.erase(clean.begin());
    while (!clean.empty() && (clean.back() == L' ' || clean.back() == L'\t')) clean.pop_back();
    if (clean.empty()) clean = L"Snippy snip it";

    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(m_impl->keywordMutex);
        if (_wcsicmp(m_impl->currentKeyword.c_str(), clean.c_str()) != 0) {
            m_impl->currentKeyword = clean;
            changed = true;
        }
    }

    if (changed && m_impl->running.load()) {
        m_impl->reloadRequested.store(true);
    }
}

std::wstring VoiceActivationManager::GetKeyword() const noexcept {
    std::lock_guard<std::mutex> lock(m_impl->keywordMutex);
    return m_impl->currentKeyword;
}

void VoiceActivationManager::Start() noexcept {
    if (!m_impl->supported.load()) return;
    if (m_impl->running.exchange(true)) return;

    m_impl->stopRequested.store(false);
    m_impl->workerThread = std::thread([this]() { m_impl->WorkerLoop(); });
}

void VoiceActivationManager::Stop() noexcept {
    if (!m_impl->running.exchange(false)) return;

    m_impl->stopRequested.store(true);
    if (m_impl->session) {
        try {
            m_impl->session.StopAsync();
        } catch (...) {}
    }

    if (m_impl->workerThread.joinable()) {
        m_impl->workerThread.join();
    }
}

void VoiceActivationManager::Shutdown() noexcept {
    Stop();
    m_impl->TeardownSession();
}

bool VoiceActivationManager::IsRunning() const noexcept {
    return m_impl->running.load();
}

bool VoiceActivationManager::IsSupported() const noexcept {
    return m_impl->supported.load();
}

} // namespace snippy
