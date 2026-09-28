#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <optional>

namespace dd::update
{
inline constexpr std::int64_t retryAfterFailureMilliseconds =
    24LL * 60LL * 60LL * 1000LL;
inline constexpr std::int64_t retryAfterSuccessMilliseconds =
    7LL * 24LL * 60LL * 60LL * 1000LL;

[[nodiscard]] std::optional<int> compareVersions (
    const juce::String& currentVersion,
    const juce::String& candidateVersion);

[[nodiscard]] bool isCheckDue (std::int64_t nowMilliseconds,
                               std::int64_t nextCheckMilliseconds) noexcept;

class UpdateChecker final : private juce::Thread
{
public:
    using UpdateAvailableCallback = std::function<void (juce::String)>;

    explicit UpdateChecker (UpdateAvailableCallback);
    ~UpdateChecker() override;

    void startIfDue();

private:
    void run() override;

    UpdateAvailableCallback onUpdateAvailable;
    juce::CriticalSection streamLock;
    juce::WebInputStream* activeStream = nullptr;
    bool startWasRequested = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UpdateChecker)
};
} // namespace dd::update
