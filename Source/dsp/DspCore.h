// Core DSP building blocks used by the Ambience engine.
// Pure C++ (no JUCE) so the engine can be unit-tested and rendered offline.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace amb
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
constexpr double kSpeedOfSound = 343.0; // m/s at ~20 degC

inline double dbToGain (double db) noexcept { return std::pow (10.0, db / 20.0); }
inline double gainToDb (double g) noexcept { return 20.0 * std::log10 (std::max (g, 1.0e-12)); }

template <typename T>
inline T lerp (T a, T b, T t) noexcept { return a + (b - a) * t; }

// Flush tiny values that could otherwise become denormals inside recursive filters.
inline float flushDenormal (float x) noexcept { return std::abs (x) < 1.0e-20f ? 0.0f : x; }

//==============================================================================
// Biquad (RBJ Audio EQ Cookbook designs), transposed direct form II.
struct BiquadCoeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    // Magnitude in dB at a normalised frequency given cos(w) and cos(2w).
    double magnitudeDb (double cw, double c2w) const noexcept
    {
        const double num = b0 * b0 + b1 * b1 + b2 * b2 + 2.0 * (b0 * b1 + b1 * b2) * cw + 2.0 * b0 * b2 * c2w;
        const double den = 1.0 + a1 * a1 + a2 * a2 + 2.0 * (a1 + a1 * a2) * cw + 2.0 * a2 * c2w;
        return 10.0 * std::log10 (std::max (num, 1.0e-30) / std::max (den, 1.0e-30));
    }
};

enum class BiquadType
{
    lowShelf,
    highShelf,
    peak,
    lowPass,
    highPass
};

// Designs a biquad from precomputed cos/sin of the centre frequency (so hot loops avoid trig).
inline BiquadCoeffs designBiquad (BiquadType type, double cosW0, double sinW0, double q, double gainDb) noexcept
{
    const double alpha = sinW0 / (2.0 * q);
    const double A = std::pow (10.0, gainDb / 40.0);
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

    switch (type)
    {
        case BiquadType::lowShelf:
        {
            const double sa = 2.0 * std::sqrt (A) * alpha;
            b0 = A * ((A + 1) - (A - 1) * cosW0 + sa);
            b1 = 2 * A * ((A - 1) - (A + 1) * cosW0);
            b2 = A * ((A + 1) - (A - 1) * cosW0 - sa);
            a0 = (A + 1) + (A - 1) * cosW0 + sa;
            a1 = -2 * ((A - 1) + (A + 1) * cosW0);
            a2 = (A + 1) + (A - 1) * cosW0 - sa;
            break;
        }
        case BiquadType::highShelf:
        {
            const double sa = 2.0 * std::sqrt (A) * alpha;
            b0 = A * ((A + 1) + (A - 1) * cosW0 + sa);
            b1 = -2 * A * ((A - 1) + (A + 1) * cosW0);
            b2 = A * ((A + 1) + (A - 1) * cosW0 - sa);
            a0 = (A + 1) - (A - 1) * cosW0 + sa;
            a1 = 2 * ((A - 1) - (A + 1) * cosW0);
            a2 = (A + 1) - (A - 1) * cosW0 - sa;
            break;
        }
        case BiquadType::peak:
            b0 = 1 + alpha * A;
            b1 = -2 * cosW0;
            b2 = 1 - alpha * A;
            a0 = 1 + alpha / A;
            a1 = -2 * cosW0;
            a2 = 1 - alpha / A;
            break;
        case BiquadType::lowPass:
            b0 = (1 - cosW0) * 0.5;
            b1 = 1 - cosW0;
            b2 = (1 - cosW0) * 0.5;
            a0 = 1 + alpha;
            a1 = -2 * cosW0;
            a2 = 1 - alpha;
            break;
        case BiquadType::highPass:
            b0 = (1 + cosW0) * 0.5;
            b1 = -(1 + cosW0);
            b2 = (1 + cosW0) * 0.5;
            a0 = 1 + alpha;
            a1 = -2 * cosW0;
            a2 = 1 - alpha;
            break;
    }

    BiquadCoeffs c;
    c.b0 = b0 / a0;
    c.b1 = b1 / a0;
    c.b2 = b2 / a0;
    c.a1 = a1 / a0;
    c.a2 = a2 / a0;
    return c;
}

