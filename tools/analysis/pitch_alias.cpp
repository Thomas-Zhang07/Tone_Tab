#include "dsp/PitchShifter.h"
#include <vector>
#include <complex>
#include <cstdio>
#include <cmath>
#include <algorithm>

using cd = std::complex<double>;
static constexpr double PI = 3.14159265358979323846264338327950288;

static void fft (std::vector<cd>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * PI / (double) len;
        const cd wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            cd w (1);
            for (size_t j = 0; j < len / 2; ++j)
            {
                cd u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl;
            }
        }
    }
}

static void topPeaks (const std::vector<float>& v, double sr, size_t start, size_t N, int count)
{
    std::vector<cd> a (N);
    for (size_t i = 0; i < N; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * PI * i / (N - 1));
        a[i] = (start + i < v.size() ? v[start + i] : 0.0f) * w;
    }
    fft (a);
    std::vector<std::pair<double,double>> mag;
    for (size_t k = 1; k < N / 2; ++k) mag.push_back ({ std::abs (a[k]), k * sr / (double) N });
    std::sort (mag.rbegin(), mag.rend());
    const double ref = mag.empty() ? 1 : mag[0].first;
    for (int i = 0; i < count && i < (int) mag.size(); ++i)
    {
        // skip neighbours of a peak already printed
        bool dup = false;
        for (int j = 0; j < i; ++j)
            if (std::abs (mag[(size_t) i].second - mag[(size_t) j].second) < 2.0 * sr / (double) N) dup = true;
        if (dup) { --count; continue; }
        std::printf ("%8.0f Hz (%+6.1f dB)  ", mag[(size_t) i].second, 20 * std::log10 (mag[(size_t) i].first / ref + 1e-30));
    }
    std::printf ("\n");
}

static void runTone (double sr, double f, double semis, double grainMs)
{
    const size_t warm = (size_t) (1.0 * sr);
    const size_t n = warm + (size_t) (1.5 * sr);
    std::vector<float> in (n, 0.0f), out (n, 0.0f);
    for (size_t i = warm; i < n; ++i) in[i] = 0.25f * (float) std::sin (2.0 * PI * f * (double) (i - warm) / sr);
    PitchShifter ps; ps.prepare (sr, grainMs); ps.setSemitones (semis);
    for (size_t i = 0; i < n; ++i) out[i] = ps.process (in[i]);
    std::printf ("in %7.0f Hz, semis %+4.0f -> ", f, semis);
    topPeaks (out, sr, warm + (size_t) (0.7 * sr), 16384, 4);
}

int main()
{
    const double sr = 48000.0;
    std::printf ("== pitch shifter up-octave (grain 30 ms) spectral peaks ==\n");
    for (double f : { 3000.0, 6000.0, 9000.0, 12000.0, 15000.0, 18000.0 })
        runTone (sr, f, +12.0, 30.0);
    std::printf ("\n== down-octave (grain 60 ms) ==\n");
    for (double f : { 6000.0, 12000.0 })
        runTone (sr, f, -12.0, 60.0);
    return 0;
}
