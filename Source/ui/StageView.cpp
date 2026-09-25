#include "StageView.h"

#include "../dsp/Params.h"
#include "../dsp/RoomModel.h"
#include "LookAndFeel.h"

namespace ui
{
namespace
{
    constexpr float minDist = 0.1f, maxDist = 1000.0f;
    constexpr float innerRadius = 16.0f;
} // namespace

StageView::StageView (juce::AudioProcessorValueTreeState& state)
    : apvts (state),
      panAttachment (*state.getParameter (amb::pid::pan), [this] (float v) { pan = v; repaint(); }, state.undoManager),
      distanceAttachment (*state.getParameter (amb::pid::distance), [this] (float v) { distance = v; repaint(); }, state.undoManager)
{
    panAttachment.sendInitialUpdate();
    distanceAttachment.sendInitialUpdate();
    setRepaintsOnMouseActivity (false);
    startTimerHz (30);
}

StageView::~StageView() { stopTimer(); }

juce::Colour StageView::materialColour (int m)
{
    static const juce::uint32 c[] = { 0xff9c7bd4, 0xff7fc8e8, 0xffc8a27a, 0xffa67c52, 0xffc9c9c0, 0xffc0634a, 0xff8e949c, 0xff5e86c4 };
    return juce::Colour (c[juce::jlimit (0, 7, m)]);
}

float StageView::value (const char* id) const
{
    if (auto* v = apvts.getRawParameterValue (id))
        return v->load();
    return 0.0f;
}

void StageView::timerCallback()
{
    // Repaint only when something visible changed.
    juce::String snap;
    for (auto* id : { amb::pid::width, amb::pid::obstacle, amb::pid::material, amb::pid::amount, amb::pid::thickness,
                      amb::pid::obstacleSize, amb::pid::room, amb::pid::renderMode, amb::pid::inputMode, amb::pid::leak })
        snap << value (id) << ";";
    snap << (int) monoOut;
    if (snap != lastSnapshot)
    {
        lastSnapshot = snap;
        repaint();
    }
}

juce::Point<float> StageView::listenerPos() const
{
    return { getWidth() * 0.5f, getHeight() - 34.0f };
}

float StageView::maxRadius() const
{
    return getWidth() * 0.5f - 22.0f;
}

// The half-disc is drawn as a half-ellipse so the view uses the full panel height.
float StageView::verticalScale() const
{
    return juce::jlimit (0.5f, 2.0f, (getHeight() - 64.0f) / maxRadius());
}

float StageView::distanceToRadius (float m) const
{
    const float u = std::log (juce::jlimit (minDist, maxDist, m) / minDist) / std::log (maxDist / minDist);
    return innerRadius + u * (maxRadius() - innerRadius);
}

float StageView::radiusToDistance (float px) const
{
    const float u = juce::jlimit (0.0f, 1.0f, (px - innerRadius) / (maxRadius() - innerRadius));
    return minDist * std::pow (maxDist / minDist, u);
}

juce::Point<float> StageView::sourcePos() const
{
    const float th = pan / 100.0f * juce::MathConstants<float>::halfPi;
    const float r = distanceToRadius (distance);
    const auto l = listenerPos();
    return { l.x + r * std::sin (th), l.y - r * std::cos (th) * verticalScale() };
}

//==============================================================================
void StageView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto l = listenerPos();
    const float R = maxRadius();
    const float vs = verticalScale();

    g.setColour (colours::surface);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (colours::line);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    // Soft vignette around the listener.
    juce::ColourGradient glow (colours::accent.withAlpha (0.07f), l.x, l.y, colours::accent.withAlpha (0.0f), l.x, l.y - R * vs, true);
    g.setGradientFill (glow);
    g.fillEllipse (l.x - R, l.y - R * vs, 2 * R, 2 * R * vs);

    const bool headphones = value (amb::pid::renderMode) > 0.5f;