inline BiquadCoeffs designBiquadHz (BiquadType type, double sampleRate, double freq, double q, double gainDb) noexcept
{
    const double w0 = kTwoPi * std::min (freq, 0.49 * sampleRate) / sampleRate;
    return designBiquad (type, std::cos (w0), std::sin (w0), q, gainDb);
}

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float s1 = 0, s2 = 0;

    void set (const BiquadCoeffs& c) noexcept
    {
        b0 = (float) c.b0;
        b1 = (float) c.b1;
        b2 = (float) c.b2;
        a1 = (float) c.a1;
        a2 = (float) c.a2;
    }

    void reset() noexcept { s1 = s2 = 0; }

    float process (float x) noexcept
    {
        const float y = b0 * x + s1;
        s1 = flushDenormal (b1 * x - a1 * y + s2);
        s2 = flushDenormal (b2 * x - a2 * y);
        return y;
    }
};

//==============================================================================
// First-order pole/zero section designed from an analogue prototype
// H(s) = (s/wz + 1) / (s/wp + 1) * k via the bilinear transform with pre-warping.
struct FirstOrder
{
    float b0 = 1, b1 = 0, a1 = 0;
    float s = 0;

    void setIdentity() noexcept { b0 = 1; b1 = 0; a1 = 0; }

    // Generic analogue section (b1s*s + b0s) / (a1s*s + a0s); c = bilinear constant.
    void setAnalog (double b1s, double b0s, double a1s, double a0s, double c) noexcept
    {
        const double nb0 = b1s * c + b0s;
        const double nb1 = b0s - b1s * c;
        const double na0 = a1s * c + a0s;
        const double na1 = a0s - a1s * c;
        b0 = (float) (nb0 / na0);
        b1 = (float) (nb1 / na0);
        a1 = (float) (na1 / na0);
    }

    void reset() noexcept { s = 0; }

    float process (float x) noexcept
    {
        const float y = b0 * x + s;
        s = flushDenormal (b1 * x - a1 * y);
        return y;
    }

    double magnitudeDb (double w) const noexcept
    {
        // |b0 + b1 e^-jw| / |1 + a1 e^-jw|
        const double c = std::cos (w);
        const double num = b0 * b0 + b1 * b1 + 2.0 * b0 * b1 * c;
        const double den = 1.0 + a1 * a1 + 2.0 * a1 * c;
        return 10.0 * std::log10 (std::max (num, 1.0e-30) / std::max (den, 1.0e-30));
    }
};

//==============================================================================
// Topology-preserving-transform state-variable filter (Zavalishin), used for the
// crossover in the mono-safe time panner and for bass-mono. Frequency response is
// independent of the sample rate (pre-warped), and it is well behaved under modulation.
struct Svf
{
    float g = 0, k = 1.41421356f, a1 = 0, a2 = 0, a3 = 0;
    float ic1 = 0, ic2 = 0;

    void setup (double sampleRate, double freq, double q) noexcept
    {
        g = (float) std::tan (kPi * std::min (freq, 0.49 * sampleRate) / sampleRate);
        k = (float) (1.0 / q);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void reset() noexcept { ic1 = ic2 = 0; }

    // Returns low-pass output; high-pass available through processHigh.
    float processLow (float v0) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = flushDenormal (2 * v1 - ic1);
        ic2 = flushDenormal (2 * v2 - ic2);
        return v2;
    }

    float processHigh (float v0) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = flushDenormal (2 * v1 - ic1);
        ic2 = flushDenormal (2 * v2 - ic2);
        return v0 - k * v1 - v2;
    }
};

//==============================================================================
// Circular delay line with 3rd-order Lagrange fractional read. Exact at integer delays
// (including zero, which makes a zero-delay read bit-transparent).
class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        int size = 1;
        while (size < maxDelaySamples + 8)
            size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        writePos = 0;
    }

    void reset() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); writePos = 0; }

    void push (float x) noexcept
    {
        writePos = (writePos + 1) & mask;
        buffer[(size_t) writePos] = x;
    }

    // Delay in samples relative to the most recently pushed sample.
    float read (float delay) const noexcept
    {
        const float maxD = (float) (mask - 4);
        delay = std::clamp (delay, 0.0f, maxD);
        const int i = (int) delay;
        const float f = delay - (float) i;
        if (f == 0.0f)
            return at (i);

        // Node offsets: use {0,1,2,3} when i == 0 (no future samples available), otherwise centred.
        const int base = i == 0 ? 0 : i - 1;
        const float d = delay - (float) base; // position within the 4 nodes (0..3)
        const float x0 = at (base), x1 = at (base + 1), x2 = at (base + 2), x3 = at (base + 3);
        const float dm1 = d - 1.0f, dm2 = d - 2.0f, dm3 = d - 3.0f;
        return -x0 * dm1 * dm2 * dm3 * (1.0f / 6.0f)
               + x1 * d * dm2 * dm3 * 0.5f
               - x2 * d * dm1 * dm3 * 0.5f
               + x3 * d * dm1 * dm2 * (1.0f / 6.0f);
    }

    float at (int delaySamples) const noexcept { return buffer[(size_t) ((writePos - delaySamples) & mask)]; }

