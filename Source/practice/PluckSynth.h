#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <vector>

// 拨弦合成器（Karplus-Strong），用来放示范音和伴奏。音频线程里不分配内存。
class PluckSynth
{
public:
    struct NoteOn
    {
        int midi = 64;
        float velocity = 0.8f;
        double lengthSeconds = 0.5;
        int delaySamples = 0;        // 在当前块里延后多少个样本开始
        float bendSemitones = 0.0f;  // 推弦到多高
        bool palmMute = false, dead = false, harmonic = false, vibrato = false, hammer = false, letRing = false;
        bool bass = false;
        float pan = 0.0f;            // -1..1
        float gain = 1.0f;
        float pickPosition = 0.18f;  // 0.05..0.5 along the string
    };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const int maxLen = (int) (sr / 25.0) + 4;     // lowest note about 25 Hz
        excitation.assign ((size_t) maxLen, 0.0f);
        for (auto& v : voices)
        {
            v.buffer.assign ((size_t) maxLen, 0.0f);
            v.active = false;
        }
    }

    void allNotesOff()
    {
        for (auto& v : voices)
            v.releaseNow = true;
    }

    void noteOn (const NoteOn& n) noexcept
    {
        // 找空闲声部；没有就抢最老的
        Voice* v = nullptr;
        for (auto& c : voices) if (! c.active) { v = &c; break; }
        if (v == nullptr)
        {
            v = &voices[0];
            for (auto& c : voices) if (c.age > v->age) v = &c;
        }

        const int midi = n.harmonic ? n.midi + 12 : n.midi;
        const double freq = 440.0 * std::pow (2.0, (midi - 69) / 12.0);

        float brightness = n.palmMute ? 0.25f : (n.bass ? 0.35f : (n.harmonic ? 0.9f : 0.6f));
        brightness *= 0.7f + 0.3f * juce::jlimit (0.0f, 1.0f, n.velocity);
        v->brightness = juce::jlimit (0.08f, 0.95f, brightness);
        // 环路里的一阶低通会额外延迟 (1-b)/b 个样本，要从延迟线长度里扣掉，否则音会偏低
        const double filterDelay = (1.0 - v->brightness) / v->brightness;
        v->baseDelay = juce::jlimit (2.0, (double) v->buffer.size() - 2.0, sr / freq - filterDelay);
        v->bendTarget = n.bendSemitones;
        v->bendSamples = juce::jmax (1.0, n.lengthSeconds * sr * 0.4);
        v->vibrato = n.vibrato;
        v->vibPhase = 0.0;
        v->startDelay = n.delaySamples;
        v->holdSamples = n.letRing ? (int) (sr * 4.0) : (int) (n.lengthSeconds * sr);
        v->age = 0;
        v->releaseNow = false;
        v->active = true;
        v->writePos = 0;
        v->lp = 0.0f;
        v->dead = n.dead;

        // 衰减：闷音很快，贝斯和普通音比较长
        const double sustainSec = n.dead ? 0.03 : (n.palmMute ? 0.18 : (n.bass ? 2.2 : 1.6));
        v->loss = (float) std::pow (0.001, 1.0 / (sustainSec * freq));

        const float amp = n.velocity * n.gain * (n.hammer ? 0.6f : 1.0f) * (n.dead ? 0.5f : 1.0f);
        const float angle = (n.pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        v->gainL = amp * std::cos (angle);
        v->gainR = amp * std::sin (angle);

        // Excitation: noise with a pick-position comb, then a gentle low-pass.
        // The comb is what gives a plucked string its characteristic spectrum.
        const int len = juce::jmin ((int) v->baseDelay + 2, (int) excitation.size());
        const float soft = n.hammer ? 0.35f : (0.45f + 0.25f * juce::jlimit (0.0f, 1.0f, n.velocity));
        const int pickDelay = juce::jmax (1, (int) (juce::jlimit (0.05f, 0.5f, n.pickPosition) * v->baseDelay));

        for (int i = 0; i < len; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            excitation[(size_t) i] = (float) (int32_t) seed * (1.0f / 2147483648.0f);
        }

        float state = 0.0f;
        for (int i = 0; i < (int) v->buffer.size(); ++i)
        {
            if (i < len)
            {
                float e = excitation[(size_t) i];
                if (i >= pickDelay)
                    e -= excitation[(size_t) (i - pickDelay)];

                state += soft * (e - state);
                v->buffer[(size_t) i] = state;
            }
            else
                v->buffer[(size_t) i] = 0.0f;
        }
        v->writePos = len % (int) v->buffer.size();
    }

    // 叠加到输出
    void render (float* left, float* right, int numSamples) noexcept
    {
        for (auto& v : voices)
        {
            if (! v.active)
                continue;

            const int size = (int) v.buffer.size();
            for (int i = 0; i < numSamples; ++i)
            {
                if (v.startDelay > 0) { --v.startDelay; continue; }

                // 推弦 / 揉弦：改变延迟线长度
                double delay = v.baseDelay;
                if (v.bendTarget > 0.0f)
                {
                    const double progress = juce::jmin (1.0, (double) v.age / v.bendSamples);
                    delay /= std::pow (2.0, v.bendTarget * progress / 12.0);
                }
                if (v.vibrato)
                {
                    v.vibPhase += juce::MathConstants<double>::twoPi * 5.5 / sr;
                    delay /= std::pow (2.0, 0.25 * std::sin (v.vibPhase) / 12.0);
                }

                double readPos = v.writePos - delay;
                while (readPos < 0) readPos += size;
                const int i0 = (int) readPos;
                const int i1 = (i0 + 1) % size;
                const float frac = (float) (readPos - i0);
                const float y = v.buffer[(size_t) i0] * (1.0f - frac) + v.buffer[(size_t) i1] * frac;

                // 一阶低通 + 衰减
                v.lp += v.brightness * (y - v.lp);
                float loss = v.loss;
                if (v.releaseNow || v.age > v.holdSamples)
                    loss *= 0.992f;                       // 松开：快速衰减
                v.buffer[(size_t) v.writePos] = v.lp * loss;
                v.writePos = (v.writePos + 1) % size;

                left[i] += y * v.gainL;
                right[i] += y * v.gainR;
                ++v.age;
            }

            // 足够安静就停掉
            if (v.age > 2000 && (v.age & 255) == 0)
            {
                float peak = 0.0f;
                for (auto s : v.buffer) peak = juce::jmax (peak, std::abs (s));
                if (peak < 1.0e-4f)
                    v.active = false;
            }
            if (v.age > (int) (sr * 8.0))
                v.active = false;
        }
    }

private:
    struct Voice
    {
        std::vector<float> buffer;
        bool active = false, releaseNow = false, vibrato = false, dead = false;
        double baseDelay = 100.0, bendSamples = 1.0, vibPhase = 0.0;
        float bendTarget = 0.0f, loss = 0.996f, brightness = 0.5f, lp = 0.0f, gainL = 0.5f, gainR = 0.5f;
        int writePos = 0, startDelay = 0, holdSamples = 0, age = 0;
    };

    double sr = 48000.0;
    std::array<Voice, 48> voices;
    std::vector<float> excitation;
    uint32_t seed = 987654321u;
};
