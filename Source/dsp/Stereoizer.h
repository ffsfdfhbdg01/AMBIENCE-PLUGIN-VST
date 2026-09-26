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
// Structure: an 8th-order Linkwitz-Riley split at each band edge. LR low/high outputs are in phase,
// so (LP - HP) / (LP + HP) is real-valued with a sign change at the crossover. M' runs through the
// matching all-pass (LP + HP) chain and S through the (LP - HP) chain; a polyphase IIR Hilbert pair
// (Niemitalo) then puts S at +90 degrees relative to M'. All filters are bilinear with pre-warping,
// so the matching between the two paths is exact up to rounding. Pure C++, real-time safe.
#pragma once

#include "DspCore.h"

namespace amb
{
class Stereoizer
{
public:
    static constexpr int numEdges = 7;

    void prepare (double sampleRate)
    {
        static const double edges[numEdges] = { 200, 420, 850, 1700, 3300, 6500, 15000 };
        constexpr double q1 = 0.54119610014619698, q2 = 1.30656296487637653; // 4th-order Butterworth
        const double sqrt2 = std::sqrt (2.0);
        for (int e = 0; e < numEdges; ++e)
        {
            const double fc = std::min (edges[e], 0.45 * sampleRate);
            const double K = 1.0 / std::tan (kPi * fc / sampleRate);
            auto& st = stages[(size_t) e];
            // M' path: LP8 + HP8 = B(-s) / B(s) (all-pass).
            st.mid[0] = Section::bilinear (1, -1 / q1, 1, 1, 1 / q1, 1, K);
            st.mid[1] = Section::bilinear (1, -1 / q2, 1, 1, 1 / q2, 1, K);
            // S path: HP8 = s^8 / B^2 (lowest edge), LP8 = 1 / B^2 (highest edge),
            // otherwise LP8 - HP8 = (1 - s^2)(1 + s^2)(1 + sqrt2 s + s^2)(1 - sqrt2 s + s^2) / B^2.
            const double num[4][3] = { { -1, 0, 1 }, { 1, 0, 1 }, { 1, sqrt2, 1 }, { 1, -sqrt2, 1 } };
            for (int k = 0; k < 4; ++k)
            {
                const double q = (k % 2 == 0) ? q1 : q2;
                if (e == 0)
                    st.side[k] = Section::bilinear (1, 0, 0, 1, 1 / q, 1, K);
                else if (e == numEdges - 1)
                    st.side[k] = Section::bilinear (0, 0, 1, 1, 1 / q, 1, K);
                else
                    st.side[k] = Section::bilinear (num[k][0], num[k][1], num[k][2], 1, 1 / q, 1, K);
            }
        }
        reset();
    }

    void reset() noexcept
    {
        for (auto& st : stages)
        {
            for (auto& s : st.mid)
                s.reset();
            for (auto& s : st.side)
                s.reset();
        }
        for (auto& h : hilbertMid)
            h.reset();
        for (auto& h : hilbertSide)
            h.reset();
        midDelay = 0.0;
    }

    // m: mono input. midOut: all-pass version of m (use it in place of m). sideOut: unit-scale side.
    void process (float m, float& midOut, float& sideOut) noexcept
    {
        double x = m, s = m;
        for (auto& st : stages)
        {
            x = st.mid[1].process (st.mid[0].process (x));
            for (auto& sec : st.side)
                s = sec.process (s);
        }
        for (auto& h : hilbertMid)
            x = h.process (x);
        for (auto& h : hilbertSide)
            s = h.process (s);
        midOut = (float) midDelay; // the Hilbert pair needs one sample of delay on this path
        midDelay = x;
        sideOut = (float) s;
    }

private:
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

    struct Stage
    {
        Section mid[2];
        Section side[4];
    };

    static constexpr double hilbertA[4] = { 0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737 };
    static constexpr double hilbertB[4] = { 0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278 };

    std::array<Stage, numEdges> stages;
    std::array<HilbertSection, 4> hilbertMid = makeHilbert (hilbertA);
    std::array<HilbertSection, 4> hilbertSide = makeHilbert (hilbertB);
    double midDelay = 0.0;
};

} // namespace amb
