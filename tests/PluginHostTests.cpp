// Host-level tests of the plugin processor: channel layouts (mono ... 7.1.4), routing, bypass,
// LFE pass-through and state. Built from the plugin sources as a console app (no plugin binary).
#include <cstdio>
#include <functional>
#include <string>

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
    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
