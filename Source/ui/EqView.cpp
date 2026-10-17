#include "EqView.h"

#include "LookAndFeel.h"

namespace ui
{
namespace
{
    constexpr double minFreq = 20.0, maxFreq = 20000.0, rangeDb = 20.0;
    constexpr double displayRate = 48000.0;
} // namespace

//==============================================================================
EqGraph::EqGraph (juce::AudioProcessorValueTreeState& state) : apvts (state)
{
    setRepaintsOnMouseActivity (false);
    startTimerHz (30);
}

EqGraph::~EqGraph() { stopTimer(); }

void EqGraph::setEq (const amb::pid::EqIds& eqIds, const juce::String& name)
{
    ids = &eqIds;
    eqName = name;
    hover = dragging = none;
    lastSnapshot.clear();
    repaint();
}

void EqGraph::setNote (const juce::String& text)
{
    if (note != text)
    {
        note = text;
        repaint();
    }
}

void EqGraph::resized() { plot = getLocalBounds().toFloat().reduced (8.0f, 6.0f).withTrimmedBottom (12.0f).withTrimmedLeft (18.0f); }

float EqGraph::value (const char* id) const
{
    if (auto* v = apvts.getRawParameterValue (id))
        return v->load();
    return 0.0f;
}

amb::EqParams EqGraph::read() const
{
    amb::EqParams p;
    if (ids == nullptr)
        return p;
    p.on = value (ids->on) > 0.5f;
    p.lowCut = value (ids->lowCut);
    p.lowShelfFreq = value (ids->lowShelfFreq);
    p.lowShelfGain = value (ids->lowShelfGain);
    p.peakFreq = value (ids->peakFreq);
    p.peakGain = value (ids->peakGain);
    p.peakQ = value (ids->peakQ);
    p.highShelfFreq = value (ids->highShelfFreq);
    p.highShelfGain = value (ids->highShelfGain);
    p.highCut = value (ids->highCut);
    return p;
}

void EqGraph::setValue (const char* id, float plain)
{
    if (auto* p = apvts.getParameter (id))
    {
        const auto& range = p->getNormalisableRange();
        p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (range.start, range.end, plain)));
    }
}

juce::StringArray EqGraph::bandIds (int band) const
{
    if (ids == nullptr)
        return {};
    switch (band)
    {
        case lowCut: return { ids->lowCut };
        case lowShelf: return { ids->lowShelfFreq, ids->lowShelfGain };
        case peak: return { ids->peakFreq, ids->peakGain, ids->peakQ };
        case highShelf: return { ids->highShelfFreq, ids->highShelfGain };
        case highCut: return { ids->highCut };
        default: return {};
    }
}

void EqGraph::gesture (int band, bool begin)
{
    for (auto& id : bandIds (band))
        if (auto* p = apvts.getParameter (id))
        {
            if (begin)
                p->beginChangeGesture();
            else
                p->endChangeGesture();
        }
}

float EqGraph::freqToX (double f) const
{
    const double t = std::log (std::clamp (f, minFreq, maxFreq) / minFreq) / std::log (maxFreq / minFreq);
    return plot.getX() + (float) t * plot.getWidth();
}

double EqGraph::xToFreq (float x) const
{
    const double t = std::clamp ((double) (x - plot.getX()) / std::max (1.0f, plot.getWidth()), 0.0, 1.0);
    return minFreq * std::pow (maxFreq / minFreq, t);
}

float EqGraph::dbToY (double db) const { return plot.getCentreY() - (float) (db / rangeDb) * plot.getHeight() * 0.5f; }

double EqGraph::yToDb (float y) const { return (plot.getCentreY() - y) / (plot.getHeight() * 0.5f) * rangeDb; }