private:
    std::vector<float> buffer;
    int mask = 0;
    int writePos = 0;
};

//==============================================================================
// 1/3-octave analysis grid (IEC 61260 base-ten mid-band frequencies) used by the response
// fitter and by the model-based auto gain.
inline std::vector<double> thirdOctaveGrid (double sampleRate, double fLow = 25.0, double fHigh = 16000.0)
{
    std::vector<double> f;
    for (int k = -16; k <= 13; ++k)
    {
        const double fm = 1000.0 * std::pow (10.0, 0.1 * k);
        if (fm >= fLow * 0.99 && fm <= fHigh * 1.01 && fm < 0.45 * sampleRate)
            f.push_back (fm);
    }
    return f;
}

//==============================================================================
// Fits the gains of a fixed cascade of shelving/peaking biquads (plus a broadband gain) to
// a target magnitude curve in dB with a damped Gauss-Newton (Levenberg-Marquardt) solver.
// Deterministic (fixed iteration count, no randomness) and allocation-free after prepare().
// Used to realise physically derived curves (ISO 9613-1 air absorption, mass-law transmission
// loss, Maekawa diffraction) as low-cost minimum-phase IIR filters.
struct FitBasis
{
    BiquadType type = BiquadType::highShelf;
    double freq = 1000.0;
    double q = 0.707;
    double minGain = -80.0;
    double maxGain = 24.0;
};

class ResponseFitter
{
public:
    static constexpr int maxSections = 8;

    void prepare (double sampleRate, const std::vector<FitBasis>& b)
    {
        fs = sampleRate;
        basis = b;
        grid = thirdOctaveGrid (fs);
        nGrid = (int) grid.size();
        cw.resize ((size_t) nGrid);
        c2w.resize ((size_t) nGrid);
        for (int k = 0; k < nGrid; ++k)
        {
            const double w = kTwoPi * grid[(size_t) k] / fs;
            cw[(size_t) k] = std::cos (w);
            c2w[(size_t) k] = std::cos (2 * w);
        }
        sectCos.resize (basis.size());
        sectSin.resize (basis.size());
        for (size_t i = 0; i < basis.size(); ++i)
        {
            const double w0 = kTwoPi * std::min (basis[i].freq, 0.45 * fs) / fs;
            sectCos[i] = std::cos (w0);
            sectSin[i] = std::sin (w0);
        }
        resp.assign (basis.size() * (size_t) nGrid, 0.0);
        sumBuf.assign ((size_t) nGrid, 0.0);
        respD.assign (basis.size() * (size_t) nGrid, 0.0);
        gains.fill (0.0);
        broadband = 0.0;
    }

    int numSections() const noexcept { return (int) basis.size(); }

    void setSectionFrequency (int i, double freq) noexcept
    {
        basis[(size_t) i].freq = freq;
        const double w0 = kTwoPi * std::min (freq, 0.45 * fs) / fs;
        sectCos[(size_t) i] = std::cos (w0);
        sectSin[(size_t) i] = std::sin (w0);
    }
    int gridSize() const noexcept { return nGrid; }
    const std::vector<double>& gridFrequencies() const noexcept { return grid; }

    void clear() noexcept { gains.fill (0.0); broadband = 0.0; }

