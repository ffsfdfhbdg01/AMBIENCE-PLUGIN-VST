// Room model: first-order image-source early reflections (Allen & Berkley 1979) for a
// shoebox room + an 8-line feedback delay network late reverb (Jot & Chaigne 1991) with
// frequency-dependent decay. Deterministic: fixed delays, no random modulation.
#pragma once

#include "AcousticModels.h"

namespace amb
{
struct RoomSpec
{
    const char* name;
    double width, length, height; // m
    double t60;                   // s, mid frequencies
    double hfRatio;               // T60(high) / T60(mid)
};

// Orientative values (listening calibration): volumes ~30 / 150 / 600 / 5000 m^3.
inline const RoomSpec& roomSpec (int type) noexcept
{
    static const RoomSpec specs[] = {
        { "Off", 0, 0, 0, 0, 1 },
        { "Outdoor", 0, 0, 0, 0, 1 },
        { "Small Room", 4.0, 3.0, 2.5, 0.4, 0.55 },
        { "Medium Room", 7.0, 6.5, 3.3, 0.6, 0.5 },
        { "Large Room", 12.0, 12.0, 4.2, 0.9, 0.5 },
        { "Hall", 25.0, 20.0, 10.0, 1.8, 0.45 },
    };
    return specs[std::clamp (type, 0, 5)];
}

constexpr int roomOff = 0;
constexpr int roomOutdoor = 1;
constexpr int numRoomTypes = 6;

class RoomModel
{
public:
    static constexpr int numLines = 8;
    static constexpr int numReflections = 6;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        input.prepare ((int) std::ceil (0.40 * fs));
        for (auto& l : lines)
            l.prepare ((int) std::ceil (0.12 * fs));
        // Reference IR energies per room type for normalisation (done off the audio thread).
        for (int t = 0; t < numRoomTypes; ++t)
            refEnergy[(size_t) t] = t >= 2 ? measureTailEnergy (t) : 1.0;
        configure (roomOff, 1.0);
        reset();
    }

    void reset() noexcept
    {
        input.reset();
        for (auto& l : lines)
            l.reset();
        for (auto& s : damp)
            s = 0.0f;
        erLpL = erLpR = 0.0f;
        snapGeometry();
    }

    int type() const noexcept { return roomType; }

    void configure (int type, double decayScale) noexcept
    {
        roomType = type;
        const auto& s = roomSpec (type);
        if (type >= 2)
        {
            const double V = s.width * s.length * s.height;
            const double S = 2.0 * (s.width * s.length + s.width * s.height + s.length * s.height);
            const double mfp = 4.0 * V / S; // mean free path
            const double d0 = std::clamp (1.5 * mfp / kSpeedOfSound, 0.007, 0.045);
            static const double mult[numLines] = { 1.0, 1.131, 1.273, 1.412, 1.567, 1.733, 1.901, 2.087 };
            for (int i = 0; i < numLines; ++i)
            {
                int d = (int) std::round (d0 * mult[i] * fs);
                d |= 1; // odd lengths reduce common factors
                delays[(size_t) i] = std::max (d, 3);
            }
        }
        setDecay (decayScale);
    }

    void setDecay (double decayScale) noexcept
    {
        decay = std::clamp (decayScale, 0.25, 4.0);
        const auto& s = roomSpec (roomType);
        if (roomType < 2)
            return;
        const double t60 = s.t60 * decay;
        const double t60hf = t60 * s.hfRatio;
        for (int i = 0; i < numLines; ++i)
        {
            const double g = std::pow (10.0, -3.0 * delays[(size_t) i] / (t60 * fs));
            const double ghf = std::pow (10.0, -3.0 * delays[(size_t) i] / (t60hf * fs));
            const double r = ghf / g;
            const double b = (1.0 - r) / (1.0 + r);
            lineGain[(size_t) i] = (float) (g * (1.0 - b));
            lineFb[(size_t) i] = (float) b;
        }
        const double V = s.width * s.length * s.height;
        const double S = 2.0 * (s.width * s.length + s.width * s.height + s.length * s.height);
        wallReflection = std::sqrt (std::clamp (1.0 - 0.161 * V / (S * t60), 0.05, 0.95)); // Sabine mean absorption
        criticalDist = models::criticalDistance (V, t60);
        tailNorm = 1.0 / std::sqrt (refEnergy[(size_t) roomType] * decay);
    }

    double criticalDistance() const noexcept { return criticalDist; }

