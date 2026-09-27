#include "SpatialEngine.h"

namespace amb
{
namespace
{
    constexpr double monoSafeMaxTime = 0.30e-3;  // s, band-limited ICTD with Mono-safe ON
    constexpr double fullMaxTime = 1.0e-3;       // s, full lateralisation by time (Blauert 1997)
    constexpr double timeCrossover = 1200.0;     // Hz, ITD dominates below ~1.5 kHz (duplex theory)
    constexpr double widenDepth = 1.0;           // mono-source widening (Stereoizer side gain) at 200 % (listening calibration)
    constexpr double stereoSideRatio = 0.3;      // assumed S/M energy of stereo material (auto gain model)
    constexpr double m2sDepth = 0.7;             // Mono to Stereo side gain at 100 % width (listening calibration)
    constexpr double m2sSidePower = 0.4;         // mean |S/M|^2 of the Stereoizer band weighting (auto gain model)
    constexpr double detectStereoRatio = 1.0e-3; // Mono to Stereo detector: S/M energy above -30 dB -> stereo content
    constexpr double detectMonoRatio = 1.0e-4;   // ... below -40 dB -> mono content (hysteresis)
    constexpr double detectFloor = 1.0e-9;       // mean-square mid below ~-90 dBFS: hold the decision
    constexpr double maxDistanceGainDb = 24.0;
    constexpr double roomFadeSeconds = 0.05;

    std::vector<FitBasis> airBasis()
    {
        return { { BiquadType::highShelf, 600, 0.707, -80, 6 },
                 { BiquadType::highShelf, 1500, 0.707, -80, 6 },
                 { BiquadType::highShelf, 4000, 0.707, -80, 6 },
                 { BiquadType::highShelf, 9000, 0.707, -80, 6 },
                 { BiquadType::highShelf, 14000, 0.707, -80, 6 } };
    }

    std::vector<FitBasis> obstructionBasis()
    {
        return { { BiquadType::highShelf, 150, 0.707, -40, 10 },
                 { BiquadType::highShelf, 400, 0.707, -40, 10 },
                 { BiquadType::highShelf, 1000, 0.707, -40, 10 },
                 { BiquadType::highShelf, 2500, 0.707, -40, 10 },
                 { BiquadType::highShelf, 6000, 0.707, -40, 10 } };
    }

