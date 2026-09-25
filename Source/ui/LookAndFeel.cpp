#include "LookAndFeel.h"

#include "BinaryData.h"

namespace ui
{
namespace
{
    juce::Typeface::Ptr& regularFace()
    {
        static juce::Typeface::Ptr t = juce::Typeface::createSystemTypefaceFor (BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize);
        return t;
    }
    juce::Typeface::Ptr& semiboldFace()
    {
        static juce::Typeface::Ptr t = juce::Typeface::createSystemTypefaceFor (BinaryData::InterSemiBold_ttf, BinaryData::InterSemiBold_ttfSize);
        return t;
    }
} // namespace

AmbienceLookAndFeel::AmbienceLookAndFeel()
{
    regular = regularFace();
    semibold = semiboldFace();

    using namespace colours;
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, accentSoft);
    setColour (juce::Label::textColourId, text);
    setColour (juce::Label::textWhenEditingColourId, text);
    setColour (juce::Label::backgroundWhenEditingColourId, surfaceRaised);
    setColour (juce::Label::outlineWhenEditingColourId, accent);
    setColour (juce::TextEditor::backgroundColourId, surfaceRaised);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::outlineColourId, line);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::TextEditor::highlightColourId, accentSoft);
    setColour (juce::CaretComponent::caretColourId, accent);
    setColour (juce::ComboBox::backgroundColourId, surfaceRaised);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::outlineColourId, line);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::PopupMenu::backgroundColourId, surfaceRaised);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::headerTextColourId, textDim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accentSoft);
    setColour (juce::PopupMenu::highlightedTextColourId, text);
    setColour (juce::TextButton::buttonColourId, surfaceRaised);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, textDim);
    setColour (juce::TextButton::textColourOnId, background);
    setColour (juce::TooltipWindow::backgroundColourId, surfaceRaised);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::AlertWindow::backgroundColourId, surface);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, line);
}

juce::Font AmbienceLookAndFeel::font (float height, bool bold)
{
    return juce::Font (juce::FontOptions (bold ? semiboldFace() : regularFace()).withHeight (height));
}

juce::Typeface::Ptr AmbienceLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    return f.isBold() ? semibold : regular;
}

//==============================================================================
void AmbienceLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = start + pos * (end - start);
    const float thickness = std::max (2.5f, radius * 0.11f);
    const bool enabled = s.isEnabled();
    const auto accentCol = enabled ? colours::accent : colours::textFaint;

    // Face
    g.setColour (colours::surfaceRaised);
    g.fillEllipse (c.x - radius * 0.72f, c.y - radius * 0.72f, radius * 1.44f, radius * 1.44f);
    g.setColour (colours::line);
    g.drawEllipse (c.x - radius * 0.72f, c.y - radius * 0.72f, radius * 1.44f, radius * 1.44f, 1.0f);

    // Track
    juce::Path track;
    track.addCentredArc (c.x, c.y, radius - thickness * 0.5f, radius - thickness * 0.5f, 0, start, end, true);
    g.setColour (colours::line);
    g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Value arc (bipolar parameters grow from the centre)
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? (start + end) * 0.5f : start;
    juce::Path value;
    value.addCentredArc (c.x, c.y, radius - thickness * 0.5f, radius - thickness * 0.5f, 0, std::min (from, angle), std::max (from, angle), true);
    g.setColour (accentCol);
    g.strokePath (value, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Pointer
    const float inner = radius * 0.25f, outer = radius * 0.62f;
    const juce::Point<float> p1 (c.x + inner * std::sin (angle), c.y - inner * std::cos (angle));
    const juce::Point<float> p2 (c.x + outer * std::sin (angle), c.y - outer * std::cos (angle));
    g.setColour (enabled ? colours::text : colours::textFaint);
    g.drawLine ({ p1, p2 }, 2.0f);
}

juce::Label* AmbienceLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (font (12.5f));
    l->setJustificationType (juce::Justification::centred);
    l->setColour (juce::Label::textColourId, colours::text);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    return l;
}

void AmbienceLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    // Flat labels: no outline unless being edited.
    if (label.isBeingEdited())
    {
        g.setColour (colours::surfaceRaised);
        g.fillRoundedRectangle (label.getLocalBounds().toFloat(), 4.0f);
        g.setColour (colours::accent);
        g.drawRoundedRectangle (label.getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f);
        return;
    }
    const auto bg = label.findColour (juce::Label::backgroundColourId);
    if (! bg.isTransparent())
    {
        g.setColour (bg);
        g.fillRoundedRectangle (label.getLocalBounds().toFloat(), 4.0f);
    }
    g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (label.isEnabled() ? 1.0f : 0.45f));
    g.setFont (getLabelFont (label).withHeight (label.getFont().getHeight()));
    g.drawFittedText (label.getText(), label.getLocalBounds().reduced (2, 0), label.getJustificationType(), 1, 1.0f);
}