    // Called once per control tick. Source position: distance r [m], azimuth [rad] (0 front,
    // + right). levelLin = Room Level, rolloffExp = rolloff/6.02 (1 = inverse distance law).
    void setGeometry (double r, double azimuth, double recDistance, double rolloffExp, double levelLin, int tickLen) noexcept
    {
        const double inv = 1.0 / std::max (tickLen, 1);
        auto ramp = [inv] (Ramp& rp, double target)
        {
            rp.target = target;
            rp.inc = (target - rp.cur) * inv;
        };

        if (roomType == roomOff)
        {
            for (auto& e : er)
            {
                ramp (e.delay, e.delay.cur);
                ramp (e.gainL, 0.0);
                ramp (e.gainR, 0.0);
            }
            ramp (fdnGain, 0.0);
            ramp (fdnInDelay, fdnInDelay.cur);
            return;
        }

        if (roomType == roomOutdoor)
        {
            // Ground reflection only (source and listener at ear height 1.6 m); grass/soil lowers
            // the reflection factor (listening calibration).
            const double h = 1.6;
            const double dr = std::sqrt (r * r + 4.0 * h * h);
            const double g = 0.55 * std::pow (recDistance / dr, rolloffExp) * levelLin;
            double pl, pr;
            panGains (std::sin (azimuth), pl, pr);
            ramp (er[0].delay, (dr - r) / kSpeedOfSound * fs);
            ramp (er[0].gainL, g * pl);
            ramp (er[0].gainR, g * pr);
            for (int i = 1; i < numReflections; ++i)
            {
                ramp (er[(size_t) i].delay, er[(size_t) i].delay.cur);
                ramp (er[(size_t) i].gainL, 0.0);
                ramp (er[(size_t) i].gainR, 0.0);
            }
            ramp (fdnGain, 0.0);
            ramp (fdnInDelay, fdnInDelay.cur);
            return;
        }

        const auto& s = roomSpec (roomType);
        const double W = s.width, L = s.length, H = s.height;
        const double lx = 0.5 * W, ly = 0.3 * L, lz = std::min (1.6, 0.6 * H);
        constexpr double margin = 0.3;
        double sx = std::clamp (lx + r * std::sin (azimuth), margin, W - margin);
        double sy = std::clamp (ly + r * std::cos (azimuth), margin, L - margin);
        const double sz = lz;
        const double rEff = std::max (0.05, std::sqrt ((sx - lx) * (sx - lx) + (sy - ly) * (sy - ly)));

        const double img[numReflections][3] = {
            { -sx, sy, sz }, { 2 * W - sx, sy, sz }, { sx, -sy, sz }, { sx, 2 * L - sy, sz }, { sx, sy, -sz }, { sx, sy, 2 * H - sz },
        };

        double meanDelay = 0;
        for (int i = 0; i < numReflections; ++i)
        {
            const double dx = img[i][0] - lx, dy = img[i][1] - ly, dz = img[i][2] - lz;
            const double d = std::sqrt (dx * dx + dy * dy + dz * dz);
            const double delaySamples = std::max (0.0, (d - rEff) / kSpeedOfSound * fs);
            const double g = wallReflection * std::pow (recDistance / d, rolloffExp) * levelLin;
            const double az = std::atan2 (dx, dy);
            double pl, pr;
            panGains (std::sin (az), pl, pr);
            ramp (er[(size_t) i].delay, delaySamples);
            ramp (er[(size_t) i].gainL, g * pl);
            ramp (er[(size_t) i].gainR, g * pr);
            meanDelay += delaySamples;
        }
        meanDelay /= numReflections;

        // Diffuse field level: equals the direct sound at the critical distance (Kuttruff).
        const double tail = std::pow (recDistance / criticalDist, rolloffExp) * levelLin * tailNorm;
        ramp (fdnGain, tail);
        ramp (fdnInDelay, meanDelay);
    }

    void snapGeometry() noexcept
    {
        for (auto& e : er)
        {
            e.delay.cur = e.delay.target; e.delay.inc = 0;
            e.gainL.cur = e.gainL.target; e.gainL.inc = 0;
            e.gainR.cur = e.gainR.target; e.gainR.inc = 0;
        }
        fdnGain.cur = fdnGain.target; fdnGain.inc = 0;
        fdnInDelay.cur = fdnInDelay.target; fdnInDelay.inc = 0;
    }

    bool isSilent() const noexcept { return roomType == roomOff && silentCounter > (int) (0.5 * fs); }