juce::Point<float> EqGraph::nodePos (int band, const amb::EqParams& p) const
{
    // Cut points sit on the curve (about 3 dB down at the cut-off), so they do not cover a shelf at
    // the same frequency; shelf and peak points show their gain.
    auto onCurve = [&] (double f) { return dbToY (std::clamp (amb::ParametricEq<1>::responseDb (p, displayRate, f), -rangeDb, rangeDb)); };
    switch (band)
    {
        case lowCut: return { freqToX (p.lowCut), onCurve (p.lowCut) };
        case lowShelf: return { freqToX (p.lowShelfFreq), dbToY (p.lowShelfGain) };
        case peak: return { freqToX (p.peakFreq), dbToY (p.peakGain) };
        case highShelf: return { freqToX (p.highShelfFreq), dbToY (p.highShelfGain) };
        case highCut: return { freqToX (p.highCut), onCurve (p.highCut) };
        default: return {};
    }
}

int EqGraph::nodeAt (juce::Point<float> pos) const
{
    if (ids == nullptr)
        return none;
    const auto p = read();
    int best = none;
    float bestDist = 11.0f;
    for (int b = 0; b < numBands; ++b)
    {
        const float d = nodePos (b, p).getDistanceFrom (pos);
        if (d < bestDist)
        {
            bestDist = d;
            best = b;
        }
    }
    return best;
}

juce::String EqGraph::describe (int band, const amb::EqParams&) const
{
    auto text = [this] (const char* id)
    {
        auto* p = apvts.getParameter (id);
        return p != nullptr ? p->getCurrentValueAsText() : juce::String();
    };
    if (ids == nullptr)
        return {};
    switch (band)
    {
        case lowCut: return "LOW CUT  " + text (ids->lowCut) + (value (ids->lowCut) > 20.5f ? "  12 dB/oct" : "");
        case lowShelf: return "LOW SHELF  " + text (ids->lowShelfFreq) + "  " + text (ids->lowShelfGain);
        case peak: return "PEAK  " + text (ids->peakFreq) + "  " + text (ids->peakGain) + "  " + text (ids->peakQ);
        case highShelf: return "HIGH SHELF  " + text (ids->highShelfFreq) + "  " + text (ids->highShelfGain);
        case highCut: return "HIGH CUT  " + text (ids->highCut) + (value (ids->highCut) < 19500.0f ? "  12 dB/oct" : "");
        default: return {};
    }
}