    // target and weight have gridSize() entries. Levenberg-Marquardt with step acceptance
    // (a step is only taken if it lowers the weighted squared error).
    void fit (const double* target, const double* weight, int iterations = 6) noexcept
    {
        const int n = (int) basis.size();
        const int np = n + 1; // + broadband gain
        constexpr double delta = 0.25;
        double lambda = 1.0e-2;

        auto costOf = [&] (const double* g, double bb) noexcept
        {
            double c = 0;
            for (int k = 0; k < nGrid; ++k)
                sumBuf[(size_t) k] = bb;
            for (int i = 0; i < n; ++i)
            {
                const auto co = sectionCoeffs (i, g[i]);
                for (int k = 0; k < nGrid; ++k)
                    sumBuf[(size_t) k] += co.magnitudeDb (cw[(size_t) k], c2w[(size_t) k]);
            }
            for (int k = 0; k < nGrid; ++k)
            {
                const double e = sumBuf[(size_t) k] - target[k];
                c += weight[k] * e * e;
            }
            return c;
        };

        double cost = costOf (gains.data(), broadband);

        for (int it = 0; it < iterations; ++it)
        {
            for (int i = 0; i < n; ++i)
            {
                evalSection (i, gains[(size_t) i], &resp[(size_t) (i * nGrid)]);
                evalSection (i, gains[(size_t) i] + delta, &respD[(size_t) (i * nGrid)]);
            }

            double JtJ[maxSections + 1][maxSections + 1] = {};
            double Jte[maxSections + 1] = {};

            for (int k = 0; k < nGrid; ++k)
            {
                double sum = broadband;
                for (int i = 0; i < n; ++i)
                    sum += resp[(size_t) (i * nGrid + k)];
                const double e = sum - target[k];
                const double w = weight[k];

                double J[maxSections + 1];
                for (int i = 0; i < n; ++i)
                    J[i] = (respD[(size_t) (i * nGrid + k)] - resp[(size_t) (i * nGrid + k)]) / delta;
                J[n] = 1.0;

                for (int a = 0; a < np; ++a)
                {
                    Jte[a] += w * J[a] * e;
                    for (int b = 0; b < np; ++b)
                        JtJ[a][b] += w * J[a] * J[b];
                }
            }

            bool accepted = false;
            for (int attempt = 0; attempt < 6 && ! accepted; ++attempt)
            {
                double A[maxSections + 1][maxSections + 1];
                double step[maxSections + 1];
                for (int a = 0; a < np; ++a)
                {
                    for (int b = 0; b < np; ++b)
                        A[a][b] = JtJ[a][b];
                    A[a][a] += lambda * JtJ[a][a] + 1.0e-9;
                    step[a] = -Jte[a];
                }
                solve (A, step, np);

                double cand[maxSections];
                for (int i = 0; i < n; ++i)
                    cand[i] = std::clamp (gains[(size_t) i] + step[i], basis[(size_t) i].minGain, basis[(size_t) i].maxGain);
                const double candBB = std::clamp (broadband + step[n], -120.0, 24.0);
                const double c = costOf (cand, candBB);
                if (c < cost)
                {
                    for (int i = 0; i < n; ++i)
                        gains[(size_t) i] = cand[i];
                    broadband = candBB;
                    cost = c;
                    lambda = std::max (lambda * 0.3, 1.0e-6);
                    accepted = true;
                }
                else
                    lambda *= 10.0;
            }
            if (! accepted)
                break;
        }
    }

    void setGain (int i, double g) noexcept { gains[(size_t) i] = std::clamp (g, basis[(size_t) i].minGain, basis[(size_t) i].maxGain); }
    void setBroadband (double g) noexcept { broadband = g; }
    double sectionFrequency (int i) const noexcept { return basis[(size_t) i].freq; }

    double gain (int i) const noexcept { return gains[(size_t) i]; }
    double broadbandGain() const noexcept { return broadband; }

    // Response in dB of the cascade with arbitrary section gains (and broadband) at grid point k.
    double cascadeDb (const double* sectionGains, double bb, int k) const noexcept
    {
        double sum = bb;
        for (int i = 0; i < (int) basis.size(); ++i)
            sum += sectionCoeffs (i, sectionGains[i]).magnitudeDb (cw[(size_t) k], c2w[(size_t) k]);
        return sum;
    }

    BiquadCoeffs sectionCoeffs (int i, double g) const noexcept
    {
        return designBiquad (basis[(size_t) i].type, sectCos[(size_t) i], sectSin[(size_t) i], basis[(size_t) i].q, g);
    }

private:
    void evalSection (int i, double g, double* out) const noexcept
    {
        const auto c = sectionCoeffs (i, g);
        for (int k = 0; k < nGrid; ++k)
            out[k] = c.magnitudeDb (cw[(size_t) k], c2w[(size_t) k]);
    }

    static void solve (double A[maxSections + 1][maxSections + 1], double* b, int n) noexcept
    {
        for (int c = 0; c < n; ++c)
        {
            int piv = c;
            for (int r = c + 1; r < n; ++r)
                if (std::abs (A[r][c]) > std::abs (A[piv][c]))
                    piv = r;
            if (piv != c)
            {
                for (int k = 0; k < n; ++k)
                    std::swap (A[c][k], A[piv][k]);
                std::swap (b[c], b[piv]);
            }
            const double d = A[c][c];
            if (std::abs (d) < 1.0e-15)
                continue;
            for (int r = c + 1; r < n; ++r)
            {
                const double f = A[r][c] / d;
                for (int k = c; k < n; ++k)
                    A[r][k] -= f * A[c][k];
                b[r] -= f * b[c];
            }
        }
        for (int c = n - 1; c >= 0; --c)
        {
            double s = b[c];
            for (int k = c + 1; k < n; ++k)
                s -= A[c][k] * b[k];
            b[c] = std::abs (A[c][c]) < 1.0e-15 ? 0.0 : s / A[c][c];
        }
    }

