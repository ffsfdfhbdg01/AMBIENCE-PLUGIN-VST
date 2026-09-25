// Physical and psychoacoustic models. Every formula cites its source; values without a
// literature source are marked "listening calibration" (see docs/DESIGN.md).
#pragma once

#include "DspCore.h"

namespace amb::models
{
//==============================================================================
// ISO 9613-1:1993, eqs. (3)-(5) and Annex B: pure-tone atmospheric absorption coefficient
// in dB/m for frequency f [Hz], temperature [degC], relative humidity [%], pressure [kPa].
inline double airAbsorptionDbPerMetre (double f, double tempC, double relHumidity, double pressureKPa = 101.325) noexcept
{
    constexpr double pr = 101.325;  // reference pressure, kPa
    constexpr double T0 = 293.15;   // reference temperature, K
    constexpr double T01 = 273.16;  // triple-point isotherm, K
    const double T = tempC + 273.15;
    const double pa = pressureKPa / pr;

    const double C = -6.8346 * std::pow (T01 / T, 1.261) + 4.6151;
    const double h = relHumidity * std::pow (10.0, C) / pa; // molar concentration of water vapour, %

    const double frO = pa * (24.0 + 4.04e4 * h * (0.02 + h) / (0.391 + h));
    const double frN = pa * std::pow (T / T0, -0.5) * (9.0 + 280.0 * h * std::exp (-4.170 * (std::pow (T / T0, -1.0 / 3.0) - 1.0)));

    const double f2 = f * f;
    return 8.686 * f2
           * (1.84e-11 / pa * std::sqrt (T / T0)
              + std::pow (T / T0, -2.5)
                    * (0.01275 * std::exp (-2239.1 / T) / (frO + f2 / frO)
                       + 0.1068 * std::exp (-3352.0 / T) / (frN + f2 / frN)));
}

//==============================================================================
// Maekawa (1968) barrier/edge diffraction: attenuation in dB for path-length difference
// delta [m] at frequency f. N = 2*delta/lambda (Fresnel number). Practical barriers rarely
// exceed ~20-25 dB (scattering, turbulence), hence the cap.
inline double maekawaAttenuationDb (double f, double delta, double capDb = 25.0) noexcept
{
    const double N = 2.0 * std::max (delta, 0.0) * f / kSpeedOfSound;
    return std::min (capDb, 10.0 * std::log10 (3.0 + 20.0 * N));
}

//==============================================================================
// Room acoustics (Kuttruff, Room Acoustics): critical distance where the direct sound equals
// the diffuse reverberant field, r_c = sqrt(Q*A/(16*pi)) with Sabine A = 0.161*V/T60,
// i.e. r_c ~= 0.057 * sqrt(Q*V/T60).
inline double criticalDistance (double volume, double t60, double Q = 2.0) noexcept
{
    return 0.057 * std::sqrt (Q * volume / std::max (t60, 0.05));
}

//==============================================================================
// Transmission loss of partitions.
// Single leaf: Sharp (1978) prediction — mass law below fc/2 (field incidence,
// R = 20 log10(m f) - 47 dB), linear (in log f) transition to the coincidence value
// R(fc) = 20 log10(m fc) + 10 log10(2 eta / pi) - 47, and above fc
// R = 20 log10(m f) + 10 log10(2 eta f / (pi fc)) - 47.
inline double sharpSingleLeafDb (double f, double m, double fc, double eta) noexcept
{
    auto massLaw = [m] (double fr) { return 20.0 * std::log10 (m * fr) - 47.0; };
    double R;
    if (f <= 0.5 * fc)
        R = massLaw (f);
    else
    {
        const double Rfc = 20.0 * std::log10 (m * fc) + 10.0 * std::log10 (2.0 * eta / kPi) - 47.0;
        if (f < fc)
        {
            const double R1 = massLaw (0.5 * fc);
            const double t = std::log2 (f / (0.5 * fc)); // 0..1 over the octave below fc
            R = R1 + (Rfc - R1) * t;
        }
        else
            R = 20.0 * std::log10 (m * f) + 10.0 * std::log10 (2.0 * eta * f / (kPi * fc)) - 47.0;
    }
    return std::max (R, 0.0);
}

enum class MaterialModel
{
    curtain,
    singleLeaf,
    doubleLeaf,
    carBody
};

struct Material
{
    const char* name;
    MaterialModel model;
    double mass;        // surface density per leaf [kg/m^2] (single leaf: whole panel)
    double fc;          // coincidence frequency [Hz]
    double eta;         // total loss factor (listening calibration within literature range)
    double cavity;      // double leaf: cavity depth [m]
    double builtInLeak; // open-area fraction of typical gaps (listening calibration)
};

// Values: Hopkins (2007), Long (2014), manufacturers' Rw data; eta and leaks: listening calibration.
inline const Material& material (int index) noexcept
{
    static const Material table[] = {
        { "Heavy Curtain", MaterialModel::curtain, 0.7, 0.0, 0.0, 0.0, 0.0 },
        { "Glass 6 mm", MaterialModel::singleLeaf, 15.0, 2000.0, 0.03, 0.0, 0.0 },
        { "Hollow Door", MaterialModel::singleLeaf, 9.0, 6000.0, 0.03, 0.0, 0.003 },
        { "Solid Wood Door", MaterialModel::singleLeaf, 25.0, 700.0, 0.03, 0.0, 0.0005 },
        { "Drywall (stud wall)", MaterialModel::doubleLeaf, 10.0, 2800.0, 0.03, 0.075, 0.0 },
        { "Brick 12 cm", MaterialModel::singleLeaf, 220.0, 200.0, 0.1, 0.0, 0.0 },
        { "Concrete 20 cm", MaterialModel::singleLeaf, 460.0, 95.0, 0.1, 0.0, 0.0 },
        { "Car Body", MaterialModel::carBody, 12.0, 10000.0, 0.1, 0.0, 0.0 },
    };
    constexpr int n = (int) (sizeof (table) / sizeof (table[0]));
    return table[std::clamp (index, 0, n - 1)];
}

constexpr int numMaterials = 8;

// Leak parameter 0..1 -> open-area fraction 0..10 % on a logarithmic scale.
inline double leakToOpenArea (double leak01) noexcept
{
    leak01 = std::clamp (leak01, 0.0, 1.0);
    return 0.1 * (std::pow (10.0, 3.0 * leak01) - 1.0) / 999.0;
}

// Mass-air-mass resonance of a double leaf (Hopkins 2007): f0 ~= 60 * sqrt((m1+m2)/(m1 m2 d)).
inline double massAirMassResonance (double m1, double m2, double d) noexcept
{
    return 60.0 * std::sqrt ((m1 + m2) / (m1 * m2 * std::max (d, 0.01)));
}

// Transmission loss R(f) in dB of a material, thickness scale k (mass and 1/fc scale with
// thickness), and composite leakage through gaps:
// tau = (1 - p) tau_wall + p,  R = -10 log10(tau)  (composite transmission coefficient).
inline double transmissionLossDb (int materialIndex, double f, double thicknessScale, double leak01) noexcept
{
    const auto& mat = material (materialIndex);
    const double k = std::clamp (thicknessScale, 0.1, 10.0);
    double R = 0.0;

    switch (mat.model)
    {
        case MaterialModel::curtain:
            // Porous, limp textile: insertion loss rises gently with frequency and saturates.
            // Listening calibration consistent with published curtain insertion-loss data (~3-10 dB).
            R = std::min (10.0 * std::sqrt (k), (1.0 + 6.0 * std::sqrt (f / 2000.0)) * std::sqrt (k));
            break;

        case MaterialModel::singleLeaf:
            R = sharpSingleLeafDb (f, mat.mass * k, mat.fc / k, mat.eta);
            break;

        case MaterialModel::doubleLeaf:
        {
            const double m1 = mat.mass * k, m2 = mat.mass * k;
            const double f0 = massAirMassResonance (m1, m2, mat.cavity);
            const double Rmass = std::max (0.0, 20.0 * std::log10 ((m1 + m2) * f) - 47.0);
            const double lf = std::log2 (f / f0);
            const double dip = 6.0 * std::exp (-(lf * lf) / (2.0 * 0.3 * 0.3));
            double Rd = Rmass - dip + (f > f0 ? 20.0 * std::log10 (f / f0) : 0.0);
            const double R1 = sharpSingleLeafDb (f, m1, mat.fc / k, mat.eta);
            const double R2 = sharpSingleLeafDb (f, m2, mat.fc / k, mat.eta);
            Rd = std::min (Rd, R1 + R2 + 6.0);
            // Structural bridging through studs limits the double-leaf gain (listening calibration).
            Rd = std::min (Rd, Rmass + 12.0);
            R = std::max (Rd, 0.0);
            break;
        }

        case MaterialModel::carBody:
        {
            R = std::min (40.0, sharpSingleLeafDb (f, mat.mass * k, mat.fc / k, mat.eta));
            // Low-frequency cabin/panel resonance (listening calibration).
            const double lf = std::log2 (f / 80.0);
            R -= 4.0 * std::exp (-(lf * lf) / (2.0 * 0.4 * 0.4));
            R = std::max (R, 0.0);
            break;
        }
    }

    R = std::min (R, 70.0); // flanking transmission limit (listening calibration)

    const double p = std::clamp (mat.builtInLeak + leakToOpenArea (leak01), 0.0, 1.0);
    const double tau = (1.0 - p) * std::pow (10.0, -R / 10.0) + p;
    return -10.0 * std::log10 (tau);
}

//==============================================================================
// Spherical head model (Brown & Duda 1998; Woodworth 1938).
constexpr double kHeadRadius = 0.0875; // m

// Head-shadow high-frequency gain alpha(theta), theta = angle between source and ear axis.
inline double headShadowAlpha (double thetaRad) noexcept
{
    constexpr double alphaMin = 0.1;
    constexpr double thetaMin = 150.0 * kPi / 180.0;
    return (1.0 + alphaMin / 2.0) + (1.0 - alphaMin / 2.0) * std::cos (thetaRad / thetaMin * kPi);
}

// Per-ear arrival time relative to the head centre plus a/c (so it is >= 0), Brown & Duda eq. (3).
inline double earDelaySeconds (double thetaRad) noexcept
{
    const double ac = kHeadRadius / kSpeedOfSound;
    thetaRad = std::abs (thetaRad);
    if (thetaRad < kPi / 2)
        return ac * (1.0 - std::cos (thetaRad));
    return ac * (1.0 + thetaRad - kPi / 2);
}

// Woodworth ITD for azimuth theta (0 = front, pi/2 = side).
inline double woodworthItd (double azimuthRad) noexcept
{
    const double a = std::abs (azimuthRad);
    return kHeadRadius / kSpeedOfSound * (a + std::sin (a));
}

//==============================================================================
// ITU-R BS.1770 K-weighting magnitude (pre-filter shelf + RLB high-pass), evaluated with the
// standard 48 kHz coefficients. Used only as a spectral weighting for the model-based auto gain.
inline double kWeightingPowerGain (double f) noexcept
{
    const double w = kTwoPi * f / 48000.0;
    auto mag2 = [w] (double b0, double b1, double b2, double a1, double a2)
    {
        const double cw = std::cos (w), c2w = std::cos (2 * w);
        const double num = b0 * b0 + b1 * b1 + b2 * b2 + 2 * (b0 * b1 + b1 * b2) * cw + 2 * b0 * b2 * c2w;
        const double den = 1 + a1 * a1 + a2 * a2 + 2 * (a1 + a1 * a2) * cw + 2 * a2 * c2w;
        return num / den;
    };
    return mag2 (1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585)
           * mag2 (1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621);
}

} // namespace amb::models
