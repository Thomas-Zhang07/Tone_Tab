// 周边效果器：降噪、八度、移调、哇音、相位、揉音、法兹、图示均衡、箱体模拟、空灵混响
#include "BuiltinEffects.h"
#include "dsp/Biquad.h"
#include "dsp/DcBlocker.h"
#include "dsp/ParamSmoother.h"
#include "dsp/PitchShifter.h"
#include "Text.h"
#include <juce_dsp/juce_dsp.h>
#include <array>

namespace
{
    inline float dbToGain (float db) { return juce::Decibels::decibelsToGain (db, -120.0f); }
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    // 包络跟随（峰值，快起慢落）
    struct EnvelopeFollower
    {
        float env = 0.0f, att = 0.9f, rel = 0.999f;
        void set (double sr, double attackMs, double releaseMs)
        {
            att = (float) std::exp (-1.0 / (attackMs * 0.001 * sr));
            rel = (float) std::exp (-1.0 / (releaseMs * 0.001 * sr));
        }
        inline float process (float x) noexcept
        {
            const float a = std::abs (x);
            env = a > env ? att * env + (1.0f - att) * a : rel * env + (1.0f - rel) * a;
            return env;
        }
    };

    // =========================================================================
    // 降噪：向下扩展 + 迟滞 + 保持，可选去 50/60 Hz 嗡声
    class NoiseReductionFx final : public BuiltinEffect
    {
    public:
        NoiseReductionFx() : BuiltinEffect ("denoise", TR ("Noise Reduction", "降噪"))
        {
            threshold = addFloat ("threshold", "门限", -85.0f, -20.0f, -58.0f, "dB");
            amount    = addFloat ("amount", "降噪量", 0.0f, 80.0f, 40.0f, "dB");
            release   = addFloat ("release", "释放", 20.0f, 600.0f, 140.0f, "ms", 150.0f);
            hum       = addChoice ("hum", "去嗡声", { "关", "50 Hz（中国）", "60 Hz" }, 0);
        }

        void prepareEffect (double sr, int) override
        {
            detector.set (sr, 3.0, 25.0);   // 起音 3 ms：底噪里零星的尖峰不会把门打开
            gainNow = 1.0f;
            holdSamples = 0;
            open = true;
            lastHum = -1;
            for (auto& n : notches) n.reset();
        }

        void processEffect (juce::AudioBuffer<float>& b) override
        {
            const double sr = currentSampleRate;
            const int humMode = hum->getIndex();
            if (humMode != lastHum)
            {
                lastHum = humMode;
                const double base = humMode == 2 ? 60.0 : 50.0;
                for (int i = 0; i < 3; ++i)
                    notches[(size_t) i].setPeak (sr, base * (i + 1), 12.0, -30.0);
            }

            auto* l = b.getWritePointer (0);
            auto* r = b.getWritePointer (1);
            const int n = b.getNumSamples();

            if (humMode > 0)
                for (int ch = 0; ch < 2; ++ch)
                    for (auto& f : notches)
                        f.processBlock (b.getWritePointer (ch), n, ch);

            const float openLevel = dbToGain (threshold->get());
            const float closeLevel = openLevel * 0.5f;                       // 6 dB 迟滞，防止抖动
            const float floorGain = dbToGain (-amount->get());
            // “释放”= 从打开到压到最低所需的时间（约 40 dB），比时间常数更直观
            const float relCoef = (float) std::exp (-4.6 / (release->get() * 0.001 * sr));
            const float attCoef = (float) std::exp (-1.0 / (0.001 * sr));
            const int holdLen = (int) (0.04 * sr);

            for (int i = 0; i < n; ++i)
            {
                const float e = detector.process (0.5f * (l[i] + r[i]));
                if (e > openLevel)       { open = true; holdSamples = holdLen; }
                else if (e < closeLevel) { if (holdSamples > 0) --holdSamples; else open = false; }

                const float target = open ? 1.0f : floorGain;
                const float c = target > gainNow ? attCoef : relCoef;
                gainNow = c * gainNow + (1.0f - c) * target;
                l[i] *= gainNow;
                r[i] *= gainNow;
            }
        }