    void process (float x, float& outL, float& outR) noexcept
    {
        input.push (x);
        float eL = 0, eR = 0;
        const int nRefl = roomType == roomOutdoor ? 1 : numReflections;
        for (int i = 0; i < nRefl; ++i)
        {
            auto& e = er[(size_t) i];
            e.delay.step();
            e.gainL.step();
            e.gainR.step();
            const float v = input.read ((float) e.delay.cur);
            eL += (float) e.gainL.cur * v;
            eR += (float) e.gainR.cur * v;
        }
        // Wall absorption lowers the high frequencies of reflections (listening calibration).
        constexpr float erLpCoeff = 0.45f;
        erLpL = flushDenormal (eL + erLpCoeff * (erLpL - eL));
        erLpR = flushDenormal (eR + erLpCoeff * (erLpR - eR));
        outL = erLpL;
        outR = erLpR;

        fdnGain.step();
        fdnInDelay.step();
        if (roomType >= 2)
        {
            const float in = input.read ((float) fdnInDelay.cur) * (float) fdnGain.cur;
            float s[numLines];
            float sum = 0;
            for (int i = 0; i < numLines; ++i)
            {
                const float v = lines[(size_t) i].at (delays[(size_t) i] - 1);
                damp[(size_t) i] = flushDenormal (lineGain[(size_t) i] * v + lineFb[(size_t) i] * damp[(size_t) i]);
                s[i] = damp[(size_t) i];
                sum += s[i];
            }
            const float hh = sum * (2.0f / numLines);
            static const float inSign[numLines] = { 1, -1, 1, -1, 1, -1, 1, -1 };
            static const float lSign[numLines] = { 1, 1, -1, -1, 1, 1, -1, -1 };
            static const float rSign[numLines] = { 1, -1, -1, 1, 1, -1, -1, 1 };
            float tL = 0, tR = 0;
            for (int i = 0; i < numLines; ++i)
            {
                lines[(size_t) i].push (s[i] - hh + inSign[i] * in * 0.35355339f);
                tL += lSign[i] * s[i];
                tR += rSign[i] * s[i];
            }
            outL += tL * 0.35355339f;
            outR += tR * 0.35355339f;
        }

        if (roomType == roomOff)
            silentCounter = std::min (silentCounter + 1, 1 << 30);
        else
            silentCounter = 0;
    }

    // Energy of the reverb (ER + tail) relative to unit input energy, for the auto gain model.
    double predictedEnergy() const noexcept
    {
        double e = 0;
        const int nRefl = roomType == roomOutdoor ? 1 : numReflections;
        for (int i = 0; i < nRefl; ++i)
            e += er[(size_t) i].gainL.target * er[(size_t) i].gainL.target + er[(size_t) i].gainR.target * er[(size_t) i].gainR.target;
        if (roomType >= 2)
            e += fdnGain.target * fdnGain.target;
        return e;
    }

private:
    struct Ramp
    {
        double cur = 0, target = 0, inc = 0;
        void step() noexcept
        {
            if (inc != 0)
            {
                cur += inc;
                if ((inc > 0 && cur >= target) || (inc < 0 && cur <= target))
                {
                    cur = target;
                    inc = 0;
                }
            }
        }
    };

    struct Reflection
    {
        Ramp delay, gainL, gainR;
    };

    static void panGains (double p, double& l, double& r) noexcept
    {
        const double th = (std::clamp (p, -1.0, 1.0) + 1.0) * kPi / 4.0;
        l = std::cos (th);
        r = std::sin (th);
    }

    // Tail IR energy (sum over L+R) for unit input gain, used to normalise the tail so its total
    // energy equals the diffuse-field target.
    double measureTailEnergy (int type)
    {
        RoomModel tmp;
        tmp.fs = fs;
        for (auto& l : tmp.lines)
            l.prepare ((int) std::ceil (0.12 * fs));
        tmp.input.prepare (16);
        tmp.refEnergy.fill (1.0);
        tmp.configure (type, 1.0);
        tmp.fdnGain.cur = tmp.fdnGain.target = 1.0;
        tmp.fdnInDelay.cur = tmp.fdnInDelay.target = 0.0;
        for (auto& e : tmp.er)
        {
            e.gainL.cur = e.gainR.cur = 0;
            e.gainL.target = e.gainR.target = 0;
        }
        const int len = (int) (roomSpec (type).t60 * 1.6 * fs);
        double energy = 0;
        for (int n = 0; n < len; ++n)
        {
            float l, r;
            tmp.process (n == 0 ? 1.0f : 0.0f, l, r);
            energy += (double) l * l + (double) r * r;
        }
        return std::max (energy, 1.0e-9);
    }

    double fs = 48000.0;
    int roomType = roomOff;
    double decay = 1.0;
    DelayLine input;
    std::array<DelayLine, numLines> lines;
    std::array<int, numLines> delays { 331, 373, 419, 467, 521, 577, 631, 691 };
    std::array<float, numLines> lineGain {}, lineFb {}, damp {};
    std::array<Reflection, numReflections> er {};
    std::array<double, numRoomTypes> refEnergy { 1, 1, 1, 1, 1, 1 };
    Ramp fdnGain, fdnInDelay;
    double wallReflection = 0.8, criticalDist = 1.0, tailNorm = 1.0;
    float erLpL = 0, erLpR = 0;
    int silentCounter = 0;
};

} // namespace amb
