// Ambience spatial engine: distance, time/level panning, width, obstacles, room, auto gain.
// Pure C++, real-time safe after prepare(). See docs/DESIGN.md for the signal flow.
#pragma once

#include "Params.h"
#include "RoomModel.h"
#include "Stereoizer.h"

namespace amb
{
class SpatialEngine
{
public:
    static constexpr int tickLength = 32; // control-rate interval (samples), aligned to absolute time

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    // Call before process() with the current plain parameter values.
    void setParams (const EngineParams& p);

    // Multichannel output (5.0 ... 7.1.4): call at prepare time (allocates). A layout with 1 or 2
    // channels restores mono/stereo operation.
    void setOutputLayout (const SpeakerLayout& layout);
    const SpeakerLayout& getOutputLayout() const noexcept { return outLayout; }

    // in: numIn channels (1 or 2). out: numOut channels (1 or 2, or the multichannel layout's count).
    // May alias in == out.
    void process (const float* const* in, int numIn, float* const* out, int numOut, int numSamples) noexcept;

    int getLatencySamples() const noexcept { return 0; }
    float getAutoGainDb() const noexcept { return (float) autoGainCurrentDb; }
    double getSampleRate() const noexcept { return fs; }

    // Model queries (used by tests and the UI).
    double airFilterDb (double freq) const noexcept;       // current air-absorption filter response
    double obstacleFilterDb (double freq) const noexcept;  // obstruction (direct path) response
    double occlusionFilterDb (double freq) const noexcept; // occlusion response
    double criticalDistance() const noexcept { return rooms[(size_t) activeRoom].criticalDistance(); }

private:
    struct Smooth
    {
        double cur = 0, target = 0;
        void tick (double c) noexcept
        {
            if (cur != target)
            {
                cur = target + (cur - target) * c;
                if (std::abs (cur - target) < 1.0e-7)
                    cur = target;
            }
        }
        void snap() noexcept { cur = target; }
    };

    struct Ramp
    {
        float cur = 0, inc = 0;
        int left = 0;
        void set (double target, int len) noexcept
        {
            left = len;
            inc = (float) ((target - cur) / len);
            if (inc == 0.0f)
                left = 0;
            tgt = (float) target;
        }
        void snap (double target) noexcept { cur = tgt = (float) target; inc = 0; left = 0; }
        void step() noexcept
        {
            if (left > 0)
            {
                cur += inc;
                if (--left == 0)
                    cur = tgt;
            }
        }
        float tgt = 0;
    };

    void computeTargets() noexcept;
    void controlTick (bool snap) noexcept;
    void runFits (bool full) noexcept;
    void updateAutoGainModel() noexcept;
    void processSample (float xL, float xR, float& oL, float& oR) noexcept;
    void processSampleSurround (float xL, float xR, float* outs) noexcept;
    void stepRamps() noexcept;
    void directPath (float xL, float xR, float& M, float& side, float& roomIn) noexcept;

    static void panLawGains (int law, double p, double& l, double& r) noexcept;
    void headphoneGeometry (double azimuthRad, double r, double& dL, double& dR, double& gL, double& gR, double& aL, double& aR) const noexcept;

    double fs = 48000.0;
    int numInChannels = 2, numOutChannels = 2;
    bool needsSnap = true;
    int tickPos = 0;
    int tickCounter = 0;
    EngineParams params;

    // Smoothed control values (per tick)
    Smooth sLogDist, sLogRec, sRolloff, sPan, sTimePan, sPsi, sRho, sMonoSafe, sWidth, sPropMix,
        sMicB, sAmtObs, sAmtOcc, sBassMix, sAutoGain, sOutput, sRoomLevel, sDecay, sMonoDepth, sEq, sM2S, sWiden, sKeep, sProx;
    double tickCoeff = 0.0, fastCoeff = 0.0;

    // Per-sample ramps
    Ramp rDirect, rGL, rGR, rKs, rTauL, rTauR, rProp, rPropMix, rWS, rBassMix, rRho, rHpG, rOut, rRoomX, rMonoOut;
    std::array<Ramp, 4> rHpDelay, rHpGain; // [source*2 + ear]
    Ramp rSafe, rPsiIn, rMonoDepth, rMonoGain, rM2SMix, rM2SSide;
    Biquad posEqL, posEqR;
    bool posEqActive = false;

    // DSP state
    FirstOrder prox[2];
    DelayLine propLine[2];
    FittedCascade<2> air, obstruction;
    FittedCascade<maxOutputChannels> occlusion;
    SpeakerLayout outLayout;
    bool surround = false;
    std::array<Ramp, maxOutputChannels> rFront {};
    Stereoizer stereoizer;
    bool m2sRunning = false;

    // Mono-content detector for Mono to Stereo on stereo inputs (e.g. a dual-mono clip on a stereo
    // track): side vs mid energy of the raw input, with hysteresis; held during silence.
    double detAccM = 0, detAccS = 0, detM = 0, detS = 0, detCoeff = 0;
    bool detMono = true;
    Svf timeXover, bassHp1, bassHp2;
    DelayLine timeLineM[1], timeLineLP[1];
    DelayLine hpLine[2];
    FirstOrder hpFilter[4];
    std::array<RoomModel, 2> rooms;
    int activeRoom = 0;
    int pendingRoom = -1;
    bool roomFading = false;

    // Fit bookkeeping
    double airFitDr = -1, airFitT = -100, airFitH = -1;
    bool airFitOn = false;
    double obsFitDelta = -1;
    int occFitMat = -1;
    double occFitThick = -1, occFitLeak = -1;
    std::vector<double> fitTarget, fitWeight, grid;
    int fitRoundRobin = 0;

    double autoGainTargetDb = 0.0, autoGainCurrentDb = 0.0;
    double bassFreq = 80.0;
    int bassFreqIndex = 0;
    bool bassRunning = false;
};

} // namespace amb