    private:
        juce::AudioParameterFloat *threshold, *amount, *release;
        juce::AudioParameterChoice* hum;
        EnvelopeFollower detector;
        std::array<Biquad, 3> notches;
        float gainNow = 1.0f;
        int holdSamples = 0, lastHum = -1;
        bool open = true;
    };

    // =========================================================================
    // 八度：复音（变调，能弹和弦）或单音模拟（分频 + 整流，类似 OC-2 / Octavia）
    class OctaveFx final : public BuiltinEffect
    {
    public:
        OctaveFx() : BuiltinEffect ("octave", TR ("Octave", "八度"))
        {
            mode = addChoice ("mode", "模式", { "复音（可弹和弦）", "单音模拟" }, 0);
            dry  = addFloat ("dry", "原声", 0.0f, 1.0f, 0.8f);
            down = addFloat ("down", "低八度", 0.0f, 1.0f, 0.7f);
            up   = addFloat ("up", "高八度", 0.0f, 1.0f, 0.0f);
            down2 = addFloat ("down2", "低两个八度", 0.0f, 1.0f, 0.0f);
        }

        void prepareEffect (double sr, int) override
        {
            shiftDown.prepare (sr, 60.0);  shiftDown.setSemitones (-12.0);
            shiftUp.prepare (sr, 30.0);    shiftUp.setSemitones (12.0);
            shiftDown2.prepare (sr, 80.0); shiftDown2.setSemitones (-24.0);
            sub2Lp.setLowpass (sr, 400.0, 0.7);
            aaIn.setLowpass (sr, sr * 0.45, 0.7);
            flip2 = 1.0f;
            detectLp.setLowpass (sr, 900.0, 0.7);
            subLp1.setLowpass (sr, 700.0, 0.7);
            subLp2.setLowpass (sr, 700.0, 0.7);
            upHp.setHighpass (sr, 90.0, 0.7);
            env.set (sr, 2.0, 60.0);
            flip = 1.0f;
            armed = true;
        }

        void processEffect (juce::AudioBuffer<float>& b) override
        {
            auto* l = b.getWritePointer (0);
            auto* r = b.getWritePointer (1);
            const bool poly = mode->getIndex() == 0;
            const float d = dry->get(), dn = down->get(), u = up->get(), dn2 = down2->get();

            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float x = 0.5f * (l[i] + r[i]);
                float sub, high, sub2 = 0.0f;

                if (poly)
                {
                    // Band-limit before the time-domain shifters to avoid folding
                    // high harmonics back into the audible range.
                    const float ax = aaIn.process (x, 0);
                    sub = shiftDown.process (ax);
                    high = shiftUp.process (ax);
                    if (dn2 > 0.0f)
                        sub2 = shiftDown2.process (ax);
                }
                else
                {
                    // 分频器：每个周期翻转一次方波 -> 低八度
                    const float lp = detectLp.process (x, 0);
                    const float e = env.process (lp);
                    const float th = 0.15f * e;
                    if (armed && lp > th)
                    {
                        flip = -flip;
                        armed = false;
                        if (flip > 0.0f) flip2 = -flip2;   // 再分一次频 -> 低两个八度
                    }
                    else if (! armed && lp < -th) armed = true;
                    sub = subLp2.process (subLp1.process (flip * e * 1.6f, 0), 0);
                    sub2 = sub2Lp.process (flip2 * e * 1.6f, 0);

                    // 全波整流 -> 高八度（带点法兹味）
                    high = upHp.process (std::abs (x) * 2.0f, 0);
                }

                const float y = d * x + dn * sub + u * high + dn2 * sub2;
                l[i] = r[i] = y;
            }
        }

    private:
        int effectLatencySamples() const override
        {
            if (mode->getIndex() != 0)
                return 0;
            return juce::jmax (shiftDown.getLatencySamples(), shiftUp.getLatencySamples());
        }

