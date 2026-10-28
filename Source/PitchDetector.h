#pragma once

#include <juce_core/juce_core.h>
#include <cmath>
#include <vector>

// YIN 基频检测（de Cheveigné & Kawahara, 2002），用于调音器和以后的单音判定。
// 在界面线程上跑，不占用音频线程。
class PitchDetector
{
public:
    struct Result
    {
        float frequency = 0.0f;  // Hz，0 表示没检测到
        float clarity   = 0.0f;  // 0..1，越大越可信
        float rmsDb     = -100.0f;
    };

    // buffer 长度 = 2 * W，可检测的最低频率约为 sampleRate / W
    Result detect (const float* buffer, int length, double sampleRate,
                   float minHz = 60.0f, float maxHz = 1400.0f, float threshold = 0.15f)
    {
        Result r;
        if (length < 64 || sampleRate <= 0.0)
            return r;

        double sumSq = 0.0;
        for (int i = 0; i < length; ++i)
            sumSq += (double) buffer[i] * buffer[i];
        r.rmsDb = juce::Decibels::gainToDecibels ((float) std::sqrt (sumSq / length), -100.0f);

        if (r.rmsDb < -55.0f)   // 太小声，当作没弹
            return r;

        const int W = length / 2;
        const int tauMin = juce::jmax (2, (int) (sampleRate / maxHz));
        const int tauMax = juce::jmin (W - 1, (int) (sampleRate / minHz));
        if (tauMax <= tauMin)
            return r;

        diff.assign ((size_t) tauMax + 2, 0.0f);

        // 差分函数
        for (int tau = 1; tau <= tauMax + 1; ++tau)
        {
            float s = 0.0f;
            for (int j = 0; j < W; ++j)
            {
                const float d = buffer[j] - buffer[j + tau];
                s += d * d;
            }
            diff[(size_t) tau] = s;
        }

        // 累积均值归一化
        diff[0] = 1.0f;
        float running = 0.0f;
        for (int tau = 1; tau <= tauMax + 1; ++tau)
        {
            running += diff[(size_t) tau];
            diff[(size_t) tau] = running > 0.0f ? diff[(size_t) tau] * (float) tau / running : 1.0f;
        }

        // 找第一个低于阈值的谷底；没有就取全局最小
        int best = -1;
        for (int tau = tauMin; tau <= tauMax; ++tau)
        {
            if (diff[(size_t) tau] < threshold)
            {
                while (tau + 1 <= tauMax && diff[(size_t) tau + 1] < diff[(size_t) tau])
                    ++tau;
                best = tau;
                break;
            }
        }

        if (best < 0)
        {
            best = tauMin;
            for (int tau = tauMin; tau <= tauMax; ++tau)
                if (diff[(size_t) tau] < diff[(size_t) best])
                    best = tau;

            if (diff[(size_t) best] > 0.35f)   // 太不确定，放弃
                return r;
        }

        // 抛物线插值，得到亚采样精度
        float betterTau = (float) best;
        if (best > 1 && best < tauMax)
        {
            const float s0 = diff[(size_t) best - 1], s1 = diff[(size_t) best], s2 = diff[(size_t) best + 1];
            const float denom = 2.0f * (2.0f * s1 - s2 - s0);
            if (std::abs (denom) > 1.0e-9f)
                betterTau = (float) best + (s2 - s0) / denom;
        }

        r.frequency = (float) (sampleRate / betterTau);
        r.clarity = juce::jlimit (0.0f, 1.0f, 1.0f - diff[(size_t) best]);
        return r;
    }

private:
    std::vector<float> diff;
};

// 频率 -> 音名与音分偏差
struct NoteInfo
{
    juce::String name;      // 例如 "E2"
    int midiNote = -1;
    float cents = 0.0f;     // -50..+50

    static NoteInfo fromFrequency (float hz, float a4 = 440.0f)
    {
        NoteInfo n;
        if (hz <= 0.0f)
            return n;

        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const float midi = 69.0f + 12.0f * std::log2 (hz / a4);
        n.midiNote = (int) std::lround (midi);
        n.cents = (midi - (float) n.midiNote) * 100.0f;
        n.name = juce::String (names[((n.midiNote % 12) + 12) % 12]) + juce::String (n.midiNote / 12 - 1);
        return n;
    }
};
