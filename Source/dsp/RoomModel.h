// Room model: first-order image-source early reflections (Allen & Berkley 1979) for a
// shoebox room, diffused into short clusters, + a 16-line feedback delay network late reverb
// (Jot & Chaigne 1991) with input diffusion and frequency-dependent decay (wall absorption and
// ISO 9613-1 air absorption). Deterministic: fixed delays, no random modulation.
#pragma once

#include <map>
#include <mutex>

#include "AcousticModels.h"

namespace amb
{
enum class RoomKind
{
    off,
    openField, // ground reflection only
    openWalls, // walls + ground, no ceiling (street, courtyard): sparse, weak tail
    room       // closed shoebox
};

struct RoomSpec
{
    const char* name;
    RoomKind kind;
    double width, length, height; // m (open spaces: notional height for the tail model)
    double t60;                   // s, mid frequencies
    double hfRatio;               // T60(high) / T60(mid)
    double tailScale;             // diffuse level scale (open spaces lose energy upwards)
};

// Variant convention: A = neutral, B = damped / furnished, C = live / hard surfaces.
// All values are orientative (listening calibration), see docs/DESIGN.md.
inline const RoomSpec& roomSpec (int type) noexcept
{
    using K = RoomKind;
    static const RoomSpec specs[] = {
        { "Off", K::off, 0, 0, 0, 0, 1, 0 },
        { "Open A (field)", K::openField, 0, 0, 0, 0, 1, 0 },
        { "Open B (street)", K::openWalls, 14.0, 60.0, 12.0, 1.1, 0.4, 0.3 },
        { "Open C (courtyard)", K::openWalls, 20.0, 24.0, 12.0, 0.9, 0.5, 0.4 },
        { "Small A", K::room, 4.0, 3.0, 2.5, 0.4, 0.55, 1 },
        { "Small B (dry)", K::room, 3.5, 3.0, 2.4, 0.25, 0.45, 1 },
        { "Small C (tiled)", K::room, 3.0, 2.5, 2.4, 0.9, 0.8, 1 },
        { "Medium A", K::room, 7.0, 6.5, 3.3, 0.6, 0.5, 1 },
        { "Medium B (furnished)", K::room, 6.0, 5.0, 2.7, 0.45, 0.45, 1 },
        { "Medium C (empty)", K::room, 9.0, 7.0, 3.2, 1.0, 0.7, 1 },
        { "Large A", K::room, 12.0, 12.0, 4.2, 0.9, 0.5, 1 },
        { "Large B (studio)", K::room, 15.0, 12.0, 6.0, 0.7, 0.45, 1 },
        { "Large C (warehouse)", K::room, 30.0, 20.0, 8.0, 2.2, 0.6, 1 },
        { "Hall A", K::room, 25.0, 20.0, 10.0, 1.8, 0.45, 1 },
        { "Hall B (theatre)", K::room, 30.0, 22.0, 12.0, 1.3, 0.5, 1 },
        { "Hall C (church)", K::room, 50.0, 20.0, 20.0, 4.0, 0.55, 1 },
    };
    constexpr int n = (int) (sizeof (specs) / sizeof (specs[0]));
    return specs[std::clamp (type, 0, n - 1)];
}

constexpr int roomOff = 0;
constexpr int numRoomTypes = 16;

inline bool roomHasTail (int type) noexcept
{
    const auto k = roomSpec (type).kind;
    return k == RoomKind::room || k == RoomKind::openWalls;
}

// Room parameter index in state version 1 (Off, Outdoor, Small, Medium, Large, Hall) -> current.
inline int migrateRoomIndexV1 (int oldIndex) noexcept
{
    static const int map[] = { 0, 1, 4, 7, 10, 13 };
    return map[std::clamp (oldIndex, 0, 5)];
}

class RoomModel
{
public:
    static constexpr int numLines = 16;
    static constexpr int numReflections = 6;

