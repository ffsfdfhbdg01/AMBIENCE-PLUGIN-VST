// Room model: first-order image-source early reflections (Allen & Berkley 1979) for a
// shoebox room, diffused into short clusters, + a 16-line feedback delay network late reverb
// (Jot & Chaigne 1991) with input diffusion and frequency-dependent decay (wall absorption and
// ISO 9613-1 air absorption). Deterministic: fixed delays, no random modulation.
#pragma once


#include "AcousticModels.h"
#include "Surround.h"

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
    double bassRatio = 1.0;       // T60(125-250 Hz) / T60(mid) (Beranek's bass ratio)
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
        { "Medium C (empty)", K::room, 9.0, 7.0, 3.2, 1.0, 0.7, 1, 1.1 },
        { "Large A", K::room, 12.0, 12.0, 4.2, 0.9, 0.5, 1, 1.1 },
        { "Large B (studio)", K::room, 15.0, 12.0, 6.0, 0.7, 0.45, 1, 0.95 },
        { "Large C (warehouse)", K::room, 30.0, 20.0, 8.0, 2.2, 0.6, 1, 1.2 },
        { "Hall A", K::room, 25.0, 20.0, 10.0, 1.8, 0.45, 1, 1.2 },
        { "Hall B (theatre)", K::room, 30.0, 22.0, 12.0, 1.3, 0.5, 1, 1.1 },
        { "Hall C (church)", K::room, 50.0, 20.0, 20.0, 4.0, 0.55, 1, 1.3 },
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

namespace detail
{
    // Sign of entry (row, col) of the Sylvester-Hadamard matrix.
    constexpr int hadamardSign (int row, int col) noexcept
    {
        int v = row & col, parity = 0;
        while (v != 0)
        {
            parity ^= v & 1;
            v >>= 1;
        }
        return parity != 0 ? -1 : 1;
    }

