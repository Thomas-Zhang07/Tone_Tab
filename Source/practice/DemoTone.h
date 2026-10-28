#pragma once

#include "dsp/Biquad.h"
#include <cmath>

// Tone shaping for the tab demo/backing synth only. The live guitar keeps its
// NAM/effects chain; this bus makes the synthesized guitar sound like a guitar
// in a room instead of a raw Karplus-Strong string.
class DemoTone
{
public:
    void prepare (double sampleRate)
    {
        hp.setHighpass (sampleRate, 70.0, 0.7);
        body1.setPeak (sampleRate, 110.0, 2.0, 2.0);
        body2.setPeak (sampleRate, 220.0, 2.0, 2.5);
        body3.setPeak (sampleRate, 440.0, 2.0, 2.0);
        presence.setHighShelf (sampleRate, 2500.0, 2.0);
        cab.setLowpass (sampleRate, 5500.0, 0.7);
        reset();
    }

    void reset()
    {
        hp.reset(); body1.reset(); body2.reset(); body3.reset();
        presence.reset(); cab.reset();
    }

    void process (float* left, float* right, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float l = hp.process (left[i], 0);
            float r = hp.process (right[i], 1);
            l = body1.process (l, 0); r = body1.process (r, 1);
            l = body2.process (l, 0); r = body2.process (r, 1);
            l = body3.process (l, 0); r = body3.process (r, 1);
            l = presence.process (l, 0); r = presence.process (r, 1);
            l = cab.process (l, 0); r = cab.process (r, 1);

            left[i] = std::tanh (l * 1.1f) * 0.9f;
            right[i] = std::tanh (r * 1.1f) * 0.9f;
        }
    }

private:
    Biquad hp, body1, body2, body3, presence, cab;
};