    // Room extent (dashed) — radius of the room's half-diagonal footprint.
    const int roomType = (int) value (amb::pid::room);
    if (roomType >= 2)
    {
        const auto& spec = amb::roomSpec (roomType);
        const float ext = (float) (0.5 * std::sqrt (spec.width * spec.width + spec.length * spec.length));
        const float rr = distanceToRadius (ext);
        juce::Path arc;
        arc.addCentredArc (l.x, l.y, rr, rr * vs, 0, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
        juce::Path dashed;
        const float dashes[] = { 4.0f, 5.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, arc, dashes, 2);
        g.setColour (colours::cool.withAlpha (0.45f));
        g.fillPath (dashed);
        g.setFont (AmbienceLookAndFeel::font (11.5f, true));
        g.drawText (juce::String (spec.name).toUpperCase(), juce::Rectangle<float> (l.x - 80, l.y - rr * vs - 18, 160, 14), juce::Justification::centred);
    }
    else if (roomType == 1)
    {
        g.setColour (colours::cool.withAlpha (0.5f));
        g.setFont (AmbienceLookAndFeel::font (11.5f, true));
        g.drawText ("OUTDOOR", bounds.reduced (14, 10).removeFromTop (14), juce::Justification::topRight);
    }

    // Distance rings with labels.
    g.setFont (AmbienceLookAndFeel::font (10.5f));
    for (float m : { 1.0f, 3.0f, 10.0f, 30.0f, 100.0f, 300.0f, 1000.0f })
    {
        const float r = distanceToRadius (m);
        juce::Path arc;
        arc.addCentredArc (l.x, l.y, r, r * vs, 0, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
        g.setColour (m == 1000.0f ? colours::lineStrong : colours::line);
        g.strokePath (arc, juce::PathStrokeType (1.0f));
        g.setColour (colours::textFaint);
        const juce::String label = m >= 1000.0f ? "1 km" : juce::String ((int) m) + " m";
        g.drawText (label, juce::Rectangle<float> (l.x + 4, l.y - r * vs - 13, 44, 12), juce::Justification::left);
    }
    // Direction spokes every 30 degrees.
    for (int deg = -90; deg <= 90; deg += 30)
    {
        const float th = juce::degreesToRadians ((float) deg);
        g.setColour (deg == 0 ? colours::lineStrong : colours::line.withAlpha (0.6f));
        g.drawLine (l.x + innerRadius * std::sin (th), l.y - innerRadius * std::cos (th) * vs, l.x + R * std::sin (th), l.y - R * std::cos (th) * vs, 1.0f);
    }
    // Baseline.
    g.setColour (colours::line);
    g.drawLine (l.x - R, l.y, l.x + R, l.y, 1.0f);

    // Speakers (Speakers mode) at the ends of the baseline.
    g.setFont (AmbienceLookAndFeel::font (11.0f, true));
    if (! headphones)
    {
        for (int side : { -1, 1 })
        {
            const float x = l.x + side * (R + 2.0f);
            juce::Rectangle<float> box (x - 6, l.y - 16, 12, 12);
            g.setColour (colours::textDim);
            g.drawRoundedRectangle (box, 2.0f, 1.2f);
            g.drawText (side < 0 ? "L" : "R", juce::Rectangle<float> (x - 8, l.y + 2, 16, 12), juce::Justification::centred);
        }
    }

    // Listener: head (with ears in Headphones mode) or microphone capsule.
    g.setColour (colours::surfaceRaised);
    g.fillEllipse (l.x - 11, l.y - 11, 22, 22);
    g.setColour (colours::text);
    g.drawEllipse (l.x - 11, l.y - 11, 22, 22, 1.5f);
    juce::Path nose;
    nose.addTriangle (l.x - 4, l.y - 10, l.x + 4, l.y - 10, l.x, l.y - 16);
    g.fillPath (nose);
    if (headphones)
    {
        g.setColour (colours::cool);
        g.fillRoundedRectangle (l.x - 15, l.y - 5, 5, 10, 2.0f);
        g.fillRoundedRectangle (l.x + 10, l.y - 5, 5, 10, 2.0f);
    }

    const auto s = sourcePos();
    const float th = pan / 100.0f * juce::MathConstants<float>::halfPi;
    const float rs = distanceToRadius (distance);

    // Obstacle between listener and source.
    const int obstacle = (int) value (amb::pid::obstacle);
    const float amount = value (amb::pid::amount) / 100.0f;
    if (obstacle > 0 && amount > 0.0f)
    {
        const float rm = innerRadius + (rs - innerRadius) * 0.55f;
        const juce::Point<float> m (l.x + rm * std::sin (th), l.y - rm * std::cos (th) * vs);
        // Wall drawn perpendicular to the on-screen line of sight.
        auto dir = juce::Point<float> (std::sin (th), -std::cos (th) * vs);
        dir /= std::max (1.0e-3f, dir.getDistanceFromOrigin());
        const juce::Point<float> tangent (-dir.y, dir.x);
        if (obstacle == 2)
        {
            const int mat = (int) value (amb::pid::material);
            const float thick = 2.5f + 3.0f * std::log2 (1.0f + value (amb::pid::thickness)) * 1.6f;
            const float half = 34.0f;
            const auto colour = materialColour (mat).withAlpha (0.35f + 0.65f * amount);
            g.setColour (colour);
            g.drawLine ({ m - tangent * half, m + tangent * half }, thick);
            // Leak: a gap in the wall.
            const float leak = value (amb::pid::leak) / 100.0f;
            if (leak > 0.01f)
            {
                g.setColour (colours::surface);
                g.drawLine ({ m + tangent * (half * 0.35f), m + tangent * (half * (0.35f + 0.5f * leak)) }, thick + 2.0f);
            }
            g.setColour (colour.withAlpha (0.9f));
            g.setFont (AmbienceLookAndFeel::font (11.0f, true));
            juce::String name (amb::models::material (mat).name);
            g.drawText (name, juce::Rectangle<float> (m.x - 70, m.y + 8 + thick * 0.5f, 140, 14), juce::Justification::centred);
        }
        else
        {
            const float size = 8.0f + 9.0f * value (amb::pid::obstacleSize);
            const auto centre = m + tangent * (size * 0.35f);
            g.setColour (colours::textDim.withAlpha (0.25f + 0.55f * amount));
            g.fillRoundedRectangle (juce::Rectangle<float> (size, size).withCentre (centre), 3.0f);
            g.setFont (AmbienceLookAndFeel::font (11.0f, true));
            g.drawText ("OBSTRUCTION", juce::Rectangle<float> (centre.x - 60, centre.y + size * 0.5f + 3, 120, 14), juce::Justification::centred);
        }
    }

    // Source width as an arc around the listener at the source radius.
    if (! monoOut)
    {
        const float w = value (amb::pid::width) / 100.0f;
        const int inputMode = (int) value (amb::pid::inputMode);
        const bool stereoSource = inputMode == 2 || (inputMode == 0 && apvts.processor.getTotalNumInputChannels() > 1);
        const float spreadDeg = stereoSource ? 30.0f * w : 30.0f * std::max (0.0f, w - 1.0f);
        if (spreadDeg > 0.5f)
        {
            juce::Path arc;
            const float sp = juce::degreesToRadians (spreadDeg);
            arc.addCentredArc (l.x, l.y, rs, rs * vs, 0, th - sp, th + sp, true);
            g.setColour (colours::accent.withAlpha (0.28f));
            g.strokePath (arc, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // Line of sight.
    g.setColour (colours::accent.withAlpha (0.35f));
    const float dashes[] = { 3.0f, 4.0f };
    g.drawDashedLine ({ l, s }, dashes, 2, 1.0f);

    // Source dot.
    const float rad = dragging ? 10.0f : (hover ? 9.0f : 8.0f);
    g.setColour (colours::accent.withAlpha (0.18f));
    g.fillEllipse (s.x - rad * 2.2f, s.y - rad * 2.2f, rad * 4.4f, rad * 4.4f);
    g.setColour (colours::accent);
    g.fillEllipse (s.x - rad, s.y - rad, rad * 2, rad * 2);
    g.setColour (colours::background);
    g.fillEllipse (s.x - 2.5f, s.y - 2.5f, 5.0f, 5.0f);

    // Readout.
    auto* distParam = apvts.getParameter (amb::pid::distance);
    auto* panParam = apvts.getParameter (amb::pid::pan);
    const juce::String readout = distParam->getCurrentValueAsText() + (monoOut ? "" : "   " + panParam->getCurrentValueAsText());
    g.setFont (AmbienceLookAndFeel::font (12.0f, true));
    g.setColour (colours::text);
    auto tr = juce::Rectangle<float> (s.x - 70, s.y - rad - 22, 140, 16);
    if (tr.getY() < 4)
        tr.setY (s.y + rad + 6);
    g.drawText (readout, tr, juce::Justification::centred);

    // Hint.
    g.setColour (colours::textFaint);
    g.setFont (AmbienceLookAndFeel::font (11.0f));
    g.drawText (juce::String::fromUTF8 ("Drag to position  \xc2\xb7  wheel: distance  \xc2\xb7  double-click: reset"), bounds.reduced (12, 8).removeFromBottom (12).toNearestInt(),
                juce::Justification::centredLeft);
}

//==============================================================================
void StageView::setFromPoint (juce::Point<float> p)
{
    const auto l = listenerPos();
    auto d = p - l;
    d.y = std::min (d.y, 0.0f) / verticalScale(); // front half-plane, undo the vertical stretch
    const float angle = std::atan2 (d.x, -d.y);
    const float r = d.getDistanceFromOrigin();
    if (! monoOut)
        panAttachment.setValueAsPartOfGesture (juce::jlimit (-100.0f, 100.0f, angle / juce::MathConstants<float>::halfPi * 100.0f));
    distanceAttachment.setValueAsPartOfGesture (radiusToDistance (r));
}

void StageView::mouseDown (const juce::MouseEvent& e)
{
    dragging = true;
    if (! monoOut)
        panAttachment.beginGesture();
    distanceAttachment.beginGesture();
    setFromPoint (e.position);
    repaint();
}

void StageView::mouseDrag (const juce::MouseEvent& e) { setFromPoint (e.position); }

void StageView::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    if (! monoOut)
        panAttachment.endGesture();
    distanceAttachment.endGesture();
    repaint();
}

void StageView::mouseDoubleClick (const juce::MouseEvent&)
{
    panAttachment.setValueAsCompleteGesture (0.0f);
    distanceAttachment.setValueAsCompleteGesture (value (amb::pid::recDistance));
}

void StageView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const float factor = std::pow (2.0f, -w.deltaY * (w.isReversed ? -1.0f : 1.0f) * 1.5f);
    distanceAttachment.setValueAsCompleteGesture (juce::jlimit (minDist, maxDist, distance * factor));
}

void StageView::mouseMove (const juce::MouseEvent& e)
{
    const bool h = e.position.getDistanceFrom (sourcePos()) < 16.0f;
    if (h != hover)
    {
        hover = h;
        repaint();
    }
    setMouseCursor (hover ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor);
}

void StageView::mouseExit (const juce::MouseEvent&)
{
    hover = false;
    repaint();
}

} // namespace ui