    void prepare (double sampleRate)
    {
        allocate (sampleRate);
        // Reference IR energies per room type for normalisation (done off the audio thread,
        // computed once per sample rate and shared by all instances).
        refEnergy = referenceEnergies (fs);
        configure (roomOff, 1.0);
        reset();
    }

    void reset() noexcept
    {
        input.reset();
        for (auto& l : lines)
            l.reset();
        for (auto& v : airState)
            v = 0.0f;
        for (auto& f : shelf)
            f.reset();
        for (auto& d : inDiffuser)
            d.reset();
        for (auto& ch : erDiffuser)
            for (auto& d : ch)
                d.reset();
        erLpL = erLpR = erLpM = inLp = 0.0f;
        snapGeometry();
    }

    int type() const noexcept { return roomType; }

    // Called on the audio thread when the room type changes: no allocation.
    void configure (int type, double decayScale) noexcept
    {
        roomType = type;
        const auto& s = roomSpec (type);
        double scale = 0.8; // diffusion time scale (open field: short)
        if (roomHasTail (type))
        {
            const double V = s.width * s.length * s.height;
            const double S = 2.0 * (s.width * s.length + s.width * s.height + s.length * s.height);
            const double mfpTime = 4.0 * V / S / kSpeedOfSound; // mean free path time (Kuttruff)
            // Line lengths spread over 1 : 2.3 around the mean free path, rounded to primes
            // (no common factors -> no periodic build-up; Jot & Chaigne 1991).
            const double d0 = std::clamp (0.75 * mfpTime, 0.005, 0.035);
            static const double jitter[numLines] = { 0, 0.013, -0.009, 0.017, -0.012, 0.006, -0.015, 0.011,
                                                     -0.004, 0.014, -0.011, 0.008, -0.016, 0.012, -0.007, 0.0 };
            int prev = 0;
            for (int i = 0; i < numLines; ++i)
            {
                const double m = std::pow (2.3, i / (double) (numLines - 1)) * (1.0 + jitter[i]);
                int d = nextPrime (std::max ((int) std::round (d0 * m * fs), prev + 2));
                delays[(size_t) i] = d;
                prev = d;
            }
            scale = std::clamp (std::sqrt (mfpTime * kSpeedOfSound / 3.3), 0.7, 2.2);
        }
        // Single-bounce wall absorption of reflections and of the diffuse-field input (sample-rate
        // independent one-pole): grass for the open field, otherwise from the room's HF ratio.
        const double erFc = s.kind == RoomKind::openField ? 1500.0
                            : s.kind == RoomKind::openWalls ? 5000.0
                                                             : std::clamp (2500.0 + 6000.0 * (s.hfRatio - 0.4), 2500.0, 6000.0);
        erWallCoeff = onePoleForGain (erFc, std::sqrt (0.5));
        erLpCoeff = (float) erWallCoeff;

        // Diffusers: short all-passes smear each discrete reflection into a dense cluster (no
        // comb filtering / flanging against the direct sound) and build the echo density of the tail.
        static const double erMs[3][3] = { { 0.61, 1.37, 2.63 }, { 0.79, 1.13, 2.91 }, { 0.67, 1.29, 2.47 } };
        static const float erG[3] = { 0.6f, 0.6f, 0.5f };
        for (int c = 0; c < 3; ++c)
            for (int k = 0; k < 3; ++k)
                erDiffuser[(size_t) c][(size_t) k].set ((int) std::round (erMs[c][k] * scale * 1.0e-3 * fs), erG[k]);
        static const double inMs[4] = { 1.31, 2.17, 3.73, 5.29 };
        static const float inG[4] = { 0.72f, 0.72f, 0.62f, 0.62f };
        for (int k = 0; k < 4; ++k)
            inDiffuser[(size_t) k].set ((int) std::round (inMs[k] * scale * 1.0e-3 * fs), inG[k]);

        lastDecay = -1.0;
        setDecay (decayScale);
    }

