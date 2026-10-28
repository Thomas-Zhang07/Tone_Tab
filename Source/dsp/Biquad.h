#pragma once

#include <cmath>
#include <juce_core/juce_core.h>

// 双二阶滤波器（RBJ Audio EQ Cookbook），最多 2 声道。
// 系数只是几个 float，改参数时不分配内存，可以在音频线程里直接调用 set*。
struct Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1[2] {}, z2[2] {};

    void reset() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0.0f; }

    inline float process (float x, int ch) noexcept
    {
        const float y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return y;
    }

    void processBlock (float* data, int n, int ch) noexcept
    {
        for (int i = 0; i < n; ++i)
            data[i] = process (data[i], ch);
    }

    void setLowpass (double sr, double f, double q = 0.7071)    { set (sr, f, q, 0.0, Type::lowpass); }
    void setHighpass (double sr, double f, double q = 0.7071)   { set (sr, f, q, 0.0, Type::highpass); }
    void setBandpass (double sr, double f, double q)            { set (sr, f, q, 0.0, Type::bandpass); }
    void setPeak (double sr, double f, double q, double db)     { set (sr, f, q, db, Type::peak); }
    void setLowShelf (double sr, double f, double db)           { set (sr, f, 0.7071, db, Type::lowShelf); }
    void setHighShelf (double sr, double f, double db)          { set (sr, f, 0.7071, db, Type::highShelf); }

private:
    enum class Type { lowpass, highpass, bandpass, peak, lowShelf, highShelf };

    void set (double sr, double f, double q, double db, Type type) noexcept
    {
        f = juce::jlimit (10.0, sr * 0.49, f);
        const double A = std::pow (10.0, db / 40.0);
        const double w0 = juce::MathConstants<double>::twoPi * f / sr;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * q);
        double B0 = 1, B1 = 0, B2 = 0, A0 = 1, A1 = 0, A2 = 0;

        switch (type)
        {
            case Type::lowpass:
                B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = (1 - cw) / 2;
                A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha;
                break;
            case Type::highpass:
                B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = (1 + cw) / 2;
                A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha;
                break;
            case Type::bandpass:
                B0 = alpha; B1 = 0; B2 = -alpha;
                A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha;
                break;
            case Type::peak:
                B0 = 1 + alpha * A; B1 = -2 * cw; B2 = 1 - alpha * A;
                A0 = 1 + alpha / A; A1 = -2 * cw; A2 = 1 - alpha / A;
                break;
            case Type::lowShelf:
            {
                const double s = 2 * std::sqrt (A) * alpha;
                B0 = A * ((A + 1) - (A - 1) * cw + s);
                B1 = 2 * A * ((A - 1) - (A + 1) * cw);
                B2 = A * ((A + 1) - (A - 1) * cw - s);
                A0 = (A + 1) + (A - 1) * cw + s;
                A1 = -2 * ((A - 1) + (A + 1) * cw);
                A2 = (A + 1) + (A - 1) * cw - s;
                break;
            }
            case Type::highShelf:
            {
                const double s = 2 * std::sqrt (A) * alpha;
                B0 = A * ((A + 1) + (A - 1) * cw + s);
                B1 = -2 * A * ((A - 1) + (A + 1) * cw);
                B2 = A * ((A + 1) + (A - 1) * cw - s);
                A0 = (A + 1) - (A - 1) * cw + s;
                A1 = 2 * ((A - 1) - (A + 1) * cw);
                A2 = (A + 1) - (A - 1) * cw - s;
                break;
            }
        }

        b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0);
        a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
    }
};
