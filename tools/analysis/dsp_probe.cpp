#include "dsp/PitchShifter.h"
#include "dsp/Biquad.h"
#include <vector>
#include <complex>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <random>

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
        const double ang = 2.0 * PI / (double) len * -1.0;
        const cd wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            cd w (1);
            for (size_t j = 0; j < len / 2; ++j)
            {
                cd u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

static double dominantFreq (const std::vector<float>& v, double sr, size_t start, size_t N)
{
    std::vector<cd> a (N);
    for (size_t i = 0; i < N; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * PI * i / (N - 1));
        a[i] = (start + i < v.size() ? v[start + i] : 0.0f) * w;
    }
    fft (a);
    size_t best = 1;
    double bestMag = 0;
    for (size_t k = 1; k < N / 2; ++k)
        if (const double m = std::abs (a[k]); m > bestMag) { bestMag = m; best = k; }
    const double m0 = std::abs (a[best - 1]), m1 = bestMag, m2 = std::abs (a[best + 1]);
    const double d = 0.5 * (m0 - m2) / (m0 - 2 * m1 + m2 + 1e-30);
    return (best + d) * sr / (double) N;
}

static double spurious (const std::vector<float>& v, double sr, size_t start, double f0, size_t N)
{
    std::vector<cd> a (N);
    for (size_t i = 0; i < N; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * PI * i / (N - 1));
        a[i] = (start + i < v.size() ? v[start + i] : 0.0f) * w;
    }
    fft (a);
    const size_t pk = (size_t) std::lround (f0 * (double) N / sr);
    double sig = 0, other = 0;
    for (size_t k = 1; k < N / 2; ++k)
    {
        const double p = std::norm (a[k]);
        if (k + 3 > pk && k < pk + 3) sig += p;
        else other += p;
    }
    return 10.0 * std::log10 (other / std::max (1e-30, sig));
}

struct ShiftResult { double freq, latencyAtOnset, latencyAtCentroid, spuriousDb, rippleDb, reportedMs; };

static ShiftResult runShift (double sr, double grainMs, double semis)
{
    ShiftResult res {};
    const size_t warm = (size_t) (1.0 * sr);
    const size_t n = warm + (size_t) (2.0 * sr);
    std::vector<float> in (n), out (n);
    for (size_t i = warm; i < n; ++i)
        in[i] = 0.3f * (float) std::sin (2.0 * PI * 220.0 * (double) (i - warm) / sr);

    PitchShifter ps;
    ps.prepare (sr, grainMs);
    ps.setSemitones (semis);
    for (size_t i = 0; i < n; ++i) out[i] = ps.process (in[i]);

    const size_t a0 = warm + (size_t) (1.5 * sr);
    const size_t N = 16384;
    res.freq = dominantFreq (out, sr, a0, N);
    const double expected = 220.0 * std::pow (2.0, semis / 12.0);
    res.spuriousDb = spurious (out, sr, a0, expected, N);

    const size_t w = (size_t) (0.02 * sr);
    double mn = 1e9, mx = 0;
    for (size_t s = warm + (size_t) (1.0 * sr); s + w < n; s += w)
    {
        double e = 0;
        for (size_t i = s; i < s + w; ++i) e += (double) out[i] * out[i];
        const double r = std::sqrt (e / w);
        mn = std::min (mn, r); mx = std::max (mx, r);
    }
    res.rippleDb = 20.0 * std::log10 (mx / std::max (1e-12, mn));

    // latency: fresh instance, silence + single impulse
    PitchShifter p2;
    p2.prepare (sr, grainMs);
    p2.setSemitones (semis);
    const size_t m = (size_t) (1.0 * sr);
    const size_t imp = (size_t) (0.5 * sr);
    std::vector<float> io (m, 0.0f), oo (m, 0.0f);
    io[imp] = 1.0f;
    for (size_t i = 0; i < m; ++i) oo[i] = p2.process (io[i]);
    res.latencyAtOnset = -1;
    double sum = 0, wsum = 0;
    for (size_t i = imp; i < imp + (size_t) (0.25 * sr) && i < m; ++i)
    {
        const double a = std::abs (oo[i]);
        if (res.latencyAtOnset < 0 && a > 1e-3) res.latencyAtOnset = (double) (i - imp) * 1000.0 / sr;
        if (a > 1e-4) { sum += a; wsum += a * (double) (i - imp); }
    }
    res.latencyAtCentroid = sum > 0 ? wsum / sum * 1000.0 / sr : -1;
    res.reportedMs = p2.getLatencySamples() * 1000.0 / sr;
    return res;
}

