#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PresetManager.h"
#include "dsp/SpatialEngine.h"

class AmbienceProcessor : public juce::AudioProcessor
{
public:
    static constexpr int stateVersion = 2; // 2: room list extended to 16 variants

    AmbienceProcessor();
    ~AmbienceProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    bool isMonoOutput() const noexcept { return monoOutput.load(); }
    bool isSurroundOutput() const noexcept { return surroundOut.load(); }
    int getOutputChannelsForUi() const noexcept { return outputChannelsForUi.load(); }

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets;

    // Metering (written on the audio thread, read by the editor).
    std::atomic<float> meterPeak[2] { 0.0f, 0.0f }, meterRms[2] { 0.0f, 0.0f };
    std::atomic<float> correlation { 1.0f }, autoGainDb { 0.0f };
    std::atomic<int> monoToStereoStatus { 0 }; // amb::SpatialEngine::MonoToStereoStatus
    std::atomic<int> uiScalePercent { 100 };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    void updateMeters (const juce::AudioBuffer<float>& buffer, int numOut, int numSamples) noexcept;
    void writeDry (const juce::AudioBuffer<float>& src, juce::AudioBuffer<float>& dst, int numIn, int numOut, int n) const;

    amb::SpatialEngine engine;
    std::vector<std::pair<std::string, std::atomic<float>*>> rawParams;
    juce::AudioParameterBool* bypassParam = nullptr;
    juce::AudioBuffer<float> dryBuffer, foldBuffer, lfeBuffer;
    amb::SpeakerLayout outLayout;
    std::atomic<bool> surroundOut { false };
    bool surroundIn = false;
    std::atomic<int> outputChannelsForUi { 2 };
    std::array<float, amb::maxOutputChannels> foldL {}, foldR {};
    int lfeIn = -1, lfeOut = -1;
    float bypassMix = 1.0f; // 1 = processed, 0 = bypassed
    float bypassStep = 0.0f;
    std::atomic<bool> monoOutput { false };
    float corrNum = 0, corrL = 0, corrR = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmbienceProcessor)
};
