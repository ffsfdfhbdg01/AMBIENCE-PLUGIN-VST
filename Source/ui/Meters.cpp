#include "Meters.h"

#include "../PluginProcessor.h"
#include "LookAndFeel.h"

namespace ui
{
Meters::Meters (AmbienceProcessor& p) : proc (p) { startTimerHz (30); }

void Meters::timerCallback()
{
    for (int c = 0; c < 2; ++c)
    {
        const float pk = proc.meterPeak[c].load();
        peak[c] = std::max (pk, peak[c] * 0.86f);
        rms[c] = rms[c] * 0.7f + proc.meterRms[c].load() * 0.3f;
        if (pk >= peakHold[c])
        {
            peakHold[c] = pk;
            holdCounter[c] = 45;
        }
        else if (--holdCounter[c] <= 0)
            peakHold[c] *= 0.92f;
    }
    corr = corr * 0.8f + proc.correlation.load() * 0.2f;
    ag = proc.autoGainDb.load();
    repaint();
}

void Meters::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const bool mono = proc.isMonoOutput();
    constexpr float minDb = -60.0f, maxDb = 6.0f;
    auto toX = [&] (float lin, juce::Rectangle<float> r)
    {
        const float db = juce::jlimit (minDb, maxDb, juce::Decibels::gainToDecibels (lin, minDb));
        return r.getX() + (db - minDb) / (maxDb - minDb) * r.getWidth();
    };

    // Level bars (horizontal): RMS fill + peak-hold tick.
    g.setFont (AmbienceLookAndFeel::font (10.0f, true));
    const int nBars = mono ? 1 : 2;
    auto barsArea = b.removeFromTop (mono ? 16.0f : 30.0f);
    for (int c = 0; c < nBars; ++c)
    {
        auto row = barsArea.removeFromTop (12.0f);
        barsArea.removeFromTop (4.0f);
        g.setColour (colours::textFaint);
        g.drawText (mono ? "M" : (c == 0 ? "L" : "R"), row.removeFromLeft (12.0f), juce::Justification::centredLeft);
        g.setColour (colours::surfaceRaised);
        g.fillRoundedRectangle (row, 3.0f);
        const float x = toX (rms[c], row);
        juce::ColourGradient grad (colours::ok, row.getX(), 0, colours::warn, row.getRight(), 0, false);
        grad.addColour ((-12.0 - minDb) / (maxDb - minDb), colours::ok);
        grad.addColour ((-3.0 - minDb) / (maxDb - minDb), colours::accent);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (row.withRight (std::max (row.getX(), x)), 3.0f);
        g.setColour (colours::text.withAlpha (0.85f));
        g.fillRect (toX (peakHold[c], row) - 1.0f, row.getY(), 2.0f, row.getHeight());
    }
    // dB scale
    auto scale = b.removeFromTop (12.0f).withTrimmedLeft (12.0f);
    g.setFont (AmbienceLookAndFeel::font (9.5f));
    g.setColour (colours::textFaint);
    for (int db : { -48, -36, -24, -12, -6, 0 })
    {
        const float x = toX (juce::Decibels::decibelsToGain ((float) db), scale);
        g.drawText (juce::String (db), juce::Rectangle<float> (x - 12, scale.getY(), 24, 12), juce::Justification::centred);
    }
    b.removeFromTop (8.0f);

    // Correlation meter.
    auto cr = b.removeFromTop (10.0f).withTrimmedLeft (12.0f);
    g.setColour (colours::surfaceRaised);
    g.fillRoundedRectangle (cr, 3.0f);
    const float cx = cr.getCentreX() + corr * cr.getWidth() * 0.5f;
    g.setColour (corr < 0.0f ? colours::warn : colours::ok);
    g.fillRoundedRectangle (juce::Rectangle<float> (std::min (cr.getCentreX(), cx), cr.getY(), std::abs (cx - cr.getCentreX()), cr.getHeight()), 2.0f);
    g.setColour (colours::lineStrong);
    g.drawLine (cr.getCentreX(), cr.getY() - 2, cr.getCentreX(), cr.getBottom() + 2, 1.0f);
    auto labels = b.removeFromTop (14.0f).withTrimmedLeft (12.0f);
    g.setColour (colours::textDim);
    g.setFont (AmbienceLookAndFeel::font (9.5f, true));
    g.drawText ("-1", labels, juce::Justification::left);
    g.drawText ("CORRELATION", labels, juce::Justification::centred);
    g.drawText ("+1", labels, juce::Justification::right);

    // Auto gain readout.
    const bool agOn = proc.apvts.getRawParameterValue ("autoGain")->load() > 0.5f;
    g.setFont (AmbienceLookAndFeel::font (11.0f, true));
    g.setColour (agOn ? colours::accent : colours::textFaint);
    const juce::String agText = agOn ? juce::String ("AUTO GAIN ") + (ag >= 0 ? "+" : "") + juce::String (ag, 1) + " dB" : juce::String ("AUTO GAIN OFF");
    g.drawText (agText, b.withTrimmedLeft (12.0f), juce::Justification::centredLeft);
}
} // namespace ui