void EqGraph::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (colours::background);
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (colours::line);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);
    if (ids == nullptr)
        return;

    const auto p = read();

    // Grid
    g.setFont (AmbienceLookAndFeel::font (9.5f));
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        const float x = freqToX (f);
        g.setColour (colours::line.withAlpha (0.7f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (colours::textFaint);
        const juce::String label = f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f);
        g.drawText (label, juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 1.0f, 40.0f, 11.0f), juce::Justification::centred);
    }
    for (int db : { -12, -6, 0, 6, 12 })
    {
        const float y = dbToY (db);
        g.setColour (db == 0 ? colours::lineStrong : colours::line.withAlpha (0.7f));
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (colours::textFaint);
        g.drawText ((db > 0 ? "+" : "") + juce::String (db), juce::Rectangle<float> (bounds.getX() + 2.0f, y - 6.0f, 22.0f, 12.0f),
                    juce::Justification::centredLeft);
    }

    // Response
    juce::Path curve, fill;
    const int n = std::max (2, (int) plot.getWidth());
    const float zeroY = dbToY (0.0);
    for (int i = 0; i <= n; ++i)
    {
        const float x = plot.getX() + plot.getWidth() * (float) i / (float) n;
        const double db = std::clamp (amb::ParametricEq<1>::responseDb (p, displayRate, xToFreq (x)), -rangeDb, rangeDb);
        const float y = dbToY (db);
        if (i == 0)
        {
            curve.startNewSubPath (x, y);
            fill.startNewSubPath (x, zeroY);
        }
        else
            curve.lineTo (x, y);
        fill.lineTo (x, y);
    }
    fill.lineTo (plot.getRight(), zeroY);
    fill.closeSubPath();
    const auto curveColour = p.on ? colours::accent : colours::textFaint;
    g.setColour (p.on ? colours::accentSoft : colours::textFaint.withAlpha (0.12f));
    g.fillPath (fill);
    g.setColour (curveColour);
    g.strokePath (curve, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Points, then their labels on top. A label that would cover another point or label is left
    // out, except the one of the point under the mouse.
    static const char* labels[numBands] = { "LC", "LS", "P", "HS", "HC" };
    const bool bandOn[numBands] = { p.lowCut > 20.5f, std::abs (p.lowShelfGain) > 0.05f, std::abs (p.peakGain) > 0.05f,
                                    std::abs (p.highShelfGain) > 0.05f, p.highCut < 19500.0f };
    const int hot = dragging != none ? dragging : hover;
    juce::Rectangle<float> dots[numBands];
    for (int b = 0; b < numBands; ++b)
    {
        const auto c = nodePos (b, p);
        const float r = b == hot ? 6.5f : 5.0f;
        dots[b] = { c.x - r, c.y - r, 2.0f * r, 2.0f * r };
        const auto colour = (b == lowCut || b == highCut) ? colours::cool : colours::accent;
        if (bandOn[b] && p.on)
        {
            g.setColour (colour);
            g.fillEllipse (dots[b]);
        }
        else
        {
            g.setColour (colours::surfaceRaised);
            g.fillEllipse (dots[b]);
            g.setColour (colour.withAlpha (p.on ? 1.0f : 0.5f));
            g.drawEllipse (dots[b], 1.4f);
        }
    }
    juce::Array<juce::Rectangle<float>> taken;
    g.setFont (AmbienceLookAndFeel::font (9.0f, true));
    for (int i = -1; i < numBands; ++i)
    {
        const int b = i < 0 ? hot : i;
        if (b == none || (i >= 0 && b == hot))
            continue;
        const float lx = std::clamp (dots[b].getCentreX() - 12.0f, plot.getX() - 6.0f, plot.getRight() - 18.0f);
        const juce::Rectangle<float> lr (lx, dots[b].getY() - 13.0f, 24.0f, 11.0f);
        bool covers = std::any_of (taken.begin(), taken.end(), [&] (const auto& o) { return o.intersects (lr); });
        for (int o = 0; o < numBands; ++o)
            covers = covers || (o != b && dots[o].intersects (lr));
        if (covers && b != hot)
            continue;
        taken.add (lr);
        g.setColour (b == hot ? colours::text : colours::textDim);
        g.drawText (labels[b], lr, juce::Justification::centred);
    }

    // Read-out: the point under the mouse, else the note (e.g. no room) or the hints.
    g.setFont (AmbienceLookAndFeel::font (10.5f, true));
    const auto readout = plot.withHeight (14.0f).translated (0.0f, 2.0f).reduced (6.0f, 0.0f);
    if (hot != none)
    {
        g.setColour (colours::text);
        g.drawText (describe (hot, p), readout, juce::Justification::centredRight);
    }
    else if (note.isNotEmpty())
    {
        g.setColour (colours::warn);
        g.drawText (eqName + juce::String::fromUTF8 ("  \xc2\xb7  ") + note, readout, juce::Justification::centredRight);
    }
    else
    {
        g.setColour (colours::textFaint);
        g.drawText (eqName + (p.on ? "" : "  (OFF)") + juce::String::fromUTF8 ("  \xc2\xb7  drag points \xc2\xb7 wheel: Q \xc2\xb7 double-click: reset"),
                    readout, juce::Justification::centredRight);
    }
}

