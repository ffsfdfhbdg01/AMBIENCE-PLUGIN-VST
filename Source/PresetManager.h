#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/Params.h"

// Factory presets (from dsp/Params.h) + user presets stored as versioned XML files.
class PresetManager
{
public:
    static constexpr int presetFormatVersion = 2; // 2: 16 room variants
    static inline const juce::String extension { ".ambpreset" };

    explicit PresetManager (juce::AudioProcessorValueTreeState& state);

    static juce::File userPresetFolder();

    juce::StringArray factoryNames() const;
    juce::StringArray userNames() const;

    void loadFactory (int index);
    bool loadUser (const juce::String& name);
    bool saveUser (const juce::String& name);
    bool deleteUser (const juce::String& name);

    // Step through the combined list (factory then user).
    void loadNext (int direction);

    juce::String currentName() const { return current; }
    void setCurrentName (const juce::String& n) { current = n; }
    int currentFactoryIndex() const;
    bool currentIsUser() const;

private:
    void setParam (const juce::String& id, float plainValue);
    void resetToDefaults();

    juce::AudioProcessorValueTreeState& apvts;
    juce::String current { "Init (Neutral)" };
};
