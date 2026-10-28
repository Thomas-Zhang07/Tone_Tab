#pragma once

#include <cmath>

// One-pole DC blocker. Cheap, zero added latency (the 3 dB point is far below
// the guitar range), and it removes the offset produced by asymmetric clippers.
struct DcBlocker
{
    void prepare (double sampleRate, double cutoffHz = 12.0)
    {
        r = (float) std::exp (-2.0 * 3.141592653589793 * cutoffHz / sampleRate);
        reset();
    }

    void reset()
    {
        x1[0] = x1[1] = 0.0f;
        y1[0] = y1[1] = 0.0f;
    }

    inline float process (float x, int ch) noexcept
    {
        const float y = x - x1[ch] + r * y1[ch];
        x1[ch] = x;
        y1[ch] = y;
        return y;
    }

    void processBlock (float* data, int n, int ch) noexcept
    {
        for (int i = 0; i < n; ++i)
            data[i] = process (data[i], ch);
    }

    float r = 0.9995f;
    float x1[2] {}, y1[2] {};
};