        juce::AudioParameterChoice* mode;
        juce::AudioParameterFloat *dry, *down, *up, *down2;
        PitchShifter shiftDown, shiftUp, shiftDown2;
        Biquad detectLp, subLp1, subLp2, upHp, sub2Lp, aaIn;
        EnvelopeFollower env;
        float flip = 1.0f, flip2 = 1.0f;
        bool armed = true;
    };

    // =========================================================================
    class PitchShiftFx final : public BuiltinEffect
    {
    public:
        PitchShiftFx() : BuiltinEffect ("pitch", TR ("Pitch Shift / Harmony", "移调 / 和声"))
        {
            semis = addInt ("semis", "半音", -12, 12, 7);
            cents = addFloat ("cents", "微调", -50.0f, 50.0f, 0.0f, "cent");
            mix   = addFloat ("mix", "混合", 0.0f, 1.0f, 0.5f);
        }

        void prepareEffect (double sr, int) override
        {
            shifter.prepare (sr, 40.0);
            aaIn.setLowpass (sr, sr * 0.45, 0.7);
            aaOut.setLowpass (sr, sr * 0.45, 0.7);
        }
        void resetEffect() override { shifter.reset(); aaIn.reset(); aaOut.reset(); }
        int effectLatencySamples() const override { return shifter.getLatencySamples(); }

        void processEffect (juce::AudioBuffer<float>& b) override
        {
            shifter.setSemitones (semis->get() + cents->get() / 100.0);
            const float m = mix->get();
            auto* l = b.getWritePointer (0);
            auto* r = b.getWritePointer (1);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float x = 0.5f * (l[i] + r[i]);
                const float y = aaOut.process (shifter.process (aaIn.process (x, 0)), 0);
                l[i] = r[i] = x * (1.0f - m) + y * m;
            }
        }

    private:
        juce::AudioParameterInt* semis;
        juce::AudioParameterFloat *cents, *mix;
        PitchShifter shifter;
        Biquad aaIn, aaOut;
    };

    // =========================================================================
    class WahFx final : public BuiltinEffect
    {
    public:
        WahFx() : BuiltinEffect ("wah", TR ("Wah", "哇音"))
        {
            mode  = addChoice ("mode", "模式", { "手动", "自动（跟随力度）", "自动（循环）" }, 1);
            pos   = addFloat ("position", "踏板位置", 0.0f, 1.0f, 0.5f);
            sens  = addFloat ("sensitivity", "灵敏度", 0.0f, 1.0f, 0.5f);
            rate  = addFloat ("rate", "循环速度", 0.1f, 8.0f, 1.5f, "Hz", 1.5f);
            reso  = addFloat ("resonance", "共鸣", 1.0f, 12.0f, 5.0f);
            mix   = addFloat ("mix", "混合", 0.0f, 1.0f, 1.0f);
        }

        void prepareEffect (double sr, int) override
        {
            env.set (sr, 4.0, 120.0);
            svf.reset();
            lfo = 0.0;
            smoothedPos = pos->get();
            posCoef = (float) std::exp (-1.0 / (0.008 * sr));
        }

        void processEffect (juce::AudioBuffer<float>& b) override
        {
            const double sr = currentSampleRate;
            const int m = mode->getIndex();
            const float s = sens->get(), wet = mix->get();
            const double q = reso->get();
            const double inc = twoPi * rate->get() / sr;
            auto* l = b.getWritePointer (0);
            auto* r = b.getWritePointer (1);

            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float x = 0.5f * (l[i] + r[i]);
                float target;
                if (m == 0)      target = pos->get();
                else if (m == 1) target = juce::jlimit (0.0f, 1.0f, env.process (x) * (2.0f + 30.0f * s * s));
                else
                {
                    target = 0.5f + 0.5f * (float) std::sin (lfo);
                    lfo += inc;
                    if (lfo > twoPi) lfo -= twoPi;
                }
                smoothedPos = posCoef * smoothedPos + (1.0f - posCoef) * target;

                if ((i & 15) == 0)
                    svf.set (sr, 350.0 * std::pow (2400.0 / 350.0, (double) smoothedPos), q);

                float low;
                const float bp = svf.process (x, 0, low);
                const float y = 1.6f * bp + 0.15f * low;
                l[i] = r[i] = x * (1.0f - wet) + y * wet;
            }
        }

    private:
        juce::AudioParameterChoice* mode;
        juce::AudioParameterFloat *pos, *sens, *rate, *reso, *mix;
        EnvelopeFollower env;
        Svf svf;
        double lfo = 0.0;
        float smoothedPos = 0.5f, posCoef = 0.99f;
    };

    // =========================================================================
    class PhaserFx final : public BuiltinEffect
    {
    public:
        PhaserFx() : BuiltinEffect ("phaser", TR ("Phaser", "相位"))
        {
            rate     = addFloat ("rate", "速度", 0.05f, 6.0f, 0.5f, "Hz", 0.8f);
            depth    = addFloat ("depth", "深度", 0.0f, 1.0f, 0.75f);
            feedback = addFloat ("feedback", "反馈", 0.0f, 0.9f, 0.4f);
            stages   = addChoice ("stages", "级数", { "4 级", "6 级", "8 级" }, 1);
            mix      = addFloat ("mix", "混合", 0.0f, 1.0f, 0.5f);
        }

        void prepareEffect (double, int) override
        {
            for (auto& ch : state) std::fill (ch.begin(), ch.end(), 0.0f);
            fb[0] = fb[1] = 0.0f;
            lfo = 0.0;
        }

        void processEffect (juce::AudioBuffer<float>& b) override
        {
            const double sr = currentSampleRate;
            const double inc = twoPi * rate->get() / sr;
            const int nStages = 4 + 2 * stages->getIndex();
            const float d = depth->get(), fbk = feedback->get(), m = mix->get();

            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                if ((i & 7) == 0)
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        // 右声道相位差 90°，形成立体声
                        const double lf = 0.5 + 0.5 * std::sin (lfo + (ch == 1 ? juce::MathConstants<double>::halfPi : 0.0));
                        const double f = 200.0 * std::pow (12.0, lf * d);
                        const double t = std::tan (juce::MathConstants<double>::pi * f / sr);
                        coef[ch] = (float) ((t - 1.0) / (t + 1.0));
                    }

                for (int ch = 0; ch < 2; ++ch)
                {
                    auto* data = b.getWritePointer (ch);
                    const float x = data[i];
                    float y = x + fb[ch] * fbk;
                    for (int s = 0; s < nStages; ++s)
                    {
                        const float out = coef[ch] * y + state[(size_t) ch][(size_t) s];
                        state[(size_t) ch][(size_t) s] = y - coef[ch] * out;
                        y = out;
                    }
                    fb[ch] = y;
                    data[i] = x * (1.0f - 0.5f * m) + y * m;
                }

                lfo += inc;
                if (lfo > twoPi) lfo -= twoPi;
            }
        }

    private:
        juce::AudioParameterFloat *rate, *depth, *feedback, *mix;
        juce::AudioParameterChoice* stages;
        std::array<std::array<float, 8>, 2> state {};
        float coef[2] {}, fb[2] {};
        double lfo = 0.0;
    };

    // =========================================================================
    // 揉音：音高颤动（调制延迟，100% 湿声）
    class VibratoFx final : public BuiltinEffect
    {
    public:
        VibratoFx() : BuiltinEffect ("vibrato", TR ("Vibrato", "揉音"))
        {
            rate  = addFloat ("rate", "速度", 0.5f, 12.0f, 5.0f, "Hz", 4.0f);
            depth = addFloat ("depth", "深度", 0.0f, 1.0f, 0.35f);
            mix   = addFloat ("mix", "混合", 0.0f, 1.0f, 1.0f);
        }

        void prepareEffect (double sr, int) override
        {
            size = 1;
            while (size < (int) (0.03 * sr)) size <<= 1;
            for (auto& line : lines) line.assign ((size_t) size, 0.0f);
            writePos = 0;
            lfo = 0.0;
        }

        void processEffect (juce::AudioBuffer<float>& b) override
        {
            const double sr = currentSampleRate;
            const double inc = twoPi * rate->get() / sr;
            const double base = 0.004 * sr, swing = 0.0035 * sr * depth->get();
            const float m = mix->get();

            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const double delay = base + swing * std::sin (lfo);
                lfo += inc;
                if (lfo > twoPi) lfo -= twoPi;

                for (int ch = 0; ch < 2; ++ch)
                {
                    auto* data = b.getWritePointer (ch);
                    lines[(size_t) ch][(size_t) writePos] = data[i];
                    double p = writePos - delay;
                    while (p < 0) p += size;
                    const int i0 = (int) p;
                    const float f = (float) (p - i0);
                    const float wet = lines[(size_t) ch][(size_t) i0] * (1.0f - f) + lines[(size_t) ch][(size_t) ((i0 + 1) & (size - 1))] * f;
                    data[i] = data[i] * (1.0f - m) + wet * m;
                }
                writePos = (writePos + 1) & (size - 1);
            }
        }

    private:
        juce::AudioParameterFloat *rate, *depth, *mix;
        std::array<std::vector<float>, 2> lines;
        int size = 1, writePos = 0;
        double lfo = 0.0;
    };

    // =========================================================================
    class FuzzFx final : public BuiltinEffect
    {
    public:
        FuzzFx() : BuiltinEffect ("fuzz", TR ("Fuzz", "法兹"))
        {
            fuzz  = addFloat ("fuzz", "法兹", 0.0f, 1.0f, 0.7f);
            tone  = addFloat ("tone", "音色", 0.0f, 1.0f, 0.5f);
            level = addFloat ("level", "音量", -40.0f, 6.0f, -16.0f, "dB");
            bias  = addFloat ("bias", "偏压", 0.0f, 1.0f, 0.2f);
        }

        void prepareEffect (double sr, int block) override
        {
            oversampling.initProcessing ((size_t) block);
            oversampling.reset();
            dc.prepare (sr, 12.0);
            pre.setHighpass (sr, 90.0, 0.7);
            last = -1.0f;
        }

        void processEffect (juce::AudioBuffer<float>& b) override
        {
            const double sr = currentSampleRate;
            if (! juce::approximatelyEqual (last, tone->get()))
            {
                last = tone->get();
                post.setLowpass (sr, 1200.0 + 6000.0 * last, 0.7);
                scoop.setPeak (sr, 900.0, 0.7, -6.0 * (1.0 - last));
            }

            for (int ch = 0; ch < 2; ++ch)
                pre.processBlock (b.getWritePointer (ch), b.getNumSamples(), ch);

            const float g = dbToGain (20.0f + 40.0f * fuzz->get());
            // 偏压越大越不对称，弹轻时会出现“破音 / 断续”的门限感
            const float bs = 0.05f + 0.6f * bias->get();
            const float dcOffset = 1.0f - std::exp (-bs);
            juce::dsp::AudioBlock<float> block (b);
            auto up = oversampling.processSamplesUp (block);
            for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
            {
                auto* d = up.getChannelPointer (ch);
                for (size_t i = 0; i < up.getNumSamples(); ++i)
                {
                    // 晶体管法兹：非对称硬饱和
                    const float x = d[i] * g + bs;
                    d[i] = (x > 0.0f ? 1.0f - std::exp (-x) : -0.75f * (1.0f - std::exp (1.4f * x))) - dcOffset;
                }
            }
            oversampling.processSamplesDown (block);

            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = b.getWritePointer (ch);
                dc.processBlock (d, b.getNumSamples(), ch);
                post.processBlock (d, b.getNumSamples(), ch);
                scoop.processBlock (d, b.getNumSamples(), ch);
            }
            b.applyGain (dbToGain (level->get()));
        }

        int effectLatencySamples() const override { return (int) std::round (oversampling.getLatencyInSamples()); }

    private:
        juce::AudioParameterFloat *fuzz, *tone, *level, *bias;
        juce::dsp::Oversampling<float> oversampling { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true };
        Biquad pre, post, scoop;
        DcBlocker dc;
        float last = -1.0f;
    };

    // =========================================================================
    class GraphicEqFx final : public BuiltinEffect
    {
    public:
        GraphicEqFx() : BuiltinEffect ("geq", TR ("Graphic EQ", "图示均衡"))
        {
            static const char* names[] = { "100 Hz", "200 Hz", "400 Hz", "800 Hz", "1.6 kHz", "3.2 kHz", "6.4 kHz" };
            for (int i = 0; i < 7; ++i)
                bands[(size_t) i] = addFloat ("b" + juce::String (i), names[i], -12.0f, 12.0f, 0.0f, "dB");
            level = addFloat ("level", "音量", -12.0f, 12.0f, 0.0f, "dB");
        }

        void prepareEffect (double sr, int) override
        {
            std::fill (last.begin(), last.end(), -100.0f);
            for (auto& f : filters) f.prepare (sr);
            levelSm.prepare (sr); levelSm.snap (dbToGain (level->get()));
        }

        void processEffect (juce::AudioBuffer<float>& b) override
        {
            for (int i = 0; i < 7; ++i)
            {
                const float v = bands[(size_t) i]->get();
                if (! juce::approximatelyEqual (v, last[(size_t) i]))
                {
                    last[(size_t) i] = v;
                    filters[(size_t) i].setPeak (100.0 * std::pow (2.0, i), 1.2, v);
                }
            }

            levelSm.setTarget (dbToGain (level->get()));
            const int n = b.getNumSamples();
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = b.getWritePointer (ch);
                for (int i = 0; i < 7; ++i)
                    filters[(size_t) i].processBlock (d, n, ch);
            }
            for (int i = 0; i < n; ++i)
            {
                const float g = levelSm.next();
                for (int ch = 0; ch < 2; ++ch)
                    b.getWritePointer (ch)[i] *= g;
            }
        }

        void resetEffect() override { for (auto& f : filters) f.reset(); }

    private:
        std::array<juce::AudioParameterFloat*, 7> bands {};
        juce::AudioParameterFloat* level;
        std::array<GlidedBiquad, 7> filters;
        SmoothedGain levelSm;
        std::array<float, 7> last {};
    };

    // =========================================================================
    // 箱体模拟：给只有箱头的 NAM 模型或失真效果器补上音箱的频响
    class CabSimFx final : public BuiltinEffect
    {
    public:
        CabSimFx() : BuiltinEffect ("cab", TR ("Cab Sim", "箱体模拟"))
        {
            type   = addChoice ("type", "箱体", { "1x12 开放式", "2x12", "4x12 封闭式" }, 2);
            lowCut = addFloat ("lowcut", "低切", 40.0f, 200.0f, 75.0f, "Hz");
            bright = addFloat ("bright", "亮度", 0.0f, 1.0f, 0.5f);
            reso   = addFloat ("resonance", "低频共鸣", 0.0f, 8.0f, 3.0f, "dB");
            level  = addFloat ("level", "音量", -24.0f, 12.0f, 0.0f, "dB");
            mic    = addFloat ("mic", "话筒位置", 0.0f, 1.0f, 0.3f);
        }

        void prepareEffect (double, int) override
        {
            lastHash = -1.0f;
            for (auto* f : { &hp, &res, &mid, &notch, &lp1, &lp2 }) f->reset();
        }

        void processEffect (juce::AudioBuffer<float>& b) override
        {
            const double sr = currentSampleRate;
            const float hash = (float) type->getIndex() * 1000.0f + lowCut->get() + bright->get() * 7.0f + reso->get() * 13.0f + mic->get() * 31.0f;
            if (! juce::approximatelyEqual (hash, lastHash))
            {
                lastHash = hash;
                const int t = type->getIndex();
                // 话筒对准喇叭中心更亮、移到边缘更暗更厚
                const double micPos = mic->get();
                const double brightMul = (0.7 + 0.7 * bright->get()) * (1.15 - 0.45 * micPos);
                const double resF[]   = { 105.0, 110.0, 95.0 };
                const double midF[]   = { 2200.0, 1800.0, 2800.0 };
                const double midG[]   = { 3.0, 2.0, 3.5 };
                const double notchG[] = { -1.0, -2.0, -3.5 };
                const double lpF[]    = { 5600.0, 5000.0, 4500.0 };
                hp.setHighpass (sr, lowCut->get(), 0.7);
                res.setPeak (sr, resF[t], 1.2, reso->get());
                mid.setPeak (sr, midF[t], 1.0, midG[t] * (1.2 - 0.8 * micPos));
                notch.setPeak (sr, 480.0, 1.0, notchG[t]);
                lp1.setLowpass (sr, lpF[t] * brightMul, 0.8);
                lp2.setLowpass (sr, lpF[t] * brightMul * 1.25, 0.6);
            }

            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = b.getWritePointer (ch);
                for (int i = 0; i < b.getNumSamples(); ++i)
                    d[i] = lp2.process (lp1.process (notch.process (mid.process (res.process (hp.process (d[i], ch), ch), ch), ch), ch), ch);
            }
            b.applyGain (dbToGain (level->get()));
        }

    private:
        juce::AudioParameterChoice* type;
        juce::AudioParameterFloat *lowCut, *bright, *reso, *level, *mic;
        Biquad hp, res, mid, notch, lp1, lp2;
        float lastHash = -1.0f;
    };

    // =========================================================================
    // 空灵混响：混响尾音不断被升高八度后再送回混响
    class ShimmerFx final : public BuiltinEffect
    {
    public:
        ShimmerFx() : BuiltinEffect ("shimmer", TR ("Shimmer", "空灵混响"))
        {
            size     = addFloat ("size", "空间", 0.0f, 1.0f, 0.85f);
            shimmer  = addFloat ("shimmer", "闪烁", 0.0f, 0.8f, 0.45f);
            interval = addChoice ("interval", "音程", { "高八度", "高五度" }, 0);
            mix      = addFloat ("mix", "混合", 0.0f, 1.0f, 0.35f);
        }

        void prepareEffect (double sr, int block) override
        {
            reverb.setSampleRate (sr);
            reverb.reset();
            shifter.prepare (sr, 50.0);
            wetL.assign ((size_t) block, 0.0f);
            wetR.assign ((size_t) block, 0.0f);
            feedback.assign ((size_t) block, 0.0f);
            fbHp.setHighpass (sr, 400.0, 0.7);
            fbLp.setLowpass (sr, 7000.0, 0.7);
        }

        void resetEffect() override { reverb.reset(); shifter.reset(); }

        void processEffect (juce::AudioBuffer<float>& b) override
        {
            const int n = b.getNumSamples();
            if (n > (int) wetL.size())
                return;

            juce::Reverb::Parameters p;
            p.roomSize = 0.7f + 0.29f * size->get();
            p.damping = 0.3f;
            p.wetLevel = 1.0f;
            p.dryLevel = 0.0f;
            p.width = 1.0f;
            reverb.setParameters (p);
            shifter.setSemitones (interval->getIndex() == 0 ? 12.0 : 7.0);

            auto* l = b.getWritePointer (0);
            auto* r = b.getWritePointer (1);
            const float s = shimmer->get();

            for (int i = 0; i < n; ++i)
            {
                // 上一块的混响尾音升调后叠回输入（相差一个块，不影响听感）
                const float fb = fbLp.process (fbHp.process (shifter.process (feedback[(size_t) i]), 0), 0);
                wetL[(size_t) i] = 0.3f * l[i] + s * fb;
                wetR[(size_t) i] = 0.3f * r[i] + s * fb;
            }

            reverb.processStereo (wetL.data(), wetR.data(), n);

            const float m = mix->get();
            for (int i = 0; i < n; ++i)
            {
                // juce::Reverb 内部湿声是 3 倍，反馈前除回去，再软限幅，保证回路不会越滚越大
                feedback[(size_t) i] = std::tanh (0.5f * (wetL[(size_t) i] + wetR[(size_t) i]) / 3.0f);
                l[i] = l[i] * (1.0f - 0.5f * m) + wetL[(size_t) i] * m * 0.5f;
                r[i] = r[i] * (1.0f - 0.5f * m) + wetR[(size_t) i] * m * 0.5f;
            }
        }

    private:
        juce::AudioParameterFloat *size, *shimmer, *mix;
        juce::AudioParameterChoice* interval;
        juce::Reverb reverb;
        PitchShifter shifter;
        std::vector<float> wetL, wetR, feedback;
        Biquad fbHp, fbLp;
    };
}

std::unique_ptr<BuiltinEffect> createMoreEffect (const juce::String& id)
{
    if (id == "denoise") return std::make_unique<NoiseReductionFx>();
    if (id == "octave")  return std::make_unique<OctaveFx>();
    if (id == "pitch")   return std::make_unique<PitchShiftFx>();
    if (id == "wah")     return std::make_unique<WahFx>();
    if (id == "phaser")  return std::make_unique<PhaserFx>();
    if (id == "vibrato") return std::make_unique<VibratoFx>();
    if (id == "fuzz")    return std::make_unique<FuzzFx>();
    if (id == "geq")     return std::make_unique<GraphicEqFx>();
    if (id == "cab")     return std::make_unique<CabSimFx>();
    if (id == "shimmer") return std::make_unique<ShimmerFx>();
    return nullptr;
}