    template <int N>
    constexpr std::array<float, N> hadamardRow (int row) noexcept
    {
        std::array<float, N> r {};
        for (int i = 0; i < N; ++i)
            r[(size_t) i] = (float) hadamardSign (row, i);
        return r;
    }
} // namespace detail

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
        refData = referenceData();
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
        for (int i = 0; i < numLines; ++i)
        {
            auto& o = lfo[(size_t) i];
            const double phase = kTwoPi * i / numLines * 5.0; // spread start phases
            o.c = (float) std::cos (phase);
            o.s = (float) std::sin (phase);
            o.x1 = o.y1 = 0.0f;
        }
        for (auto& f : shelf)
            f.reset();
        for (auto& f : lowShelf)
            f.reset();
        for (auto& f : shadowHp)
            f.reset();
        tailSideHp.reset();
        for (auto& d : inDiffuser)
            d.reset();
        for (auto& ch : erDiffuser)
            for (auto& d : ch)
                d.reset();
        for (auto& d : erDiffuserEar)
            d.reset();
        for (auto& ch : erDiffMulti)
            for (auto& d : ch)
                d.reset();
        erLpMulti.fill (0.0f);
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
            const double d0 = std::clamp (0.75 * mfpTime, 0.012, 0.035); // >= 12 ms: enough modal density in small rooms
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
        // Headphones: the right ear hears the same scattering cluster as the left (only delayed and
        // shadowed), otherwise the interaural time cue of the reflections would be lost.
        for (int k = 0; k < 3; ++k)
            erDiffuserEar[(size_t) k].set ((int) std::round (erMs[0][k] * scale * 1.0e-3 * fs), erG[k]);
        diffusionScale = scale;
        setMultiDiffusers();
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
        const double t60lf = t60 * s.bassRatio;
        const double wLow = kTwoPi * 250.0;
        const double cLow = wLow / std::tan (wLow / (2.0 * fs));
        for (int i = 0; i < numLines; ++i)
        {
            const double secs = delays[(size_t) i] / fs;
            lineGain[(size_t) i] = (float) std::pow (10.0, -3.0 * secs / t60);
            const double hfGain = std::pow (10.0, -3.0 * secs * (1.0 / t60hf - 1.0 / t60));
            shelf[(size_t) i].setAnalog (hfGain / wShelf, 1.0, 1.0 / wShelf, 1.0, cShelf);
            // Low shelf (DC gain lfGain, unity above ~250 Hz): longer bass decay in large rooms.
            const double lfGain = std::pow (10.0, -3.0 * secs * (1.0 / t60lf - 1.0 / t60));
            lowShelf[(size_t) i].setAnalog (1.0 / wLow, lfGain, 1.0 / wLow, 1.0, cLow);
            airCoeff[(size_t) i] = (float) onePoleForGain (8000.0, std::pow (10.0, -airExtraDbPerSecond * secs / 20.0));
        }
        const double V = s.width * s.length * s.height;
        const double S = 2.0 * (s.width * s.length + s.width * s.height + s.length * s.height);
        wallReflection = std::sqrt (std::clamp (1.0 - 0.161 * V / (S * t60), 0.05, 0.95)); // Sabine mean absorption
        criticalDist = models::criticalDistance (V, t60);
        tailNorm = s.tailScale / std::sqrt (refData[(size_t) roomType].energy * decay);
    }

    double criticalDistance() const noexcept { return criticalDist; }

    // Called once per control tick. Source position: distance r [m], azimuth [rad] (0 front,
    // + right). levelLin = Room Level, rolloffExp = rolloff/6.02 (1 = inverse distance law).
    void setGeometry (double r, double azimuth, double recDistance, double rolloffExp, double levelLin, int tickLen) noexcept
    {
        const double inv = 1.0 / std::max (tickLen, 1);
        // Keep the per-ear path running for a few ticks after leaving headphones, until the ramps
        // towards the loudspeaker targets have finished.
        binauralTicks = binaural > 0.0 ? 3 : std::max (0, binauralTicks - 1);
        auto ramp = [inv] (Ramp& rp, double target)
        {
            rp.target = target;
            rp.inc = (target - rp.cur) * inv;
        };

        if (roomType == roomOff)
        {
            for (int i = 0; i < numReflections; ++i)
                silenceReflection (i, inv);
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
            setReflection (0, (dr - r) / kSpeedOfSound * fs, g, azimuth, inv);
            setReflectionAir (dr);
            for (int i = 1; i < numReflections; ++i)
                silenceReflection (i, inv);
            ramp (fdnGain, 0.0);
            ramp (fdnInDelay, fdnInDelay.cur);
            return;
        }

        const auto& s = roomSpec (roomType);
        const double W = s.width, L = s.length, H = s.height;
        // Listener / microphone position from Proximity: 0 = room centre (reflections late and even),
        // 0.5 = centre line, 30 % of the length from the front wall (default), 1 = near a corner
        // (strong reflections right after the direct sound; cf. "Proximity" in film reverbs).
        const double lx = W * (0.5 - 0.8 * std::max (0.0, proximity - 0.5));
        const double ly = L * (0.5 - 0.4 * proximity);
        const double lz = std::min (1.6, 0.6 * H);
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
            setReflection (i, delaySamples, g, std::atan2 (dx, dy), inv, i == 5);
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

    // Multichannel output (5.0 ... 7.1.4). Call at prepare time (allocates). Reflections are panned
    // over the loudspeaker ring (the ceiling reflection onto the height layer), each channel gets
    // its own diffusion cluster and its own orthogonal combination of the FDN lines; the total tail
    // energy equals the stereo tail (centre -6 dB, heights -1.5 dB, LFE silent).
    void setLayout (const SpeakerLayout& l)
    {
        layout = l;
        multi = l.isSurround();
        for (auto& ch : erDiffMulti)
            for (auto& d : ch)
                d.prepare ((int) std::ceil (0.009 * fs));
        static const int rows[maxOutputChannels] = { 3, 6, 9, 10, 12, 15, 7, 11, 13, 14, 1, 2 }; // zero-sum, != inRow
        double sumP = 0.0;
        std::array<double, maxOutputChannels> power {};
        for (int c = 0; c < l.numChannels; ++c)
        {
            const auto& sp = l.speakers[(size_t) c];
            power[(size_t) c] = sp.lfe ? 0.0 : (c == l.centre ? 0.25 : (sp.elevationDeg > 10.0 ? 0.7 : 1.0));
            sumP += power[(size_t) c];
        }
        for (int c = 0; c < maxOutputChannels; ++c)
        {
            tailWeight[(size_t) c] = c < l.numChannels && sumP > 0.0 ? (float) (0.25 * std::sqrt (2.0 * power[(size_t) c] / sumP)) : 0.0f;
            for (int i = 0; i < numLines; ++i)
                tailSign[(size_t) c][(size_t) i] = (float) detail::hadamardSign (rows[c], i);
        }
        setMultiDiffusers();
        reset();
    }

    // Multichannel counterpart of process(): outs[c] for the layout's channels, plus the mono output.
    void processMulti (float x, float* outs, float& outM) noexcept
    {
        input.push (x);
        const int nCh = layout.numChannels;
        float e[maxOutputChannels] = {};
        float eM = 0;
        const int nRefl = roomSpec (roomType).kind == RoomKind::openField ? 1 : numReflections;
        for (int i = 0; i < nRefl; ++i)
        {
            auto& r = er[(size_t) i];
            r.delay.step();
            r.gainL.step();
            r.gainR.step();
            const float v = input.read ((float) r.delay.cur);
            eM += (float) std::sqrt (r.gainL.cur * r.gainL.cur + r.gainR.cur * r.gainR.cur) * v;
            auto& g = erMulti[(size_t) i];
            for (int c = 0; c < nCh; ++c)
            {
                g[(size_t) c].step();
                e[c] += (float) g[(size_t) c].cur * v;
            }
        }
        for (int c = 0; c < nCh; ++c)
        {
            float& lp = erLpMulti[(size_t) c];
            lp = flushDenormal (e[c] + erLpCoeff * (lp - e[c]));
            auto& d = erDiffMulti[(size_t) c];
            outs[c] = d[2].process (d[1].process (d[0].process (lp)));
        }
        erLpM = flushDenormal (eM + erLpCoeff * (erLpM - eM));
        outM = erDiffuser[2][2].process (erDiffuser[2][1].process (erDiffuser[2][0].process (erLpM)));

        fdnGain.step();
        fdnInDelay.step();
        if (roomHasTail (roomType))
        {
            float s[numLines];
            tailStep (s);
            static constexpr auto mSign = detail::hadamardRow<numLines> (mRow);
            float tM = 0;
            for (int i = 0; i < numLines; ++i)
                tM += mSign[(size_t) i] * s[i];
            outM += tM * 0.35355339f;
            for (int c = 0; c < nCh; ++c)
            {
                const float w = tailWeight[(size_t) c];
                if (w == 0.0f)
                    continue;
                const auto& sign = tailSign[(size_t) c];
                float t = 0;
                for (int i = 0; i < numLines; ++i)
                    t += sign[(size_t) i] * s[i];
                outs[c] += w * t;
            }
        }
        for (int c = 0; c < nCh; ++c)
            if (layout.speakers[(size_t) c].lfe)
                outs[c] = 0.0f;
        silentCounter = roomType == roomOff ? std::min (silentCounter + 1, 1 << 30) : 0;
    }

    // Headphones rendering amount (0 = loudspeakers, 1 = binaural): reflections get per-ear delays
    // (Woodworth / Brown & Duda) and head shadow instead of amplitude panning; the tail gets a
    // natural interaural coherence at low frequencies. Call before setGeometry().
    void setBinaural (double amount) noexcept { binaural = std::clamp (amount, 0.0, 1.0); }

    // Listener / microphone distance to the walls, 0..1 (see setGeometry). Call before setGeometry().
    void setProximity (double amount) noexcept { proximity = std::clamp (amount, 0.0, 1.0); }

    void snapGeometry() noexcept
    {
        for (auto& e : er)
            for (Ramp* rp : { &e.delay, &e.delayR, &e.gainL, &e.gainR, &e.shadowL, &e.shadowR })
            {
                rp->cur = rp->target;
                rp->inc = 0;
            }
        for (auto& ref : erMulti)
            for (auto& m : ref)
            {
                m.cur = m.target;
                m.inc = 0;
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
        const bool ears = binaural > 0.0 || binauralTicks > 0;
        float shL = 0, shR = 0;
        for (int i = 0; i < nRefl; ++i)
        {
            auto& e = er[(size_t) i];
            e.delay.step();
            e.gainL.step();
            e.gainR.step();
            const float v = input.read ((float) e.delay.cur);
            eM += (float) std::sqrt (e.gainL.cur * e.gainL.cur + e.gainR.cur * e.gainR.cur) * v;
            if (ears)
            {
                e.delayR.step();
                e.shadowL.step();
                e.shadowR.step();
                const float vR = input.read ((float) e.delayR.cur);
                eL += (float) e.gainL.cur * v;
                eR += (float) e.gainR.cur * vR;
                shL += (float) e.shadowL.cur * v;
                shR += (float) e.shadowR.cur * vR;
            }
            else
            {
                eL += (float) e.gainL.cur * v;
                eR += (float) e.gainR.cur * v;
            }
        }
        if (ears)
        {
            // Head shadow of every reflection: H = 1 + (h - 1) HP1(s) with the Brown & Duda corner,
            // so one shared first-order high-pass per ear is exact for all reflections.
            eL += shadowHp[0].process (shL);
            eR += shadowHp[1].process (shR);
        }
        erLpL = flushDenormal (eL + erLpCoeff * (erLpL - eL));
        erLpR = flushDenormal (eR + erLpCoeff * (erLpR - eR));
        erLpM = flushDenormal (eM + erLpCoeff * (erLpM - eM));
        outL = erDiffuser[0][2].process (erDiffuser[0][1].process (erDiffuser[0][0].process (erLpL)));
        outR = erDiffuser[1][2].process (erDiffuser[1][1].process (erDiffuser[1][0].process (erLpR)));
        if (ears)
        {
            const float same = erDiffuserEar[2].process (erDiffuserEar[1].process (erDiffuserEar[0].process (erLpR)));
            outR += (float) binaural * (same - outR);
        }
        outM = erDiffuser[2][2].process (erDiffuser[2][1].process (erDiffuser[2][0].process (erLpM)));

        fdnGain.step();
        fdnInDelay.step();
        if (roomHasTail (roomType))
        {
            float s[numLines];
            tailStep (s);
            static constexpr auto lSign = detail::hadamardRow<numLines> (lRow), rSign = detail::hadamardRow<numLines> (rRow),
                                  mSign = detail::hadamardRow<numLines> (mRow);
            float tL = 0, tR = 0, tM = 0;
            for (int i = 0; i < numLines; ++i)
            {
                tL += lSign[(size_t) i] * s[i];
                tR += rSign[(size_t) i] * s[i];
                tM += mSign[(size_t) i] * s[i];
            }
            tL *= 0.25f;
            tR *= 0.25f;
            if (binaural > 0.0 || binauralTicks > 0)
            {
                // Binaural diffuse field: both ears receive nearly the same low frequencies (the head
                // is small against the wavelength; coherence ~ sinc(k d)). The side of the tail is
                // high-passed at ~400 Hz (Menzer & Faller 2009 principle).
                const float mid = 0.5f * (tL + tR), side = 0.5f * (tL - tR);
                const float sideHp = tailSideHp.process (side);
                const float sd = side + (float) binaural * (sideHp - side);
                tL = mid + sd;
                tR = mid - sd;
            }
            outL += tL;
            outR += tR;
            outM += tM * 0.35355339f;
        }

        if (roomType == roomOff)
            silentCounter = std::min (silentCounter + 1, 1 << 30);
        else
            silentCounter = 0;
    }

    // One sample of the late reverb: input diffusion, modulated lines with wall/air absorption and
    // the Hadamard feedback. s receives the 16 line outputs, combined by orthogonal output rows.
    void tailStep (float* s) noexcept
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
        float fb[numLines];
        for (int i = 0; i < numLines; ++i)
        {
            // Slow, subtle delay modulation (deterministic quadrature oscillators, distinct rates):
            // time-variation smears the fixed resonances of the network, so long tails do not
            // ring metallically (cf. Lexicon "spin/wander", Dattorro 1997). Pitch deviation < 2.5 cents.
            auto& o = lfo[(size_t) i];
            const float c = o.c * o.cr - o.s * o.sr, sn = o.s * o.cr + o.c * o.sr;
            o.c = c;
            o.s = sn;
            // Fractional read with a first-order all-pass interpolator (flat magnitude, cheap).
            const float d = (float) (delays[(size_t) i] - 1) - modDepth * (1.0f + sn);
            const int di = (int) d;
            const float frac = d - (float) di;
            const float ap = (1.0f - frac) / (1.0f + frac);
            const float xin = lines[(size_t) i].at (di);
            const float tap = flushDenormal (ap * (xin - o.y1) + o.x1);
            o.x1 = xin;
            o.y1 = tap;
            const float v = lowShelf[(size_t) i].process (shelf[(size_t) i].process (tap)) * lineGain[(size_t) i];
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
        static constexpr auto inSign = detail::hadamardRow<numLines> (inRow);
        for (int i = 0; i < numLines; ++i)
            lines[(size_t) i].push ((fb[i] + inSign[(size_t) i] * in) * 0.25f);
    }

    // Energy gain (L + R) of the reverb (ER + tail) at frequency f relative to the input, for the
    // model-based auto gain: reflections through their low-pass (the diffusers are all-passes),
    // tail from its measured, normalised spectrum.
    double predictedEnergy (double f) const noexcept
    {
        if (roomType == roomOff)
            return 0.0;
        double e = 0;
        const int nRefl = roomSpec (roomType).kind == RoomKind::openField ? 1 : numReflections;
        for (int i = 0; i < nRefl; ++i)
            e += er[(size_t) i].gainL.target * er[(size_t) i].gainL.target + er[(size_t) i].gainR.target * er[(size_t) i].gainR.target;
        const double a = erLpCoeff, cw = std::cos (kTwoPi * std::min (f, 0.49 * fs) / fs);
        e *= (1.0 - a) * (1.0 - a) / (1.0 - 2.0 * a * cw + a * a);
        if (roomHasTail (roomType))
        {
            const auto& ref = refData[(size_t) roomType];
            const double pos = std::clamp (std::log2 (f / bandCentre (0)), 0.0, (double) (numBands - 1));
            const int i0 = std::min ((int) pos, numBands - 2);
            const double shapeDb = lerp (ref.shapeDb[(size_t) i0], ref.shapeDb[(size_t) i0 + 1], pos - i0);
            e += fdnGain.target * fdnGain.target * ref.energy * std::pow (10.0, shapeDb / 10.0);
        }
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
        Ramp delay, delayR, gainL, gainR, shadowL, shadowR; // delay = left ear / loudspeakers
    };

    // Reflection targets: loudspeakers (amplitude panning, one delay) blended with headphones (per-ear
    // delay and head-shadow high-frequency gain h, applied as gain * (h - 1) on the shadow bus).
    void setReflection (int i, double delaySamples, double g, double az, double inv, bool ceiling = false) noexcept
    {
        if (multi)
        {
            float gains[maxOutputChannels];
            layout.pan (az * 180.0 / kPi, ceiling && layout.hasHeights(), gains);
            auto& m = erMulti[(size_t) i];
            for (int c = 0; c < layout.numChannels; ++c)
            {
                m[(size_t) c].target = g * gains[c];
                m[(size_t) c].inc = (m[(size_t) c].target - m[(size_t) c].cur) * inv;
            }
        }
        auto ramp = [inv] (Ramp& rp, double target)
        {
            rp.target = target;
            rp.inc = (target - rp.cur) * inv;
        };
        auto& e = er[(size_t) i];
        double pl, pr;
        panGains (std::sin (az), pl, pr);
        const double b = binaural;
        // Angle between the reflection and each ear axis (left ear at -90 deg, right at +90 deg).
        auto wrap = [] (double a) { a = std::fmod (std::abs (a), kTwoPi); return a > kPi ? kTwoPi - a : a; };
        const double thL = wrap (az + kPi / 2), thR = wrap (az - kPi / 2);
        const double alphaSide = models::headShadowAlpha (kPi / 2);
        const double hL = models::headShadowAlpha (thL) / alphaSide, hR = models::headShadowAlpha (thR) / alphaSide;
        const double ear = std::sqrt (0.5);
        // The shadow shelf's own low-frequency phase delay is removed (as for the direct sound) so the
        // interaural delay in the ITD-dominant band follows Woodworth.
        const double wc = 2.0 * kSpeedOfSound / models::kHeadRadius, wRef = kTwoPi * 750.0;
        auto earDelay = [&] (double theta, double h)
        { return models::earDelaySeconds (theta) + (std::atan (h * wRef / wc) - std::atan (wRef / wc)) / wRef; };
        const double dL = earDelay (thL, hL), dR = earDelay (thR, hR), dMin = std::min (dL, dR);
        ramp (e.delay, delaySamples + b * (dL - dMin + models::earDelaySeconds (0.0)) * fs);
        ramp (e.delayR, delaySamples + b * (dR - dMin + models::earDelaySeconds (0.0)) * fs);
        ramp (e.gainL, g * lerp (pl, ear, b));
        ramp (e.gainR, g * lerp (pr, ear, b));
        ramp (e.shadowL, g * ear * b * (hL - 1.0));
        ramp (e.shadowR, g * ear * b * (hR - 1.0));
    }

    // Per-channel diffusion clusters: the base delays stretched by a channel-dependent factor, so every
    // loudspeaker gets its own (mutually decorrelated) cluster.
    void setMultiDiffusers() noexcept
    {
        if (! multi)
            return;
        static const double baseMs[3] = { 0.61, 1.37, 2.63 };
        static const float g[3] = { 0.6f, 0.6f, 0.5f };
        for (int c = 0; c < layout.numChannels; ++c)
            for (int k = 0; k < 3; ++k)
            {
                const double stretch = 1.0 + 0.071 * c + 0.037 * ((c * (k + 2)) % 5);
                erDiffMulti[(size_t) c][(size_t) k].set ((int) std::round (baseMs[k] * stretch * diffusionScale * 1.0e-3 * fs), g[k]);
            }
    }

    void silenceReflection (int i, double inv) noexcept
    {
        for (auto& m : erMulti[(size_t) i])
        {
            m.target = 0.0;
            m.inc = -m.cur * inv;
        }
        auto& e = er[(size_t) i];
        for (Ramp* rp : { &e.gainL, &e.gainR, &e.shadowL, &e.shadowR })
        {
            rp->target = 0.0;
            rp->inc = -rp->cur * inv;
        }
        e.delay.inc = e.delayR.inc = 0.0;
        e.delay.target = e.delay.cur;
        e.delayR.target = e.delayR.cur;
    }

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
    static constexpr double modDepthMs = 0.5;  // peak-to-peak line modulation (tail spectrum close to Gaussian noise)
    static constexpr int inRow = 5, lRow = 3, rRow = 6, mRow = 12; // Sylvester-Hadamard rows (orthogonal, zero-sum)
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
        {
            const double wc = 2.0 * kSpeedOfSound / models::kHeadRadius; // Brown & Duda head-shadow corner
            for (auto& f : shadowHp)
                f.setAnalog (1.0 / wc, 0.0, 1.0 / wc, 1.0, wc / std::tan (wc / (2.0 * fs)));
            const double ws = kTwoPi * 400.0;
            tailSideHp.setAnalog (1.0 / ws, 0.0, 1.0 / ws, 1.0, ws / std::tan (ws / (2.0 * fs)));
        }
        modDepth = (float) (modDepthMs * 1.0e-3 * fs * 0.5);
        for (int i = 0; i < numLines; ++i)
        {
            const double rate = 0.15 + 0.65 * i / (numLines - 1) * (i % 2 == 0 ? 1.0 : 0.93); // 0.15 - 0.8 Hz
            lfo[(size_t) i].cr = (float) std::cos (kTwoPi * rate / fs);
            lfo[(size_t) i].sr = (float) std::sin (kTwoPi * rate / fs);
        }
        input.prepare ((int) std::ceil (0.80 * fs));
        for (auto& l : lines)
            l.prepare ((int) std::ceil (0.10 * fs));
        for (auto& d : inDiffuser)
            d.prepare ((int) std::ceil (0.013 * fs));
        for (auto& ch : erDiffuser)
            for (auto& d : ch)
                d.prepare ((int) std::ceil (0.007 * fs));
        for (auto& d : erDiffuserEar)
            d.prepare ((int) std::ceil (0.007 * fs));
    }

    static constexpr int numBands = 10; // octave bands 31.5 Hz - 16 kHz
    static double bandCentre (int b) noexcept { return 31.25 * std::pow (2.0, b); }

    struct RefData
    {
        double energy = 1.0;                // L+R tail energy gain in 500 Hz - 2 kHz for unit FDN gain
        std::array<double, numBands> shapeDb {}; // octave-band tail energy relative to that band
    };

    // Measured once per process at 48 kHz (the values are ratios to a unit impulse and do not depend
    // on the sample rate), so preparing at 96 or 192 kHz costs nothing extra.
    static std::array<RefData, numRoomTypes> referenceData()
    {
        static const std::array<RefData, numRoomTypes> data = []
        {
            RoomModel probe;
            probe.allocate (48000.0);
            std::array<RefData, numRoomTypes> d {};
            probe.refData = d;
            for (int t = 0; t < numRoomTypes; ++t)
                if (roomHasTail (t))
                    d[(size_t) t] = probe.measureTail (t);
            return d;
        }();
        return data;
    }

    // Tail IR energy (sum over L+R) for unit FDN gain, relative to a unit impulse, in the mid band
    // (500 Hz - 2 kHz) and in octave bands. The mid-band value normalises the tail so its level
    // equals the diffuse-field target where the critical-distance model applies (the frequency-
    // dependent decay then only darkens the top); the band shape feeds the auto gain model.
    RefData measureTail (int type)
    {
        struct BandPass
        {
            Biquad f[4];
            BandPass (double sr, double lo, double hi)
            {
                f[0].set (designBiquadHz (BiquadType::highPass, sr, lo, 0.7071, 0.0));
                f[1] = f[0];
                f[2].set (designBiquadHz (BiquadType::lowPass, sr, std::min (hi, 0.45 * sr), 0.7071, 0.0));
                f[3] = f[2];
            }
            float process (float x) noexcept { return f[3].process (f[2].process (f[1].process (f[0].process (x)))); }
        };
        // [band][signal]: 0 = L, 1 = R, 2 = reference impulse; band numBands = mid band.
        std::vector<std::array<BandPass, 3>> bp;
        for (int b = 0; b <= numBands; ++b)
        {
            const double lo = b < numBands ? bandCentre (b) / std::sqrt (2.0) : 500.0;
            const double hi = b < numBands ? bandCentre (b) * std::sqrt (2.0) : 2000.0;
            bp.push_back ({ BandPass (fs, lo, hi), BandPass (fs, lo, hi), BandPass (fs, lo, hi) });
        }
        std::vector<double> energy ((size_t) numBands + 1, 0.0), reference ((size_t) numBands + 1, 0.0);

        configure (type, 1.0);
        reset();
        fdnGain.cur = fdnGain.target = 1.0;
        fdnInDelay.cur = fdnInDelay.target = 0.0;
        for (auto& e : er)
        {
            e.gainL.cur = e.gainR.cur = 0;
            e.gainL.target = e.gainR.target = 0;
        }
        // Energy is integrated to ~-55 dB (0.9 T60 at the slowest band, allowing for the bass ratio);
        // the band-pass reference needs only a short impulse response.
        const int len = (int) (roomSpec (type).t60 * std::max (1.0, roomSpec (type).bassRatio) * 0.95 * fs);
        const int refLen = (int) (0.25 * fs);
        for (int n = 0; n < len; ++n)
        {
            float l, r;
            const float x = n == 0 ? 1.0f : 0.0f;
            process (x, l, r);
            for (size_t b = 0; b < bp.size(); ++b)
            {
                const float bl = bp[b][0].process (l), br = bp[b][1].process (r);
                energy[b] += (double) bl * bl + (double) br * br;
                if (n < refLen)
                {
                    const float bi = bp[b][2].process (x);
                    reference[b] += (double) bi * bi;
                }
            }
        }
        RefData d;
        auto ratio = [&] (size_t b) { return std::max (energy[b] / std::max (reference[b], 1.0e-12), 1.0e-9); };
        d.energy = ratio ((size_t) numBands);
        for (int b = 0; b < numBands; ++b)
            d.shapeDb[(size_t) b] = 10.0 * std::log10 (ratio ((size_t) b) / d.energy);
        return d;
    }

    double fs = 48000.0;
    int roomType = roomOff;
    double decay = 1.0, lastDecay = -1.0;
    DelayLine input;
    std::array<DelayLine, numLines> lines;
    std::array<int, numLines> delays {};
    std::array<float, numLines> lineGain {}, airCoeff {}, airState {};
    std::array<FirstOrder, numLines> shelf {}, lowShelf {};
    std::array<FirstOrder, 2> shadowHp {};
    FirstOrder tailSideHp;
    double binaural = 0.0, proximity = 0.5, diffusionScale = 1.0;
    SpeakerLayout layout;
    bool multi = false;
    std::array<std::array<Ramp, maxOutputChannels>, numReflections> erMulti {};
    std::array<std::array<Diffuser, 3>, maxOutputChannels> erDiffMulti;
    std::array<float, maxOutputChannels> erLpMulti {}, tailWeight {};
    std::array<std::array<float, numLines>, maxOutputChannels> tailSign {};
    int binauralTicks = 0;
    struct Lfo
    {
        float c = 1, s = 0, cr = 1, sr = 0;
        float x1 = 0, y1 = 0; // all-pass interpolator state
    };
    std::array<Lfo, numLines> lfo {};
    float modDepth = 0.0f;
    std::array<Diffuser, 4> inDiffuser;
    std::array<std::array<Diffuser, 3>, 3> erDiffuser;
    std::array<Diffuser, 3> erDiffuserEar;
    std::array<Reflection, numReflections> er {};
    std::array<RefData, numRoomTypes> refData {};
    Ramp fdnGain, fdnInDelay;
    double wallReflection = 0.8, criticalDist = 1.0, tailNorm = 1.0;
    double erWallCoeff = 0.45;
    static inline const double air8k = models::airAbsorptionDbPerMetre (8000.0, 20.0, 50.0);
    float erLpCoeff = 0.45f, erLpL = 0, erLpR = 0, erLpM = 0, inLp = 0;
    int silentCounter = 0;
};

} // namespace amb
