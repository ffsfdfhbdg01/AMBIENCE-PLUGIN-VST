// ambience_render — offline renderer for listening examples and measurements.
//
//   ambience_render in.wav out.wav [--preset "Name"] [--set id=value ...] [--channels 1|2] [--block N]
//   ambience_render --list-presets
//   ambience_render --list-params
//   ambience_render --make-test-signals DIR     (sweep, pink noise, impulses at 48 kHz)
#include <cstdio>
#include <cstdlib>
#include <string>

#include "../Source/dsp/SpatialEngine.h"
#include "Render.h"
#include "TestSignals.h"

using namespace amb;

static int usage()
{
    std::puts ("usage: ambience_render in.wav out.wav [--preset NAME] [--set id=value]... [--channels 1|2] [--block N]\n"
               "       ambience_render --list-presets | --list-params | --make-test-signals DIR");
    return 2;
}

static int makeTestSignals (const std::string& dir)
{
    const int fs = 48000;
    tools::WavData w;
    w.sampleRate = fs;
    w.channels = 1;

    // Exponential sine sweep 20 Hz - 20 kHz, 10 s, -6 dBFS.
    {
        const int n = fs * 10;
        std::vector<float> s ((size_t) n);
        const double f1 = 20.0, f2 = 20000.0, T = 10.0, K = T / std::log (f2 / f1);
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / fs;
            s[(size_t) i] = 0.5f * (float) std::sin (kTwoPi * f1 * K * (std::exp (t / K) - 1.0));
        }
        w.data = { s };
        tools::writeWav (dir + "/sweep_20-20k_10s.wav", w);
    }
    // Pink noise 10 s.
    w.data = { tools::pinkNoise (fs * 10) };
    tools::writeWav (dir + "/pink_noise_10s.wav", w);
    // Stereo pink noise (decorrelated) 10 s.
    {
        tools::WavData st;
        st.sampleRate = fs;
        st.channels = 2;
        st.data = { tools::pinkNoise (fs * 10, 1), tools::pinkNoise (fs * 10, 2) };
        tools::writeWav (dir + "/pink_noise_stereo_10s.wav", st);
    }
    // Impulses every 500 ms (clicks), 5 s.
    {
        std::vector<float> s ((size_t) fs * 5, 0.0f);
        for (int i = 0; i < fs * 5; i += fs / 2)
            s[(size_t) i] = 0.9f;
        w.data = { s };
        tools::writeWav (dir + "/impulses_5s.wav", w);
    }
    std::printf ("test signals written to %s\n", dir.c_str());
    return 0;
}

int main (int argc, char** argv)
{
    if (argc < 2)
        return usage();
    const std::string a1 = argv[1];
    if (a1 == "--list-presets")
    {
        for (auto& p : factoryPresets())
            std::printf ("%s\n", p.name);
        return 0;
    }
    if (a1 == "--list-params")
    {
        for (auto& s : paramSpecs())
        {
            std::printf ("%-14s %-18s default %g  range %g..%g %s", s.id, s.name, s.defaultValue, s.minValue, s.maxValue, s.unit);
            if (! s.choices.empty())
            {
                std::printf ("  [");
                for (size_t i = 0; i < s.choices.size(); ++i)
                    std::printf ("%s%zu=%s", i ? ", " : "", i, s.choices[i]);
                std::printf ("]");
            }
            std::printf ("\n");
        }
        return 0;
    }
    if (a1 == "--make-test-signals")
        return argc > 2 ? makeTestSignals (argv[2]) : usage();
    if (argc < 3)
        return usage();

    const std::string inPath = argv[1], outPath = argv[2];
    EngineParams params;
    int channels = 2, block = 512;
    for (int i = 3; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--preset" && i + 1 < argc)
        {
            const std::string name = argv[++i];
            bool found = false;
            for (auto& p : factoryPresets())
                if (name == p.name)
                {
                    params = paramsForPreset (p);
                    found = true;
                }
            if (! found)
            {
                std::fprintf (stderr, "unknown preset: %s\n", name.c_str());
                return 1;
            }
        }
        else if (a == "--set" && i + 1 < argc)
        {
            const std::string kv = argv[++i];
            const auto eq = kv.find ('=');
            if (eq == std::string::npos)
                return usage();
            params.set (kv.substr (0, eq), std::strtof (kv.c_str() + eq + 1, nullptr));
        }
        else if (a == "--channels" && i + 1 < argc)
            channels = std::clamp (std::atoi (argv[++i]), 1, 2);
        else if (a == "--block" && i + 1 < argc)
            block = std::max (1, std::atoi (argv[++i]));
        else
            return usage();
    }

    tools::WavData in;
    if (! tools::readWav (inPath, in))
    {
        std::fprintf (stderr, "cannot read %s\n", inPath.c_str());
        return 1;
    }
    if (in.channels > 2)
        in.data.resize (2);
    const auto out = tools::render (params, in.data, channels, in.sampleRate, block);
    tools::WavData w;
    w.sampleRate = in.sampleRate;
    w.channels = channels;
    w.data = out;
    if (! tools::writeWav (outPath, w))
    {
        std::fprintf (stderr, "cannot write %s\n", outPath.c_str());
        return 1;
    }
    std::printf ("rendered %zu samples, %d ch -> %s\n", out[0].size(), channels, outPath.c_str());
    return 0;
}
