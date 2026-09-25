#include "PluginProcessor.h"

#include "PluginEditor.h"

namespace
{
juce::String formatValue (const amb::ParamSpec& s, float v)
{
    const juce::String id (s.id);
    if (id == amb::pid::pan)
    {
        if (std::abs (v) < 0.5f)
            return "C";
        return (v < 0 ? "L" : "R") + juce::String (std::abs (v), 0);
    }
    if (id == amb::pid::distance || id == amb::pid::recDistance)
        return juce::String (v, v < 10.0f ? 2 : (v < 100.0f ? 1 : 0)) + " m";
    if (id == amb::pid::obstacleSize)
        return juce::String (v, 2) + " m";
    if (id == amb::pid::thickness)
        return juce::String (v, 2) + "x";
    if (id == amb::pid::temperature)
        return juce::String (v, 1) + juce::String::fromUTF8 (" \xc2\xb0""C");
    juce::String unit (s.unit);
    const int decimals = (unit == "dB" || unit == "dB/dd") ? 1 : 0;
    if (unit == "dB" && v > 0.05f)
        return "+" + juce::String (v, decimals) + " dB";
    return juce::String (v, decimals) + (unit.isEmpty() ? "" : " " + unit);
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout AmbienceProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& s : amb::paramSpecs())
    {
        const juce::ParameterID pid { s.id, 1 };
        switch (s.kind)
        {
            case amb::ParamKind::choice:
            {
                juce::StringArray items;
                for (auto* c : s.choices)
                    items.add (c);
                layout.add (std::make_unique<juce::AudioParameterChoice> (pid, s.name, items, (int) s.defaultValue));
                break;
            }
            case amb::ParamKind::toggle:
                // Two-state choice rather than AudioParameterBool: its normalised value is quantised,
                // so a restored state reads back exactly (pluginval state-restore test).
                layout.add (std::make_unique<juce::AudioParameterChoice> (pid, s.name, juce::StringArray { "Off", "On" }, s.defaultValue > 0.5f ? 1 : 0));
                break;
            case amb::ParamKind::floating:
            {
                juce::NormalisableRange<float> range;
                if (s.scale == amb::ParamScale::logarithmic)
                    range = juce::NormalisableRange<float> (
                        s.minValue, s.maxValue,
                        [] (float a, float b, float t) { return a * std::pow (b / a, t); },
                        [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
                        [] (float a, float b, float v) { return juce::jlimit (a, b, v); });
                else
                    range = juce::NormalisableRange<float> (s.minValue, s.maxValue, s.step);
                const auto spec = s;
                auto attrs = juce::AudioParameterFloatAttributes()
                                 .withLabel (s.unit)
                                 .withStringFromValueFunction ([spec] (float v, int) { return formatValue (spec, v); })
                                 .withValueFromStringFunction ([] (const juce::String& t)
                                                               {
                                                                   auto str = t.trim();
                                                                   if (str.equalsIgnoreCase ("C"))
                                                                       return 0.0f;
                                                                   if (str.startsWithIgnoreCase ("L"))
                                                                       return -str.substring (1).getFloatValue();
                                                                   if (str.startsWithIgnoreCase ("R"))
                                                                       return str.substring (1).getFloatValue();
                                                                   return str.getFloatValue();
                                                               });
                layout.add (std::make_unique<juce::AudioParameterFloat> (pid, s.name, range, s.defaultValue, attrs));
                break;
            }
        }
    }
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "bypass", 1 }, "Bypass", false));
    return layout;
}

AmbienceProcessor::AmbienceProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "AmbienceState", createLayout()),
      presets (apvts)
{
    for (const auto& s : amb::paramSpecs())
        rawParams.emplace_back (s.id, apvts.getRawParameterValue (s.id));
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter ("bypass"));
}

bool AmbienceProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;
    // Supported: 1->1, 1->2, 2->2.
    return ! (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::mono());
}

void AmbienceProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    dryBuffer.setSize (2, samplesPerBlock, false, true, true);
    bypassMix = bypassParam->get() ? 0.0f : 1.0f;
    bypassStep = (float) (1.0 / (0.02 * sampleRate));
    setLatencySamples (engine.getLatencySamples());
    monoOutput = getTotalNumOutputChannels() == 1;
}

void AmbienceProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = juce::jlimit (1, 2, getTotalNumInputChannels());
    const int numOut = juce::jlimit (1, 2, getTotalNumOutputChannels());
    const int n = buffer.getNumSamples();
    monoOutput = numOut == 1;

    amb::EngineParams p;
    for (auto& [id, ptr] : rawParams)
        p.set (id, ptr->load (std::memory_order_relaxed));
    engine.setParams (p);

    const float target = bypassParam->get() ? 0.0f : 1.0f;
    const bool crossfading = bypassMix != target;

    // Dry signal mapped to the output layout (mono input feeds both outputs).
    if (crossfading || target == 0.0f)
    {
        if (dryBuffer.getNumSamples() < n)
            dryBuffer.setSize (2, n, false, false, true);
        for (int c = 0; c < numOut; ++c)
            dryBuffer.copyFrom (c, 0, buffer, std::min (c, numIn - 1), 0, n);
    }

    if (target == 0.0f && ! crossfading)
    {
        for (int c = 0; c < numOut; ++c)
            buffer.copyFrom (c, 0, dryBuffer, c, 0, n);
        updateMeters (buffer, numOut, n);
        return;
    }

    const float* in[2] = { buffer.getReadPointer (0), buffer.getReadPointer (std::min (1, numIn - 1)) };
    float* out[2] = { buffer.getWritePointer (0), buffer.getWritePointer (std::min (1, buffer.getNumChannels() - 1)) };
    engine.process (in, numIn, out, numOut, n);
    autoGainDb = engine.getAutoGainDb();

    if (crossfading)
    {
        for (int i = 0; i < n; ++i)
        {
            bypassMix = target > bypassMix ? std::min (target, bypassMix + bypassStep) : std::max (target, bypassMix - bypassStep);
            for (int c = 0; c < numOut; ++c)
            {
                float* o = buffer.getWritePointer (c);
                o[i] = dryBuffer.getSample (c, i) + bypassMix * (o[i] - dryBuffer.getSample (c, i));
            }
        }
    }

    for (int c = numOut; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, n);
    updateMeters (buffer, numOut, n);
}

void AmbienceProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int numIn = getTotalNumInputChannels();
    if (numIn == 1)
        for (int c = 1; c < getTotalNumOutputChannels(); ++c)
            buffer.copyFrom (c, 0, buffer, 0, 0, buffer.getNumSamples());
}

void AmbienceProcessor::updateMeters (const juce::AudioBuffer<float>& buffer, int numOut, int n) noexcept
{
    float pk[2] = { 0, 0 };
    double sq[2] = { 0, 0 }, cross = 0;
    const float* l = buffer.getReadPointer (0);
    const float* r = buffer.getReadPointer (numOut > 1 ? 1 : 0);
    for (int i = 0; i < n; ++i)
    {
        pk[0] = std::max (pk[0], std::abs (l[i]));
        pk[1] = std::max (pk[1], std::abs (r[i]));
        sq[0] += (double) l[i] * l[i];
        sq[1] += (double) r[i] * r[i];
        cross += (double) l[i] * r[i];
    }
    for (int c = 0; c < 2; ++c)
    {
        meterPeak[c] = std::max (pk[c], meterPeak[c].load() * 0.9f);
        meterRms[c] = (float) std::sqrt (sq[c] / std::max (n, 1));
    }
    // Correlation with ~300 ms integration.
    const float a = std::exp (-(float) n / (0.3f * (float) getSampleRate()));
    corrNum = a * corrNum + (1 - a) * (float) cross;
    corrL = a * corrL + (1 - a) * (float) sq[0];
    corrR = a * corrR + (1 - a) * (float) sq[1];
    const float den = std::sqrt (corrL * corrR);
    correlation = den > 1.0e-12f ? juce::jlimit (-1.0f, 1.0f, corrNum / den) : 1.0f;
}

//==============================================================================
int AmbienceProcessor::getNumPrograms() { return (int) amb::factoryPresets().size(); }
int AmbienceProcessor::getCurrentProgram() { return juce::jmax (0, presets.currentFactoryIndex()); }
void AmbienceProcessor::setCurrentProgram (int index) { presets.loadFactory (index); }
const juce::String AmbienceProcessor::getProgramName (int index)
{
    const auto& list = amb::factoryPresets();
    return juce::isPositiveAndBelow (index, (int) list.size()) ? juce::String (list[(size_t) index].name) : juce::String();
}

void AmbienceProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("stateVersion", stateVersion, nullptr);
    state.setProperty ("uiScale", uiScalePercent.load(), nullptr);
    state.setProperty ("presetName", presets.currentName(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AmbienceProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;
    auto state = juce::ValueTree::fromXml (*xml);
    const int version = state.getProperty ("stateVersion", 1);
    // Migration hook: states from future versions are loaded best-effort; parameters unknown
    // to this version are ignored by the APVTS, missing ones keep their defaults.
    if (version < 1)
        state.setProperty ("stateVersion", 1, nullptr);
    uiScalePercent = juce::jlimit (50, 200, (int) state.getProperty ("uiScale", 100));
    presets.setCurrentName (state.getProperty ("presetName", "Init (Neutral)").toString());
    apvts.replaceState (state);
}

juce::AudioProcessorEditor* AmbienceProcessor::createEditor() { return new AmbienceEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AmbienceProcessor(); }
