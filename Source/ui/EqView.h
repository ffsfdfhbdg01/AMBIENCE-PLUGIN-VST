#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../dsp/Params.h"

namespace ui
{
// Interactive response graph of one EQ. Five points: low cut and high cut (drag sideways), low
// shelf, peak and high shelf (drag frequency / gain). Mouse wheel on the peak = Q (on a shelf =
// gain, on a cut = frequency); Ctrl/Cmd = fine; double-click = reset the band. Reads and writes the
// plug-in parameters with host gestures, so every move is automatable and undoable.
class EqGraph : public juce::Component, private juce::Timer
{
public:
    explicit EqGraph (juce::AudioProcessorValueTreeState& state);
    ~EqGraph() override;

    void setEq (const amb::pid::EqIds& ids, const juce::String& name);
    void setNote (const juce::String& text);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    enum Band
    {
        none = -1,
        lowCut,
        lowShelf,
        peak,
        highShelf,
        highCut,
        numBands
    };

    void timerCallback() override;
    amb::EqParams read() const;
    float value (const char* id) const;
    void setValue (const char* id, float plain);
    void gesture (int band, bool begin);
    juce::StringArray bandIds (int band) const; // frequency first, then gain / Q
    juce::Point<float> nodePos (int band, const amb::EqParams& p) const;
    int nodeAt (juce::Point<float> pos) const;
    juce::String describe (int band, const amb::EqParams& p) const;

    float freqToX (double f) const;
    double xToFreq (float x) const;
    float dbToY (double db) const;
    double yToDb (float y) const;

    juce::AudioProcessorValueTreeState& apvts;
    const amb::pid::EqIds* ids = nullptr;
    juce::String eqName, note;
    juce::Rectangle<float> plot;
    int hover = none, dragging = none;
    juce::Point<float> dragStart;
    float startFreq = 0, startGain = 0;
    juce::String lastSnapshot;
};

// EQ section of the Advanced panel: three tabs (input / output / reverb only), on/off and the graph.
// A tab shows a dot while its EQ is on and not neutral.
class EqSection : public juce::Component, private juce::Timer
{
public:
    explicit EqSection (juce::AudioProcessorValueTreeState& state);
    ~EqSection() override { stopTimer(); }
    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void setRoomOff (bool off);

private:
    void selectTab (int index);
    bool isActive (int index) const;
    void timerCallback() override;

    juce::AudioProcessorValueTreeState& apvts;
    juce::TextButton tabs[3];
    juce::ToggleButton onToggle { "EQ On" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttachment;
    EqGraph graph;
    int current = 0, activeMask = 0;
    bool roomOff = false;
};
} // namespace ui