juce::Slider::SliderLayout AmbienceLookAndFeel::getSliderLayout (juce::Slider& s)
{
    juce::Slider::SliderLayout layout;
    auto b = s.getLocalBounds();
    if (s.getTextBoxPosition() == juce::Slider::TextBoxBelow)
    {
        layout.textBoxBounds = b.removeFromBottom (18);
        layout.sliderBounds = b;
    }
    else
        layout.sliderBounds = b;
    return layout;
}

//==============================================================================
void AmbienceLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    const float corner = 5.0f;
    const bool left = b.isConnectedOnLeft(), right = b.isConnectedOnRight();

    juce::Path p;
    p.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), corner, corner, ! left, ! right, ! left, ! right);

    auto fill = on ? colours::accent : colours::surfaceRaised;
    if (! on && (highlighted || down))
        fill = fill.brighter (0.08f);
    if (! b.isEnabled())
        fill = fill.withMultipliedAlpha (0.4f);
    g.setColour (fill);
    g.fillPath (p);
    g.setColour (on ? colours::accent : colours::line);
    g.strokePath (p, juce::PathStrokeType (1.0f));
}

void AmbienceLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const bool on = b.getToggleState();
    auto col = on ? colours::background : colours::textDim;
    if (b.isMouseOver() && ! on)
        col = colours::text;
    if (! b.isEnabled())
        col = col.withMultipliedAlpha (0.45f);
    g.setColour (col);
    g.setFont (font (12.5f, true));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 0), juce::Justification::centred, 1);
}

void AmbienceLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    // Pill switch + label.
    auto r = b.getLocalBounds().toFloat();
    const auto sw = r.removeFromLeft (30.0f).withSizeKeepingCentre (28.0f, 16.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? colours::accent : colours::surfaceRaised);
    g.fillRoundedRectangle (sw, 8.0f);
    g.setColour (on ? colours::accent : (highlighted ? colours::lineStrong : colours::line));
    g.drawRoundedRectangle (sw, 8.0f, 1.0f);
    const float knob = 12.0f;
    const float kx = on ? sw.getRight() - knob - 2.0f : sw.getX() + 2.0f;
    g.setColour (on ? colours::background : colours::textDim);
    g.fillEllipse (kx, sw.getCentreY() - knob * 0.5f, knob, knob);
    g.setColour (b.isEnabled() ? (on ? colours::text : colours::textDim) : colours::textFaint);
    g.setFont (font (12.5f, true));
    g.drawFittedText (b.getButtonText(), r.toNearestInt().withTrimmedLeft (6), juce::Justification::centredLeft, 1);
}

void AmbienceLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (colours::surfaceRaised);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (box.hasKeyboardFocus (true) || box.isMouseOver (true) ? colours::lineStrong : colours::line);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
    arrow.startNewSubPath (ax - 4.0f, ay - 2.0f);
    arrow.lineTo (ax, ay + 2.0f);
    arrow.lineTo (ax + 4.0f, ay - 2.0f);
    g.setColour (box.isEnabled() ? colours::textDim : colours::textFaint);
    g.strokePath (arrow, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void AmbienceLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 0, box.getWidth() - 28, box.getHeight());
    label.setFont (font (13.0f));
}

void AmbienceLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (colours::surfaceRaised);
    g.setColour (colours::line);
    g.drawRect (0, 0, w, h);
}

void AmbienceLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
    g.setColour (colours::surfaceRaised);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (colours::lineStrong);
    g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
    g.setColour (colours::text);
    g.setFont (font (13.0f));
    g.drawFittedText (text, r.toNearestInt().reduced (10, 6), juce::Justification::centredLeft, 6);
}

juce::Rectangle<int> AmbienceLookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    juce::AttributedString s;
    s.append (text, font (13.0f));
    juce::TextLayout tl;
    tl.createLayout (s, 320.0f);
    const int w = (int) tl.getWidth() + 22, h = (int) tl.getHeight() + 14;
    return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 18,
                                 pos.y > parent.getCentreY() ? pos.y - (h + 8) : pos.y + 16, w, h)
        .constrainedWithin (parent);
}

} // namespace ui
