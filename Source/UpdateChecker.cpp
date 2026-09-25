#include "UpdateChecker.h"

#include <juce_core/juce_core.h>

#ifndef AMBIENCE_GITHUB_REPO
 #define AMBIENCE_GITHUB_REPO "ffsfdfhbdg01/AMBIENCE-PLUGIN-VST"
#endif

UpdateChecker::UpdateChecker() : alive (std::make_shared<std::atomic<bool>> (true)) {}

UpdateChecker::~UpdateChecker() { alive->store (false); }

juce::String UpdateChecker::currentVersion() { return JucePlugin_VersionString; }
juce::String UpdateChecker::repository() { return AMBIENCE_GITHUB_REPO; }
juce::String UpdateChecker::releasesPageUrl() { return "https://github.com/" + repository() + "/releases/latest"; }

bool UpdateChecker::isNewer (const juce::String& a, const juce::String& b)
{
    auto parts = [] (juce::String v)
    {
        v = v.trim().trimCharactersAtStart ("vV");
        juce::StringArray tokens;
        tokens.addTokens (v.upToFirstOccurrenceOf ("-", false, false), ".", {});
        std::array<int, 3> n { 0, 0, 0 };
        for (int i = 0; i < 3 && i < tokens.size(); ++i)
            n[(size_t) i] = tokens[i].getIntValue();
        return n;
    };
    return parts (a) > parts (b);
}

UpdateChecker::Result UpdateChecker::parseResponse (const juce::String& json, int httpStatus, const juce::String& current)
{
    Result r;
    r.currentVersion = current;
    if (httpStatus == 404)
    {
        r.error = "No published release was found, or the GitHub repository is private.";
        return r;
    }
    if (httpStatus != 0 && httpStatus != 200)
    {
        r.error = "GitHub answered with HTTP status " + juce::String (httpStatus) + ".";
        return r;
    }
    const auto parsed = juce::JSON::parse (json);
    const auto tag = parsed.getProperty ("tag_name", {}).toString();
    if (tag.isEmpty())
    {
        r.error = "Unexpected response from GitHub.";
        return r;
    }
    r.latestVersion = tag.trimCharactersAtStart ("vV");
    r.releaseUrl = parsed.getProperty ("html_url", releasesPageUrl()).toString();
    r.status = isNewer (tag, current) ? Result::Status::updateAvailable : Result::Status::upToDate;
    return r;
}

void UpdateChecker::checkAsync (std::function<void (const Result&)> onDone)
{
    if (checking.exchange (true))
        return;

    auto flag = alive;
    auto* self = this;
    juce::Thread::launch ([flag, self, onDone]
    {
        int status = 0;
        juce::String body;
        const juce::URL url ("https://api.github.com/repos/" + repository() + "/releases/latest");
        const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                 .withExtraHeaders ("Accept: application/vnd.github+json\r\nUser-Agent: SAMS-Ambience-Control/" + currentVersion())
                                 .withConnectionTimeoutMs (10000)
                                 .withStatusCode (&status);
        if (auto stream = url.createInputStream (options))
            body = stream->readEntireStreamAsString();

        Result result;
        if (body.isEmpty() && status == 0)
        {
            result.currentVersion = currentVersion();
            result.error = "Could not connect to GitHub. Check your internet connection.";
        }
        else
            result = parseResponse (body, status, currentVersion());

        juce::MessageManager::callAsync ([flag, self, onDone, result]
        {
            if (! flag->load())
                return;
            self->checking = false;
            onDone (result);
        });
    });
}