void EqGraph::timerCallback()
{
    if (ids == nullptr)
        return;
    const auto p = read();
    const juce::String snapshot = juce::String ((int) p.on) + "|" + juce::String (p.lowCut) + "|" + juce::String (p.lowShelfFreq) + "|"
                                  + juce::String (p.lowShelfGain) + "|" + juce::String (p.peakFreq) + "|" + juce::String (p.peakGain) + "|"
                                  + juce::String (p.peakQ) + "|" + juce::String (p.highShelfFreq) + "|" + juce::String (p.highShelfGain) + "|"
                                  + juce::String (p.highCut);
    if (snapshot != lastSnapshot)
    {
        lastSnapshot = snapshot;
        repaint();
    }
}

void EqGraph::mouseMove (const juce::MouseEvent& e)
{
    const int h = nodeAt (e.position);
    if (h != hover)
    {
        hover = h;
        setMouseCursor (h != none ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void EqGraph::mouseExit (const juce::MouseEvent&)
{
    if (hover != none && dragging == none)
    {
        hover = none;
        repaint();
    }
}

void EqGraph::mouseDown (const juce::MouseEvent& e)
{
    dragging = nodeAt (e.position);
    if (dragging == none || ids == nullptr)
        return;
    if (auto* um = apvts.undoManager)
        um->beginNewTransaction ("EQ");
    gesture (dragging, true);
    dragStart = e.position;
    const auto p = read();
    switch (dragging)
    {
        case lowCut: startFreq = p.lowCut; break;
        case lowShelf: startFreq = p.lowShelfFreq; startGain = p.lowShelfGain; break;
        case peak: startFreq = p.peakFreq; startGain = p.peakGain; break;
        case highShelf: startFreq = p.highShelfFreq; startGain = p.highShelfGain; break;
        case highCut: startFreq = p.highCut; break;
        default: break;
    }
}

void EqGraph::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == none || ids == nullptr)
        return;
    const float scale = (e.mods.isCommandDown() || e.mods.isCtrlDown()) ? 0.2f : 1.0f;
    const auto delta = (e.position - dragStart) * scale;
    const float freq = (float) xToFreq (freqToX (startFreq) + delta.x);
    const float gain = (float) std::clamp (yToDb (dbToY (startGain) + delta.y), -18.0, 18.0);
    const auto idsList = bandIds (dragging);
    setValue (idsList[0].toRawUTF8(), freq);
    if (dragging == lowShelf || dragging == peak || dragging == highShelf)
        setValue (idsList[1].toRawUTF8(), gain);
    repaint();
}

void EqGraph::mouseUp (const juce::MouseEvent&)
{
    if (dragging != none)
        gesture (dragging, false);
    dragging = none;
    repaint();
}

void EqGraph::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int b = nodeAt (e.position);
    if (b == none)
        return;
    if (auto* um = apvts.undoManager)
        um->beginNewTransaction ("Reset EQ band");
    gesture (b, true);
    for (auto& id : bandIds (b))
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->getDefaultValue());
    gesture (b, false);
    repaint();
}

void EqGraph::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int b = nodeAt (e.position);
    if (b == none || ids == nullptr)
    {
        Component::mouseWheelMove (e, wheel);
        return;
    }
    const float amount = wheel.deltaY * ((e.mods.isCommandDown() || e.mods.isCtrlDown()) ? 0.25f : 1.0f);
    const auto idsList = bandIds (b);
    gesture (b, true);
    if (b == peak)
        setValue (ids->peakQ, value (ids->peakQ) * std::exp (2.0f * amount));
    else if (b == lowShelf || b == highShelf)
        setValue (idsList[1].toRawUTF8(), value (idsList[1].toRawUTF8()) + 10.0f * amount);
    else
        setValue (idsList[0].toRawUTF8(), value (idsList[0].toRawUTF8()) * std::exp (amount));
    gesture (b, false);
    repaint();
}

