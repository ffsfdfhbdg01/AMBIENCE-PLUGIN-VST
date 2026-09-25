// Deterministic test signals, WAV I/O and analysis helpers shared by the tests and the CLI.
#pragma once

#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../Source/dsp/DspCore.h"

namespace amb::tools
{
// Deterministic white/pink noise (LCG + Paul Kellet's pink filter).
class Noise
{
public:
    explicit Noise (uint32_t seed = 12345u) : state (seed) {}

    float white() noexcept
    {
        state = state * 1664525u + 1013904223u;
        return (float) ((double) (state >> 8) / 8388608.0 - 1.0);
    }

    float pink() noexcept
    {
        const float w = white();
        b0 = 0.99886f * b0 + w * 0.0555179f;
        b1 = 0.99332f * b1 + w * 0.0750759f;
        b2 = 0.96900f * b2 + w * 0.1538520f;
        b3 = 0.86650f * b3 + w * 0.3104856f;
        b4 = 0.55000f * b4 + w * 0.5329522f;
        b5 = -0.7616f * b5 - w * 0.0168980f;
        const float out = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f;
        b6 = w * 0.115926f;
        return out * 0.11f;
    }

private:
    uint32_t state;
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
};

inline std::vector<float> sine (double freq, double fs, int n, float amp = 0.5f)
{
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i)
        v[(size_t) i] = amp * (float) std::sin (kTwoPi * freq * i / fs);
    return v;
}

inline std::vector<float> pinkNoise (int n, uint32_t seed = 777u)
{
    Noise nz (seed);
    std::vector<float> v ((size_t) n);
    for (auto& x : v)
        x = nz.pink();
    return v;
}

inline double rms (const float* x, int n)
{
    double s = 0;
    for (int i = 0; i < n; ++i)
        s += (double) x[i] * x[i];
    return std::sqrt (s / std::max (n, 1));
}

// In-place radix-2 FFT.
inline void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -kTwoPi / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1);
            for (size_t j = 0; j < len / 2; ++j)
            {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// Averaged (Welch, Hann) power spectrum, bins of fs/nfft.
inline std::vector<double> powerSpectrum (const float* x, int n, int nfft = 8192)
{
    std::vector<double> acc ((size_t) nfft / 2 + 1, 0.0);
    std::vector<std::complex<double>> buf ((size_t) nfft);
    int frames = 0;
    for (int start = 0; start + nfft <= n; start += nfft / 2)
    {
        for (int i = 0; i < nfft; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (kTwoPi * i / nfft);
            buf[(size_t) i] = x[start + i] * w;
        }
        fft (buf);
        for (int k = 0; k <= nfft / 2; ++k)
            acc[(size_t) k] += std::norm (buf[(size_t) k]);
        ++frames;
    }
    for (auto& v : acc)
        v /= std::max (frames, 1);
    return acc;
}

// Band powers on the 1/3-octave grid (band edges at fm * 2^(+-1/6)).
inline std::vector<double> thirdOctaveBands (const std::vector<double>& spec, double fs, int nfft, const std::vector<double>& centres)
{
    std::vector<double> out;
    for (double fm : centres)
    {
        const double lo = fm * std::pow (2.0, -1.0 / 6.0), hi = fm * std::pow (2.0, 1.0 / 6.0);
        double s = 0;
        for (size_t k = 0; k < spec.size(); ++k)
        {
            const double f = (double) k * fs / nfft;
            if (f >= lo && f < hi)
                s += spec[k];
        }
        out.push_back (s);
    }
    return out;
}

//==============================================================================
// Minimal WAV (PCM 16/24/32-bit int, 32-bit float) reader/writer.
struct WavData
{
    int sampleRate = 48000;
    int channels = 1;
    std::vector<std::vector<float>> data;
};

inline bool readWav (const std::string& path, WavData& wav)
{
    FILE* f = std::fopen (path.c_str(), "rb");
    if (! f)
        return false;
    auto rd = [f] (void* p, size_t n) { return std::fread (p, 1, n, f) == n; };
    char riff[4];
    uint32_t sz;
    char wave[4];
    if (! rd (riff, 4) || ! rd (&sz, 4) || ! rd (wave, 4) || std::memcmp (riff, "RIFF", 4) || std::memcmp (wave, "WAVE", 4))
    {
        std::fclose (f);
        return false;
    }
    uint16_t fmt = 0, ch = 0, bits = 0;
    uint32_t sr = 0;
    std::vector<uint8_t> raw;
    while (true)
    {
        char id[4];
        uint32_t len;
        if (! rd (id, 4) || ! rd (&len, 4))
            break;
        if (! std::memcmp (id, "fmt ", 4))
        {
            std::vector<uint8_t> c (len);
            rd (c.data(), len);
            std::memcpy (&fmt, &c[0], 2);
            std::memcpy (&ch, &c[2], 2);
            std::memcpy (&sr, &c[4], 4);
            std::memcpy (&bits, &c[14], 2);
            if (fmt == 0xFFFE && len >= 26)
                std::memcpy (&fmt, &c[24], 2);
        }
        else if (! std::memcmp (id, "data", 4))
        {
            raw.resize (len);
            rd (raw.data(), len);
            break;
        }
        else
            std::fseek (f, (long) (len + (len & 1)), SEEK_CUR);
    }
    std::fclose (f);
    if (ch == 0 || raw.empty())
        return false;
    wav.sampleRate = (int) sr;
    wav.channels = ch;
    const int bps = bits / 8;
    const size_t frames = raw.size() / (size_t) (bps * ch);
    wav.data.assign (ch, std::vector<float> (frames));
    for (size_t i = 0; i < frames; ++i)
        for (int c = 0; c < ch; ++c)
        {
            const uint8_t* p = &raw[(i * ch + (size_t) c) * (size_t) bps];
            float v = 0;
            if (fmt == 3 && bits == 32)
                std::memcpy (&v, p, 4);
            else if (bits == 16)
                v = (float) (int16_t) (p[0] | (p[1] << 8)) / 32768.0f;
            else if (bits == 24)
                v = (float) ((int32_t) ((uint32_t) p[0] << 8 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 24) >> 8) / 8388608.0f;
            else if (bits == 32)
            {
                int32_t iv;
                std::memcpy (&iv, p, 4);
                v = (float) ((double) iv / 2147483648.0);
            }
            wav.data[(size_t) c][i] = v;
        }
    return true;
}

// Writes 32-bit float WAV.
inline bool writeWav (const std::string& path, const WavData& wav)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f)
        return false;
    const uint32_t frames = wav.data.empty() ? 0 : (uint32_t) wav.data[0].size();
    const uint16_t ch = (uint16_t) wav.channels;
    const uint32_t dataLen = frames * ch * 4;
    auto w32 = [f] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [f] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f);
    w32 (36 + dataLen);
    std::fwrite ("WAVEfmt ", 1, 8, f);
    w32 (16);
    w16 (3);
    w16 (ch);
    w32 ((uint32_t) wav.sampleRate);
    w32 ((uint32_t) wav.sampleRate * ch * 4);
    w16 ((uint16_t) (ch * 4));
    w16 (32);
    std::fwrite ("data", 1, 4, f);
    w32 (dataLen);
    for (uint32_t i = 0; i < frames; ++i)
        for (int c = 0; c < ch; ++c)
            std::fwrite (&wav.data[(size_t) c][i], 4, 1, f);
    std::fclose (f);
    return true;
}

} // namespace amb::tools
