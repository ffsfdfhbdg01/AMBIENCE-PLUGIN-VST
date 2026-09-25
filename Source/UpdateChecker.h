#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

// Queries the latest GitHub release of the project and compares it with the running version.
// The network request runs on a background thread; the callback is delivered on the message thread
// (and dropped if the owner has been destroyed meanwhile).
class UpdateChecker
{
public:
    struct Result
    {
        enum class Status
        {
            upToDate,
            updateAvailable,
            failed
        };
        Status status = Status::failed;
        juce::String currentVersion, latestVersion, releaseUrl, error;
    };

    UpdateChecker();
    ~UpdateChecker();

    void checkAsync (std::function<void (const Result&)> onDone);
    bool isChecking() const noexcept { return checking.load(); }

    static juce::String currentVersion();
    static juce::String repository();
    static juce::String releasesPageUrl();

    // True if version string a (e.g. "v1.2.0") is newer than b ("1.1.3").
    static bool isNewer (const juce::String& a, const juce::String& b);

    // Parses a GitHub "releases/latest" JSON response (exposed for testing).
    static Result parseResponse (const juce::String& json, int httpStatus, const juce::String& current);

private:
    std::shared_ptr<std::atomic<bool>> alive;
    std::atomic<bool> checking { false };
};
