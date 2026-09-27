// Acceptance tests for the Ambience DSP engine (PROMPT.md section 11).
// Self-contained: no external test framework. Exit code != 0 on failure.
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <tuple>

#include "../Source/dsp/AcousticModels.h"
#include "../Source/dsp/SpatialEngine.h"
#include "../tools/Render.h"
#include "../tools/TestSignals.h"

using namespace amb;
using namespace amb::tools;

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

void near (double actual, double expected, double tol, const std::string& what)
{
    char buf[256];
    std::snprintf (buf, sizeof (buf), "%s: got %.4f, expected %.4f +- %.4f", what.c_str(), actual, expected, tol);
    check (std::abs (actual - expected) <= tol, buf);
}

struct TestCase
{
    const char* name;
    std::function<void()> fn;
};

double maxAbsDiff (const std::vector<float>& a, const std::vector<float>& b, int from = 0)
{
    double m = 0;
    for (size_t i = (size_t) from; i < a.size(); ++i)
        m = std::max (m, (double) std::abs (a[i] - b[i]));
    return m;
}

double peak (const std::vector<float>& a)
{
    double m = 0;
    for (float v : a)
        m = std::max (m, (double) std::abs (v));
    return m;
}

EngineParams dryParams()
{
    EngineParams p;
    p.airAbsorption = false;
    return p;
}

double measureItdMs (const std::vector<float>& l, const std::vector<float>& r, double fs, int from)
{
    // Cross-correlation lag of R relative to L (positive: R later), +-1.5 ms search.
    const int maxLag = (int) (0.0015 * fs);
    double best = -1e30;
    int bestLag = 0;
    const int n = (int) l.size();
    for (int lag = -maxLag; lag <= maxLag; ++lag)
    {
        double s = 0;
        for (int i = from + maxLag; i < n - maxLag; ++i)
            s += (double) l[(size_t) i] * r[(size_t) (i + lag)];
        if (s > best)
        {
            best = s;
            bestLag = lag;
        }
    }
    // Parabolic refinement.
    auto corr = [&] (int lag)
    {
        double s = 0;
        for (int i = from + maxLag; i < n - maxLag; ++i)
            s += (double) l[(size_t) i] * r[(size_t) (i + lag)];
        return s;
    };
    const double ym = corr (bestLag - 1), y0 = corr (bestLag), yp = corr (bestLag + 1);
    const double frac = 0.5 * (ym - yp) / (ym - 2 * y0 + yp);
    return (bestLag + frac) / fs * 1000.0;
}

double octaveEnergy (const std::vector<float>& x, double fc, double fs)
{
    Biquad h1, h2, l1, l2;
    h1.set (designBiquadHz (BiquadType::highPass, fs, fc / std::sqrt (2.0), 0.707, 0));
    h2 = h1;
    l1.set (designBiquadHz (BiquadType::lowPass, fs, fc * std::sqrt (2.0), 0.707, 0));
    l2 = l1;
    double e = 0;
    for (float v : x)
    {
        const float y = l2.process (l1.process (h2.process (h1.process (v))));
        e += (double) y * y;
    }
    return e;
}

std::vector<float> lowpassed (const std::vector<float>& x, double fs, double fc)
{
    Biquad a, b;
    a.set (designBiquadHz (BiquadType::lowPass, fs, fc, 0.707, 0));
    b.set (designBiquadHz (BiquadType::lowPass, fs, fc, 0.707, 0));
    std::vector<float> y (x.size());
    for (size_t i = 0; i < x.size(); ++i)
        y[i] = b.process (a.process (x[i]));
    return y;
}

} // namespace

