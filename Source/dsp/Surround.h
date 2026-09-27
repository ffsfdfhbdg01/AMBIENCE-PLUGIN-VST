// Loudspeaker layouts for multichannel output (5.0 ... 7.1.4) and pairwise panning over a ring of
// loudspeakers (constant power between the two adjacent speakers; Pulkki 1997 principle).
#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "DspCore.h"

namespace amb
{
struct Speaker
{
    double azimuthDeg = 0.0;   // 0 = front, + = right
    double elevationDeg = 0.0; // > 0 = height layer
    bool lfe = false;
};

constexpr int maxOutputChannels = 12; // up to 7.1.4

struct SpeakerLayout
{
    std::array<Speaker, maxOutputChannels> speakers {};
    int numChannels = 0;
    int left = -1, right = -1, centre = -1; // front channels (-1 if absent)

    bool isSurround() const noexcept { return numChannels > 2; }
    bool hasHeights() const noexcept
    {
        for (int c = 0; c < numChannels; ++c)
            if (! speakers[(size_t) c].lfe && speakers[(size_t) c].elevationDeg > 10.0)
                return true;
        return false;
    }

    void finalise() noexcept
    {
        left = right = centre = -1;
        for (int c = 0; c < numChannels; ++c)
        {
            const auto& s = speakers[(size_t) c];
            if (s.lfe || s.elevationDeg > 10.0)
                continue;
            if (std::abs (s.azimuthDeg) < 1.0)
                centre = c;
            else if (std::abs (s.azimuthDeg + 30.0) < 1.0)
                left = c;
            else if (std::abs (s.azimuthDeg - 30.0) < 1.0)
                right = c;
        }
    }

    // Constant-power pairwise panning of a direction onto one layer (height or ear level).
    // gains[c] is written for every channel (0 for channels not used).
    void pan (double azimuthDeg, bool height, float* gains) const noexcept
    {
        std::array<int, maxOutputChannels> ring {};
        int n = 0;
        for (int c = 0; c < numChannels; ++c)
        {
            gains[c] = 0.0f;
            const auto& s = speakers[(size_t) c];
            if (! s.lfe && (s.elevationDeg > 10.0) == height)
                ring[(size_t) n++] = c;
        }
        if (n == 0)
        {
            if (height)
                pan (azimuthDeg, false, gains);
            return;
        }
        if (n == 1)
        {
            gains[ring[0]] = 1.0f;
            return;
        }
        std::sort (ring.begin(), ring.begin() + n,
                   [this] (int a, int b) { return speakers[(size_t) a].azimuthDeg < speakers[(size_t) b].azimuthDeg; });
        auto wrap = [] (double a) { return a - 360.0 * std::floor ((a + 180.0) / 360.0); }; // -> [-180, 180)
        const double az = wrap (azimuthDeg);
        for (int k = 0; k < n; ++k)
        {
            const int a = ring[(size_t) k], b = ring[(size_t) ((k + 1) % n)];
            const double azA = speakers[(size_t) a].azimuthDeg;
            double span = speakers[(size_t) b].azimuthDeg - azA;
            if (span <= 0.0)
                span += 360.0;
            double offset = az - azA;
            if (offset < 0.0)
                offset += 360.0;
            if (offset <= span)
            {
                if (offset < 1.0e-6 || span - offset < 1.0e-6) // exactly on a speaker: only that speaker
                {
                    gains[offset < 1.0e-6 ? a : b] = 1.0f;
                    return;
                }
                const double phi = offset / span * kPi / 2.0;
                gains[a] = (float) std::cos (phi);
                gains[b] += (float) std::sin (phi);
                return;
            }
        }
    }
};

} // namespace amb
