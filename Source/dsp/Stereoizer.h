// Mono-to-stereo converter: generates a side signal S from a mono mid M so that
//   L = M' + a*S,  R = M' - a*S
// gives a spacious stereo image while
//   * the mono sum L + R = 2 M' is exactly M' (an all-pass version of M): no comb filtering in mono,
//   * both channels keep the same magnitude at every frequency (S is in phase quadrature with M'),
//     so there is no tonal colouring and no level lean to either side,
//   * the image is spread by alternating the sign of the interchannel phase difference in about
//     one-octave bands (frequency-dependent lateralisation, cf. Orban 1970; Gerzon 1992),
//   * the bass (< ~200 Hz) and the extreme top stay mono.
//
// Structure: an 8-band Linkwitz-Riley (8th order) filter-bank tree with all-pass compensation, so all
// bands are in phase with their sum (the all-pass M'). S is the band sum with alternating signs
// (bass band and top band excluded); a polyphase IIR Hilbert pair (Niemitalo) then puts S at +90
// degrees relative to M'. Because every band is in phase with M', S stays in quadrature even when
// the band gains change, which is what "Keep Mono" uses: per band, a zero-latency ambience estimate
// (recorded reverberation + room tone) sets how much of that band goes into S, so the voice stays
// mono and only the recorded space is widened. Pure C++, real-time safe, zero latency.
#pragma once

#include "DspCore.h"

namespace amb
{
class Stereoizer
{
public:
    static constexpr int numEdges = 7;
    static constexpr int numBands = numEdges + 1;
    static constexpr int tickLength = 32;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        static const double edges[numEdges] = { 200, 420, 850, 1700, 3300, 6500, 15000 };
        constexpr double q1 = 0.54119610014619698, q2 = 1.30656296487637653; // 4th-order Butterworth
        for (int e = 0; e < numEdges; ++e)
        {
            const double fc = std::min (edges[e], 0.45 * sampleRate);
            const double K = 1.0 / std::tan (kPi * fc / sampleRate);
            // LR8 = (4th-order Butterworth)^2: low = 1 / B^2, high = s^8 / B^2, all-pass = B(-s) / B(s).
            for (int k = 0; k < 4; ++k)
            {
                const double q = (k % 2 == 0) ? q1 : q2;
                lowProto[(size_t) e][(size_t) k] = Section::bilinear (0, 0, 1, 1, 1 / q, 1, K);
                highProto[(size_t) e][(size_t) k] = Section::bilinear (1, 0, 0, 1, 1 / q, 1, K);
            }
            apProto[(size_t) e][0] = Section::bilinear (1, -1 / q1, 1, 1, 1 / q1, 1, K);
            apProto[(size_t) e][1] = Section::bilinear (1, -1 / q2, 1, 1, 1 / q2, 1, K);
        }
        // Tree: band e = LP(e) of the high branch of edge e-1, then the all-passes of the edges above
        // it, so every band is in phase with the total all-pass (Linkwitz-Riley property).
        for (int e = 0; e < numEdges; ++e)
        {
            lowSplit[(size_t) e] = lowProto[(size_t) e];
            highSplit[(size_t) e] = highProto[(size_t) e];
            int n = 0;
            for (int j = e + 1; j < numEdges; ++j)
            {
                comp[(size_t) e][(size_t) n++] = apProto[(size_t) j][0];
                comp[(size_t) e][(size_t) n++] = apProto[(size_t) j][1];
            }
            compCount[(size_t) e] = n;
        }
        const double tickSeconds = tickLength / fs;
        envCoeff = std::exp (-1.0 / (0.010 * fs));                        // 10 ms energy envelope
        gainCoeff = std::exp (-tickSeconds / 0.015);                      // gain smoothing
        decayPerTick = std::pow (10.0, -6.0 * tickSeconds / assumedT60);  // energy decay of the tail
        floorWindowTicks = std::max (1, (int) std::round (0.5 / tickSeconds)); // minimum statistics window
        firstWindowTicks = std::max (1, (int) std::round (0.2 / tickSeconds)); // first estimate after a reset
        onsetTicks = std::max (1, (int) std::round (0.03 / tickSeconds));      // envelope settling after silence
        commitDelay = std::clamp ((int) std::round (0.05 / tickSeconds), 1, maxCommitDelay - 1);
        reset();
    }

