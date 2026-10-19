// Host-level tests of the plugin processor: channel layouts (mono ... 7.1.4), routing, bypass,
// LFE pass-through, state, the EQ parameters and the editor. Built from the plugin sources as a console
// app (no plugin binary).
#include <cstdio>
#include <functional>
#include <string>

#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

namespace
{
int failures = 0, checks = 0;

void check (bool ok, const std::string& what)
{
    ++checks;
    if (! ok)
    {
        ++failures;
        std::printf ("    FAIL: %s\n", what.c_str());
    }
}

double energy (const juce::AudioBuffer<float>& b, int ch, int from = 0)
{
    double e = 0;
    for (int i = from; i < b.getNumSamples(); ++i)
        e += (double) b.getSample (ch, i) * b.getSample (ch, i);
    return e;
}

int channelOf (const juce::AudioChannelSet& set, juce::AudioChannelSet::ChannelType t) { return set.getChannelIndexForType (t); }

// Runs `blocks` blocks of pink-ish noise through a processor configured for in -> out.
juce::AudioBuffer<float> run (AmbienceProcessor& p, const juce::AudioChannelSet& in, const juce::AudioChannelSet& out,
                              const std::function<void (juce::AudioBuffer<float>&)>& fillInput, int blocks = 40, int block = 512)
{
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (in);
    layout.outputBuses.add (out);
    const bool ok = p.setBusesLayout (layout);
    check (ok, "layout accepted: " + in.getDescription().toStdString() + " -> " + out.getDescription().toStdString());
    p.prepareToPlay (48000.0, block);
    const int nCh = std::max (in.size(), out.size());
    juce::AudioBuffer<float> all (out.size(), blocks * block);
    juce::MidiBuffer midi;
    for (int b = 0; b < blocks; ++b)
    {
        juce::AudioBuffer<float> buf (nCh, block);
        buf.clear();
        fillInput (buf);
        p.processBlock (buf, midi);
        for (int c = 0; c < out.size(); ++c)
            all.copyFrom (c, b * block, buf, c, 0, block);
    }
    p.releaseResources();
    return all;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::Random rng (42);
    auto noise = [&rng] (juce::AudioBuffer<float>& b, int ch)
    {
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (ch, i, (rng.nextFloat() * 2.0f - 1.0f) * 0.3f);
    };

    const auto s51 = juce::AudioChannelSet::create5point1();
    const auto s714 = juce::AudioChannelSet::create7point1point4();
    const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();

    {
        std::printf ("layouts\n");
        AmbienceProcessor p;
        for (auto out : { s51, juce::AudioChannelSet::create5point0(), juce::AudioChannelSet::create7point1(), juce::AudioChannelSet::create7point1point2(),
                          juce::AudioChannelSet::create5point1point4(), s714 })
        {
            juce::AudioProcessor::BusesLayout l;
            l.inputBuses.add (mono);
            l.outputBuses.add (out);
            check (p.checkBusesLayoutSupported (l), "mono -> " + out.getDescription().toStdString() + " supported");
            l.inputBuses.getReference (0) = out;
            check (p.checkBusesLayoutSupported (l), out.getDescription().toStdString() + " -> same supported");
        }
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (stereo);
        l.outputBuses.add (mono);
        check (! p.checkBusesLayoutSupported (l), "stereo -> mono rejected");
    }
    {
        std::printf ("mono dialogue -> 5.1: centre only, LFE silent\n");
        AmbienceProcessor p;
        const auto o = run (p, mono, s51, [&] (juce::AudioBuffer<float>& b) { noise (b, 0); });
        const int C = channelOf (s51, juce::AudioChannelSet::centre), L = channelOf (s51, juce::AudioChannelSet::left),
                  lfe = channelOf (s51, juce::AudioChannelSet::LFE), Ls = channelOf (s51, juce::AudioChannelSet::leftSurround);
        check (energy (o, C) > 0.0, "centre has the dialogue");
        check (energy (o, L) == 0.0 && energy (o, Ls) == 0.0, "no dialogue in L / Ls (room off)");
        check (energy (o, lfe) == 0.0, "LFE silent");
    }
    {
        std::printf ("mono -> 7.1.4 with a room: room in surrounds and heights\n");
        AmbienceProcessor p;
        p.apvts.getParameter ("room")->setValueNotifyingHost (p.apvts.getParameter ("room")->convertTo0to1 (15));
        const auto o = run (p, mono, s714, [&] (juce::AudioBuffer<float>& b) { noise (b, 0); });
        const int Ltf = channelOf (s714, juce::AudioChannelSet::topFrontLeft), Lrs = channelOf (s714, juce::AudioChannelSet::leftSurroundRear);
        check (energy (o, Ltf, 12000) > 0.0 && energy (o, Lrs, 12000) > 0.0, "height and rear channels carry the room");
        bool finite = true;
        for (int c = 0; c < o.getNumChannels(); ++c)
            for (int i = 0; i < o.getNumSamples(); ++i)
                finite = finite && std::isfinite (o.getSample (c, i));
        check (finite, "finite output");
    }
    {
        std::printf ("5.1 -> 5.1 (surround aux): LFE passes through, bypass is transparent\n");
        AmbienceProcessor p;
        const int lfe = channelOf (s51, juce::AudioChannelSet::LFE);
        juce::AudioBuffer<float> lastIn;
        const auto o = run (p, s51, s51, [&] (juce::AudioBuffer<float>& b)
        {
            for (int c = 0; c < b.getNumChannels(); ++c)
                noise (b, c);
            lastIn = b;
        });
        double diff = 0;
        const int n = lastIn.getNumSamples(), off = o.getNumSamples() - n;
        for (int i = 0; i < n; ++i)
            diff = std::max (diff, (double) std::abs (o.getSample (lfe, off + i) - lastIn.getSample (lfe, i)));
        check (diff == 0.0, "LFE input passed to LFE output unchanged");

        AmbienceProcessor q;
        q.apvts.getParameter ("bypass")->setValueNotifyingHost (1.0f);
        juce::AudioBuffer<float> in2;
        const auto ob = run (q, s51, s51, [&] (juce::AudioBuffer<float>& b)
        {
            for (int c = 0; c < b.getNumChannels(); ++c)
                noise (b, c);
            in2 = b;
        });
        double d2 = 0;
        for (int c = 0; c < 6; ++c)
            for (int i = 0; i < n; ++i)
                d2 = std::max (d2, (double) std::abs (ob.getSample (c, off + i) - in2.getSample (c, i)));
        check (d2 == 0.0, "bypass: 5.1 in = 5.1 out");
    }
    {
        std::printf ("bypass mono -> 5.1: dry signal in the centre\n");
        AmbienceProcessor p;
        p.apvts.getParameter ("bypass")->setValueNotifyingHost (1.0f);
        const auto o = run (p, mono, s51, [&] (juce::AudioBuffer<float>& b) { noise (b, 0); });
        const int C = channelOf (s51, juce::AudioChannelSet::centre), L = channelOf (s51, juce::AudioChannelSet::left);
        check (energy (o, C) > 0.0 && energy (o, L) == 0.0, "bypassed mono sits in C only");
    }
    {
        std::printf ("stereo -> stereo unchanged behaviour at defaults (transparent)\n");
        AmbienceProcessor p;
        juce::AudioBuffer<float> in;
        const auto o = run (p, stereo, stereo, [&] (juce::AudioBuffer<float>& b)
        {
            noise (b, 0);
            noise (b, 1);
            in = b;
        });
        const int n = in.getNumSamples(), off = o.getNumSamples() - n;
        double d = 0;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
                d = std::max (d, (double) std::abs (o.getSample (c, off + i) - in.getSample (c, i)));
        check (d < 1.0e-6, "stereo defaults are transparent");
    }
    {
        std::printf ("Mono to Stereo: mono track (1 -> 1), mono -> stereo, mono clip in one channel of a stereo track\n");
        auto setOn = [] (AmbienceProcessor& p, const char* id) { p.apvts.getParameter (id)->setValueNotifyingHost (1.0f); };
        using M2S = amb::SpatialEngine::MonoToStereoStatus;
        AmbienceProcessor a;
        setOn (a, "monoToStereo");
        run (a, mono, mono, [&] (juce::AudioBuffer<float>& b) { noise (b, 0); });
        check (a.monoToStereoStatus.load() == (int) M2S::monoOutput, "1 -> 1: reported as needing a stereo output");

        AmbienceProcessor m;
        setOn (m, "monoToStereo");
        const auto o = run (m, mono, stereo, [&] (juce::AudioBuffer<float>& b) { noise (b, 0); }, 200); // ~2 s: room-tone estimate settled
        check (m.monoToStereoStatus.load() == (int) M2S::active, "1 -> 2: active");
        // Correlation below 8 kHz (white noise carries ~40 % of its energy above 15 kHz, which stays mono).
        juce::IIRFilter fl, fr;
        fl.setCoefficients (juce::IIRCoefficients::makeLowPass (48000.0, 8000.0));
        fr.setCoefficients (juce::IIRCoefficients::makeLowPass (48000.0, 8000.0));
        double lr = 0, ll = 0, rr = 0;
        for (int i = 0; i < o.getNumSamples(); ++i)
        {
            const float l = fl.processSingleSampleRaw (o.getSample (0, i)), r = fr.processSingleSampleRaw (o.getSample (1, i));
            if (i < o.getNumSamples() / 2)
                continue;
            lr += (double) l * r;
            ll += (double) l * l;
            rr += (double) r * r;
        }
        std::printf ("    1 -> 2 output correlation below 8 kHz: %.2f\n", lr / std::sqrt (ll * rr));
        check (lr / std::sqrt (ll * rr) < 0.9, "1 -> 2: decorrelated stereo output");

        AmbienceProcessor s;
        setOn (s, "monoToStereo");
        const auto os = run (s, stereo, stereo, [&] (juce::AudioBuffer<float>& b) { noise (b, 0); }); // right channel silent
        const double el = energy (os, 0, os.getNumSamples() / 2), er = energy (os, 1, os.getNumSamples() / 2);
        check (s.monoToStereoStatus.load() == (int) M2S::active, "left-only clip on a stereo track: detected as mono");
        check (std::abs (10.0 * std::log10 (el / er)) < 0.5, "left-only clip on a stereo track: centred");

        // Cubase / Nuendo: a mono track routed to a stereo group arrives as L = R at -3 dB (equal-power pan law).
        AmbienceProcessor g;
        setOn (g, "monoToStereo");
        double inMid = 0; // (L + R) / 2 of the input = L
        const auto og = run (g, stereo, stereo, [&] (juce::AudioBuffer<float>& b)
        {
            noise (b, 0);
            b.applyGain (0, 0, b.getNumSamples(), 0.70710678f);
            b.copyFrom (1, 0, b, 0, 0, b.getNumSamples());
            inMid += energy (b, 0);
        }, 200);
        check (g.monoToStereoStatus.load() == (int) M2S::active, "mono track into a stereo group: Mono to Stereo active");
        juce::IIRFilter gl, gr;
        gl.setCoefficients (juce::IIRCoefficients::makeLowPass (48000.0, 8000.0));
        gr.setCoefficients (juce::IIRCoefficients::makeLowPass (48000.0, 8000.0));
        double glr = 0, gll = 0, grr = 0;
        for (int i = 0; i < og.getNumSamples(); ++i)
        {
            const float l = gl.processSingleSampleRaw (og.getSample (0, i)), r = gr.processSingleSampleRaw (og.getSample (1, i));
            if (i < og.getNumSamples() / 2)
                continue;
            glr += (double) l * r;
            gll += (double) l * l;
            grr += (double) r * r;
        }
        double outMid = 0;
        for (int i = 0; i < og.getNumSamples(); ++i)
            outMid += 0.25 * juce::square ((double) og.getSample (0, i) + og.getSample (1, i));
        std::printf ("    mono track into a stereo group: correlation below 8 kHz %.2f, mono sum %+.2f dB\n", glr / std::sqrt (gll * grr),
                     10.0 * std::log10 (outMid / inMid));
        check (glr / std::sqrt (gll * grr) < 0.9, "mono track into a stereo group: stereo output");
        check (std::abs (10.0 * std::log10 (outMid / inMid)) < 0.5, "mono track into a stereo group: mono sum keeps its level");
    }
    {
        std::printf ("state: sessions without Keep Mono load with it off\n");
        AmbienceProcessor p;
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
            if (e->getStringAttribute ("id") == "keepMono")
            {
                xml->removeChildElement (e, true);
                break;
            }
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        AmbienceProcessor q;
        q.setStateInformation (old.getData(), (int) old.getSize());
        check (q.apvts.getRawParameterValue ("keepMono")->load() < 0.5f, "Keep Mono off for older sessions");
        check (AmbienceProcessor().apvts.getRawParameterValue ("keepMono")->load() > 0.5f, "Keep Mono on for new instances");
    }
    {
        std::printf ("EQ: parameter text, state, audio and the editor tabs\n");
        AmbienceProcessor p;
        auto* lowCut = p.apvts.getParameter ("eqInLowCut");
        auto* highCut = p.apvts.getParameter ("eqOutHighCut");
        auto* peakFreq = p.apvts.getParameter ("eqRevPeakFreq");
        auto* peakQ = p.apvts.getParameter ("eqInPeakQ");
        check (lowCut->getCurrentValueAsText() == "Off" && highCut->getCurrentValueAsText() == "Off", "cut filters show Off at their limits");
        check (peakQ->getCurrentValueAsText() == "Q 1.00", "Q shown as Q 1.00");
        auto plain = [] (juce::RangedAudioParameter* q, const juce::String& text) { return q->convertFrom0to1 (q->getValueForText (text)); };
        check (std::abs (plain (peakFreq, "2.5k") - 2500.0f) < 1.0f && std::abs (plain (peakFreq, "2.5 kHz") - 2500.0f) < 1.0f, "\"2.5k\" / \"2.5 kHz\" = 2500 Hz");
        check (std::abs (plain (lowCut, "120 Hz") - 120.0f) < 0.5f, "\"120 Hz\" = 120 Hz");
        check (plain (highCut, "off") > 19999.0f && plain (lowCut, "Off") < 20.01f, "\"off\" switches a cut filter off");
        check (std::abs (plain (peakQ, "Q 2.5") - 2.5f) < 0.01f, "\"Q 2.5\" = 2.5");
        peakFreq->setValueNotifyingHost (peakFreq->convertTo0to1 (2500.0f));
        check (peakFreq->getCurrentValueAsText() == "2.50 kHz", "2500 Hz shown as 2.50 kHz");

        // State round trip, and older sessions (no EQ parameters) load a neutral EQ.
        auto setPlain = [] (AmbienceProcessor& q, const char* id, float v)
        {
            auto* par = q.apvts.getParameter (id);
            par->setValueNotifyingHost (par->convertTo0to1 (v));
        };
        setPlain (p, "eqRevHighShelfGain", -6.0f);
        setPlain (p, "eqOutOn", 0.0f);
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        AmbienceProcessor r;
        r.setStateInformation (mb.getData(), (int) mb.getSize());
        check (std::abs (r.apvts.getRawParameterValue ("eqRevHighShelfGain")->load() + 6.0f) < 0.01f
                   && r.apvts.getRawParameterValue ("eqOutOn")->load() < 0.5f,
               "EQ settings restored from the session");
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        juce::Array<juce::XmlElement*> eqParams;
        for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
            if (const auto id = e->getStringAttribute ("id"); id.startsWith ("eqIn") || id.startsWith ("eqOut") || id.startsWith ("eqRev"))
                eqParams.add (e);
        check (eqParams.size() == 30, "30 EQ parameters stored");
        for (auto* e : eqParams)
            xml->removeChildElement (e, true);
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        AmbienceProcessor o;
        setPlain (o, "eqRevHighShelfGain", 3.0f);
        o.setStateInformation (old.getData(), (int) old.getSize());
        check (std::abs (o.apvts.getRawParameterValue ("eqRevHighShelfGain")->load()) < 0.01f && o.apvts.getRawParameterValue ("eqOutOn")->load() > 0.5f,
               "sessions without the EQ load it neutral");

        // Output high cut at 1 kHz on white noise: most of the energy goes; switched off it is transparent again.
        auto runEq = [&] (bool on)
        {
            AmbienceProcessor q;
            setPlain (q, "eqOutHighCut", 1000.0f);
            setPlain (q, "eqOutOn", on ? 1.0f : 0.0f);
            juce::AudioBuffer<float> in (2, 40 * 512);
            int pos = 0;
            const auto out = run (q, stereo, stereo, [&] (juce::AudioBuffer<float>& b)
            {
                noise (b, 0);
                b.copyFrom (1, 0, b, 0, 0, b.getNumSamples());
                in.copyFrom (0, pos, b, 0, 0, b.getNumSamples());
                pos += b.getNumSamples();
            });
            double d = 0;
            for (int i = 0; i < out.getNumSamples(); ++i)
                d = std::max (d, (double) std::abs (out.getSample (0, i) - in.getSample (0, i)));
            return std::make_pair (10.0 * std::log10 (energy (out, 0) / energy (in, 0)), d);
        };
        const auto [cutDb, cutDiff] = runEq (true);
        const auto [offDb, offDiff] = runEq (false);
        std::printf ("    output high cut 1 kHz on white noise: %.1f dB (EQ off: max difference %.2g)\n", cutDb, offDiff);
        check (cutDb < -10.0, "output EQ high cut removes the highs");
        check (offDiff < 1.0e-6, "output EQ switched off: transparent");
        juce::ignoreUnused (cutDiff, offDb);

        // Editor: the EQ section, its three tabs, the On switch follows the selected tab.
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        std::function<ui::EqSection*(juce::Component&)> find = [&] (juce::Component& c) -> ui::EqSection*
        {
            if (auto* e = dynamic_cast<ui::EqSection*> (&c))
                return e;
            for (auto* child : c.getChildren())
                if (auto* e = find (*child))
                    return e;
            return nullptr;
        };
        auto* section = find (*editor);
        check (section != nullptr, "editor has the EQ section");
        if (section != nullptr)
        {
            juce::TextButton* tabs[3] = {};
            juce::ToggleButton* onSwitch = nullptr;
            for (auto* child : section->getChildren())
            {
                if (auto* t = dynamic_cast<juce::TextButton*> (child))
                    for (int i = 0; i < 3; ++i)
                        if (t->getButtonText() == juce::StringArray { "INPUT", "OUTPUT", "REVERB ONLY" }[i])
                            tabs[i] = t;
                if (auto* t = dynamic_cast<juce::ToggleButton*> (child))
                    onSwitch = t;
            }
            check (tabs[0] != nullptr && tabs[1] != nullptr && tabs[2] != nullptr && onSwitch != nullptr, "three EQ tabs and an On switch");
            if (tabs[0] != nullptr && tabs[1] != nullptr && tabs[2] != nullptr && onSwitch != nullptr)
            {
                check (tabs[0]->getToggleState() && onSwitch->getToggleState(), "Input tab selected first, EQ on");
                tabs[1]->onClick(); // Output: switched off above
                check (tabs[1]->getToggleState() && ! tabs[0]->getToggleState() && ! onSwitch->getToggleState(), "Output tab shows the Output EQ state (off)");
                tabs[2]->onClick();
                onSwitch->setToggleState (false, juce::sendNotificationSync);
                check (p.apvts.getRawParameterValue ("eqRevOn")->load() < 0.5f && p.apvts.getRawParameterValue ("eqInOn")->load() > 0.5f,
                       "On switch of the Reverb tab drives only the Reverb EQ");
                const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
                check (image.isValid(), "editor paints with the EQ");
            }
        }
    }
    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
