#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "dsp/RoomModel.h"

namespace
{
// juce::String (float, 0) would print full precision, so zero decimals are rounded explicitly.
juce::String fixed (float v, int decimals)
{
    return decimals <= 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, decimals);
}

juce::String formatValue (const amb::ParamSpec& s, float v)
{
    const juce::String id (s.id);
    if (id == amb::pid::pan)
    {
        if (std::abs (v) < 0.5f)
            return "C";
        return (v < 0 ? "L" : "R") + fixed (std::abs (v), 0);
    }
    if (id == amb::pid::distance || id == amb::pid::recDistance)
        return fixed (v, v < 10.0f ? 2 : (v < 100.0f ? 1 : 0)) + " m";
    if (id == amb::pid::obstacleSize)
        return juce::String (v, 2) + " m";
    if (id == amb::pid::thickness)
        return juce::String (v, 2) + "x";
    if (id == amb::pid::temperature)
        return juce::String (v, 1) + juce::String::fromUTF8 (" \xc2\xb0""C");
    juce::String unit (s.unit);
    const int decimals = (unit == "dB" || unit == "dB/dd") ? 1 : 0;
    if (unit == "dB" && v > 0.05f)
        return "+" + fixed (v, decimals) + " dB";
    return fixed (v, decimals) + (unit.isEmpty() ? "" : " " + unit);
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

namespace
{
// Loudspeaker position of a JUCE channel type (ITU-R BS.775 / Dolby Atmos bed positions).
bool speakerForType (juce::AudioChannelSet::ChannelType t, amb::Speaker& s)
{
    using CT = juce::AudioChannelSet::ChannelType;
    s = {};
    switch (t)
    {
        case CT::left: s.azimuthDeg = -30; return true;
        case CT::right: s.azimuthDeg = 30; return true;
        case CT::centre: s.azimuthDeg = 0; return true;
        case CT::LFE: case CT::LFE2: s.lfe = true; return true;
        case CT::leftSurround: s.azimuthDeg = -110; return true;
        case CT::rightSurround: s.azimuthDeg = 110; return true;
        case CT::leftSurroundSide: s.azimuthDeg = -90; return true;
        case CT::rightSurroundSide: s.azimuthDeg = 90; return true;
        case CT::leftSurroundRear: s.azimuthDeg = -150; return true;
        case CT::rightSurroundRear: s.azimuthDeg = 150; return true;
        case CT::centreSurround: s.azimuthDeg = 180; return true;
        case CT::wideLeft: s.azimuthDeg = -60; return true;
        case CT::wideRight: s.azimuthDeg = 60; return true;
        case CT::topFrontLeft: s.azimuthDeg = -45; s.elevationDeg = 45; return true;
        case CT::topFrontRight: s.azimuthDeg = 45; s.elevationDeg = 45; return true;
        case CT::topFrontCentre: s.azimuthDeg = 0; s.elevationDeg = 45; return true;
        case CT::topRearLeft: s.azimuthDeg = -135; s.elevationDeg = 45; return true;
        case CT::topRearRight: s.azimuthDeg = 135; s.elevationDeg = 45; return true;
        case CT::topRearCentre: s.azimuthDeg = 180; s.elevationDeg = 45; return true;
        case CT::topSideLeft: s.azimuthDeg = -90; s.elevationDeg = 45; return true;
        case CT::topSideRight: s.azimuthDeg = 90; s.elevationDeg = 45; return true;
        case CT::topMiddle: s.azimuthDeg = 0; s.elevationDeg = 80; return true;
        default: return false;
    }
}

bool layoutFromSet (const juce::AudioChannelSet& set, amb::SpeakerLayout& layout)
{
    layout = {};
    if (set.size() > amb::maxOutputChannels)
        return false;
    for (int c = 0; c < set.size(); ++c)
        if (! speakerForType (set.getTypeOfChannel (c), layout.speakers[(size_t) layout.numChannels++]))
            return false;
    layout.finalise();
    return true;
}

// Multichannel outputs need a front stage (L, C, R).
bool isSupportedSurround (const juce::AudioChannelSet& set)
{
    amb::SpeakerLayout l;
    return set.size() > 2 && layoutFromSet (set, l) && l.left >= 0 && l.right >= 0 && l.centre >= 0;
}
} // namespace

bool AmbienceProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const bool inBasic = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    if (out == juce::AudioChannelSet::mono())
        return in == juce::AudioChannelSet::mono(); // 1 -> 1
    if (out == juce::AudioChannelSet::stereo())
        return inBasic; // 1 -> 2, 2 -> 2
    // Multichannel (5.0 ... 7.1.4): from mono, stereo or the same layout (e.g. on a surround aux).
    return isSupportedSurround (out) && (inBasic || in == out);
}

void AmbienceProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);

    const auto outSet = getChannelLayoutOfBus (false, 0);
    const auto inSet = getChannelLayoutOfBus (true, 0);
    amb::SpeakerLayout layout;
    surroundOut = isSupportedSurround (outSet) && layoutFromSet (outSet, layout);
    outputChannelsForUi = outSet.size();
    engine.setOutputLayout (surroundOut ? layout : amb::SpeakerLayout {});
    outLayout = layout;

    // Multichannel input: folded to a stereo source (centre at -3 dB into both, surrounds and
    // heights at -6 dB); the LFE input is passed straight to the LFE output.
    foldL.fill (0.0f);
    foldR.fill (0.0f);
    lfeIn = lfeOut = -1;
    surroundIn = inSet.size() > 2;
    if (surroundIn)
    {
        amb::SpeakerLayout inLayout;
        layoutFromSet (inSet, inLayout);
        for (int c = 0; c < inLayout.numChannels; ++c)
        {
            const auto& sp = inLayout.speakers[(size_t) c];
            if (sp.lfe)
            {
                lfeIn = c;
                continue;
            }
            const float w = (std::abs (sp.azimuthDeg) <= 30.0 && sp.elevationDeg < 10.0) ? 1.0f : 0.5f;
            if (std::abs (sp.azimuthDeg) < 1.0 || std::abs (std::abs (sp.azimuthDeg) - 180.0) < 1.0)
                foldL[(size_t) c] = foldR[(size_t) c] = 0.70710678f * w;
            else if (sp.azimuthDeg < 0.0)
                foldL[(size_t) c] = w;
            else
                foldR[(size_t) c] = w;
        }
    }
    if (surroundOut)
        for (int c = 0; c < layout.numChannels; ++c)
            if (layout.speakers[(size_t) c].lfe)
                lfeOut = c;

    const int numOut = std::max (2, getTotalNumOutputChannels());
    dryBuffer.setSize (numOut, samplesPerBlock, false, true, true);
    foldBuffer.setSize (2, samplesPerBlock, false, true, true);
    lfeBuffer.setSize (1, samplesPerBlock, false, true, true);
    bypassMix = bypassParam->get() ? 0.0f : 1.0f;
    bypassStep = (float) (1.0 / (0.02 * sampleRate));
    setLatencySamples (engine.getLatencySamples());
    monoOutput = getTotalNumOutputChannels() == 1;
}

// Dry (bypassed) signal on the output layout: mono -> centre (or L/R), stereo -> L/R, a multichannel
// input -> the same channels.
void AmbienceProcessor::writeDry (const juce::AudioBuffer<float>& src, juce::AudioBuffer<float>& dst, int numIn, int numOut, int n) const
{
    for (int c = 0; c < numOut; ++c)
    {
        if (! surroundOut)
        {
            dst.copyFrom (c, 0, src, std::min (c, numIn - 1), 0, n);
            continue;
        }
        dst.clear (c, 0, n);
        if (surroundIn)
            dst.copyFrom (c, 0, src, c, 0, n);
        else if (numIn == 1)
        {
            if (c == outLayout.centre)
                dst.copyFrom (c, 0, src, 0, 0, n);
        }
        else if (c == outLayout.left || c == outLayout.right)
            dst.copyFrom (c, 0, src, c == outLayout.left ? 0 : 1, 0, n);
    }
}

void AmbienceProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numInTotal = getTotalNumInputChannels();
    const int numOutTotal = getTotalNumOutputChannels();
    const int numOut = surroundOut ? numOutTotal : juce::jlimit (1, 2, numOutTotal);
    const int numIn = surroundIn ? numInTotal : juce::jlimit (1, 2, numInTotal);
    const int n = buffer.getNumSamples();
    monoOutput = numOut == 1;

    amb::EngineParams p;
    for (auto& [id, ptr] : rawParams)
        p.set (id, ptr->load (std::memory_order_relaxed));
    engine.setParams (p);

    const float target = bypassParam->get() ? 0.0f : 1.0f;
    const bool crossfading = bypassMix != target;

    if (crossfading || target == 0.0f)
    {
        if (dryBuffer.getNumSamples() < n || dryBuffer.getNumChannels() < numOut)
            dryBuffer.setSize (std::max (2, numOut), n, false, false, true);
        writeDry (buffer, dryBuffer, numIn, numOut, n);
    }

    if (target == 0.0f && ! crossfading)
    {
        for (int c = 0; c < numOut; ++c)
            buffer.copyFrom (c, 0, dryBuffer, c, 0, n);
        updateMeters (buffer, numOut, n);
        return;
    }

    // Engine input: mono / stereo, or a multichannel input folded to stereo.
    const float* in[2];
    int engineIn = numIn;
    if (surroundIn)
    {
        if (foldBuffer.getNumSamples() < n)
            foldBuffer.setSize (2, n, false, false, true);
        foldBuffer.clear (0, n);
        for (int c = 0; c < numIn; ++c)
        {
            if (foldL[(size_t) c] != 0.0f)
                foldBuffer.addFrom (0, 0, buffer, c, 0, n, foldL[(size_t) c]);
            if (foldR[(size_t) c] != 0.0f)
                foldBuffer.addFrom (1, 0, buffer, c, 0, n, foldR[(size_t) c]);
        }
        if (lfeIn >= 0)
        {
            if (lfeBuffer.getNumSamples() < n)
                lfeBuffer.setSize (1, n, false, false, true);
            lfeBuffer.copyFrom (0, 0, buffer, lfeIn, 0, n);
        }
        in[0] = foldBuffer.getReadPointer (0);
        in[1] = foldBuffer.getReadPointer (1);
        engineIn = 2;
    }
    else
    {
        in[0] = buffer.getReadPointer (0);
        in[1] = buffer.getReadPointer (std::min (1, numIn - 1));
    }
    float* out[amb::maxOutputChannels];
    for (int c = 0; c < numOut; ++c)
        out[c] = buffer.getWritePointer (std::min (c, buffer.getNumChannels() - 1));
    engine.process (in, engineIn, out, numOut, n);
    if (surroundIn && lfeIn >= 0 && lfeOut >= 0)
        buffer.copyFrom (lfeOut, 0, lfeBuffer, 0, 0, n);
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
    const int numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    if (! surroundOut)
    {
        if (numIn == 1)
            for (int c = 1; c < numOut; ++c)
                buffer.copyFrom (c, 0, buffer, 0, 0, n);
        return;
    }
    if (surroundIn)
        return; // same layout in and out
    if (dryBuffer.getNumSamples() < n || dryBuffer.getNumChannels() < numOut)
        return; // not prepared for this block size (host bypass before prepare): leave untouched
    writeDry (buffer, dryBuffer, std::min (numIn, 2), numOut, n);
    for (int c = 0; c < numOut; ++c)
        buffer.copyFrom (c, 0, dryBuffer, c, 0, n);
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
    if (version < 2)
    {
        // v1 had 6 rooms (Off, Outdoor, Small, Medium, Large, Hall); map to the A variants.
        for (auto child : state)
            if (child.hasType ("PARAM") && child.getProperty ("id").toString() == amb::pid::room)
                child.setProperty ("value", amb::migrateRoomIndexV1 ((int) (float) child.getProperty ("value")), nullptr);
        state.setProperty ("stateVersion", stateVersion, nullptr);
    }
    // Parameters added after the state was saved start at their defaults (the APVTS would otherwise
    // keep whatever value they currently have).
    for (const auto& s : amb::paramSpecs())
        if (! state.getChildWithProperty ("id", juce::String (s.id)).isValid())
        {
            juce::ValueTree child ("PARAM");
            child.setProperty ("id", juce::String (s.id), nullptr);
            // Keep Mono did not exist before 1.4: older sessions keep their full-signal widening.
            const float value = juce::String (s.id) == amb::pid::keepMono ? 0.0f : s.defaultValue;
            child.setProperty ("value", value, nullptr);
            state.appendChild (child, nullptr);
        }
    uiScalePercent = juce::jlimit (50, 200, (int) state.getProperty ("uiScale", 100));
    presets.setCurrentName (state.getProperty ("presetName", "Init (Neutral)").toString());
    apvts.replaceState (state);
}

juce::AudioProcessorEditor* AmbienceProcessor::createEditor() { return new AmbienceEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AmbienceProcessor(); }