    double fs = 48000.0;
    std::vector<FitBasis> basis;
    std::vector<double> grid, cw, c2w, sectCos, sectSin, resp, respD, sumBuf;
    int nGrid = 0;
    std::array<double, maxSections> gains {};
    double broadband = 0.0;
};

//==============================================================================
// Runtime cascade driven by fitted gains: smooths gains, recomputes coefficients at control
// rate, processes N channels. Identity (0 dB) sections keep a zero state, so an inactive
// cascade can be skipped without discontinuities.
template <int NumChannels>
class FittedCascade
{
public:
    void prepare (double sampleRate, const std::vector<FitBasis>& basis)
    {
        fitter.prepare (sampleRate, basis);
        n = (int) basis.size();
        current.fill (0.0);
        target.fill (0.0);
        currentBB = targetBB = 0.0;
        for (auto& ch : filters)
            for (auto& f : ch)
                f = Biquad();
        active = false;
    }

    void reset() noexcept
    {
        for (auto& ch : filters)
            for (auto& f : ch)
                f.reset();
    }

    ResponseFitter fitter;

    // Scale applied to the fitted gains (e.g. obstacle "Amount").
    void setTargetFromFit (double scale) noexcept
    {
        for (int i = 0; i < n; ++i)
            target[(size_t) i] = fitter.gain (i) * scale;
        targetBB = fitter.broadbandGain() * scale;
    }

    void snap() noexcept
    {
        current = target;
        currentBB = targetBB;
        updateCoeffs (true);
    }

    // Called once per control tick. coeff = one-pole smoothing coefficient per tick.
    void tick (double coeff) noexcept
    {
        bool changed = false;
        for (int i = 0; i < n; ++i)
        {
            const double t = target[(size_t) i];
            double& c = current[(size_t) i];
            if (c != t)
            {
                c = t + (c - t) * coeff;
                if (std::abs (c - t) < 1.0e-4)
                    c = t;
                changed = true;
            }
        }
        if (currentBB != targetBB)
        {
            currentBB = targetBB + (currentBB - targetBB) * coeff;
            if (std::abs (currentBB - targetBB) < 1.0e-4)
                currentBB = targetBB;
        }
        if (changed)
            updateCoeffs (false);
    }

    // Recompute coefficients (e.g. after a section frequency changed).
    void refresh() noexcept { updateCoeffs (false); }

    bool isActive() const noexcept { return active; }
    double broadbandDb() const noexcept { return currentBB; }
    const double* currentGains() const noexcept { return current.data(); }

    float process (int ch, float x) noexcept
    {
        for (int i = 0; i < n; ++i)
            x = filters[(size_t) ch][(size_t) i].process (x);
        return x;
    }

    // Response of the current (smoothed) state at fitter grid point k, excluding broadband.
    // Cached: recomputed only when the coefficients change.
    double currentSectionsDb (int k) const noexcept
    {
        if (responseDirty)
        {
            responseCache.resize ((size_t) fitter.gridSize());
            for (int i = 0; i < fitter.gridSize(); ++i)
                responseCache[(size_t) i] = fitter.cascadeDb (current.data(), 0.0, i);
            responseDirty = false;
        }
        return responseCache[(size_t) k];
    }

private:
    void updateCoeffs (bool force) noexcept
    {
        responseDirty = true;
        bool any = false;
        for (int i = 0; i < n; ++i)
        {
            if (current[(size_t) i] != 0.0)
                any = true;
            const auto c = fitter.sectionCoeffs (i, current[(size_t) i]);
            for (auto& ch : filters)
                ch[(size_t) i].set (c);
        }
        if (! any && (active || force))
            for (auto& ch : filters)
                for (int i = 0; i < n; ++i)
                    ch[(size_t) i].reset();
        active = any;
    }

    int n = 0;
    std::array<double, ResponseFitter::maxSections> current {}, target {};
    double currentBB = 0.0, targetBB = 0.0;
    std::array<std::array<Biquad, ResponseFitter::maxSections>, NumChannels> filters {};
    bool active = false;
    mutable std::vector<double> responseCache;
    mutable bool responseDirty = true;
};

} // namespace amb
