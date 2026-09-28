#include "UpdateChecker.h"

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

#include <array>
#include <atomic>
#include <vector>

#ifndef DEFAULT_DISTORTION_VERSION
 #define DEFAULT_DISTORTION_VERSION "0.0.0"
#endif

namespace dd::update
{
namespace
{
constexpr auto latestReleaseApi =
    "https://api.github.com/repos/lsooxlla8/default_distortion/releases/latest";
constexpr auto nextCheckKey = "nextUpdateCheckMilliseconds";
constexpr int connectionTimeoutMilliseconds = 5000;
constexpr int maximumResponseBytes = 256 * 1024;

std::atomic<bool> processCheckInFlight { false };

juce::InterProcessLock& updatePreferencesLock()
{
    static juce::InterProcessLock lock {
        "icanseesounds-default-distortion-update" };
    return lock;
}

juce::PropertiesFile::Options updateOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "default_distortion-update";
    options.filenameSuffix = "settings";
    options.folderName = "icanseesounds";
    options.osxLibrarySubFolder = "Application Support";
    options.millisecondsBeforeSaving = 0;
    options.processLock = &updatePreferencesLock();
    return options;
}

void setNextCheck (std::int64_t nextCheckMilliseconds)
{
    juce::PropertiesFile properties { updateOptions() };
    properties.setValue (
        nextCheckKey, juce::String (nextCheckMilliseconds));
    properties.saveIfNeeded();
}

bool reserveDueCheck (std::int64_t nowMilliseconds)
{
    juce::PropertiesFile properties { updateOptions() };
    const auto nextCheck = properties.getValue (nextCheckKey).getLargeIntValue();
    if (! isCheckDue (nowMilliseconds, nextCheck))
        return false;

    // Reserve the request as a failure before touching the network. A crash,
    // timeout, or missing connection therefore retries tomorrow rather than
    // hammering GitHub every time another editor is opened.
    properties.setValue (
        nextCheckKey,
        juce::String (nowMilliseconds + retryAfterFailureMilliseconds));
    properties.saveIfNeeded();
    return true;
}

std::optional<std::vector<int>> parseVersion (juce::String version)
{
    version = version.trim();
    if (version.startsWithIgnoreCase ("v"))
        version = version.substring (1);
    if (version.isEmpty())
        return std::nullopt;

    std::vector<int> parts;
    for (const auto& token : juce::StringArray::fromTokens (
             version, ".", ""))
    {
        if (token.isEmpty() || token.containsOnly ("0123456789") == false)
            return std::nullopt;
        parts.push_back (token.getIntValue());
    }
    if (parts.empty())
        return std::nullopt;
    return parts;
}

juce::String readBoundedResponse (juce::WebInputStream& stream)
{
    juce::MemoryOutputStream output;
    std::array<char, 8192> buffer {};
    while (! stream.isExhausted())
    {
        const auto bytesRead = stream.read (
            buffer.data(), static_cast<int> (buffer.size()));
        if (bytesRead <= 0)
            break;
        if (output.getDataSize() + static_cast<size_t> (bytesRead)
            > maximumResponseBytes)
            return {};
        output.write (buffer.data(), static_cast<size_t> (bytesRead));
    }
    return output.toString();
}
} // namespace

std::optional<int> compareVersions (const juce::String& currentVersion,
                                    const juce::String& candidateVersion)
{
    const auto current = parseVersion (currentVersion);
    const auto candidate = parseVersion (candidateVersion);
    if (! current.has_value() || ! candidate.has_value())
        return std::nullopt;

    const auto partCount = juce::jmax (current->size(), candidate->size());
    for (size_t index = 0; index < partCount; ++index)
    {
        const auto currentPart = index < current->size() ? (*current)[index] : 0;
        const auto candidatePart =
            index < candidate->size() ? (*candidate)[index] : 0;
        if (candidatePart != currentPart)
            return candidatePart > currentPart ? 1 : -1;
    }
    return 0;
}

bool isCheckDue (std::int64_t nowMilliseconds,
                 std::int64_t nextCheckMilliseconds) noexcept
{
    if (nextCheckMilliseconds <= 0 || nowMilliseconds >= nextCheckMilliseconds)
        return true;

    // If the system clock was moved backwards, do not suppress update checks
    // for an unexpectedly long time.
    return nextCheckMilliseconds - nowMilliseconds
        > retryAfterSuccessMilliseconds;
}

UpdateChecker::UpdateChecker (UpdateAvailableCallback callback)
    : juce::Thread ("default_distortion update check"),
      onUpdateAvailable (std::move (callback))
{
}

UpdateChecker::~UpdateChecker()
{
    signalThreadShouldExit();
    {
        const juce::ScopedLock lock { streamLock };
        if (activeStream != nullptr)
            activeStream->cancel();
    }
    stopThread (2000);
}

void UpdateChecker::startIfDue()
{
    if (startWasRequested)
        return;
    startWasRequested = true;

    if (processCheckInFlight.exchange (true))
        return;

    const auto now = juce::Time::currentTimeMillis();
    if (! reserveDueCheck (now))
    {
        processCheckInFlight.store (false);
        return;
    }

    if (! startThread (juce::Thread::Priority::low))
        processCheckInFlight.store (false);
}

void UpdateChecker::run()
{
    struct InFlightReset
    {
        ~InFlightReset() { processCheckInFlight.store (false); }
    } reset;

    juce::WebInputStream stream { juce::URL { latestReleaseApi }, false };
    stream.withConnectionTimeout (connectionTimeoutMilliseconds)
        .withNumRedirectsToFollow (3)
        .withExtraHeaders (
            "Accept: application/vnd.github+json\r\n"
            "X-GitHub-Api-Version: 2022-11-28\r\n"
            "User-Agent: default_distortion-update-check\r\n");
    {
        const juce::ScopedLock lock { streamLock };
        activeStream = &stream;
    }

    if (threadShouldExit())
    {
        const juce::ScopedLock lock { streamLock };
        activeStream = nullptr;
        return;
    }

    const auto connected = stream.connect (nullptr);
    const auto statusCode = connected ? stream.getStatusCode() : 0;
    const auto response = connected && statusCode == 200
        ? readBoundedResponse (stream) : juce::String {};
    {
        const juce::ScopedLock lock { streamLock };
        activeStream = nullptr;
    }

    if (threadShouldExit() || response.isEmpty())
        return;

    const auto parsed = juce::JSON::parse (response);
    const auto* object = parsed.getDynamicObject();
    if (object == nullptr)
        return;
    const auto tag = object->getProperty ("tag_name").toString().trim();
    const auto comparison = compareVersions (DEFAULT_DISTORTION_VERSION, tag);
    if (! comparison.has_value())
        return;

    setNextCheck (
        juce::Time::currentTimeMillis() + retryAfterSuccessMilliseconds);
    if (*comparison <= 0 || threadShouldExit())
        return;

    juce::MessageManager::callAsync (
        [callback = onUpdateAvailable, tag]
        {
            if (callback)
                callback (tag.startsWithIgnoreCase ("v")
                              ? tag.substring (1) : tag);
        });
}
} // namespace dd::update