    void setDecay (double decayScale) noexcept
    {
        decay = std::clamp (decayScale, 0.25, 4.0);
        if (decay == lastDecay || ! roomHasTail (roomType))
            return;
        lastDecay = decay;
        const auto& s = roomSpec (roomType);
        const double t60 = s.t60 * decay;
        const double t60hf = t60 * s.hfRatio;
        // Frequency-dependent decay: walls (high shelf above ~2.5 kHz, T60 * hfRatio) plus air
        // absorption during the flight between reflections (ISO 9613-1, 20 degC / 50 %), which
        // dominates the top octaves in large rooms: T60(8 kHz) of a 4 s church is ~1 s.
        const double airExtraDbPerSecond = kSpeedOfSound * (models::airAbsorptionDbPerMetre (8000.0, 20.0, 50.0)
                                                            - models::airAbsorptionDbPerMetre (1000.0, 20.0, 50.0));
        const double wShelf = kTwoPi * 2500.0;
        const double cShelf = wShelf / std::tan (wShelf / (2.0 * fs));
        for (int i = 0; i < numLines; ++i)
        {
            const double secs = delays[(size_t) i] / fs;
            lineGain[(size_t) i] = (float) std::pow (10.0, -3.0 * secs / t60);
            const double hfGain = std::pow (10.0, -3.0 * secs * (1.0 / t60hf - 1.0 / t60));
            shelf[(size_t) i].setAnalog (hfGain / wShelf, 1.0, 1.0 / wShelf, 1.0, cShelf);
            airCoeff[(size_t) i] = (float) onePoleForGain (8000.0, std::pow (10.0, -airExtraDbPerSecond * secs / 20.0));
        }
        const double V = s.width * s.length * s.height;
        const double S = 2.0 * (s.width * s.length + s.width * s.height + s.length * s.height);
        wallReflection = std::sqrt (std::clamp (1.0 - 0.161 * V / (S * t60), 0.05, 0.95)); // Sabine mean absorption
        criticalDist = models::criticalDistance (V, t60);
        tailNorm = s.tailScale / std::sqrt (refEnergy[(size_t) roomType] * decay);
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

        if (roomSpec (roomType).kind == RoomKind::openField)
        {
            // Ground reflection only (source and listener at ear height 1.6 m); grass/soil lowers
            // the reflection factor (listening calibration).
            const double h = 1.6;
            const double dr = std::sqrt (r * r + 4.0 * h * h);
            const double g = 0.55 * std::pow (recDistance / dr, rolloffExp) * levelLin;
            double pl, pr;
            panGains (std::sin (azimuth), pl, pr);
            ramp (er[0].delay, (dr - r) / kSpeedOfSound * fs);
            setReflectionAir (dr);
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
        // Sources further away than the room is large stay a proportionate distance from the walls:
        // parking them right at a wall would add an artificial short-delay comb (flanging).
        const double margin = std::clamp (0.12 * std::min (W, L), 0.3, 3.0);
        double sx = std::clamp (lx + r * std::sin (azimuth), margin, W - margin);
        double sy = std::clamp (ly + r * std::cos (azimuth), margin, L - margin);
        const double sz = lz;
        const double rEff = std::max (0.05, std::sqrt ((sx - lx) * (sx - lx) + (sy - ly) * (sy - ly)));

        const double img[numReflections][3] = {
            { -sx, sy, sz }, { 2 * W - sx, sy, sz }, { sx, -sy, sz }, { sx, 2 * L - sy, sz }, { sx, sy, -sz }, { sx, sy, 2 * H - sz },
        };

        double firstDelay = 1.0e9, meanPath = 0.0;
        for (int i = 0; i < numReflections; ++i)
        {
            const double dx = img[i][0] - lx, dy = img[i][1] - ly, dz = img[i][2] - lz;
            const double d = std::sqrt (dx * dx + dy * dy + dz * dz);
            const double delaySamples = std::max (0.0, (d - rEff) / kSpeedOfSound * fs);
            const bool open = i == 5 && s.kind == RoomKind::openWalls; // no ceiling outdoors
            // The floor reflection is partly shadowed and absorbed by furniture, seats and people.
            const double surface = (i == 4 && s.kind == RoomKind::room) ? floorFactor : 1.0;
            const double g = open ? 0.0 : surface * wallReflection * std::pow (recDistance / d, rolloffExp) * levelLin;
            const double az = std::atan2 (dx, dy);
            double pl, pr;
            panGains (std::sin (az), pl, pr);
            ramp (er[(size_t) i].delay, delaySamples);
            ramp (er[(size_t) i].gainL, g * pl);
            ramp (er[(size_t) i].gainR, g * pr);
            if (! open)
                firstDelay = std::min (firstDelay, delaySamples);
            meanPath += d / numReflections;
        }
        setReflectionAir (meanPath);

        // Diffuse field level: equals the direct sound at the critical distance (Kuttruff).
        const double tail = std::pow (recDistance / criticalDist, rolloffExp) * levelLin * tailNorm;
        ramp (fdnGain, tail);
        // The diffuse field builds up from the first reflection on (scattering), not after all of them.
        ramp (fdnInDelay, firstDelay);
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
        float m;
        process (x, outL, outR, m);
    }

    // outM: dedicated mono output with the same total energy as L+R (reflections unpanned, its
    // own orthogonal combination of the FDN lines) — used for Mono Depth and mono tracks instead
    // of an L+R fold-down.
    void process (float x, float& outL, float& outR, float& outM) noexcept
    {
        input.push (x);
        float eL = 0, eR = 0, eM = 0;
        const int nRefl = roomSpec (roomType).kind == RoomKind::openField ? 1 : numReflections;
        for (int i = 0; i < nRefl; ++i)
        {
            auto& e = er[(size_t) i];
            e.delay.step();
            e.gainL.step();
            e.gainR.step();
            const float v = input.read ((float) e.delay.cur);
            eL += (float) e.gainL.cur * v;
            eR += (float) e.gainR.cur * v;
            eM += (float) std::sqrt (e.gainL.cur * e.gainL.cur + e.gainR.cur * e.gainR.cur) * v;
        }
        erLpL = flushDenormal (eL + erLpCoeff * (erLpL - eL));
        erLpR = flushDenormal (eR + erLpCoeff * (erLpR - eR));
        erLpM = flushDenormal (eM + erLpCoeff * (erLpM - eM));
        outL = erDiffuser[0][2].process (erDiffuser[0][1].process (erDiffuser[0][0].process (erLpL)));
        outR = erDiffuser[1][2].process (erDiffuser[1][1].process (erDiffuser[1][0].process (erLpR)));
        outM = erDiffuser[2][2].process (erDiffuser[2][1].process (erDiffuser[2][0].process (erLpM)));

        fdnGain.step();
        fdnInDelay.step();
        if (roomHasTail (roomType))
        {
            const float raw = input.read ((float) fdnInDelay.cur) * (float) fdnGain.cur;
            inLp = flushDenormal (raw + erLpCoeff * (inLp - raw));
            float in = inLp;
            for (auto& d : inDiffuser)
                in = d.process (in);

            // 16-line FDN with per-line wall + air absorption and a normalised 16x16 Hadamard
            // feedback matrix (fast Walsh-Hadamard transform): every line feeds every other line
            // equally, so no line rings on its own as a comb filter (a Householder matrix of this
            // size keeps 7/8 of each line in itself).
            float s[numLines], fb[numLines];
            for (int i = 0; i < numLines; ++i)
            {
                const float v = shelf[(size_t) i].process (lines[(size_t) i].at (delays[(size_t) i] - 1)) * lineGain[(size_t) i];
                const float a = airCoeff[(size_t) i];
                airState[(size_t) i] = flushDenormal (v + a * (airState[(size_t) i] - v));
                s[i] = fb[i] = airState[(size_t) i];
            }
            for (int h = 1; h < numLines; h <<= 1)
                for (int i = 0; i < numLines; i += h << 1)
                    for (int j = i; j < i + h; ++j)
                    {
                        const float u = fb[j], w = fb[j + h];
                        fb[j] = u + w;
                        fb[j + h] = u - w;
                    }
            float tL = 0, tR = 0, tM = 0;
            for (int i = 0; i < numLines; ++i)
            {
                lines[(size_t) i].push ((fb[i] + (float) hadamardSign (inRow, i) * in) * 0.25f);
                tL += (float) hadamardSign (lRow, i) * s[i];
                tR += (float) hadamardSign (rRow, i) * s[i];
                tM += (float) hadamardSign (mRow, i) * s[i];
            }
            outL += tL * 0.25f;
            outR += tR * 0.25f;
            outM += tM * 0.35355339f;
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
        const int nRefl = roomSpec (roomType).kind == RoomKind::openField ? 1 : numReflections;
        for (int i = 0; i < nRefl; ++i)
            e += er[(size_t) i].gainL.target * er[(size_t) i].gainL.target + er[(size_t) i].gainR.target * er[(size_t) i].gainR.target;
        if (roomHasTail (roomType))
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

    // Schroeder all-pass with a preallocated buffer, so its length can change without allocating.
    struct Diffuser
    {
        std::vector<float> buf;
        int len = 1, pos = 0;
        float g = 0.5f;

        void prepare (int maxLen) { buf.assign ((size_t) std::max (1, maxLen), 0.0f); }
        void set (int length, float gain) noexcept
        {
            len = std::clamp (length, 1, (int) buf.size());
            pos = 0;
            g = gain;
        }
        void reset() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }
        float process (float x) noexcept
        {
            const float d = buf[(size_t) pos];
            const float v = x + g * d;
            buf[(size_t) pos] = flushDenormal (v);
            if (++pos >= len)
                pos = 0;
            return d - g * v;
        }
    };

    static constexpr double floorFactor = 0.6; // listening calibration
    static constexpr int inRow = 5, lRow = 3, rRow = 6, mRow = 12; // Sylvester-Hadamard rows (orthogonal, zero-sum)
    static constexpr int hadamardSign (int row, int col) noexcept
    {
        int v = row & col, parity = 0;
        while (v != 0)
        {
            parity ^= v & 1;
            v >>= 1;
        }
        return parity != 0 ? -1 : 1;
    }

    static int nextPrime (int n) noexcept
    {
        auto isPrime = [] (int v)
        {
            if (v < 2)
                return false;
            for (int k = 2; k * k <= v; ++k)
                if (v % k == 0)
                    return false;
            return true;
        };
        while (! isPrime (n))
            ++n;
        return n;
    }

    // Reflection low-pass = wall absorption x air absorption over the (mean) reflection path,
    // matched at 8 kHz (ISO 9613-1, 20 degC / 50 %).
    void setReflectionAir (double pathMetres) noexcept
    {
        const double w = std::cos (kTwoPi * std::min (8000.0, 0.45 * fs) / fs);
        const double a = erWallCoeff;
        const double wallGain2 = (1.0 - a) * (1.0 - a) / (1.0 - 2.0 * a * w + a * a);
        const double airGain = std::pow (10.0, -air8k * pathMetres / 20.0);
        erLpCoeff = (float) onePoleForGain (8000.0, std::sqrt (wallGain2) * airGain);
    }

    // One-pole low-pass y = x + a (y - x) with gain `gain` at frequency f (DC gain 1).
    double onePoleForGain (double f, double gain) const noexcept
    {
        const double G2 = std::clamp (gain * gain, 1.0e-6, 0.999999);
        const double cw = std::cos (kTwoPi * std::min (f, 0.45 * fs) / fs);
        const double b = 1.0 - G2 * cw, c = 1.0 - G2;
        return std::clamp ((b - std::sqrt (std::max (0.0, b * b - c * c))) / c, 0.0, 0.999);
    }

    static void panGains (double p, double& l, double& r) noexcept
    {
        const double th = (std::clamp (p, -1.0, 1.0) + 1.0) * kPi / 4.0;
        l = std::cos (th);
        r = std::sin (th);
    }

    void allocate (double sampleRate)
    {
        fs = sampleRate;
        input.prepare ((int) std::ceil (0.80 * fs));
        for (auto& l : lines)
            l.prepare ((int) std::ceil (0.10 * fs));
        for (auto& d : inDiffuser)
            d.prepare ((int) std::ceil (0.013 * fs));
        for (auto& ch : erDiffuser)
            for (auto& d : ch)
                d.prepare ((int) std::ceil (0.007 * fs));
    }

    static std::array<double, numRoomTypes> referenceEnergies (double sampleRate)
    {
        static std::mutex mutex;
        static std::map<long long, std::array<double, numRoomTypes>> cache;
        const std::lock_guard<std::mutex> lock (mutex);
        const auto key = (long long) std::llround (sampleRate);
        if (auto it = cache.find (key); it != cache.end())
            return it->second;
        RoomModel probe;
        probe.allocate (sampleRate);
        probe.refEnergy.fill (1.0);
        std::array<double, numRoomTypes> e {};
        for (int t = 0; t < numRoomTypes; ++t)
            e[(size_t) t] = roomHasTail (t) ? probe.measureTailEnergy (t) : 1.0;
        cache[key] = e;
        return e;
    }

    // Tail IR energy (sum over L+R) for unit input gain in the mid band (500 Hz - 2 kHz, relative to
    // a unit impulse), used to normalise the tail so its level equals the diffuse-field target
    // where the critical-distance model applies; the frequency-dependent decay then only darkens
    // the top instead of lifting the rest.
    double measureTailEnergy (int type)
    {
        Biquad hp1, hp2, lp1, lp2;
        hp1.set (designBiquadHz (BiquadType::highPass, fs, 500.0, 0.7071, 0.0));
        hp2 = hp1;
        lp1.set (designBiquadHz (BiquadType::lowPass, fs, 2000.0, 0.7071, 0.0));
        lp2 = lp1;
        auto band = [&] (Biquad (&f)[4], float x) { return f[3].process (f[2].process (f[1].process (f[0].process (x)))); };
        Biquad fl[4] = { hp1, hp2, lp1, lp2 }, fr[4] = { hp1, hp2, lp1, lp2 }, fi[4] = { hp1, hp2, lp1, lp2 };

        configure (type, 1.0);
        reset();
        fdnGain.cur = fdnGain.target = 1.0;
        fdnInDelay.cur = fdnInDelay.target = 0.0;
        for (auto& e : er)
        {
            e.gainL.cur = e.gainR.cur = 0;
            e.gainL.target = e.gainR.target = 0;
        }
        const int len = (int) (roomSpec (type).t60 * 1.6 * fs);
        double energy = 0, reference = 0;
        for (int n = 0; n < len; ++n)
        {
            float l, r;
            const float x = n == 0 ? 1.0f : 0.0f;
            process (x, l, r);
            l = band (fl, l);
            r = band (fr, r);
            const float ref = band (fi, x);
            energy += (double) l * l + (double) r * r;
            reference += (double) ref * ref;
        }
        return std::max (energy / std::max (reference, 1.0e-12), 1.0e-9);
    }

    double fs = 48000.0;
    int roomType = roomOff;
    double decay = 1.0, lastDecay = -1.0;
    DelayLine input;
    std::array<DelayLine, numLines> lines;
    std::array<int, numLines> delays {};
    std::array<float, numLines> lineGain {}, airCoeff {}, airState {};
    std::array<FirstOrder, numLines> shelf {};
    std::array<Diffuser, 4> inDiffuser;
    std::array<std::array<Diffuser, 3>, 3> erDiffuser;
    std::array<Reflection, numReflections> er {};
    std::array<double, numRoomTypes> refEnergy {};
    Ramp fdnGain, fdnInDelay;
    double wallReflection = 0.8, criticalDist = 1.0, tailNorm = 1.0;
    double erWallCoeff = 0.45;
    static inline const double air8k = models::airAbsorptionDbPerMetre (8000.0, 20.0, 50.0);
    float erLpCoeff = 0.45f, erLpL = 0, erLpR = 0, erLpM = 0, inLp = 0;
    int silentCounter = 0;
};

} // namespace amb
