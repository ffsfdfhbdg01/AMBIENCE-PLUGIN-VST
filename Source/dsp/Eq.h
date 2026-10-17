// Character EQ used in three places: the input, the output (every channel) and the reverb only (the
// room feed). Low cut (12 dB/oct), low shelf, peak, high shelf, high cut (12 dB/oct): RBJ biquads in
// double precision. Parameters are smoothed at control rate (log frequency, dB); switching the EQ
// off glides every band to neutral, so it is click-free. Neutral settings bypass the EQ entirely
// (bit-transparent).
#pragma once

#include "DspCore.h"

namespace amb
{
struct EqParams
{
    bool on = true;
    float lowCut = 20.0f;         // Hz; <= 20 Hz = off
    float lowShelfFreq = 100.0f;  // Hz
    float lowShelfGain = 0.0f;    // dB
    float peakFreq = 1000.0f;     // Hz
    float peakGain = 0.0f;        // dB
    float peakQ = 1.0f;
    float highShelfFreq = 8000.0f; // Hz
    float highShelfGain = 0.0f;   // dB
    float highCut = 20000.0f;     // Hz; >= 20 kHz = off

    static constexpr float lowCutOff = 20.0f, highCutOff = 20000.0f;
};

template <int NumChannels>
class ParametricEq
{
public:
    static constexpr int numBands = 5; // low cut, low shelf, peak, high shelf, high cut

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        reset();
    }

    void reset() noexcept
    {
        for (auto& ch : state)
            for (auto& s : ch)
                s = {};
        cur = tgt;
        dirty = true;
        update();
    }

    // Target from the parameters (control rate). An EQ that is off targets the neutral settings.
    void setTarget (const EqParams& p) noexcept
    {
        const bool on = p.on;
        tgt[logLowCut] = std::log (on ? std::clamp ((double) p.lowCut, 20.0, 20000.0) : 20.0);
        tgt[logLowShelf] = std::log (std::clamp ((double) p.lowShelfFreq, 20.0, 20000.0));
        tgt[lowShelfGain] = on ? std::clamp ((double) p.lowShelfGain, -24.0, 24.0) : 0.0;
        tgt[logPeak] = std::log (std::clamp ((double) p.peakFreq, 20.0, 20000.0));
        tgt[peakGain] = on ? std::clamp ((double) p.peakGain, -24.0, 24.0) : 0.0;
        tgt[logQ] = std::log (std::clamp ((double) p.peakQ, 0.1, 20.0));
        tgt[logHighShelf] = std::log (std::clamp ((double) p.highShelfFreq, 20.0, 20000.0));
        tgt[highShelfGain] = on ? std::clamp ((double) p.highShelfGain, -24.0, 24.0) : 0.0;
        tgt[logHighCut] = std::log (on ? std::clamp ((double) p.highCut, 20.0, 20000.0) : 20000.0);
    }

    // Smoothing step (once per control tick); coefficients are recomputed only while values move.
    void tick (double coeff, bool snap) noexcept
    {
        bool moved = false;
        for (size_t i = 0; i < cur.size(); ++i)
        {
            if (cur[i] == tgt[i])
                continue;
            cur[i] = snap ? tgt[i] : tgt[i] + (cur[i] - tgt[i]) * coeff;
            if (std::abs (cur[i] - tgt[i]) < 1.0e-5)
                cur[i] = tgt[i];
            moved = true;
        }
        if (moved || dirty)
            update();
    }

    bool isActive() const noexcept { return anyActive; }

    float process (int ch, float x) noexcept
    {
        double y = x;
        auto& st = state[(size_t) ch];
        for (int b = 0; b < numBands; ++b)
        {
            if (! bandActive[(size_t) b])
                continue;
            const auto& k = coeffs[(size_t) b];
            auto& s = st[(size_t) b];
            const double out = k.b0 * y + s.s1;
            s.s1 = k.b1 * y - k.a1 * out + s.s2;
            s.s2 = k.b2 * y - k.a2 * out;
            y = out;
        }
        return (float) y;
    }

