#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class AmbienceProcessor;

namespace ui
{
// Output peak/RMS bars, phase correlation and auto gain readout.
class Meters : public juce::Component, private juce::Timer
{
public:
    explicit Meters (AmbienceProcessor& p);
    ~Meters() override { stopTimer(); }
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    AmbienceProcessor& proc;
    float peak[2] {}, rms[2] {}, corr = 1.0f, ag = 0.0f, peakHold[2] {};
    int holdCounter[2] {};
};
} // namespace ui
