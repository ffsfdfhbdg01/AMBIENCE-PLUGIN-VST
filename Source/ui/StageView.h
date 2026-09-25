#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ui
{
// Top-down view of the scene: listener, draggable source (Pan = angle, Distance = log radius),
// obstacle, source width and room extent.
class StageView : public juce::Component, private juce::Timer
{
public:
    explicit StageView (juce::AudioProcessorValueTreeState& state);
    ~StageView() override;

    void paint (juce::Graphics&) override;
    void resized() override {}
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    void setMonoOutput (bool isMono) { monoOut = isMono; }

    static juce::Colour materialColour (int material);

private:
    void timerCallback() override;
    float value (const char* id) const;
    juce::Point<float> listenerPos() const;
    float maxRadius() const;
    float verticalScale() const;
    float distanceToRadius (float metres) const;
    float radiusToDistance (float px) const;
    juce::Point<float> sourcePos() const;
    void setFromPoint (juce::Point<float> p);

    juce::AudioProcessorValueTreeState& apvts;
    juce::ParameterAttachment panAttachment, distanceAttachment;
    float pan = 0, distance = 1;
    bool dragging = false, hover = false, monoOut = false;
    juce::String lastSnapshot;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StageView)
};
} // namespace ui