int main()
{
    const double sr = 48000.0;
    std::printf ("== PitchShifter (project header compiled as-is) ==\n");
    std::printf ("%-26s %10s %10s %10s %10s %10s %10s\n", "case", "freq(Hz)", "report(ms)", "onset(ms)", "centroid", "spur(dB)", "ripple(dB)");
    struct C { const char* name; double grain, semis; };
    const C cases[] = {
        { "octave down", 60, -12 }, { "octave up", 30, +12 },
        { "pitch +7", 40, +7 }, { "pitch -5", 40, -5 }, { "shimmer +12", 50, +12 },
    };
    for (auto& c : cases)
    {
        auto r = runShift (sr, c.grain, c.semis);
        std::printf ("%-26s %10.2f %10.1f %10.1f %10.1f %10.1f %10.2f\n",
                     c.name, r.freq, r.reportedMs, r.latencyAtOnset, r.latencyAtCentroid, r.spuriousDb, r.rippleDb);
    }

    // identity transparency on broadband noise
    {
        PitchShifter ps; ps.prepare (sr, 40.0); ps.setSemitones (0.0);
        const size_t n = (size_t) (3.0 * sr);
        std::vector<float> in (n), out (n);
        std::mt19937 rng (1234);
        std::uniform_real_distribution<float> d (-0.3f, 0.3f);
        float lp = 0;
        for (size_t i = 0; i < n; ++i) { const float w = d (rng); lp = 0.8f * lp + 0.2f * w; in[i] = lp; }
        for (size_t i = 0; i < n; ++i) out[i] = ps.process (in[i]);
        const size_t s0 = (size_t) (1.0 * sr), len = (size_t) (1.0 * sr);
        double bestErr = 1e30, bestGain = 0; size_t bestLag = 0;
        for (size_t lag = 0; lag < 4000; ++lag)
        {
            double xy = 0, xx = 0, yy = 0;
            for (size_t i = s0; i < s0 + len; i += 2)
            { xy += (double) in[i - lag] * out[i]; xx += (double) in[i - lag] * in[i - lag]; yy += (double) out[i] * out[i]; }
            const double g = xy / std::max (1e-12, xx);
            const double err = yy - 2 * g * xy + g * g * xx;
            if (err < bestErr) { bestErr = err; bestGain = g; bestLag = lag; }
        }
        double yy = 0;
        for (size_t i = s0; i < s0 + len; ++i) yy += (double) out[i] * out[i];
        std::printf ("identity (ratio 1, noise): lag %.2f ms, gain %.3f, residual %.1f dB\n",
                     bestLag * 1000.0 / sr, bestGain, 10.0 * std::log10 (std::max (1e-30, bestErr) / std::max (1e-30, yy)));
    }

    std::printf ("\n== Biquad (project header compiled as-is) ==\n");
    auto checkFilter = [] (const char* name, Biquad& b, double sr, double f, double expectedDb)
    {
        const double disc = (double) b.a1 * b.a1 - 4.0 * (double) b.a2;
        double r = 0;
        if (disc >= 0) { const double sq = std::sqrt (disc); r = std::max (std::abs ((-b.a1 + sq) / 2), std::abs ((-b.a1 - sq) / 2)); }
        else r = std::sqrt ((double) b.a2);
        b.reset();
        const int n = (int) (3.0 * sr);
        const int settle = (int) (1.5 * sr);
        double s = 0, c = 0;
        for (int i = 0; i < n; ++i)
        {
            const double x = std::sin (2.0 * PI * f * i / sr);
            const double y = b.process ((float) x, 0);
            if (i > settle) { s += y * std::sin (2.0 * PI * f * i / sr); c += y * std::cos (2.0 * PI * f * i / sr); }
        }
        const double got = 20.0 * std::log10 (std::sqrt (s * s + c * c) * 2.0 / (n - settle));
        std::printf ("%-36s pole r=%.6f  |H|= %7.3f dB (want %7.2f)\n", name, r, got, expectedDb);
    };
    {
        Biquad b; b.setHighpass (sr, 20.0, 0.7071); checkFilter ("HPF 20 Hz @48k", b, sr, 1000.0, 0.0);
        Biquad c; c.setLowShelf (sr, 100.0, 12.0); checkFilter ("LowShelf 100 Hz +12 dB @48k", c, sr, 20.0, 12.0);
        Biquad d; d.setPeak (sr, 1000.0, 12.0, -30.0); checkFilter ("Peak 1k Q12 -30 dB @48k", d, sr, 1000.0, -30.0);
        Biquad e; e.setLowpass (sr, 0.49 * sr, 0.7071); checkFilter ("LPF 0.49 fs @48k", e, sr, 1000.0, 0.0);
        Biquad g; g.setHighpass (44100.0, 20.0, 0.7071); checkFilter ("HPF 20 Hz @44.1k (float coeffs)", g, 44100.0, 1000.0, 0.0);
    }
    return 0;
}
