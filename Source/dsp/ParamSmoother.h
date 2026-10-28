#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include "dsp/Biquad.h"

// Smooth a gain / mix parameter over ~20 ms.
class SmoothedGain
{
public:
    void prepare (double sampleRate, double ms = 20.0)
    {
        value.reset (sampleRate, ms / 1000.0);
    }

    void snap (float v)          { value.setCurrentAndTargetValue (v); }
    void setTarget (float v)     { value.setTargetValue (v); }
    float next() noexcept        { return value.getNextValue(); }
    float current() const noexcept { return value.getCurrentValue(); }

private:
    juce::SmoothedValue<float> value { 0.0f };
};

// A biquad whose coefficients glide toward a new target instead of jumping.
// The filter state is preserved, so changing an EQ band does not click.
class GlidedBiquad
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        filter.reset();
        target = filter;
        gliding = false;
    }

    void reset() { filter.reset(); }

    void setLowpass (double f, double q = 0.7071)  { setTarget (makeLowpass (f, q)); }
    void setHighpass (double f, double q = 0.7071) { setTarget (makeHighpass (f, q)); }
    void setBandpass (double f, double q)          { setTarget (makeBandpass (f, q)); }
    void setPeak (double f, double q, double db)   { setTarget (makePeak (f, q, db)); }
    void setLowShelf (double f, double db)         { setTarget (makeLowShelf (f, db)); }
    void setHighShelf (double f, double db)        { setTarget (makeHighShelf (f, db)); }

    void processBlock (float* data, int n, int ch) noexcept
    {
        if (gliding)
        {
            const float k = 1.0f - std::exp (-(float) n / (float) (0.02 * sr));
            filter.b0 += (target.b0 - filter.b0) * k;
            filter.b1 += (target.b1 - filter.b1) * k;
            filter.b2 += (target.b2 - filter.b2) * k;
            filter.a1 += (target.a1 - filter.a1) * k;
            filter.a2 += (target.a2 - filter.a2) * k;

            if (std::abs (target.b0 - filter.b0) < 1.0e-6f
                && std::abs (target.b1 - filter.b1) < 1.0e-6f
                && std::abs (target.b2 - filter.b2) < 1.0e-6f
                && std::abs (target.a1 - filter.a1) < 1.0e-6f
                && std::abs (target.a2 - filter.a2) < 1.0e-6f)
            {
                filter.b0 = target.b0; filter.b1 = target.b1; filter.b2 = target.b2;
                filter.a1 = target.a1; filter.a2 = target.a2;
                gliding = false;
            }
        }

        filter.processBlock (data, n, ch);
    }

private:
    void setTarget (const Biquad& t)
    {
        target = t;
        gliding = true;
    }

    Biquad makeLowpass (double f, double q) const   { Biquad t; t.setLowpass (sr, f, q); return t; }
    Biquad makeHighpass (double f, double q) const  { Biquad t; t.setHighpass (sr, f, q); return t; }
    Biquad makeBandpass (double f, double q) const  { Biquad t; t.setBandpass (sr, f, q); return t; }
    Biquad makePeak (double f, double q, double db) const { Biquad t; t.setPeak (sr, f, q, db); return t; }
    Biquad makeLowShelf (double f, double db) const { Biquad t; t.setLowShelf (sr, f, db); return t; }
    Biquad makeHighShelf (double f, double db) const{ Biquad t; t.setHighShelf (sr, f, db); return t; }

    Biquad filter, target;
    double sr = 48000.0;
    bool gliding = false;
};