    void reset() noexcept
    {
        for (auto& a : lowSplit) for (auto& sct : a) sct.reset();
        for (auto& a : highSplit) for (auto& sct : a) sct.reset();
        for (auto& a : comp) for (auto& sct : a) sct.reset();
        for (auto& h : hilbertMid) h.reset();
        for (auto& h : hilbertSide) h.reset();
        midDelay = 0.0;
        env.fill (0.0);
        tail.fill (0.0);
        peak.fill (0.0);
        minCur.fill (1.0e30);
        minPrev.fill (0.0); // no room-tone estimate until the first window has been measured
        floorTicks = 0;
        activeTicks = 0;
        tickPeak = 0.0;
        windowsDone = 0;
        historyPos = 0;
        for (auto& h : history)
            h.fill (0.0);
        gain.fill (1.0);
        tickPos = 0;
    }

    // 0 = the whole signal is widened; 1 = only the recorded ambience (reverberation after each
    // sound + steady room tone) is widened, the direct sound (voice) stays mono.
    void setKeepMono (double amount) noexcept { keepMono = std::clamp (amount, 0.0, 1.0); }

    // m: mono input. midOut: all-pass version of m (use it in place of m). sideOut: unit-scale side.
    void process (float m, float& midOut, float& sideOut) noexcept
    {
        tickPeak = std::max (tickPeak, (double) std::abs (m));
        std::array<double, numBands> band;
        double rest = m;
        for (int e = 0; e < numEdges; ++e)
        {
            double lo = rest, hi = rest;
            for (auto& sct : lowSplit[(size_t) e])
                lo = sct.process (lo);
            for (auto& sct : highSplit[(size_t) e])
                hi = sct.process (hi);
            for (int k = 0; k < compCount[(size_t) e]; ++k)
                lo = comp[(size_t) e][(size_t) k].process (lo);
            band[(size_t) e] = lo;
            rest = hi;
        }
        band[numEdges] = rest;

        double mid = 0.0, side = 0.0;
        for (int b = 0; b < numBands; ++b)
        {
            const double v = band[(size_t) b];
            mid += v;
            env[(size_t) b] = v * v + envCoeff * (env[(size_t) b] - v * v);
            // Bands 1..6 (about 200 Hz - 15 kHz) carry the side, with alternating signs; the bass and
            // the extreme top stay mono.
            if (b >= 1 && b <= numEdges - 1)
                side += ((b & 1) != 0 ? 1.0 : -1.0) * gain[(size_t) b] * v;
        }
        if (++tickPos >= tickLength)
        {
            tickPos = 0;
            updateGains();
        }
        for (auto& h : hilbertMid)
            mid = h.process (mid);
        for (auto& h : hilbertSide)
            side = h.process (side);
        midOut = (float) midDelay; // the Hilbert pair needs one sample of delay on this path
        midDelay = mid;
        sideOut = (float) side;
    }

private:
    // Zero-latency ambience estimate per band (cf. Lebart et al. 2001: late reverberation decays
    // exponentially with the room's T60): the tail estimate follows the energy with an instant rise
    // to kappa * energy and decays at the assumed T60; the room-tone floor is a minimum tracker.
    // Ambience share = (tail + floor) / energy, limited to 1.
    void updateGains() noexcept
    {
        // The room-tone tracker only learns from settled, non-silent signal: digital silence (transport
        // stopped, gaps between clips) and the envelope's settling after it must not pull the minimum
        // to zero; windows count active time only, so the estimate survives pauses in playback.
        // Values are committed with a 50 ms delay, so the moments right before a silence (filter and
        // envelope decay) never reach the estimate.
        activeTicks = tickPeak < 1.0e-6 ? 0 : activeTicks + 1;
        tickPeak = 0.0;
        const bool learn = activeTicks > onsetTicks + commitDelay;
        const int oldest = (historyPos + 1) % (commitDelay + 1);
        for (int b = 1; b <= numEdges - 1; ++b)
        {
            const double e = env[(size_t) b];
            double& t = tail[(size_t) b];
            t = std::max (t * decayPerTick, kappa * e);
            auto& hist = history[(size_t) b];
            hist[(size_t) historyPos] = e;
            if (learn)
                minCur[(size_t) b] = std::min (minCur[(size_t) b], hist[(size_t) oldest]);
            // Room tone: minimum over the last 0.5-1 s of signal (minimum statistics, Martin 2001),
            // with a bias factor for the minimum of a fluctuating envelope.
            const double f = std::min (minCur[(size_t) b], minPrev[(size_t) b]) * floorBias;
            // Decay phase: once the band has fallen clearly below its recent peak (the peak itself
            // decaying at the room's rate), what remains is the room's reverberation, whatever the
            // recording's direct-to-reverberant ratio: 3 dB below -> direct, 9 dB below -> ambience.
            double& pk = peak[(size_t) b];
            pk = std::max (pk * decayPerTick, e);
            const double dropDb = 10.0 * std::log10 (pk / std::max (e, 1.0e-20));
            const double decayShare = std::clamp ((dropDb - 3.0) / 6.0, 0.0, 1.0);
            const double share = std::max (decayShare, std::min (1.0, (t + f) / std::max (e, 1.0e-20)));
            const double target = 1.0 + keepMono * (std::sqrt (share) - 1.0);
            gain[(size_t) b] = target + gainCoeff * (gain[(size_t) b] - target);
        }
        historyPos = oldest;
        if (learn && ++floorTicks >= (windowsDone == 0 ? firstWindowTicks : floorWindowTicks))
        {
            ++windowsDone;
            floorTicks = 0;
            minPrev = minCur;
            minCur.fill (1.0e30);
        }
    }