    std::vector<FitBasis> occlusionBasis()
    {
        return { { BiquadType::lowShelf, 100, 0.707, -40, 40 },
                 { BiquadType::highShelf, 350, 0.707, -60, 30 },
                 { BiquadType::highShelf, 1200, 0.707, -60, 30 },
                 { BiquadType::highShelf, 3500, 0.707, -60, 30 },
                 { BiquadType::highShelf, 9000, 0.707, -60, 30 },
                 { BiquadType::peak, 1000, 1.2, -30, 30 },
                 { BiquadType::peak, 100, 1.4, -30, 30 } };
    }
} // namespace

//==============================================================================
void SpatialEngine::prepare (double sampleRate, int /*maxBlockSize*/)
{
    fs = sampleRate;
    tickCoeff = std::exp (-tickLength / (0.030 * fs));
    fastCoeff = std::exp (-tickLength / (0.050 * fs));

    for (auto& d : propLine)
        d.prepare ((int) std::ceil (1000.0 / kSpeedOfSound * fs) + 64);
    timeLineM[0].prepare ((int) std::ceil (fullMaxTime * fs) + 16);
    timeLineLP[0].prepare ((int) std::ceil (fullMaxTime * fs) + 16);
    for (auto& d : hpLine)
        d.prepare ((int) std::ceil (0.004 * fs) + 16);

    air.prepare (fs, airBasis());
    obstruction.prepare (fs, obstructionBasis());
    occlusion.prepare (fs, occlusionBasis());
    grid = air.fitter.gridFrequencies();
    fitTarget.assign (grid.size(), 0.0);
    fitWeight.assign (grid.size(), 1.0);

    stereoizer.prepare (fs);
    detCoeff = std::exp (-tickLength / (0.15 * fs));

    timeXover.setup (fs, timeCrossover, 0.7071);
    bassHp1.setup (fs, 80.0, 0.7071);
    bassHp2.setup (fs, 80.0, 0.7071);

    for (auto& r : rooms)
    {
        r.prepare (fs);
        if (surround)
            r.setLayout (outLayout);
    }

    reset();
}

void SpatialEngine::reset()
{
    for (auto& f : prox)
        f.reset();
    for (auto& d : propLine)
        d.reset();
    air.reset();
    obstruction.reset();
    occlusion.reset();
    timeXover.reset();
    bassHp1.reset();
    bassHp2.reset();
    timeLineM[0].reset();
    timeLineLP[0].reset();
    for (auto& d : hpLine)
        d.reset();
    for (auto& f : hpFilter)
        f.reset();
    for (auto& r : rooms)
        r.reset();
    stereoizer.reset();
    m2sRunning = false;
    detAccM = detAccS = detM = detS = 0.0;
    detMono = false;
    tickPos = 0;
    tickCounter = 0;
    needsSnap = true;
}

//==============================================================================
void SpatialEngine::setParams (const EngineParams& p)
{
    params = p;
    computeTargets();
}

void SpatialEngine::computeTargets() noexcept
{
    const auto& p = params;
    const bool monoOut = numOutChannels == 1;
    const double r = std::clamp ((double) p.distance, 0.1, 1000.0);
    const double rr = std::clamp ((double) p.recDistance, 0.1, 10.0);

    sLogDist.target = std::log (r);
    sLogRec.target = std::log (rr);
    sRolloff.target = std::clamp ((double) p.rolloff, 0.0, 12.0) / 6.0206;
    sPan.target = monoOut ? 0.0 : std::clamp ((double) p.pan, -100.0, 100.0) / 100.0;
    sTimePan.target = std::clamp ((double) p.timePan, 0.0, 100.0) / 100.0;
    sPsi.target = p.inputMode == 1 ? 0.0 : (p.inputMode == 2 ? 1.0 : (numInChannels == 2 ? 1.0 : 0.0));
    sRho.target = (p.renderMode == 1 && ! monoOut && ! surround) ? 1.0 : 0.0;
    sMonoSafe.target = p.monoSafe ? 1.0 : 0.0;
    sWidth.target = monoOut ? 1.0 : std::clamp ((double) p.width, 0.0, 200.0) / 100.0;
    sPropMix.target = p.propDelay ? 1.0 : 0.0;
    static const double micB[3] = { 0.0, 0.5, 1.0 }; // gradient share of a 1st-order microphone
    sMicB.target = p.eqAmbience ? micB[std::clamp (p.micPattern, 0, 2)] : 0.0;
    sEq.target = p.eqAmbience ? 1.0 : 0.0;
    sAmtObs.target = p.obstacle == 1 ? std::clamp ((double) p.amount, 0.0, 100.0) / 100.0 : 0.0;
    sAmtOcc.target = p.obstacle == 2 ? std::clamp ((double) p.amount, 0.0, 100.0) / 100.0 : 0.0;
    sBassMix.target = (p.bassMono > 0 && ! monoOut) ? 1.0 : 0.0;
    sOutput.target = std::clamp ((double) p.output, -24.0, 24.0);
    sRoomLevel.target = std::clamp ((double) p.roomLevel, -24.0, 12.0);
    sDecay.target = std::clamp ((double) p.decay, 50.0, 200.0) / 100.0;
    sProx.target = std::clamp ((double) p.proximity, 0.0, 100.0) / 100.0;
    sAutoGain.target = p.autoGain ? autoGainTargetDb : 0.0;
    sMonoDepth.target = (p.monoDepth || monoOut) ? 1.0 : 0.0;

    if (p.bassMono > 0 && p.bassMono != bassFreqIndex)
    {
        bassFreqIndex = std::clamp (p.bassMono, 1, 8);
        bassFreq = bassMonoFrequencies()[bassFreqIndex];
        bassHp1.setup (fs, bassFreq, 0.7071);
        bassHp2.setup (fs, bassFreq, 0.7071);
    }

    const int roomType = std::clamp (p.room, 0, numRoomTypes - 1);
    const int currentType = rooms[(size_t) activeRoom].type();
    pendingRoom = roomType != currentType || roomFading ? roomType : -1;
}

//==============================================================================
void SpatialEngine::panLawGains (int law, double p, double& l, double& r) noexcept
{
    p = std::clamp (p, -1.0, 1.0);
    const double th = (p + 1.0) * kPi / 4.0;
    const double cl = std::cos (th), cr = std::sin (th);
    const double ll = 0.5 * (1.0 - p), lr = 0.5 * (1.0 + p);
    switch (law)
    {
        case 0: l = std::min (1.0, 1.0 - p); r = std::min (1.0, 1.0 + p); break; // 0 dB centre
        case 2: l = std::sqrt (cl * ll); r = std::sqrt (cr * lr); break;         // -4.5 dB
        case 3: l = ll; r = lr; break;                                            // -6 dB linear
        default: l = cl; r = cr; break;                                           // -3 dB constant power
    }
}

// Spherical-head geometry for one virtual source (Brown & Duda 1998) with near-field parallax:
// per-ear incidence angle, arrival delay (s), 1/r level ratio and normalised head-shadow alpha.
void SpatialEngine::headphoneGeometry (double az, double r, double& dL, double& dR, double& gL, double& gR, double& aL, double& aR) const noexcept
{
    const double a = models::kHeadRadius;
    r = std::max (r, a + 0.1);
    const double sx = r * std::sin (az), sy = r * std::cos (az);
    const double alphaFront = models::headShadowAlpha (kPi / 2);
    const double wc = 2.0 * kSpeedOfSound / a;
    // Near-field cues (1/r level parallax) fade in below 1 m (Brungart & Rabinowitz 1999).
    const double nearAmount = std::clamp ((1.0 - r) / 0.5, 0.0, 1.0);
    auto ear = [&] (double side, double& d, double& g, double& alpha)
    {
        const double vx = sx - side * a, vy = sy;
        const double len = std::sqrt (vx * vx + vy * vy);
        const double cosT = std::clamp ((vx * side) / len, -1.0, 1.0);
        const double theta = std::acos (cosT);
        alpha = models::headShadowAlpha (theta) / alphaFront;
        // The shadow filter's own low-frequency phase delay is removed so the total interaural
        // delay in the ITD-dominant band (< 1.5 kHz) follows Woodworth's formula.
        const double wRef = kTwoPi * 750.0; // phase delay removed at a mid-band frequency of the ITD range
        d = models::earDelaySeconds (theta) + (std::atan (alpha * wRef / wc) - std::atan (wRef / wc)) / wRef;
        g = lerp (1.0, r / len, nearAmount);
    };
    ear (-1.0, dL, gL, aL);
    ear (1.0, dR, gR, aR);
}

//==============================================================================
void SpatialEngine::runFits (bool full) noexcept
{
    const auto& p = params;
    const int nG = (int) grid.size();
    const int iterations = full ? 25 : 6;

    // --- Air absorption (ISO 9613-1), relative to the recording distance.
    const bool airOn = p.airAbsorption && p.eqAmbience; // part of the automatic "Equalized Ambience" EQ
    const double dr = airOn ? std::max (0.0, (double) std::clamp (p.distance, 0.1f, 1000.0f) - (double) p.recDistance) : 0.0;
    const bool airDirty = airOn != airFitOn || std::abs (dr - airFitDr) > 1.0e-3 * (1.0 + dr)
                          || (airOn && (std::abs (p.temperature - airFitT) > 0.01 || std::abs (p.humidity - airFitH) > 0.01));

    const bool obsDirty = std::abs (p.obstacleSize - obsFitDelta) > 1.0e-4;
    const bool occDirty = p.material != occFitMat || std::abs (p.thickness - occFitThick) > 1.0e-4 || std::abs (p.leak - occFitLeak) > 1.0e-3;

    int budget = full ? 3 : 1;
    for (int job = 0; job < 3 && budget > 0; ++job)
    {
        const int which = (fitRoundRobin + job) % 3;
        if (which == 0 && airDirty)
        {
            if (dr <= 0.0)
                air.fitter.clear();
            else
            {
                // Frequency where the attenuation reaches 24 dB (the accuracy limit); the shelves are
                // placed below it so the f^2-like rise of the ISO curve is followed closely.
                auto att = [&] (double f) { return models::airAbsorptionDbPerMetre (f, p.temperature, p.humidity) * dr; };
                double f24 = 40000.0;
                for (double f = 200.0; f < 40000.0; f *= 1.02)
                    if (att (f) >= 24.0)
                    {
                        f24 = f;
                        break;
                    }
                const double nyq = 0.45 * fs;
                const double placement[5] = { 0.12, 0.30, 0.55, 0.85, 1.3 };
                for (int i = 0; i < 5; ++i)
                    air.fitter.setSectionFrequency (i, std::clamp (f24 * placement[i], 150.0, nyq));
                for (int k = 0; k < nG; ++k)
                {
                    const double t = -att (grid[(size_t) k]);
                    fitTarget[(size_t) k] = std::max (t, -40.0);
                    fitWeight[(size_t) k] = t > -26.0 ? 1.0 : 0.1;
                }
                // Initial guess: each shelf takes the target step between neighbouring boundaries.
                double prevT = 0.0;
                for (int i = 0; i < 5; ++i)
                {
                    const double upper = i < 4 ? std::sqrt (air.fitter.sectionFrequency (i) * air.fitter.sectionFrequency (i + 1))
                                               : 2.0 * air.fitter.sectionFrequency (i);
                    const double tU = std::max (-att (upper), -40.0);
                    air.fitter.setGain (i, tU - prevT);
                    prevT = tU;
                }
                air.fitter.setBroadband (0.0);
                air.fitter.fit (fitTarget.data(), fitWeight.data(), iterations);
                air.refresh();
            }
            airFitDr = dr;
            airFitOn = airOn;
            airFitT = p.temperature;
            airFitH = p.humidity;
            air.setTargetFromFit (1.0);
            --budget;
        }
        else if (which == 1 && obsDirty)
        {
            for (int k = 0; k < nG; ++k)
            {
                fitTarget[(size_t) k] = -models::maekawaAttenuationDb (grid[(size_t) k], p.obstacleSize);
                fitWeight[(size_t) k] = 1.0;
            }
            obstruction.fitter.fit (fitTarget.data(), fitWeight.data(), iterations);
            obsFitDelta = p.obstacleSize;
            --budget;
        }
        else if (which == 2 && occDirty)
        {
            const auto& mat = models::material (p.material);
            const double k = std::clamp ((double) p.thickness, 0.25, 4.0);
            double fc = mat.fc > 0 ? mat.fc / k : 2000.0;
            double f0 = 100.0;
            if (mat.model == models::MaterialModel::doubleLeaf)
                f0 = models::massAirMassResonance (mat.mass * k, mat.mass * k, mat.cavity);
            else if (mat.model == models::MaterialModel::carBody)
                f0 = 80.0;
            occlusion.fitter.setSectionFrequency (5, std::clamp (fc, 60.0, 12000.0));
            occlusion.fitter.setSectionFrequency (6, std::clamp (f0, 40.0, 1000.0));

            double best = -1.0e9;
            for (int i = 0; i < nG; ++i)
            {
                fitTarget[(size_t) i] = -models::transmissionLossDb (p.material, grid[(size_t) i], k, p.leak / 100.0);
                best = std::max (best, fitTarget[(size_t) i]);
            }
            for (int i = 0; i < nG; ++i)
                fitWeight[(size_t) i] = fitTarget[(size_t) i] > best - 45.0 ? 1.0 : 0.1;
            occlusion.fitter.fit (fitTarget.data(), fitWeight.data(), iterations);
            occFitMat = p.material;
            occFitThick = p.thickness;
            occFitLeak = p.leak;
            occlusion.refresh();
            --budget;
        }
    }
    fitRoundRobin = (fitRoundRobin + 1) % 3;
}

//==============================================================================
void SpatialEngine::controlTick (bool snap) noexcept
{
    // Mono to Stereo: active for mono sources (mono input, Input = Mono) and for stereo inputs that
    // carry mono content (identical channels).
    {
        detM = detCoeff * detM + (1.0 - detCoeff) * detAccM / tickLength;
        detS = detCoeff * detS + (1.0 - detCoeff) * detAccS / tickLength;
        detAccM = detAccS = 0.0;
        if (detM > detectFloor)
        {
            if (detMono && detS > detM * detectStereoRatio)
                detMono = false;
            else if (! detMono && detS < detM * detectMonoRatio)
                detMono = true;
        }
        const bool monoOut = numOutChannels == 1;
        const double monoContent = (numInChannels == 1 || detMono) ? 1.0 : 0.0;
        sM2S.target = (params.monoToStereo && ! params.monoDepth && ! monoOut) ? std::max (1.0 - sPsi.target, monoContent) : 0.0;
        // Mono sources widened beyond 100 % use the same Stereoizer (flat per channel, no comb).
        sWiden.target = (! params.monoDepth && ! monoOut && params.width > 100.0f) ? 1.0 - sPsi.target : 0.0;
        sKeep.target = (params.monoToStereo && params.keepMono) ? 1.0 : 0.0;
    }

    if (snap)
    {
        for (Smooth* s : { &sLogDist, &sLogRec, &sRolloff, &sPan, &sTimePan, &sPsi, &sRho, &sMonoSafe, &sWidth, &sPropMix,
                           &sMicB, &sAmtObs, &sAmtOcc, &sBassMix, &sOutput, &sRoomLevel, &sDecay, &sMonoDepth, &sEq, &sM2S, &sWiden, &sKeep, &sProx })
            s->snap();
    }
    else
    {
        for (Smooth* s : { &sLogDist, &sLogRec, &sRolloff, &sPan, &sTimePan, &sPsi, &sRho, &sMonoSafe, &sWidth, &sPropMix,
                           &sMicB, &sAmtObs, &sAmtOcc, &sBassMix, &sOutput, &sRoomLevel, &sDecay, &sMonoDepth, &sEq, &sM2S, &sWiden, &sKeep, &sProx })
            s->tick (tickCoeff);
    }

    runFits (snap);
    air.setTargetFromFit (1.0);
    obstruction.setTargetFromFit (sAmtObs.cur);
    occlusion.setTargetFromFit (sAmtOcc.cur);
    if (snap)
    {
        air.snap();
        obstruction.snap();
        occlusion.snap();
    }
    else
    {
        air.tick (tickCoeff);
        obstruction.tick (tickCoeff);
        occlusion.tick (tickCoeff);
    }

    const int L = tickLength;
    auto setRamp = [snap, L] (Ramp& r, double v)
    {
        if (snap)
            r.snap (v);
        else
            r.set (v, L);
    };

    // --- Distance (inverse distance law with adjustable rolloff) + broadband filter gains.
    const double r = std::exp (sLogDist.cur);
    const double rr = std::exp (sLogRec.cur);
    const double n = sRolloff.cur;
    const double gDist = std::min (std::pow (rr / r, n), dbToGain (maxDistanceGainDb));
    setRamp (rDirect, gDist * dbToGain (air.broadbandDb() + obstruction.broadbandDb()));

    // --- Proximity-effect compensation: ratio of 1st-order gradient responses at the new and
    // the recorded distance, H = (s + B c / r) / (s + B c / r_rec)  (bounded shelf).
    const double B = sMicB.cur;
    if (B > 0.0)
    {
        const double wd = B * kSpeedOfSound / r, wr = B * kSpeedOfSound / rr;
        for (auto& f : prox)
            f.setAnalog (1.0, wd, 1.0, wr, 2.0 * fs);
    }
    else
        for (auto& f : prox)
            f.setIdentity();

    // --- Propagation delay (r/c) for Doppler.
    setRamp (rProp, r / kSpeedOfSound * fs);
    setRamp (rPropMix, sPropMix.cur);

    // --- Speakers panner: hybrid amplitude + inter-channel time difference.
    const double pan = sPan.cur, t = sTimePan.cur, psi = sPsi.cur;
    const double pa = pan * (1.0 - t * (1.0 - std::abs (pan)));
    double gl, gr, gc, gcr;
    panLawGains (params.panLaw, pa, gl, gr);
    panLawGains (params.panLaw, 0.0, gc, gcr);
    const double GL = lerp (gl, gl / gc, psi), GR = lerp (gr, gr / gc, psi);
    setRamp (rGL, GL);
    setRamp (rGR, GR);
    setRamp (rKs, std::min (GL, GR));
    if (surround)
    {
        // Mono sources: pairwise constant-power panning over the front (L-C-R, +-30 deg), so a centred
        // source sits in the centre channel (film convention). Stereo sources keep their L/R image.
        float vb[maxOutputChannels];
        outLayout.pan (pan * 30.0, false, vb);
        for (int c = 0; c < outLayout.numChannels; ++c)
        {
            const double stereoGain = c == outLayout.left ? GL : (c == outLayout.right ? GR : 0.0);
            setRamp (rFront[(size_t) c], lerp ((double) vb[c], stereoGain, psi));
        }
    }

    // Position EQ (Equalized Ambience): the channel away from the source gets a gentle high-shelf
    // cut growing with |pan| — a mild head-shadow-like spectral cue on top of level/time panning
    // (max -2.5 dB above ~5 kHz; listening calibration). Applied to M only, so Width stays mono-safe.
    {
        constexpr double maxCutDb = -2.5;
        const double eq = sEq.cur;
        const double cutL = maxCutDb * eq * std::max (0.0, pan);
        const double cutR = maxCutDb * eq * std::max (0.0, -pan);
        posEqActive = (cutL != 0.0 || cutR != 0.0) && ! surround;
        posEqL.set (designBiquadHz (BiquadType::highShelf, fs, 5000.0, 0.707, cutL));
        posEqR.set (designBiquadHz (BiquadType::highShelf, fs, 5000.0, 0.707, cutR));
        if (! posEqActive)
        {
            posEqL.reset();
            posEqR.reset();
        }
    }
    const double safe = sMonoSafe.cur;
    setRamp (rSafe, safe);
    const double tauMax = lerp (fullMaxTime, monoSafeMaxTime, safe) * fs;
    const double tau = surround ? 0.0 : t * pan * tauMax; // multichannel: amplitude panning over L-C-R
    setRamp (rTauL, std::max (0.0, tau));
    setRamp (rTauR, std::max (0.0, -tau));
    setRamp (rPsiIn, psi);

    // --- Width (M/S). Stereo sources scale S. Mono sources get a quadrature S from the Stereoizer:
    // with Mono to Stereo its amount follows Width; otherwise it widens only above 100 %.
    // The mid is replaced by the Stereoizer's matching all-pass mid (binary target, smoothed),
    // so each channel keeps a flat response and L + R keeps the magnitude of the source.
    const double w = sWidth.cur;
    const bool monoOut = numOutChannels == 1;
    const double m2s = sM2S.cur;
    setRamp (rWS, monoOut ? 0.0 : w);
    setRamp (rM2SMix, std::max (m2s, sWiden.cur));
    stereoizer.setKeepMono (sKeep.cur);
    setRamp (rM2SSide, monoOut ? 0.0 : m2s * m2sDepth * w + (1.0 - m2s) * sWiden.cur * widenDepth * std::max (0.0, w - 1.0));
    setRamp (rBassMix, sBassMix.cur);

    // --- Headphones: two virtual sources (A = M+S, B = M-S) around the azimuth.
    const double rho = sRho.cur;
    setRamp (rRho, rho);
    if (rho > 0.0 || snap)
    {
        const double az = pan * kPi / 2.0;
        const double spreadDeg = lerp (psi * w + (1.0 - psi) * std::max (0.0, w - 1.0), w, m2s) * 30.0;
        const double spread = spreadDeg * kPi / 180.0;
        double d[4], g[4], al[4];
        for (int s = 0; s < 2; ++s)
        {
            const double a = std::clamp (az + (s == 0 ? -spread : spread), -kPi / 2, kPi / 2);
            headphoneGeometry (a, r, d[s * 2], d[s * 2 + 1], g[s * 2], g[s * 2 + 1], al[s * 2], al[s * 2 + 1]);
        }
        const double dMin = std::min ({ d[0], d[1], d[2], d[3] });
        const double wc = 2.0 * kSpeedOfSound / models::kHeadRadius; // Brown & Duda: corner at 2*w0
        const double c = wc / std::tan (wc / (2.0 * fs));
        for (int i = 0; i < 4; ++i)
        {
            setRamp (rHpDelay[(size_t) i], (d[i] - dMin) * fs);
            setRamp (rHpGain[(size_t) i], g[i]);
            hpFilter[i].setAnalog (al[i] / wc, 1.0, 1.0 / wc, 1.0, c);
        }
        setRamp (rHpG, lerp (gc * 0.5, 0.70710678, psi));
    }

    // --- Room (with crossfade on type changes).
    const double roomLevelLin = dbToGain (sRoomLevel.cur);
    if (! roomFading && pendingRoom >= 0 && pendingRoom != rooms[(size_t) activeRoom].type())
    {
        const int other = 1 - activeRoom;
        rooms[(size_t) other].configure (pendingRoom, sDecay.cur);
        rooms[(size_t) other].reset();
        rooms[(size_t) other].setBinaural (rho);
        rooms[(size_t) other].setProximity (sProx.cur);
        rooms[(size_t) other].setGeometry (r, pan * kPi / 2.0, rr, n, roomLevelLin, L);
        rooms[(size_t) other].snapGeometry();
        activeRoom = other;
        pendingRoom = -1;
        if (snap)
            rRoomX.snap (other == 1 ? 1.0 : 0.0);
        else
            roomFading = true;
    }
    if (roomFading)
    {
        const double target = activeRoom == 1 ? 1.0 : 0.0;
        const double stepPerTick = L / (roomFadeSeconds * fs);
        double x = rRoomX.tgt;
        x = target > x ? std::min (target, x + stepPerTick) : std::max (target, x - stepPerTick);
        rRoomX.set (x, L);
        if (x == target)
            roomFading = false;
    }
    for (int i = 0; i < 2; ++i)
    {
        auto& room = rooms[(size_t) i];
        if (i == activeRoom)
            room.setDecay (sDecay.cur);
        room.setBinaural (rho);
        room.setProximity (sProx.cur);
        room.setGeometry (r, pan * kPi / 2.0, rr, n, roomLevelLin, L);
        if (snap)
            room.snapGeometry();
    }

    // --- Output: occlusion broadband + auto gain + output gain; mono-output normalisation.
    if (params.autoGain && (tickCounter % 8 == 0 || snap))
        updateAutoGainModel();
    sAutoGain.target = params.autoGain ? autoGainTargetDb : 0.0;
    if (snap)
        sAutoGain.snap();
    else
        sAutoGain.tick (fastCoeff);
    autoGainCurrentDb = sAutoGain.cur;
    setRamp (rOut, dbToGain (occlusion.broadbandDb() + sAutoGain.cur + sOutput.cur));
    setRamp (rMonoOut, 0.5 / lerp (gc, 1.0, psi));
    setRamp (rMonoDepth, sMonoDepth.cur);
    setRamp (rMonoGain, lerp (gc, 1.0, psi)); // same level as a centred source

    ++tickCounter;
}

//==============================================================================
// Model-based auto gain: predicted K-weighted (ITU-R BS.1770) loudness change for pink noise,
// from the analytic responses of the current chain. Deterministic, no signal analysis.
void SpatialEngine::updateAutoGainModel() noexcept
{
    const int nG = (int) grid.size();
    const double r = std::exp (sLogDist.cur), rr = std::exp (sLogRec.cur);
    const double gDist = std::min (std::pow (rr / r, sRolloff.cur), dbToGain (maxDistanceGainDb));
    const double psi = sPsi.cur, rho = sRho.cur;
    const bool monoOut = numOutChannels == 1;

    const double GL = rGL.tgt, GR = rGR.tgt, ks = rKs.tgt;
    const double sideEnergy = numInChannels == 2 ? stereoSideRatio : 0.0;
    const double pIn = numInChannels == 2 ? 2.0 * (1.0 + stereoSideRatio) : 1.0;
    const double wsT = rWS.tgt;
    const double m2sT = rM2SSide.tgt;
    double frontPower = GL * GL + GR * GR;
    if (surround)
    {
        frontPower = 0.0;
        for (int c = 0; c < outLayout.numChannels; ++c)
            frontPower += (double) rFront[(size_t) c].tgt * rFront[(size_t) c].tgt;
    }
    const double speakerPower = frontPower + 2.0 * ks * ks * (psi * psi * sideEnergy * wsT * wsT + m2sT * m2sT * m2sSidePower);

    double sumOut = 0.0, sumIn = 0.0;
    for (int k = 0; k < nG; ++k)
    {
        const double f = grid[(size_t) k];
        const double w = kTwoPi * f / fs;
        double hDb = gainToDb (gDist) + prox[0].magnitudeDb (w) + air.currentSectionsDb (k) + air.broadbandDb()
                     + obstruction.currentSectionsDb (k) + obstruction.broadbandDb();
        const double pDirect = std::pow (10.0, hDb / 10.0);
        double roomPower = 0.0;
        for (int i = 0; i < 2; ++i)
        {
            const double wRoom = i == 1 ? rRoomX.tgt : 1.0 - rRoomX.tgt;
            if (wRoom > 0.0)
                roomPower += wRoom * rooms[(size_t) i].predictedEnergy (f);
        }

        double panPower = speakerPower;
        if (rho > 0.0)
        {
            double hp = 0.0;
            for (int e = 0; e < 2; ++e)
            {
                const double a = rHpGain[(size_t) e].tgt * dbToGain (hpFilter[e].magnitudeDb (w));
                const double b = rHpGain[(size_t) (2 + e)].tgt * dbToGain (hpFilter[2 + e].magnitudeDb (w));
                hp += (a + b) * (a + b);
            }
            hp *= rHpG.tgt * rHpG.tgt;
            panPower = lerp (speakerPower, hp, rho);
        }
        double out;
        if (monoOut)
            out = pDirect + roomPower;
        else if (sMonoDepth.target > 0.5) // L = R = g * mono
            out = (pDirect + roomPower) * 2.0 * rMonoGain.tgt * rMonoGain.tgt;
        else
            out = pDirect * panPower + roomPower;
        out *= std::pow (10.0, (occlusion.currentSectionsDb (k) + occlusion.broadbandDb()) / 10.0);

        const double kw = models::kWeightingPowerGain (f);
        sumOut += kw * out;
        sumIn += kw * (monoOut ? (numInChannels == 2 ? 2.0 * (1.0 + sideEnergy) * 0.5 : 1.0) : pIn);
    }
    const double range = std::clamp ((double) params.autoGainRange, 0.0, 36.0);
    autoGainTargetDb = std::clamp (-10.0 * std::log10 (std::max (sumOut, 1.0e-20) / std::max (sumIn, 1.0e-20)), -range, range);
}

//==============================================================================
void SpatialEngine::setOutputLayout (const SpeakerLayout& layout)
{
    outLayout = layout;
    outLayout.finalise();
    surround = outLayout.isSurround();
    for (auto& r : rooms)
        if (surround)
            r.setLayout (outLayout);
    needsSnap = true;
    computeTargets();
}

void SpatialEngine::process (const float* const* in, int numIn, float* const* out, int numOut, int numSamples) noexcept
{
    const bool multichannel = surround && numOut == outLayout.numChannels;
    if (numIn != numInChannels || std::min (numOut, 2) != numOutChannels)
    {
        numInChannels = std::clamp (numIn, 1, 2);
        numOutChannels = std::clamp (numOut, 1, 2);
        computeTargets();
    }

    for (int i = 0; i < numSamples; ++i)
    {
        if (tickPos == 0)
        {
            controlTick (needsSnap);
            needsSnap = false;
        }
        const float xL = in[0][i];
        const float xR = numIn > 1 ? in[1][i] : xL;
        if (multichannel)
        {
            float outs[maxOutputChannels];
            processSampleSurround (xL, xR, outs);
            for (int c = 0; c < numOut; ++c)
                out[c][i] = outs[c];
        }
        else
        {
            float oL, oR;
            processSample (xL, xR, oL, oR);
            if (numOut > 1)
            {
                out[0][i] = oL;
                out[1][i] = oR;
            }
            else
                out[0][i] = oL;
        }
        tickPos = (tickPos + 1) % tickLength;
    }
}

void SpatialEngine::stepRamps() noexcept
{
    for (Ramp* r : { &rDirect, &rGL, &rGR, &rKs, &rTauL, &rTauR, &rProp, &rPropMix, &rWS, &rBassMix, &rRho, &rHpG,
                     &rOut, &rRoomX, &rMonoOut, &rSafe, &rPsiIn, &rMonoDepth, &rMonoGain,
                     &rM2SMix, &rM2SSide })
        r->step();
}

// Everything that does not depend on the output format: proximity, propagation, distance, air,
// obstruction, width / Stereoizer and bass mono. Returns the direct mid, the side and the room feed.
void SpatialEngine::directPath (float xL, float xR, float& M, float& side, float& roomIn) noexcept
{
    // Input: mid/side of the source. Stereo sources keep S (scaled by "stereoness" psi).
    M = 0.5f * (xL + xR);
    const float rawS = 0.5f * (xL - xR);
    float S = rawS * rPsiIn.cur;
    detAccM += (double) M * M;
    detAccS += (double) rawS * rawS;

    if (sMicB.cur > 0.0)
    {
        M = prox[0].process (M);
        S = prox[1].process (S);
    }

    // Propagation delay / Doppler (crossfaded on toggle).
    propLine[0].push (M);
    propLine[1].push (S);
    if (rPropMix.cur > 0.0f)
    {
        const float md = propLine[0].read (rProp.cur);
        const float sd = propLine[1].read (rProp.cur);
        M += rPropMix.cur * (md - M);
        S += rPropMix.cur * (sd - S);
    }

    roomIn = M;

    // Direct path: distance gain, air absorption, obstruction.
    M *= rDirect.cur;
    S *= rDirect.cur;
    if (air.isActive())
    {
        M = air.process (0, M);
        S = air.process (1, S);
    }
    if (obstruction.isActive())
    {
        M = obstruction.process (0, M);
        S = obstruction.process (1, S);
    }

    // Width: stereo S scaling + mono-compatible quadrature S (Stereoizer); bass mono on S only.
    side = S * rWS.cur;
    if (rM2SMix.cur > 0.0f || sM2S.target > 0.0 || sWiden.target > 0.0)
    {
        if (! m2sRunning)
        {
            stereoizer.reset();
            m2sRunning = true;
        }
        float mq, sq;
        stereoizer.process (M, mq, sq);
        M += rM2SMix.cur * (mq - M);
        side += sq * rM2SSide.cur;
    }
    else
        m2sRunning = false;

    if (rBassMix.cur > 0.0f || sBassMix.target > 0.0)
    {
        if (! bassRunning)
        {
            bassHp1.reset();
            bassHp2.reset();
            bassRunning = true;
        }
        const float hp = bassHp2.processHigh (bassHp1.processHigh (side));
        side += rBassMix.cur * (hp - side);
    }
    else
        bassRunning = false;
}

void SpatialEngine::processSample (float xL, float xR, float& oL, float& oR) noexcept
{
    stepRamps();
    float M, side, roomIn;
    directPath (xL, xR, M, side, roomIn);

    // Speakers: amplitude + time panning (time applied to M only, S stays antisymmetric).
    const float lp = timeXover.processLow (M);
    timeLineM[0].push (M);
    timeLineLP[0].push (lp);
    auto timeChannel = [&] (float tauSamples)
    {
        if (tauSamples <= 0.0f)
            return M;
        const float safe = rSafe.cur;
        const float bandLimited = M - lp + timeLineLP[0].read (tauSamples);
        const float full = timeLineM[0].read (tauSamples);
        return safe * bandLimited + (1.0f - safe) * full;
    };
    float chL = timeChannel (rTauL.cur);
    float chR = timeChannel (rTauR.cur);
    if (posEqActive)
    {
        chL = posEqL.process (chL);
        chR = posEqR.process (chR);
    }
    float L = rGL.cur * chL + rKs.cur * side;
    float R = rGR.cur * chR - rKs.cur * side;

    // Headphones: spherical-head binaural rendering of the two virtual sources.
    const float A = M + side, Bv = M - side;
    hpLine[0].push (A);
    hpLine[1].push (Bv);
    if (rRho.cur > 0.0f)
    {
        float ear[2] = { 0, 0 };
        for (int s = 0; s < 2; ++s)
            for (int e = 0; e < 2; ++e)
            {
                const int idx = s * 2 + e;
                auto& dr = rHpDelay[(size_t) idx];
                auto& gr = rHpGain[(size_t) idx];
                dr.step();
                gr.step();
                ear[e] += hpFilter[idx].process (hpLine[s].read (dr.cur)) * gr.cur;
            }
        const float g = rHpG.cur;
        L += rRho.cur * (ear[0] * g - L);
        R += rRho.cur * (ear[1] * g - R);
    }

    // Room (source room: fed before distance attenuation, level set by the DRR model).
    // Mono Depth / mono tracks: the direct mid (no pan, width or time differences) plus the room's
    // dedicated mono output — a true mono render rather than a fold-down of the stereo image.
    float monoSig = M;
    const float x1 = rRoomX.cur;
    for (int i = 0; i < 2; ++i)
    {
        const float wRoom = i == 1 ? x1 : 1.0f - x1;
        auto& room = rooms[(size_t) i];
        if (wRoom <= 0.0f || room.type() == roomOff)
            continue;
        float rl, rrOut, rm;
        room.process (roomIn, rl, rrOut, rm);
        L += wRoom * rl;
        R += wRoom * rrOut;
        monoSig += wRoom * rm;
    }

    if (numOutChannels == 1)
    {
        L = monoSig;
        R = L;
    }
    else if (rMonoDepth.cur > 0.0f)
    {
        const float mono = monoSig * rMonoGain.cur;
        if (rMonoDepth.cur >= 1.0f)
            L = R = mono; // exactly identical channels
        else
        {
            L += rMonoDepth.cur * (mono - L);
            R += rMonoDepth.cur * (mono - R);
        }
    }

    // Occlusion: transmission through the partition (direct + source-room reverb).
    if (occlusion.isActive())
    {
        L = occlusion.process (0, L);
        R = numOutChannels == 1 ? L : occlusion.process (1, R);
    }

    oL = L * rOut.cur;
    oR = R * rOut.cur;
}

void SpatialEngine::processSampleSurround (float xL, float xR, float* outs) noexcept
{
    stepRamps();
    float M, side, roomIn;
    directPath (xL, xR, M, side, roomIn);
    const int nCh = outLayout.numChannels;

    // Direct sound over the front channels; the width (side) on L / R.
    for (int c = 0; c < nCh; ++c)
    {
        rFront[(size_t) c].step();
        outs[c] = rFront[(size_t) c].cur * M;
    }
    if (outLayout.left >= 0 && outLayout.right >= 0)
    {
        outs[outLayout.left] += rKs.cur * side;
        outs[outLayout.right] -= rKs.cur * side;
    }

    // Room: reflections panned over the ring (ceiling -> heights), decorrelated tail per channel.
    float monoSig = M;
    const float x1 = rRoomX.cur;
    for (int i = 0; i < 2; ++i)
    {
        const float wRoom = i == 1 ? x1 : 1.0f - x1;
        auto& room = rooms[(size_t) i];
        if (wRoom <= 0.0f || room.type() == roomOff)
            continue;
        float roomOut[maxOutputChannels], rm;
        room.processMulti (roomIn, roomOut, rm);
        for (int c = 0; c < nCh; ++c)
            outs[c] += wRoom * roomOut[c];
        monoSig += wRoom * rm;
    }

    // Mono Depth: a true mono render in the centre channel (dialogue convention).
    if (rMonoDepth.cur > 0.0f)
    {
        const int target = outLayout.centre >= 0 ? outLayout.centre : -1;
        for (int c = 0; c < nCh; ++c)
        {
            float mono = 0.0f;
            if (target >= 0)
                mono = c == target ? monoSig : 0.0f;
            else if (c == outLayout.left || c == outLayout.right)
                mono = monoSig * 0.70710678f;
            outs[c] = rMonoDepth.cur >= 1.0f ? mono : outs[c] + rMonoDepth.cur * (mono - outs[c]);
        }
    }

    if (occlusion.isActive())
        for (int c = 0; c < nCh; ++c)
            outs[c] = occlusion.process (c, outs[c]);

    for (int c = 0; c < nCh; ++c)
        outs[c] = outLayout.speakers[(size_t) c].lfe ? 0.0f : outs[c] * rOut.cur;
}

//==============================================================================
double SpatialEngine::airFilterDb (double freq) const noexcept
{
    const auto& g = air.fitter.gridFrequencies();
    int best = 0;
    for (int k = 0; k < (int) g.size(); ++k)
        if (std::abs (std::log (g[(size_t) k] / freq)) < std::abs (std::log (g[(size_t) best] / freq)))
            best = k;
    return air.currentSectionsDb (best) + air.broadbandDb();
}

double SpatialEngine::obstacleFilterDb (double freq) const noexcept
{
    const auto& g = obstruction.fitter.gridFrequencies();
    int best = 0;
    for (int k = 0; k < (int) g.size(); ++k)
        if (std::abs (std::log (g[(size_t) k] / freq)) < std::abs (std::log (g[(size_t) best] / freq)))
            best = k;
    return obstruction.currentSectionsDb (best) + obstruction.broadbandDb();
}

double SpatialEngine::occlusionFilterDb (double freq) const noexcept
{
    const auto& g = occlusion.fitter.gridFrequencies();
    int best = 0;
    for (int k = 0; k < (int) g.size(); ++k)
        if (std::abs (std::log (g[(size_t) k] / freq)) < std::abs (std::log (g[(size_t) best] / freq)))
            best = k;
    return occlusion.currentSectionsDb (best) + occlusion.broadbandDb();
}

} // namespace amb