    // Response of a parameter set in dB at frequency f (for the editor's curve), same filters as the DSP.
    static double responseDb (const EqParams& p, double sampleRate, double f) noexcept
    {
        if (! p.on)
            return 0.0;
        const double w = kTwoPi * std::min (f, 0.499 * sampleRate) / sampleRate;
        const double cw = std::cos (w), c2w = std::cos (2.0 * w);
        double db = 0.0;
        BiquadCoeffs k[numBands];
        bool act[numBands];
        design (p.lowCut, p.lowShelfFreq, p.lowShelfGain, p.peakFreq, p.peakGain, p.peakQ, p.highShelfFreq, p.highShelfGain, p.highCut,
                sampleRate, k, act);
        for (int b = 0; b < numBands; ++b)
            if (act[b])
                db += k[b].magnitudeDb (cw, c2w);
        return db;
    }

private:
    // Smoothed values: log frequencies / log Q and gains in dB.
    enum Index
    {
        logLowCut,
        logLowShelf,
        lowShelfGain,
        logPeak,
        peakGain,
        logQ,
        logHighShelf,
        highShelfGain,
        logHighCut,
        numValues
    };
    using Values = std::array<double, numValues>;
    static Values neutral() noexcept
    {
        return { std::log (20.0), std::log (100.0), 0.0, std::log (1000.0), 0.0, 0.0, std::log (8000.0), 0.0, std::log (20000.0) };
    }
    struct State
    {
        double s1 = 0, s2 = 0;
    };

    static void design (double lowCut, double lsF, double lsG, double pkF, double pkG, double pkQ, double hsF, double hsG, double highCut,
                        double sampleRate, BiquadCoeffs* k, bool* act) noexcept
    {
        const double nyq = 0.49 * sampleRate;
        act[0] = lowCut > 20.5;
        act[1] = std::abs (lsG) > 0.005;
        act[2] = std::abs (pkG) > 0.005;
        act[3] = std::abs (hsG) > 0.005;
        act[4] = highCut < 19500.0 && highCut < nyq;
        if (act[0])
            k[0] = designBiquadHz (BiquadType::highPass, sampleRate, lowCut, 0.70710678, 0.0);
        if (act[1])
            k[1] = designBiquadHz (BiquadType::lowShelf, sampleRate, lsF, 0.70710678, lsG);
        if (act[2])
            k[2] = designBiquadHz (BiquadType::peak, sampleRate, std::min (pkF, nyq), pkQ, pkG);
        if (act[3])
            k[3] = designBiquadHz (BiquadType::highShelf, sampleRate, std::min (hsF, nyq), 0.70710678, hsG);
        if (act[4])
            k[4] = designBiquadHz (BiquadType::lowPass, sampleRate, highCut, 0.70710678, 0.0);
    }

    void update() noexcept
    {
        dirty = false;
        BiquadCoeffs k[numBands];
        bool act[numBands];
        design (std::exp (cur[logLowCut]), std::exp (cur[logLowShelf]), cur[lowShelfGain], std::exp (cur[logPeak]), cur[peakGain],
                std::exp (cur[logQ]), std::exp (cur[logHighShelf]), cur[highShelfGain], std::exp (cur[logHighCut]), fs, k, act);
        anyActive = false;
        for (int b = 0; b < numBands; ++b)
        {
            if (act[b] && ! bandActive[(size_t) b])
                for (auto& ch : state)
                    ch[(size_t) b] = {}; // a band that comes in starts from rest
            bandActive[(size_t) b] = act[b];
            if (act[b])
                coeffs[(size_t) b] = k[b];
            anyActive = anyActive || act[b];
        }
    }

    double fs = 48000.0;
    Values cur = neutral(), tgt = neutral();
    bool dirty = true, anyActive = false;
    std::array<bool, numBands> bandActive {};
    std::array<BiquadCoeffs, numBands> coeffs {};
    std::array<std::array<State, numBands>, (size_t) NumChannels> state {};
};

} // namespace amb
