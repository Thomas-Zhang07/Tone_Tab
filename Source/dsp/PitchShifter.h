#pragma once

#include <cmath>
#include <cstdint>
#include <vector>
#include <juce_core/juce_core.h>

// 变调器（WSOLA 波形对齐的颗粒式变调），单声道，能处理和弦。
//
// 每半个颗粒长度开始一个新颗粒，颗粒用汉宁窗淡入淡出（两两叠加正好等于 1）。
// 新颗粒开始时，在一小段范围内搜索和正在播放的旧颗粒波形最相关的位置再接上，
// 避免两个颗粒相位抵消造成的“抖动 / 发虚”。
class PitchShifter
{
public:
    void prepare (double sampleRate, double grainMs)
    {
        sr = sampleRate;
        grainLen = juce::jmax (256, (int) (grainMs * 0.001 * sr)) & ~1;
        hop = grainLen / 2;
        corrLen = juce::jmax (64, (int) (256.0 * sr / 48000.0));
        maxShift = (int) (0.012 * sr);                  // 搜索范围约 12 ms（覆盖到 83 Hz 的一个周期）
        minDelay = corrLen * 2 + 4;                     // 保证相关运算读到的都是已写入的样本

        const int need = minDelay + 2 * grainLen + maxShift + 64;
        size = 1;
        while (size < need) size <<= 1;
        mask = size - 1;
        buffer.assign ((size_t) size, 0.0f);

        window.resize ((size_t) grainLen);
        for (int i = 0; i < grainLen; ++i)
            window[(size_t) i] = 0.5f - 0.5f * (float) std::cos (juce::MathConstants<double>::twoPi * i / grainLen);

        reset();
    }

    void reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writeCount = 0;
        samplesToNextGrain = 0;
        for (auto& g : grains) g = {};
    }

    void setSemitones (double semis) { ratio = juce::jlimit (0.25, 2.0, std::pow (2.0, semis / 12.0)); }

    // 延迟（大约值，样本数）
    int getLatencySamples() const { return minDelay + (ratio > 1.0 ? (int) ((ratio - 1.0) * grainLen) : 0) + hop / 2; }

    inline float process (float x) noexcept
    {
        buffer[(size_t) (writeCount & mask)] = x;
        ++writeCount;

        if (--samplesToNextGrain <= 0)
        {
            startGrain();
            samplesToNextGrain = hop;
        }

        float out = 0.0f;
        for (auto& g : grains)
        {
            if (! g.active)
                continue;
            out += window[(size_t) g.age] * read (g.pos);
            g.pos += ratio;
            if (++g.age >= grainLen)
                g.active = false;
        }
        return out;
    }

private:
    struct Grain { double pos = 0.0; int age = 0; bool active = false; };

    inline float read (double absPos) const noexcept
    {
        const auto i0 = (int64_t) std::floor (absPos);
        const float f = (float) (absPos - (double) i0);
        return buffer[(size_t) (i0 & mask)] * (1.0f - f) + buffer[(size_t) ((i0 + 1) & mask)] * f;
    }

    void startGrain() noexcept
    {
        // 选一个空闲（或最老）的颗粒槽
        Grain* slot = &grains[0];
        Grain* other = &grains[1];
        if (grains[0].active && (! grains[1].active || grains[1].age > grains[0].age))
            std::swap (slot, other);

        // 起始延迟：保证整个颗粒期间延迟都不小于 minDelay
        const double startDelay = minDelay + (ratio > 1.0 ? (ratio - 1.0) * grainLen : 0.0);
        const double base = (double) writeCount - startDelay;
        double best = base;

        if (other->active)
        {
            // 在 [base - maxShift, base] 里找与旧颗粒接下来要播放的波形最相关的位置
            double bestScore = -1.0e30;
            for (int k = 0; k <= maxShift; ++k)
            {
                const double cand = base - k;
                double dot = 0.0, energy = 1.0e-9;
                for (int j = 0; j < corrLen; j += 2)
                {
                    const float a = read (cand + j * ratio);
                    dot += a * read (other->pos + j * ratio);
                    energy += a * a;
                }
                const double score = dot / std::sqrt (energy);
                if (score > bestScore) { bestScore = score; best = cand; }
            }
        }

        slot->pos = best;
        slot->age = 0;
        slot->active = true;
    }

    double sr = 48000.0, ratio = 1.0;
    int grainLen = 2400, hop = 1200, corrLen = 256, maxShift = 576, minDelay = 516;
    int size = 1, mask = 0, samplesToNextGrain = 0;
    int64_t writeCount = 0;
    std::vector<float> buffer, window;
    Grain grains[2];
};

// 零延迟拓扑状态变量滤波器（Cytomic / Zavalishin TPT SVF），2 声道
struct Svf
{
    float g = 0.0f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1[2] {}, ic2[2] {};

    void reset() { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.0f; }

    void set (double sr, double freq, double q) noexcept
    {
        g = (float) std::tan (juce::MathConstants<double>::pi * juce::jlimit (20.0, sr * 0.45, freq) / sr);
        k = (float) (1.0 / q);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    // 返回带通，lowOut 输出低通
    inline float process (float x, int ch, float& lowOut) noexcept
    {
        const float v3 = x - ic2[ch];
        const float v1 = a1 * ic1[ch] + a2 * v3;
        const float v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
        ic1[ch] = 2.0f * v1 - ic1[ch];
        ic2[ch] = 2.0f * v2 - ic2[ch];
        lowOut = v2;
        return v1;
    }
};
