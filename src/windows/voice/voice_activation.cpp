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

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Globalization;
using namespace Windows::Media::SpeechRecognition;

namespace snippy {

VoiceActivationManager g_voiceActivation;

struct VoiceActivationManager::Impl {
    std::function<void()> onTrigger;
    std::thread workerThread;
    std::atomic<bool> running{ false };
    std::atomic<bool> supported{ true };
    std::atomic<bool> stopRequested{ false };

    SpeechRecognizer recognizer{ nullptr };
    SpeechContinuousRecognitionSession session{ nullptr };
    event_token resultToken{};
    event_token completedToken{};

    static bool MatchesActivationPhrase(const std::wstring& str) noexcept {
        if (str.empty()) return false;

        std::wstring lower = str;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

        if (lower.find(L"snippy") != std::wstring::npos && lower.find(L"snip") != std::wstring::npos) {
            return true;
        }
        if (lower.find(L"snip it") != std::wstring::npos) {
            return true;
        }
        return false;
    }

    void WorkerLoop() noexcept {
        try {
            init_apartment(apartment_type::multi_threaded);

            if (!recognizer) {
                recognizer = SpeechRecognizer();

                std::vector<hstring> phrases = {
                    L"Snippy snip it",
                    L"snippy snip it",
                    L"Snippy snip",
                    L"snippy snip",
                    L"snip it",
                    L"Snip it"
                };

                recognizer.Constraints().Append(
                    SpeechRecognitionListConstraint(winrt::single_threaded_vector(std::move(phrases)))
                );

                auto compRes = recognizer.CompileConstraintsAsync().get();
                if (compRes.Status() != SpeechRecognitionResultStatus::Success) {
                    supported.store(false);
                    running.store(false);
                    return;
                }
            }

            session = recognizer.ContinuousRecognitionSession();

            resultToken = session.ResultGenerated([this](SpeechContinuousRecognitionSession const&,
                                                         SpeechContinuousRecognitionResultGeneratedEventArgs const& args) {
                std::wstring text = args.Result().Text().c_str();
                if (MatchesActivationPhrase(text)) {
                    Logger::LogEvent("Voice activation matched phrase: 'Snippy snip it'");
                    if (onTrigger) onTrigger();
                }
            });

            completedToken = session.Completed([this](SpeechContinuousRecognitionSession const&,
                                                      SpeechContinuousRecognitionCompletedEventArgs const& args) {
                if (running.load() && !stopRequested.load()) {
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    if (running.load() && !stopRequested.load() && session) {
                        try {
                            session.StartAsync();
                        } catch (...) {}
                    }
                }
            });

            while (running.load() && !stopRequested.load()) {
                try {
                    session.StartAsync().get();
                    Logger::LogInfo("Voice activation listener started");
                    break;
                } catch (...) {
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                }
            }

            while (running.load() && !stopRequested.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }

            if (session) {
                try {
                    session.StopAsync().get();
                } catch (...) {}
            }

        } catch (const std::exception& ex) {
            Logger::LogError(std::string("Voice activation error: ") + ex.what());
            supported.store(false);
            running.store(false);
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
    m_impl->session = nullptr;
    m_impl->recognizer = nullptr;
}

bool VoiceActivationManager::IsRunning() const noexcept {
    return m_impl->running.load();
}

bool VoiceActivationManager::IsSupported() const noexcept {
    return m_impl->supported.load();
}

} // namespace snippy
