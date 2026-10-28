#pragma once
// Minimal JUCE shim: lets the project's header-only DSP (Biquad.h, PitchShifter.h)
// be compiled standalone with g++ for measurement.
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstddef>

namespace juce
{
    template <typename T> constexpr T jlimit (T lo, T hi, T v) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
    template <typename T> constexpr T jmax (T a, T b) noexcept { return a < b ? b : a; }
    template <typename T> constexpr T jmin (T a, T b) noexcept { return b < a ? b : a; }
    template <typename T> constexpr bool approximatelyEqual (T a, T b) noexcept
    { return std::abs (a - b) <= (std::abs (a) + std::abs (b)) * (T) 1.0e-6; }
    template <typename T> struct MathConstants
    {
        static constexpr T pi    = (T) 3.14159265358979323846264338327950288;
        static constexpr T twoPi = (T) 6.28318530717958647692528676655900576;
        static constexpr T halfPi= (T) 1.57079632679489661923132169163975144;
    };
}