int main()
{
    const double fs = 48000.0;
    std::vector<TestCase> tests;

    //--------------------------------------------------------------------------
    tests.push_back ({ "1. Distance: doubling = -6.0 +- 0.1 dB", [&]
    {
        const auto x = sine (1000, fs, 48000);
        for (double r0 : { 1.0, 3.0, 20.0 })
        {
            auto p = dryParams();
            p.distance = (float) r0;
            const auto a = render (p, { x }, 1);
            p.distance = (float) (2 * r0);
            const auto b = render (p, { x }, 1);
            near (gainToDb (rms (b[0].data() + 4800, 40000) / rms (a[0].data() + 4800, 40000)), -6.0206, 0.1,
                  "doubling from " + std::to_string (r0) + " m");
        }
        auto p = dryParams();
        p.distance = 0.1f;
        p.recDistance = 10.0f;
        const auto c = render (p, { x }, 1);
        check (gainToDb (rms (c[0].data() + 4800, 40000) / rms (x.data() + 4800, 40000)) <= 24.01, "near gain clamped to +24 dB");
    } });

    tests.push_back ({ "2. Air absorption: ISO 9613-1 reference values", [&]
    {
        // ISO 9613-2 table (10 degC, 70 % RH): 3.7 / 9.7 / 32.8 / 117 dB/km at exact mid-band freqs.
        const double f[4] = { 1000.0, 1995.26, 3981.07, 7943.28 };
        const double ref[4] = { 3.7, 9.7, 32.8, 117.0 };
        for (int i = 0; i < 4; ++i)
        {
            const double a = models::airAbsorptionDbPerMetre (f[i], 10.0, 70.0) * 1000.0;
            near (a, ref[i], ref[i] * 0.02, "alpha(" + std::to_string ((int) f[i]) + " Hz)");
        }
        // Realised filter: <= 1 dB error wherever the attenuation is < 24 dB.
        for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
            for (double dist : { 10.0, 50.0, 150.0, 400.0, 1000.0 })
            {
                SpatialEngine e;
                e.prepare (sr, 512);
                EngineParams p;
                p.distance = (float) dist;
                e.setParams (p);
                std::vector<float> buf (64, 0.0f);
                const float* ip[1] = { buf.data() };
                float* op[1] = { buf.data() };
                e.process (ip, 1, op, 1, 64);
                double worst = 0;
                for (double fq : thirdOctaveGrid (sr))
                {
                    const double target = -models::airAbsorptionDbPerMetre (fq, 20.0, 50.0) * (dist - 1.0);
                    if (target > -24.0)
                        worst = std::max (worst, std::abs (e.airFilterDb (fq) - target));
                }
                check (worst <= 1.0, "air fit error " + std::to_string (worst) + " dB at " + std::to_string ((int) dist) + " m, fs "
                                         + std::to_string ((int) sr));
            }
    } });

    tests.push_back ({ "3. Headphones, Pan -100: ITD 0.66 ms, ILD", [&]
    {
        auto p = dryParams();
        p.renderMode = 1;
        p.pan = -100;
        const auto noise = lowpassed (pinkNoise (48000), fs, 1500);
        const auto out = render (p, { noise }, 2);
        const double itd = measureItdMs (lowpassed (out[0], fs, 1500), lowpassed (out[1], fs, 1500), fs, 2000);
        near (itd, 0.656, 0.02, "ITD (ms, right ear later)");

        const auto s200 = render (p, { sine (200, fs, 48000) }, 2);
        const double ild200 = gainToDb (rms (s200[0].data() + 4800, 40000) / rms (s200[1].data() + 4800, 40000));
        near (ild200, 0.0, 1.5, "ILD at 200 Hz");
        for (double f : { 4000.0, 8000.0 })
        {
            const auto s = render (p, { sine (f, fs, 48000) }, 2);
            const double ild = gainToDb (rms (s[0].data() + 4800, 40000) / rms (s[1].data() + 4800, 40000));
            check (ild >= 12.0, "ILD at " + std::to_string ((int) f) + " Hz >= 12 dB (got " + std::to_string (ild) + ")");
        }
    } });

    tests.push_back ({ "4. Pan law -3 dB: constant power", [&]
    {
        const auto x = sine (1000, fs, 24000);
        const double inP = rms (x.data() + 4800, 19000);
        for (int pan = -100; pan <= 100; pan += 10)
        {
            auto p = dryParams();
            p.timePan = 0;
            p.pan = (float) pan;
            const auto o = render (p, { x }, 2);
            const double l = rms (o[0].data() + 4800, 19000), r = rms (o[1].data() + 4800, 19000);
            near (10.0 * std::log10 ((l * l + r * r) / (inP * inP)), 0.0, 0.1, "L^2+R^2 at pan " + std::to_string (pan));
        }
    } });

    tests.push_back ({ "5. Mono-safe: mono sum deviation <= 3 dB per 1/3 octave", [&]
    {
        const int n = 48000 * 3;
        const auto x = pinkNoise (n);
        const auto centres = thirdOctaveGrid (fs, 20.0, 20000.0);
        for (int pan : { -100, -70, -40, -15, 10, 30, 60, 90 })
            for (float tp : { 50.0f, 100.0f })
                for (float w : { 100.0f, 200.0f })
                {
                    auto p = dryParams();
                    p.pan = (float) pan;
                    p.width = w;
                    p.timePan = tp;
                    const auto a = render (p, { x }, 2);
                    p.timePan = 0;
                    const auto b = render (p, { x }, 2);
                    std::vector<float> sa ((size_t) n), sb ((size_t) n);
                    for (int i = 0; i < n; ++i)
                    {
                        sa[(size_t) i] = a[0][(size_t) i] + a[1][(size_t) i];
                        sb[(size_t) i] = b[0][(size_t) i] + b[1][(size_t) i];
                    }
                    const auto ba = thirdOctaveBands (powerSpectrum (sa.data() + 4800, n - 4800, 16384), fs, 16384, centres);
                    const auto bb = thirdOctaveBands (powerSpectrum (sb.data() + 4800, n - 4800, 16384), fs, 16384, centres);
                    double worst = 0;
                    for (size_t k = 0; k < ba.size(); ++k)
                        if (bb[k] > 0 && ba[k] > 0)
                            worst = std::max (worst, std::abs (10.0 * std::log10 (ba[k] / bb[k])));
                    check (worst <= 3.0, "pan " + std::to_string (pan) + " time " + std::to_string ((int) tp) + " width "
                                             + std::to_string ((int) w) + ": " + std::to_string (worst) + " dB");
                }
    } });

    tests.push_back ({ "6. Width: L+R independent of Width (mono sources: magnitude); Width 0 -> L = R", [&]
    {
        const int n = 24000;
        const auto xl = pinkNoise (n, 1), xr = pinkNoise (n, 2);
        for (int stereo = 0; stereo < 2; ++stereo)
            for (int pan : { -100, -50, 0, 35, 100 })
            {
                auto p = dryParams();
                p.pan = (float) pan;
                std::vector<std::vector<float>> in = stereo ? std::vector<std::vector<float>> { xl, xr } : std::vector<std::vector<float>> { xl };
                p.width = 100;
                const auto ref = render (p, in, 2);
                for (float w : { 0.0f, 150.0f, 200.0f })
                {
                    p.width = w;
                    if (! stereo && w > 100.0f)
                    {
                        // Mono sources widen through the Stereoizer: L + R is an all-pass of the
                        // source, so its magnitude (not its waveform) is unchanged.
                        for (double f : { 100.0, 300.0, 1000.0, 3000.0, 8000.0 })
                        {
                            auto q = p;
                            const auto a = render (q, { sine (f, fs, 24000) }, 2);
                            q.width = 100;
                            const auto b = render (q, { sine (f, fs, 24000) }, 2);
                            std::vector<float> sa ((size_t) n), sb ((size_t) n);
                            for (int i = 0; i < n; ++i)
                            {
                                sa[(size_t) i] = a[0][(size_t) i] + a[1][(size_t) i];
                                sb[(size_t) i] = b[0][(size_t) i] + b[1][(size_t) i];
                            }
                            near (gainToDb (rms (sa.data() + 4800, n - 4800) / rms (sb.data() + 4800, n - 4800)), 0.0, 0.05,
                                  "mono source: mono sum level at " + std::to_string ((int) f) + " Hz (pan " + std::to_string (pan)
                                      + ", width " + std::to_string ((int) w) + ")");
                        }
                        continue;
                    }
                    const auto o = render (p, in, 2);
                    double m = 0;
                    for (int i = 4800; i < n; ++i)
                        m = std::max (m, (double) std::abs ((o[0][(size_t) i] + o[1][(size_t) i]) - (ref[0][(size_t) i] + ref[1][(size_t) i])));
                    check (gainToDb (m) <= -100.0, "mono sum change " + std::to_string (gainToDb (m)) + " dB (stereo " + std::to_string (stereo)
                                                       + ", pan " + std::to_string (pan) + ", width " + std::to_string ((int) w) + ")");
                }
            }
        auto p = dryParams();
        p.width = 0;
        const auto o = render (p, { xl, xr }, 2);
        check (maxAbsDiff (o[0], o[1]) < 1.0e-6, "width 0 at pan 0 gives L == R");
    } });

    tests.push_back ({ "7. Occlusion: mass-law slope, leak, amount 0 null", [&]
    {
        SpatialEngine e;
        e.prepare (fs, 256);
        EngineParams p = dryParams();
        p.obstacle = 2;
        p.material = 1; // glass 6 mm: mass-law region well below fc/2 = 1 kHz
        e.setParams (p);
        std::vector<float> buf (64, 0.0f);
        const float* ip[1] = { buf.data() };
        float* op[1] = { buf.data() };
        e.process (ip, 1, op, 1, 64);
        const double slope = (e.occlusionFilterDb (200.0) - e.occlusionFilterDb (800.0)) / 2.0;
        near (slope, 6.0, 1.5, "glass R(f) slope dB/octave (200-800 Hz)");
        // Target vs realised filter in the region that matters (within 45 dB of the minimum loss).
        for (int m = 0; m < models::numMaterials; ++m)
        {
            p.material = m;
            SpatialEngine em;
            em.prepare (fs, 256);
            em.setParams (p);
            em.process (ip, 1, op, 1, 64);
            double worst = 0, best = -1e9;
            for (double f : thirdOctaveGrid (fs, 50, 10000))
                best = std::max (best, -models::transmissionLossDb (m, f, 1.0, 0.0));
            for (double f : thirdOctaveGrid (fs, 50, 10000))
            {
                const double t = -models::transmissionLossDb (m, f, 1.0, 0.0);
                if (t > best - 30.0)
                    worst = std::max (worst, std::abs (em.occlusionFilterDb (f) - t));
            }
            check (worst <= 3.0, std::string ("material fit ") + models::material (m).name + ": " + std::to_string (worst) + " dB");
        }

        p.material = 3;
        p.leak = 0;
        SpatialEngine e1;
        e1.prepare (fs, 256);
        e1.setParams (p);
        e1.process (ip, 1, op, 1, 64);
        p.leak = 50;
        SpatialEngine e2;
        e2.prepare (fs, 256);
        e2.setParams (p);
        e2.process (ip, 1, op, 1, 64);
        check (e2.occlusionFilterDb (4000) > e1.occlusionFilterDb (4000) + 3.0, "leak raises high frequencies");

        // Amount 0 % == no obstacle (null test).
        const auto x = pinkNoise (24000);
        auto q = EngineParams();
        const auto ref = render (q, { x }, 2);
        q.obstacle = 2;
        q.amount = 0;
        const auto o = render (q, { x }, 2);
        check (gainToDb (std::max (maxAbsDiff (o[0], ref[0]), maxAbsDiff (o[1], ref[1]))) <= -120.0, "amount 0 null test");
    } });

    tests.push_back ({ "8. Obstruction follows Maekawa (+-1.5 dB, 125 Hz - 8 kHz)", [&]
    {
        for (float delta : { 0.0f, 0.3f, 1.0f, 3.0f })
        {
            SpatialEngine e;
            e.prepare (fs, 256);
            EngineParams p = dryParams();
            p.obstacle = 1;
            p.obstacleSize = delta;
            e.setParams (p);
            std::vector<float> buf (64, 0.0f);
            const float* ip[1] = { buf.data() };
            float* op[1] = { buf.data() };
            e.process (ip, 1, op, 1, 64);
            double worst = 0;
            for (double f : thirdOctaveGrid (fs, 125, 8000))
                worst = std::max (worst, std::abs (e.obstacleFilterDb (f) + models::maekawaAttenuationDb (f, delta)));
            check (worst <= 1.5, "delta " + std::to_string (delta) + " m: error " + std::to_string (worst) + " dB");
        }
    } });

    tests.push_back ({ "9. Automation: no discontinuities, NaN or Inf", [&]
    {
        const int n = 48000 * 2;
        const auto x = sine (1000, fs, n, 0.5f);
        std::vector<std::pair<int, EngineParams>> autom;
        for (int b = 0; b < n; b += 256)
        {
            EngineParams p;
            const double t = (double) b / n;
            p.pan = (float) (100.0 * std::sin (kTwoPi * 3.0 * t));
            p.distance = (float) std::pow (10.0, 2.0 * (0.5 + 0.5 * std::sin (kTwoPi * 2.0 * t)));
            p.obstacle = 2;
            p.amount = (float) (100.0 * (0.5 + 0.5 * std::sin (kTwoPi * 4.0 * t)));
            p.material = (b / 12000) % models::numMaterials;
            p.leak = (float) (50.0 * t);
            p.autoGain = true;
            p.propDelay = (b / 30000) % 2 == 1;
            autom.push_back ({ b, p });
        }
        const auto out = render (autom[0].second, { x }, 2, fs, 256, &autom);
        bool finite = true;
        double maxStep = 0, maxAmp = 0;
        for (int c = 0; c < 2; ++c)
            for (int i = 1; i < n; ++i)
            {
                const float v = out[(size_t) c][(size_t) i];
                if (! std::isfinite (v))
                    finite = false;
                maxAmp = std::max (maxAmp, (double) std::abs (v));
                maxStep = std::max (maxStep, (double) std::abs (v - out[(size_t) c][(size_t) (i - 1)]));
            }
        check (finite, "output finite");
        // A 1 kHz sine at fs 48 kHz changes at most 2*pi*1000/48000 = 0.131 x amplitude per sample.
        check (maxStep <= 0.2 * std::max (maxAmp, 1.0e-9) + 1.0e-6,
               "max step " + std::to_string (maxStep) + " vs peak " + std::to_string (maxAmp));
    } });

    tests.push_back ({ "10. Sample-rate independence (+-0.5 dB to 16 kHz)", [&]
    {
        auto responses = [] (double sr)
        {
            SpatialEngine e;
            e.prepare (sr, 256);
            EngineParams p;
            p.distance = 120;
            p.obstacle = 2;
            p.material = 4;
            e.setParams (p);
            std::vector<float> buf (64, 0.0f);
            const float* ip[1] = { buf.data() };
            float* op[1] = { buf.data() };
            e.process (ip, 1, op, 1, 64);
            std::vector<double> v;
            for (double f : thirdOctaveGrid (48000.0, 25, 16000))
                v.push_back (e.airFilterDb (f) + e.occlusionFilterDb (f));
            return v;
        };
        const auto ref = responses (48000.0);
        for (double sr : { 44100.0, 96000.0, 192000.0 })
        {
            const auto v = responses (sr);
            const auto g = thirdOctaveGrid (48000.0, 25, 16000);
            double worst = 0;
            for (size_t k = 0; k < v.size(); ++k)
                if (ref[k] > -60.0) // audible region
                    worst = std::max (worst, std::abs (v[k] - ref[k]));
            check (worst <= 0.5, "fs " + std::to_string ((int) sr) + ": max deviation " + std::to_string (worst) + " dB");
        }
    } });

    tests.push_back ({ "11. Block-size independence and determinism", [&]
    {
        const int n = 48000;
        const auto xl = pinkNoise (n, 5), xr = pinkNoise (n, 6);
        EngineParams p;
        p.distance = 12;
        p.pan = -40;
        p.room = 10;
        p.obstacle = 2;
        p.material = 4;
        p.leak = 20;
        p.width = 160;
        p.micPattern = 1;
        p.recDistance = 0.4f;
        p.autoGain = true;
        p.bassMono = 3;
        const auto a = render (p, { xl, xr }, 2, fs, 32);
        const auto b = render (p, { xl, xr }, 2, fs, 1024);
        const auto c = render (p, { xl, xr }, 2, fs, 97);
        const auto d = render (p, { xl, xr }, 2, fs, 1024);
        const double diff = std::max ({ maxAbsDiff (a[0], b[0]), maxAbsDiff (a[1], b[1]), maxAbsDiff (a[0], c[0]), maxAbsDiff (a[1], c[1]) });
        check (diff == 0.0 || gainToDb (diff) <= -120.0, "32 vs 1024 vs 97 sample blocks: " + std::to_string (gainToDb (diff)) + " dBFS");
        check (maxAbsDiff (b[0], d[0]) == 0.0 && maxAbsDiff (b[1], d[1]) == 0.0, "identical re-render");
    } });

    tests.push_back ({ "12. Factory presets render finite, non-silent audio", [&]
    {
        const auto x = pinkNoise (48000);
        for (const auto& preset : factoryPresets())
        {
            const auto p = paramsForPreset (preset);
            const auto o = render (p, { x, x }, 2);
            bool finite = true;
            for (auto& ch : o)
                for (float v : ch)
                    finite = finite && std::isfinite (v);
            check (finite, std::string (preset.name) + ": finite");
            check (rms (o[0].data() + 24000, 24000) > 1.0e-5, std::string (preset.name) + ": not silent");
            check (! p.autoGain && p.output == 0.0f, std::string (preset.name) + ": auto gain off, output 0 dB");
        }
    } });

    tests.push_back ({ "13. Neutral defaults and zero latency", [&]
    {
        const auto xl = pinkNoise (24000, 11), xr = pinkNoise (24000, 12);
        EngineParams p;
        const auto st = render (p, { xl, xr }, 2);
        check (gainToDb (std::max (maxAbsDiff (st[0], xl), maxAbsDiff (st[1], xr))) <= -120.0, "stereo->stereo null test");
        const auto mm = render (p, { xl }, 1);
        check (gainToDb (maxAbsDiff (mm[0], xl)) <= -120.0, "mono->mono null test");
        const auto ms = render (p, { xl }, 2);
        std::vector<float> half (xl.size());
        for (size_t i = 0; i < xl.size(); ++i)
            half[i] = xl[i] * 0.70710678f;
        check (gainToDb (std::max (maxAbsDiff (ms[0], half), maxAbsDiff (ms[1], half))) <= -120.0, "mono->stereo = -3 dB pan law");
        SpatialEngine e;
        e.prepare (fs, 256);
        check (e.getLatencySamples() == 0, "latency 0");
    } });

    tests.push_back ({ "14. Auto gain: off by default, compensates loudness when on", [&]
    {
        const auto x = pinkNoise (48000 * 2);
        EngineParams p;
        p.distance = 4;
        p.obstacle = 2;
        p.material = 0;
        const auto off = render (p, { x }, 2);
        p.autoGain = true;
        const auto on = render (p, { x }, 2);
        const double inR = rms (x.data() + 48000, 48000);
        const double offR = std::sqrt (0.5 * (std::pow (rms (off[0].data() + 48000, 48000), 2) + std::pow (rms (off[1].data() + 48000, 48000), 2)));
        const double onR = std::sqrt (0.5 * (std::pow (rms (on[0].data() + 48000, 48000), 2) + std::pow (rms (on[1].data() + 48000, 48000), 2)));
        check (gainToDb (offR * std::sqrt (2.0) / inR) < -12.0, "without auto gain the distance+curtain loss is large");
        // Pink-noise loudness is restored to within a few dB (K-weighting vs plain RMS differ slightly).
        near (gainToDb (onR * std::sqrt (2.0) / inR), 0.0, 3.0, "auto gain restores level (RMS proxy)");

        // With rooms: K-weighted (BS.1770) loudness of the output equals the input within 1 dB.
        auto kLoudness = [] (const std::vector<float>& y, int from)
        {
            Biquad s1, s2;
            s1.set ({ 1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585 });
            s2.set ({ 1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621 });
            double e = 0;
            for (size_t i = 0; i < y.size(); ++i)
            {
                const float v = s2.process (s1.process (y[i]));
                if ((int) i >= from)
                    e += (double) v * v;
            }
            return e;
        };
        const auto y = pinkNoise (48000 * 4);
        for (int room : { 1, 4, 7, 10, 13, 15 })
            for (float dist : { 2.0f, 12.0f })
            {
                EngineParams q;
                q.room = room;
                q.distance = dist;
                q.autoGain = true;
                q.autoGainRange = 36;
                const auto o = render (q, { y }, 2);
                const double change = 10.0 * std::log10 ((kLoudness (o[0], 96000) + kLoudness (o[1], 96000)) / kLoudness (y, 96000));
                near (change, 0.0, 1.0, std::string ("auto gain with ") + roomSpec (room).name + " at " + std::to_string ((int) dist) + " m");
            }
    } });

    tests.push_back ({ "14b. All 16 room variants render; v1 room migration", [&]
    {
        const auto x = pinkNoise (48000);
        for (int room = 0; room < numRoomTypes; ++room)
        {
            EngineParams p;
            p.room = room;
            p.distance = 6;
            const auto o = render (p, { x }, 2);
            bool finite = true;
            for (auto& ch : o)
                for (float v : ch)
                    finite = finite && std::isfinite (v);
            const double level = gainToDb (rms (o[0].data() + 24000, 24000) / rms (x.data() + 24000, 24000));
            check (finite && level > -40.0 && level < 12.0,
                   std::string (roomSpec (room).name) + ": finite, level " + std::to_string (level) + " dB");
        }
        // Tail length ordering within a size group: B (damped) < A < C (live).
        for (int base : { 4, 7, 10, 13 })
            check (roomSpec (base + 1).t60 < roomSpec (base).t60 && roomSpec (base).t60 < roomSpec (base + 2).t60,
                   std::string ("T60 order B < A < C for ") + roomSpec (base).name);
        check (migrateRoomIndexV1 (0) == 0 && migrateRoomIndexV1 (1) == 1 && migrateRoomIndexV1 (2) == 4 && migrateRoomIndexV1 (3) == 7
                   && migrateRoomIndexV1 (4) == 10 && migrateRoomIndexV1 (5) == 13,
               "v1 -> v2 room index migration");
    } });

    tests.push_back ({ "14c. Mono Depth: identical channels, depth cues kept, stereo-equivalent energy", [&]
    {
        const auto x = pinkNoise (48000 * 2, 21);
        const auto x2 = pinkNoise (48000 * 2, 22);
        for (int stereoIn = 0; stereoIn < 2; ++stereoIn)
        {
            EngineParams p;
            p.monoDepth = true;
            p.pan = -60;
            p.width = 180;
            p.timePan = 100;
            p.monoSafe = false;
            p.renderMode = 1;
            p.distance = 7;
            p.room = 8;
            p.obstacle = 2;
            p.material = 4;
            std::vector<std::vector<float>> in = stereoIn ? std::vector<std::vector<float>> { x, x2 } : std::vector<std::vector<float>> { x };
            const auto o = render (p, in, 2);
            check (maxAbsDiff (o[0], o[1]) == 0.0, "L == R (stereo input " + std::to_string (stereoIn) + ")");
        }
        // Neutral settings: same level as a centred source (-3 dB pan law per channel).
        {
            EngineParams p;
            p.monoDepth = true;
            const auto o = render (p, { x }, 2);
            std::vector<float> half (x.size());
            for (size_t i = 0; i < x.size(); ++i)
                half[i] = x[i] * 0.70710678f;
            check (gainToDb (std::max (maxAbsDiff (o[0], half), maxAbsDiff (o[1], half))) <= -120.0, "neutral mono depth = centred source");
        }
        // Distance still attenuates and adds room: compare with the stereo render at centre.
        {
            EngineParams p;
            p.distance = 8;
            p.room = 7;
            const auto st = render (p, { x }, 2);
            p.monoDepth = true;
            const auto md = render (p, { x }, 2);
            auto power = [] (const std::vector<std::vector<float>>& o)
            { return std::pow (rms (o[0].data() + 48000, 48000), 2) + std::pow (rms (o[1].data() + 48000, 48000), 2); };
            near (10.0 * std::log10 (power (md) / power (st)), 0.0, 1.5, "mono depth energy vs stereo render (dB)");
            EngineParams dry;
            dry.monoDepth = true;
            dry.distance = 8;
            const auto d = render (dry, { x }, 2);
            // 1 m -> 8 m without room: -18.1 dB direct level (+ -3 dB pan law per channel).
            near (gainToDb (rms (d[0].data() + 48000, 48000) / rms (x.data() + 48000, 48000)), -18.06 - 3.01, 0.3, "mono depth distance attenuation");
        }
        // Mono track (1 -> 1) uses the same mono room output.
        {
            EngineParams p;
            p.distance = 8;
            p.room = 7;
            const auto mono = render (p, { x }, 1);
            p.monoDepth = true;
            const auto md = render (p, { x }, 2);
            double m = 0;
            for (size_t i = 0; i < x.size(); ++i)
                m = std::max (m, (double) std::abs (mono[0][i] * 0.70710678f - md[0][i]));
            check (gainToDb (m) <= -100.0, "1->1 equals mono depth (scaled by the pan law)");
        }
    } });

    tests.push_back ({ "14d. Equalized Ambience: on by default, off removes the automatic EQ", [&]
    {
        EngineParams def;
        check (def.eqAmbience, "on by default");
        // Distance 60 m, pan right: with EQ the high end is darker than without; level at 500 Hz ~equal.
        auto band = [&] (bool eq, double f, int ch)
        {
            EngineParams p;
            p.distance = 60;
            p.pan = 60;
            p.timePan = 0;
            p.eqAmbience = eq;
            const auto o = render (p, { sine (f, fs, 48000) }, 2);
            return gainToDb (rms (o[(size_t) ch].data() + 9600, 38000));
        };
        const double hiOn = band (true, 8000, 0), hiOff = band (false, 8000, 0);
        const double loOn = band (true, 500, 1), loOff = band (false, 500, 1);
        check (hiOn < hiOff - 3.0, "EQ on: 8 kHz darker (" + std::to_string (hiOn - hiOff) + " dB)");
        near (loOn - loOff, 0.0, 0.5, "EQ does not change level at 500 Hz");
        // Off: no tonal change between 200 Hz and 8 kHz on the near channel at 60 m.
        const double flat = band (false, 8000, 1) - band (false, 200, 1);
        near (flat, 0.0, 0.3, "EQ off: flat response");
        // Proximity compensation is part of the EQ.
        EngineParams p;
        p.recDistance = 0.3f;
        p.distance = 3;
        p.micPattern = 1;
        const auto on = render (p, { sine (60, fs, 48000) }, 1);
        p.eqAmbience = false;
        const auto off = render (p, { sine (60, fs, 48000) }, 1);
        check (rms (on[0].data() + 9600, 38000) < rms (off[0].data() + 9600, 38000) * 0.8, "proximity bass removed only with EQ on");
    } });

    tests.push_back ({ "14e. Mono to Stereo: exact mono sum, no tonal change or lean, decorrelated, mono content only", [&]
    {
        EngineParams def;
        check (! def.monoToStereo, "off by default");
        auto corr = [] (const std::vector<float>& a, const std::vector<float>& b, int from)
        {
            double ab = 0, aa = 0, bb = 0;
            for (size_t i = (size_t) from; i < a.size(); ++i)
            {
                ab += (double) a[i] * b[i];
                aa += (double) a[i] * a[i];
                bb += (double) b[i] * b[i];
            }
            return ab / std::sqrt (aa * bb + 1.0e-30);
        };
        auto p = dryParams();
        p.monoToStereo = true;
        p.keepMono = false; // full-signal widening (Keep Mono: test 14g)

        // Mono sum = the Stereoizer's all-pass mid at the centre pan-law gain, sample-exact.
        const auto x = pinkNoise (96000);
        const auto o = render (p, { x }, 2);
        Stereoizer ref;
        ref.prepare (fs);
        double worst = 0, sumSq = 0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            float m, sd;
            ref.process (x[i], m, sd);
            const double expected = 2.0 * std::cos (kPi / 4.0) * m;
            worst = std::max (worst, std::abs (o[0][i] + o[1][i] - expected));
            sumSq += expected * expected;
        }
        check (gainToDb (worst / std::sqrt (sumSq / (double) x.size())) <= -100.0, "L + R = all-pass of the input (mono compatible): "
               + std::to_string (gainToDb (worst / std::sqrt (sumSq / (double) x.size()))) + " dB");

        // Per frequency: L and R equally loud (no lean), close to the original level, bass stays mono.
        for (double f : { 60.0, 150.0, 300.0, 600.0, 1000.0, 2000.0, 3000.0, 5000.0, 8000.0, 12000.0 })
        {
            const auto t = render (p, { sine (f, fs, 48000) }, 2);
            const double l = gainToDb (rms (t[0].data() + 9600, 38000)), r = gainToDb (rms (t[1].data() + 9600, 38000));
            const double orig = gainToDb (0.5 / std::sqrt (2.0) * std::cos (kPi / 4.0));
            near (l - r, 0.0, 0.25, "L/R balance at " + std::to_string ((int) f) + " Hz");
            check (l - orig > -0.1 && l - orig < 1.8, "level change at " + std::to_string ((int) f) + " Hz: " + std::to_string (l - orig) + " dB");
            if (f <= 60.0)
                check (gainToDb (maxAbsDiff (t[0], t[1], 9600) / 0.35) < -30.0, "bass stays mono");
        }

        // Decorrelation grows with Width; Width 0 keeps L == R.
        const double c100 = corr (o[0], o[1], 4800);
        p.width = 200;
        const auto wide = render (p, { x }, 2);
        const double c200 = corr (wide[0], wide[1], 4800);
        p.width = 0;
        const auto narrow = render (p, { x }, 2);
        std::printf ("    correlation (pink noise): width 100 %% = %.2f, 200 %% = %.2f\n", c100, c200);
        check (c100 < 0.92 && c200 < c100 - 0.1 && c200 > 0.0, "decorrelated, stronger with width, no phase inversion");
        check (maxAbsDiff (narrow[0], narrow[1]) == 0.0, "width 0: L == R");
        p.width = 100;

        // Stereo input with mono content (dual-mono clip on a stereo track) is converted as well.
        const auto dual = render (p, { x, x }, 2);
        check (corr (dual[0], dual[1], 9600) < 0.92, "dual-mono on a stereo input is converted");

        // True stereo content is left untouched (null test against the switch off).
        const auto y = pinkNoise (96000, 4242u);
        const auto st = render (p, { x, y }, 2);
        p.monoToStereo = false;
        const auto stOff = render (p, { x, y }, 2);
        p.monoToStereo = true;
        check (gainToDb (std::max (maxAbsDiff (st[0], stOff[0]), maxAbsDiff (st[1], stOff[1]))) <= -120.0, "stereo content unchanged");

        // Mono Depth (mono output) takes precedence: identical channels.
        p.monoDepth = true;
        const auto md = render (p, { x }, 2);
        check (maxAbsDiff (md[0], md[1]) == 0.0, "Mono Depth wins: L == R");
        p.monoDepth = false;

        // Toggling while playing is click-free and finite.
        std::vector<std::pair<int, EngineParams>> automation;
        auto on = p, off = p;
        off.monoToStereo = false;
        automation.push_back ({ 24000, off });
        automation.push_back ({ 48000, on });
        const auto sw = render (p, { sine (440, fs, 96000) }, 2, fs, 256, &automation);
        double maxStep = 0;
        for (size_t i = 1; i < sw[0].size(); ++i)
            maxStep = std::max (maxStep, (double) std::abs (sw[0][i] - sw[0][i - 1]));
        check (maxStep < 0.05, "no clicks when toggling (max step " + std::to_string (maxStep) + ")");
    } });

    tests.push_back ({ "14f. Room acoustics: frequency-dependent decay, dense tail, no comb filtering", [&]
    {
        auto octave = [&] (const std::vector<float>& x, double fc)
        {
            Biquad h1, h2, l1, l2;
            h1.set (designBiquadHz (BiquadType::highPass, fs, fc / std::sqrt (2.0), 0.707, 0));
            h2 = h1;
            l1.set (designBiquadHz (BiquadType::lowPass, fs, std::min (fc * std::sqrt (2.0), 0.45 * fs), 0.707, 0));
            l2 = l1;
            std::vector<float> y (x.size());
            for (size_t i = 0; i < x.size(); ++i)
                y[i] = l2.process (l1.process (h2.process (h1.process (x[i]))));
            return y;
        };
        auto t30 = [&] (const std::vector<float>& l, const std::vector<float>& r)
        {
            std::vector<double> edc (l.size());
            double acc = 0;
            for (size_t i = l.size(); i-- > 0;)
                edc[i] = acc += (double) l[i] * l[i] + (double) r[i] * r[i];
            int i5 = -1, i35 = -1;
            for (size_t i = 0; i < edc.size() && i35 < 0; ++i)
            {
                const double db = 10.0 * std::log10 (edc[i] / edc[0] + 1.0e-30);
                if (i5 < 0 && db <= -5.0) i5 = (int) i;
                if (db <= -35.0) i35 = (int) i;
            }
            return i5 < 0 || i35 < 0 ? -1.0 : 2.0 * (i35 - i5) / fs;
        };
        // Normalised echo density (Abel & Huang 2006) in a 20 ms window.
        auto ned = [&] (const std::vector<float>& h, double t0)
        {
            const int a = (int) (t0 * fs), b = a + (int) (0.02 * fs);
            double e = 0;
            for (int i = a; i < b; ++i)
                e += (double) h[(size_t) i] * h[(size_t) i];
            const double sd = std::sqrt (e / (b - a));
            int count = 0;
            for (int i = a; i < b; ++i)
                count += std::abs (h[(size_t) i]) > sd ? 1 : 0;
            return count / (double) (b - a) / 0.3173;
        };
        auto bandEnergy = [&] (const std::vector<float>& x, double fc, int a, int b)
        {
            const auto y = octave (x, fc);
            double e = 0;
            for (int i = a; i < b; ++i)
                e += (double) y[(size_t) i] * y[(size_t) i];
            return e;
        };
        for (int room = 0; room < numRoomTypes; ++room)
        {
            if (! roomHasTail (room))
                continue;
            RoomModel rm;
            rm.prepare (fs);
            rm.configure (room, 1.0);
            rm.setGeometry (3.0, 0.3, 1.0, 1.0, 1.0, 32);
            rm.snapGeometry();
            const int n = (int) (fs * (roomSpec (room).t60 * 2.0 + 0.3));
            std::vector<float> l ((size_t) n), r ((size_t) n);
            for (int i = 0; i < n; ++i)
                rm.process (i == 0 ? 1.0f : 0.0f, l[(size_t) i], r[(size_t) i]);
            const std::string name = roomSpec (room).name;
            const double t500 = t30 (octave (l, 500), octave (r, 500)), t8k = t30 (octave (l, 8000), octave (r, 8000));
            const bool closed = roomSpec (room).kind == RoomKind::room; // open spaces: reflections dominate
            if (closed)
                near (t500 / roomSpec (room).t60, 1.0, 0.12, name + ": T30 at 500 Hz matches the room T60");
            check (t8k < 0.8 * t500, name + ": high frequencies decay faster (8 kHz " + std::to_string (t8k) + " s)");
            // Dense, smooth tail: no audible discrete echoes once the reverb has built up.
            check (ned (l, 0.2) > 0.75, name + ": echo density at 200 ms " + std::to_string (ned (l, 0.2)));
            if (roomSpec (room).kind == RoomKind::room)
                check (ned (l, 0.08) > 0.75, name + ": echo density at 80 ms " + std::to_string (ned (l, 0.08)));
            // No white "hiss": the 8 kHz octave carries less tail energy than the 1 kHz octave.
            const int a = (int) (0.1 * fs), b = std::min (n, (int) (0.6 * fs));
            if (closed)
                check (bandEnergy (l, 8000, a, b) < bandEnergy (l, 1000, a, b), name + ": tail darker at 8 kHz than at 1 kHz");
        }
        {
            // Church: air absorption dominates the top octaves (ISO 9613-1): T30(8 kHz) well below 2 s;
            // stone and volume give a longer bass decay (bass ratio ~1.3).
            RoomModel rm;
            rm.prepare (fs);
            rm.configure (15, 1.0);
            rm.setGeometry (3.0, 0.0, 1.0, 1.0, 1.0, 32);
            rm.snapGeometry();
            const int n = (int) (fs * 8.5);
            std::vector<float> l ((size_t) n), r ((size_t) n);
            for (int i = 0; i < n; ++i)
                rm.process (i == 0 ? 1.0f : 0.0f, l[(size_t) i], r[(size_t) i]);
            const double t8k = t30 (octave (l, 8000), octave (r, 8000)), t16k = t30 (octave (l, 16000), octave (r, 16000));
            check (t8k < 1.8 && t16k < 1.1, "church: T30 8 kHz " + std::to_string (t8k) + " s, 16 kHz " + std::to_string (t16k) + " s");
            const double t125 = t30 (octave (l, 125), octave (r, 125)), t500 = t30 (octave (l, 500), octave (r, 500));
            check (t125 > 1.15 * t500, "church: bass ratio " + std::to_string (t125 / t500));
        }
        // Comb filtering (phaser/flanger colour): whitened autocorrelation of the first 40 ms of
        // the full engine response at lags 0.3-15 ms. One reflection of relative gain a gives
        // a / (1 + a^2), i.e. 0.5 for a full-depth comb.
        auto comb = [&] (EngineParams p)
        {
            std::vector<float> x (48000, 0.0f);
            x[0] = 1.0f;
            const auto o = render (p, { x }, 2);
            double worst = 0;
            const int N = (int) (0.04 * fs);
            for (int ch = 0; ch < 2; ++ch)
            {
                std::vector<double> h ((size_t) N);
                for (int i = 0; i < N; ++i)
                    h[(size_t) i] = o[(size_t) ch][(size_t) i] - (i > 0 ? o[(size_t) ch][(size_t) (i - 1)] : 0.0f);
                double r0 = 0;
                for (double v : h)
                    r0 += v * v;
                for (int lag = (int) (0.0003 * fs); lag < (int) (0.015 * fs); ++lag)
                {
                    double c = 0;
                    for (int i = 0; i + lag < N; ++i)
                        c += h[(size_t) i] * h[(size_t) (i + lag)];
                    worst = std::max (worst, std::abs (c) / r0);
                }
            }
            return worst;
        };
        for (auto [room, dist, limit] : { std::tuple { 1, 10.0f, 0.08 }, std::tuple { 1, 50.0f, 0.15 }, std::tuple { 7, 2.0f, 0.1 },
                                          std::tuple { 15, 2.0f, 0.08 }, std::tuple { 15, 10.0f, 0.12 } })
        {
            EngineParams p;
            p.room = room;
            p.distance = dist;
            p.pan = 20;
            const double c = comb (p);
            check (c < limit, std::string (roomSpec (room).name) + " at " + std::to_string ((int) dist) + " m: comb strength " + std::to_string (c));
        }
        // Mono widening above 100 % keeps each channel flat (no comb from a decorrelator).
        EngineParams p = dryParams();
        p.width = 200;
        double lo = 1e9, hi = -1e9;
        for (double f : { 200.0, 400.0, 700.0, 1000.0, 1500.0, 2500.0, 4000.0, 6000.0, 9000.0 })
        {
            const auto t = render (p, { sine (f, fs, 24000) }, 2);
            const double l = gainToDb (rms (t[0].data() + 9600, 14000));
            lo = std::min (lo, l);
            hi = std::max (hi, l);
        }
        check (hi - lo < 3.5, "mono at width 200 %: per-channel response within " + std::to_string (hi - lo) + " dB");
    } });

    tests.push_back ({ "14g. Keep Mono: the voice stays mono, the recorded ambience becomes stereo", [&]
    {
        EngineParams def;
        check (def.keepMono, "Keep Mono on by default");
        // Synthetic location dialogue: 100 ms "syllables" every 500 ms, a diffuse reverberant tail
        // (independent noise, T60 0.6 s, -8 dB) and a steady room tone at -40 dB.
        const int n = 48000 * 8;
        const auto dry = pinkNoise (n, 11u), rev = pinkNoise (n, 22u), tone = pinkNoise (n, 33u);
        std::vector<float> x ((size_t) n);
        const int period = 24000, burst = 4800;
        for (int i = 0; i < n; ++i)
        {
            const int t = i % period;
            const double onset = std::min (1.0, t / 240.0);
            const double direct = t < burst ? onset : 0.0;
            const double tail = 0.4 * std::min (1.0, t / 2400.0) * std::exp (-6.91 * std::max (0, t - burst) / (0.6 * fs));
            x[(size_t) i] = (float) (0.3 * (direct * dry[(size_t) i] + tail * rev[(size_t) i]) + 0.003 * tone[(size_t) i]);
        }
        auto segmentCorrelation = [&] (const std::vector<std::vector<float>>& o, int from, int to)
        {
            double lr = 0, ll = 0, rr = 0;
            for (int start = 2 * period; start + period <= n; start += period)
                for (int i = start + from; i < start + to; ++i)
                {
                    lr += (double) o[0][(size_t) i] * o[1][(size_t) i];
                    ll += (double) o[0][(size_t) i] * o[0][(size_t) i];
                    rr += (double) o[1][(size_t) i] * o[1][(size_t) i];
                }
            return lr / std::sqrt (ll * rr + 1.0e-30);
        };
        auto p = dryParams();
        p.monoToStereo = true;
        p.keepMono = true;
        const auto keep = render (p, { x }, 2);
        p.keepMono = false;
        const auto full = render (p, { x }, 2);
        const double keepVoice = segmentCorrelation (keep, 480, burst - 480), keepTail = segmentCorrelation (keep, 9600, 19200);
        const double fullVoice = segmentCorrelation (full, 480, burst - 480), fullTail = segmentCorrelation (full, 9600, 19200);
        std::printf ("    L/R correlation: Keep Mono voice %.2f, ambience %.2f; full widening voice %.2f, ambience %.2f\n", keepVoice,
                     keepTail, fullVoice, fullTail);
        check (keepVoice > 0.93, "Keep Mono: voice stays (nearly) mono");
        check (keepTail < fullTail + 0.06, "Keep Mono: recorded ambience is widened (nearly) as much as with full widening");
        check (fullVoice < keepVoice - 0.1, "without Keep Mono the voice is widened too");
        // Mono compatibility is unchanged: L + R is the all-pass mid in both modes.
        double worst = 0, e = 0;
        for (int i = 0; i < n; ++i)
        {
            worst = std::max (worst, (double) std::abs ((keep[0][(size_t) i] + keep[1][(size_t) i]) - (full[0][(size_t) i] + full[1][(size_t) i])));
            e += (double) x[(size_t) i] * x[(size_t) i];
        }
        check (gainToDb (worst / std::sqrt (e / n)) < -100.0, "Keep Mono does not change the mono sum");
    } });

    tests.push_back ({ "14h. Headphones: binaural reflections, coherent low-frequency tail", [&]
    {
        // A reflection from the left: arrives at the right ear ~0.66 ms later, darker (head shadow).
        RoomModel rm;
        rm.prepare (fs);
        rm.configure (1, 1.0); // open field: a single ground reflection
        rm.setBinaural (1.0);
        rm.setGeometry (10.0, -kPi / 2, 1.0, 1.0, 1.0, 32);
        rm.snapGeometry();
        const int n = 4800;
        std::vector<float> l ((size_t) n), r ((size_t) n);
        for (int i = 0; i < n; ++i)
            rm.process (i == 0 ? 1.0f : 0.0f, l[(size_t) i], r[(size_t) i]);
        // ITD in the ITD-dominant band (duplex theory), as for the direct sound in test 3.
        near (measureItdMs (lowpassed (l, fs, 700.0), lowpassed (r, fs, 700.0), fs, 0), 0.656, 0.05, "reflection ITD below 700 Hz (right ear later)");
        const auto hl = octaveEnergy (l, 4000.0, fs), hr = octaveEnergy (r, 4000.0, fs);
        check (10.0 * std::log10 (hl / hr) > 4.0, "reflection ILD at 4 kHz: " + std::to_string (10.0 * std::log10 (hl / hr)) + " dB");

        // Tail: interaural coherence below ~200 Hz in headphones, decorrelated on loudspeakers.
        auto lowCorr = [&] (double rho)
        {
            RoomModel room;
            room.prepare (fs);
            room.configure (10, 1.0);
            room.setBinaural (rho);
            room.setGeometry (5.0, 0.0, 1.0, 1.0, 1.0, 32);
            room.snapGeometry();
            const auto noise = pinkNoise (48000 * 3);
            std::vector<float> a (noise.size()), b (noise.size());
            for (size_t i = 0; i < noise.size(); ++i)
                room.process (noise[i], a[i], b[i]);
            const auto la = lowpassed (lowpassed (a, fs, 150.0), fs, 150.0), lb = lowpassed (lowpassed (b, fs, 150.0), fs, 150.0);
            double ab = 0, aa = 0, bb = 0;
            for (size_t i = 48000; i < la.size(); ++i)
            {
                ab += (double) la[i] * lb[i];
                aa += (double) la[i] * la[i];
                bb += (double) lb[i] * lb[i];
            }
            return ab / std::sqrt (aa * bb);
        };
        const double spk = lowCorr (0.0), hp = lowCorr (1.0);
        std::printf ("    tail L/R correlation below 150 Hz: loudspeakers %.2f, headphones %.2f\n", spk, hp);
        check (hp > 0.95 && hp > spk + 0.15, "headphones: coherent low-frequency tail");
    } });

    tests.push_back ({ "15. CPU benchmark (informative, processing only)", [&]
    {
        const int n = 48000 * 10, block = 512;
        const auto x = pinkNoise (n);
        std::vector<float> l ((size_t) block), r ((size_t) block);
        for (int config = 0; config < 3; ++config)
        {
            EngineParams p;
            p.distance = 10;
            p.pan = 30;
            p.room = config == 1 ? 7 : 0;
            p.monoToStereo = config == 2;
            SpatialEngine e;
            e.prepare (fs, block);
            e.setParams (p);
            const auto t0 = std::chrono::steady_clock::now();
            for (int s0 = 0; s0 < n; s0 += block)
            {
                const float* ip[2] = { x.data() + s0, x.data() + s0 };
                float* op[2] = { l.data(), r.data() };
                e.process (ip, 2, op, 2, block);
            }
            const auto t1 = std::chrono::steady_clock::now();
            const double secs = std::chrono::duration<double> (t1 - t0).count();
            static const char* names[3] = { "Room Off", "Room Medium A", "Mono to Stereo (room off)" };
            std::printf ("    %s: %.3f %% of one core (48 kHz stereo)\n", names[config], 100.0 * secs / 10.0);
            check (std::isfinite (l[0]), "rendered");
        }
    } });

    for (auto& t : tests)
    {
        const int before = failures;
        std::printf ("%s\n", t.name);
        t.fn();
        std::printf ("  -> %s\n", failures == before ? "ok" : "FAILED");
    }
    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