//==============================================================================
EqSection::EqSection (juce::AudioProcessorValueTreeState& state) : apvts (state), graph (state)
{
    static const char* names[3] = { "INPUT", "OUTPUT", "REVERB ONLY" };
    static const char* tips[3] = {
        "Input EQ: the signal entering the plug-in, before distance, room and panning (the room hears it too).",
        "Output EQ: the final output, every channel (in surround all but LFE).",
        "Reverb only: EQ of the room / reverberation alone - the direct sound is not changed."
    };
    for (int i = 0; i < 3; ++i)
    {
        auto& t = tabs[i];
        t.setButtonText (names[i]);
        t.setTooltip (tips[i]);
        t.setClickingTogglesState (false);
        t.setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < 2 ? juce::Button::ConnectedOnRight : 0));
        t.onClick = [this, i] { selectTab (i); };
        addAndMakeVisible (t);
    }
    onToggle.setTooltip ("Switches the EQ of this tab on or off (A/B). Off glides every band to neutral without clicks.");
    addAndMakeVisible (onToggle);
    addAndMakeVisible (graph);
    selectTab (0);
    startTimerHz (10);
}

bool EqSection::isActive (int index) const
{
    const amb::pid::EqIds* sets[3] = { &amb::pid::eqIn, &amb::pid::eqOut, &amb::pid::eqRev };
    const auto& ids = *sets[index];
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    return v (ids.on) > 0.5f
           && (v (ids.lowCut) > 20.5f || std::abs (v (ids.lowShelfGain)) > 0.05f || std::abs (v (ids.peakGain)) > 0.05f
               || std::abs (v (ids.highShelfGain)) > 0.05f || v (ids.highCut) < 19500.0f);
}

void EqSection::timerCallback()
{
    int mask = 0;
    for (int i = 0; i < 3; ++i)
        mask |= isActive (i) ? 1 << i : 0;
    if (mask != activeMask)
    {
        activeMask = mask;
        repaint();
    }
}

void EqSection::paintOverChildren (juce::Graphics& g)
{
    // A dot on each tab whose EQ is doing something.
    for (int i = 0; i < 3; ++i)
        if ((activeMask >> i) & 1)
        {
            const auto t = tabs[i].getBounds().toFloat();
            g.setColour (i == current ? colours::background : colours::accent);
            g.fillEllipse (t.getRight() - 12.0f, t.getCentreY() - 2.5f, 5.0f, 5.0f);
        }
}

void EqSection::selectTab (int index)
{
    current = std::clamp (index, 0, 2);
    for (int i = 0; i < 3; ++i)
        tabs[i].setToggleState (i == current, juce::dontSendNotification);
    const amb::pid::EqIds* sets[3] = { &amb::pid::eqIn, &amb::pid::eqOut, &amb::pid::eqRev };
    static const char* names[3] = { "INPUT EQ", "OUTPUT EQ", "REVERB EQ" };
    onAttachment.reset();
    onAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, sets[current]->on, onToggle);
    graph.setEq (*sets[current], names[current]);
    repaint();
    graph.setNote (current == 2 && roomOff ? "Room is Off: no reverb to EQ" : juce::String());
}

void EqSection::setRoomOff (bool off)
{
    if (roomOff == off)
        return;
    roomOff = off;
    graph.setNote (current == 2 && roomOff ? "Room is Off: no reverb to EQ" : juce::String());
}

void EqSection::paint (juce::Graphics& g)
{
    g.setColour (colours::textDim);
    g.setFont (AmbienceLookAndFeel::font (10.5f, true));
    g.drawText ("EQ", getLocalBounds().removeFromTop (24).removeFromLeft (30), juce::Justification::centredLeft);
}

void EqSection::resized()
{
    auto b = getLocalBounds();
    auto row = b.removeFromTop (24);
    row.removeFromLeft (30);
    onToggle.setBounds (row.removeFromRight (86));
    row.removeFromRight (8);
    const int w = std::min (100, row.getWidth() / 3);
    for (int i = 0; i < 3; ++i)
        tabs[i].setBounds (row.removeFromLeft (i == 2 ? w + 20 : w).reduced (0, 1));
    b.removeFromTop (6);
    graph.setBounds (b);
}
} // namespace ui