    static double flush (double v) noexcept { return std::abs (v) < 1.0e-30 ? 0.0 : v; }

    struct Section
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, s1 = 0, s2 = 0;

        // (b2 s^2 + b1 s + b0) / (a2 s^2 + a1 s + a0) with s normalised to the edge frequency;
        // K = 1 / tan(pi fc / fs) (bilinear transform pre-warped at fc).
        static Section bilinear (double nb2, double nb1, double nb0, double da2, double da1, double da0, double K)
        {
            const double K2 = K * K;
            const double d0 = da2 * K2 + da1 * K + da0;
            Section s;
            s.b0 = (nb2 * K2 + nb1 * K + nb0) / d0;
            s.b1 = (2.0 * (nb0 - nb2 * K2)) / d0;
            s.b2 = (nb2 * K2 - nb1 * K + nb0) / d0;
            s.a1 = (2.0 * (da0 - da2 * K2)) / d0;
            s.a2 = (da2 * K2 - da1 * K + da0) / d0;
            return s;
        }

        void reset() noexcept { s1 = s2 = 0; }

        double process (double x) noexcept
        {
            const double y = b0 * x + s1;
            s1 = flush (b1 * x - a1 * y + s2);
            s2 = flush (b2 * x - a2 * y);
            return y;
        }
    };

    // y[n] = c (x[n] + y[n-2]) - x[n-2]
    struct HilbertSection
    {
        double c = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        void reset() noexcept { x1 = x2 = y1 = y2 = 0; }
        double process (double x) noexcept
        {
            const double y = c * (x + y2) - x2;
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = flush (y);
            return y;
        }
    };

    static std::array<HilbertSection, 4> makeHilbert (const double (&a)[4])
    {
        std::array<HilbertSection, 4> h;
        for (int i = 0; i < 4; ++i)
            h[(size_t) i].c = a[i] * a[i];
        return h;
    }

    static constexpr double hilbertA[4] = { 0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737 };
    static constexpr double hilbertB[4] = { 0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278 };
    static constexpr double assumedT60 = 0.6;  // s, typical location interior (listening calibration)
    static constexpr double kappa = 0.05;      // minimum reverberant share of the direct energy (-13 dB)
    static constexpr double floorBias = 2.0;   // minimum of a 10 ms energy envelope vs its mean

    double fs = 48000.0;
    std::array<std::array<Section, 4>, numEdges> lowProto {}, highProto {}, lowSplit {}, highSplit {};
    std::array<std::array<Section, 2>, numEdges> apProto {};
    std::array<std::array<Section, 2 * (numEdges - 1)>, numEdges> comp {};
    std::array<int, numEdges> compCount {};
    std::array<HilbertSection, 4> hilbertMid = makeHilbert (hilbertA);
    std::array<HilbertSection, 4> hilbertSide = makeHilbert (hilbertB);
    double midDelay = 0.0;
    std::array<double, numBands> env {}, tail {}, peak {}, minCur {}, minPrev {}, gain {};
    double envCoeff = 0.0, gainCoeff = 0.0, decayPerTick = 1.0, keepMono = 0.0;
    int tickPos = 0, floorTicks = 0, floorWindowTicks = 1, firstWindowTicks = 1, activeTicks = 0, onsetTicks = 1;
    int windowsDone = 0, commitDelay = 1, historyPos = 0;
    double tickPeak = 0.0;
    static constexpr int maxCommitDelay = 512; // ticks (enough for 50 ms at 192 kHz: 300 ticks)
    std::array<std::array<double, maxCommitDelay>, numBands> history {};
};

} // namespace amb
