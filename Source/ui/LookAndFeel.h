#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ui
{
// Design tokens: dark control-room theme with a single amber accent.
namespace colours
{
    inline const juce::Colour background { 0xff111317 };
    inline const juce::Colour surface { 0xff171a20 };
    inline const juce::Colour surfaceRaised { 0xff1e222a };
    inline const juce::Colour line { 0xff2a2f39 };
    inline const juce::Colour lineStrong { 0xff3a404c };
    inline const juce::Colour text { 0xffe7e9ee };
    inline const juce::Colour textDim { 0xff8d96a4 };
    inline const juce::Colour textFaint { 0xff5c6472 };
    inline const juce::Colour accent { 0xffe9a23b };
    inline const juce::Colour accentSoft { 0x33e9a23b };
    inline const juce::Colour cool { 0xff62c6d8 };
    inline const juce::Colour warn { 0xffe5534b };
    inline const juce::Colour ok { 0xff6cc38a };
} // namespace colours

class AmbienceLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AmbienceLookAndFeel();

    static juce::Font font (float height, bool semibold = false);

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (13.0f); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override { return font (14.0f); }
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (12.5f, true); }
    juce::Font getLabelFont (juce::Label&) override { return font (12.5f); }
    void drawLabel (juce::Graphics&, juce::Label&) override;

    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;

private:
    juce::Typeface::Ptr regular, semibold;
};

} // namespace ui
