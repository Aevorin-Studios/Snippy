#pragma once

#include <functional>
#include <memory>

namespace snippy {

class VoiceActivationManager {
public:
    VoiceActivationManager() noexcept;
    ~VoiceActivationManager() noexcept;

    bool Initialize(std::function<void()> onTrigger) noexcept;
    void Start() noexcept;
    void Stop() noexcept;
    void Shutdown() noexcept;

    [[nodiscard]] bool IsRunning() const noexcept;
    [[nodiscard]] bool IsSupported() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

extern VoiceActivationManager g_voiceActivation;

} // namespace snippy
