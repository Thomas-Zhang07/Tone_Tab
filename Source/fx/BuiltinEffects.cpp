#include "BuiltinEffects.h"
#include "dsp/Biquad.h"
#include "dsp/DcBlocker.h"
#include "dsp/ParamSmoother.h"
#include "HostTempo.h"
#include "Text.h"
#include <juce_dsp/juce_dsp.h>
#include <array>

namespace
{
    inline float dbToGain (float db) { return juce::Decibels::decibelsToGain (db, -100.0f); }

    // 参数改变时才重新算系数
    struct ChangeTracker
    {
        float last[8] { -1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f };
        bool changed (std::initializer_list<float> values)
        {
            bool c = false;
            int i = 0;
            for (float v : values)
            {
                if (i < 8 && ! juce::approximatelyEqual (last[i], v)) { last[i] = v; c = true; }
                ++i;
            }
            return c;
        }
        void invalidate() { for (auto& v : last) v = -1.0e9f; }
    };

    // =========================================================================
    class NoiseGateFx final : public BuiltinEffect
    {
    public:
        NoiseGateFx() : BuiltinEffect ("gate", TR ("Noise Gate", "噪声门"))
        {
            threshold = addFloat ("threshold", "门限", -90.0f, -20.0f, -60.0f, "dB");
            release   = addFloat ("release", "释放", 10.0f, 500.0f, 80.0f, "ms", 100.0f);
            attack    = addFloat ("attack", "起音", 0.1f, 20.0f, 0.5f, "ms", 2.0f);
            ratio     = addFloat ("ratio", "压制比", 2.0f, 30.0f, 8.0f, ":1", 8.0f);
        }

        void prepareEffect (double sr, int block) override
        {
            gate.prepare ({ sr, (juce::uint32) block, 2 });
            gate.reset();
        }

        void processEffect (juce::AudioBuffer<float>& b) override
        {
            gate.setThreshold (threshold->get());
            gate.setRelease (release->get());
            gate.setAttack (attack->get());
            gate.setRatio (ratio->get());
            juce::dsp::AudioBlock<float> block (b);
            gate.process (juce::dsp::ProcessContextReplacing<float> (block));
        }

        void resetEffect() override { gate.reset(); }

    private:
        juce::AudioParameterFloat *threshold, *release, *attack, *ratio;
        juce::dsp::NoiseGate<float> gate;
    };

    // =========================================================================
    class CompressorFx final : public BuiltinEffect
    {
    public:
        CompressorFx() : BuiltinEffect ("comp", TR ("Compressor", "压缩"))
        {
            threshold = addFloat ("threshold", "门限", -50.0f, 0.0f, -24.0f, "dB");
            ratio     = addFloat ("ratio", "压缩比", 1.0f, 20.0f, 4.0f, ":1", 4.0f);
            attack    = addFloat ("attack", "启动", 0.1f, 100.0f, 8.0f, "ms", 10.0f);
            release   = addFloat ("release", "释放", 10.0f, 1000.0f, 150.0f, "ms", 150.0f);
            makeup    = addFloat ("makeup", "补偿增益", 0.0f, 24.0f, 6.0f, "dB");
            tone      = addFloat ("tone", "音色", -6.0f, 6.0f, 0.0f, "dB");
            blend     = addFloat ("blend", "干声混合", 0.0f, 1.0f, 0.0f);
        }

        void prepareEffect (double sr, int block) override
        {
            comp.prepare ({ sr, (juce::uint32) block, 2 });
            comp.reset();
            dry.setSize (2, block, false, true, false);
            lastTone = -100.0f;
        }

        void processEffect (juce::AudioBuffer<float>& b) override
        {
            const int n = b.getNumSamples();
            const float bl = blend->get();
            if (bl > 0.0f && n <= dry.getNumSamples())
                for (int ch = 0; ch < 2; ++ch)
                    dry.copyFrom (ch, 0, b, ch, 0, n);

            comp.setThreshold (threshold->get());
            comp.setRatio (ratio->get());
            comp.setAttack (attack->get());
            comp.setRelease (release->get());
            juce::dsp::AudioBlock<float> block (b);
            comp.process (juce::dsp::ProcessContextReplacing<float> (block));
            b.applyGain (dbToGain (makeup->get()));

            if (! juce::approximatelyEqual (lastTone, tone->get()))
            {
                lastTone = tone->get();
                toneFilter.setHighShelf (currentSampleRate, 2500.0, lastTone);
            }
            if (std::abs (lastTone) > 0.05f)
                for (int ch = 0; ch < 2; ++ch)
                    toneFilter.processBlock (b.getWritePointer (ch), n, ch);

            // 并联压缩：保留一部分原声的起音
            if (bl > 0.0f && n <= dry.getNumSamples())
                for (int ch = 0; ch < 2; ++ch)
                {
                    b.applyGain (ch, 0, n, 1.0f - bl);
                    b.addFrom (ch, 0, dry, ch, 0, n, bl);
                }
        }

        void resetEffect() override { comp.reset(); toneFilter.reset(); }

    private:
        juce::AudioParameterFloat *threshold, *ratio, *attack, *release, *makeup, *tone, *blend;
        juce::dsp::Compressor<float> comp;
        juce::AudioBuffer<float> dry;
        Biquad toneFilter;
        float lastTone = -100.0f;
    };

    // =========================================================================
    // 带 2 倍过采样的失真基类（减少混叠，高频更干净）
    class ClipperBase : public BuiltinEffect
    {
    public:
        using BuiltinEffect::BuiltinEffect;

        void prepareEffect (double sr, int block) override
        {
            oversampling.initProcessing ((size_t) block);
            oversampling.reset();
            dry.setSize (2, block, false, true, false);
            dc.prepare (sr, 12.0);
            levelSm.prepare (sr); levelSm.snap (dbToGain (levelDb()));
            blendSm.prepare (sr); blendSm.snap (blendAmount());
            cleanSm.prepare (sr); cleanSm.snap (cleanAmount());
            preparePre (sr);
            tracker.invalidate();
        }

        void processEffect (juce::AudioBuffer<float>& b) override
        {
            const int n = b.getNumSamples();
            updateFilters (currentSampleRate);
            const float bl = blendAmount();
            const float clean = cleanAmount();
            const bool useDry = (bl > 0.0f || clean > 0.0f) && n <= dry.getNumSamples();
            if (useDry)
                for (int ch = 0; ch < 2; ++ch)
                    dry.copyFrom (ch, 0, b, ch, 0, n);

            for (int ch = 0; ch < 2; ++ch)
                pre.processBlock (b.getWritePointer (ch), n, ch);

            juce::dsp::AudioBlock<float> block (b);
            auto up = oversampling.processSamplesUp (block);
            const float g = dbToGain (driveDb());
            for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
            {
                auto* d = up.getChannelPointer (ch);
                for (size_t i = 0; i < up.getNumSamples(); ++i)
                    d[i] = shape (d[i] * g);
            }
            oversampling.processSamplesDown (block);

            // Remove the DC offset created by asymmetric clipping before mixing.
            for (int ch = 0; ch < 2; ++ch)
                dc.processBlock (b.getWritePointer (ch), n, ch);

            // Clean path, smoothed per sample.
            if (useDry)
            {
                cleanSm.setTarget (cleanAmount());
                for (int i = 0; i < n; ++i)
                {
                    const float cl = cleanSm.next();
                    if (cl > 0.0f)
                        for (int ch = 0; ch < 2; ++ch)
                            b.getWritePointer (ch)[i] += dry.getReadPointer (ch)[i] * cl;
                }
            }

            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = b.getWritePointer (ch);
                post.processBlock (d, n, ch);
                post2.processBlock (d, n, ch);
                post3.processBlock (d, n, ch);
            }

            // Output level and dry blend, both smoothed.
            levelSm.setTarget (dbToGain (levelDb()));
            blendSm.setTarget (blendAmount());
            for (int i = 0; i < n; ++i)
            {
                const float lvl = levelSm.next();
                const float bl = blendSm.next();
                const float dryGain = bl * lvl * 1.9952623f;   // +6 dB
                for (int ch = 0; ch < 2; ++ch)
                {
                    auto* d = b.getWritePointer (ch);
                    d[i] *= lvl;
                    if (useDry && dryGain > 0.0f)
                        d[i] += dry.getReadPointer (ch)[i] * dryGain;
                }
            }
        }

        void resetEffect() override { oversampling.reset(); dc.reset(); pre.reset(); post.reset(); post2.reset(); post3.reset(); }
        // Oversampling latency plus the ~2-sample group delay of the IIR
        // pre/post filters. Reporting it keeps the host and judge aligned.
        int effectLatencySamples() const override { return (int) std::round (oversampling.getLatencyInSamples()) + 2; }

    protected:
        virtual float shape (float x) const = 0;
        virtual float driveDb() const = 0;
        virtual float levelDb() const = 0;
        virtual float blendAmount() const { return 0.0f; }
        virtual float cleanAmount() const { return 0.0f; }
        virtual void updateFilters (double sr) = 0;
        virtual void preparePre (double) {}

        juce::dsp::Oversampling<float> oversampling { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true };
        juce::AudioBuffer<float> dry;
        Biquad pre, post, post2, post3;
        DcBlocker dc;
        SmoothedGain levelSm, blendSm, cleanSm;
        ChangeTracker tracker;
    };

    // 过载。“型号”选择不同的电路风格：
    //   TS808 / TS9 风格：削波通路先切掉 720 Hz 以下（中频隆起），削波后和原声相加，再过音色低通
    //   人马（Klon）风格：低频保留得多，干声和失真两路随增益交叉混合，高音旋钮是搁架式
    //   SD-1 风格：非对称削波（偶次谐波多一点）
    //   Blues Breaker 风格：增益低、频响平直、通透
    // 这些只是按电路特点做的数字模拟，不是采样，也和原厂没有关系。
    class OverdriveFx final : public ClipperBase
    {
    public:
        OverdriveFx() : ClipperBase ("overdrive", TR ("Overdrive", "过载"))
        {
            drive = addFloat ("drive", "驱动", 0.0f, 40.0f, 18.0f, "dB");
            tone  = addFloat ("tone", "音色", 0.0f, 1.0f, 0.5f);
            level = addFloat ("level", "音量", -24.0f, 12.0f, -6.0f, "dB");
            bass  = addFloat ("bass", "低频", -6.0f, 6.0f, 0.0f, "dB");
            blend = addFloat ("blend", "干声混合", 0.0f, 1.0f, 0.0f);
            model = addChoice ("model", "型号", { "通用", "TS808 风格", "TS9 风格", "人马 Klon 风格", "SD-1 风格", "Blues Breaker 风格" }, 0);
        }

        juce::String modelLabel() const override
        {
            const int m = model->getIndex();
            return m == 0 ? juce::String() : model->getCurrentChoiceName();
        }

    private:
        enum { generic, ts808, ts9, klon, sd1, bluesBreaker };

        float shape (float x) const override
        {
            switch (currentModel)
            {
                case ts9:          return std::tanh (1.15f * x) * 0.92f;
                case klon:         return x >= 0.0f ? 1.0f - std::exp (-x) : -(1.0f - std::exp (x * 1.1f)) / 1.05f;   // 锗二极管：拐点低
                case sd1:          return x >= 0.0f ? std::tanh (x) : std::tanh (1.7f * x) / 1.7f;                 // 非对称
                case bluesBreaker: return std::tanh (0.8f * x) / 0.8f * 0.85f;
                default:           return std::tanh (x);
            }
        }
        float driveDb() const override
        {
            switch (currentModel)
            {
                case klon:         return drive->get() * 0.9f + 4.0f;
                case bluesBreaker: return drive->get() * 0.6f;
                default:           return drive->get();
            }
        }
        float levelDb() const override
        {
            const float trim[] = { 0.0f, 2.0f, 2.5f, -4.0f, 2.0f, -0.5f };
            return level->get() + trim[currentModel];
        }
        float blendAmount() const override   { return blend->get(); }

        // 原声通路（TS / SD-1 / Klon / BB 电路里本来就有）
        float cleanAmount() const override
        {
            switch (currentModel)
            {
                case ts808: case ts9: case sd1: return 0.6f;
                case klon:         return juce::jmap (drive->get() / 40.0f, 1.2f, 0.15f);
                case bluesBreaker: return 0.35f;
                default:           return 0.0f;
            }
        }

        void updateFilters (double sr) override
        {
            currentModel = model->getIndex();
            if (! tracker.changed ({ tone->get(), bass->get(), (float) currentModel, (float) sr }))
                return;
            const double t = tone->get();
            switch (currentModel)
            {
                case ts808:
                    pre.setHighpass (sr, 720.0, 0.55);
                    post.setLowpass (sr, 800.0 + 4800.0 * t * t, 0.6);
                    post2.setLowShelf (sr, 200.0, bass->get());
                    post3.setPeak (sr, 1000.0, 0.8, 1.5);
                    break;
                case ts9:
                    pre.setHighpass (sr, 720.0, 0.55);
                    post.setLowpass (sr, 1100.0 + 5500.0 * t * t, 0.7);
                    post2.setLowShelf (sr, 200.0, bass->get() - 1.0);
                    post3.setPeak (sr, 2800.0, 1.0, 2.5);       // 输出级更亮、更“刺”一点
                    break;
                case klon:
                    pre.setHighpass (sr, 330.0, 0.6);
                    post.setLowpass (sr, 7500.0, 0.6);
                    post2.setLowShelf (sr, 150.0, 1.0 + bass->get());
                    post3.setHighShelf (sr, 1300.0, (t - 0.5) * 16.0);   // 高音旋钮（搁架式，可以加也可以减）
                    break;
                case sd1:
                    pre.setHighpass (sr, 720.0, 0.55);
                    post.setLowpass (sr, 1000.0 + 5200.0 * t * t, 0.6);
                    post2.setLowShelf (sr, 200.0, bass->get());
                    post3.setPeak (sr, 850.0, 0.8, 2.0);
                    break;
                case bluesBreaker:
                    pre.setHighpass (sr, 220.0, 0.6);
                    post.setLowpass (sr, 1500.0 + 7000.0 * t, 0.5);
                    post2.setLowShelf (sr, 150.0, 1.0 + bass->get());
                    post3.setPeak (sr, 1000.0, 0.7, 0.0);
                    break;
                default:
                    pre.setHighpass (sr, 600.0, 0.6);                               // 切掉低频，让失真更紧
                    post.setLowpass (sr, 1200.0 + 6000.0 * t, 0.7);                 // 音色旋钮
                    post2.setLowShelf (sr, 200.0, 4.0 + bass->get());               // 补回低频
                    post3.setPeak (sr, 1000.0, 0.7, 0.0);
                    break;
            }
        }

        juce::AudioParameterFloat *drive, *tone, *level, *bass, *blend;
        juce::AudioParameterChoice* model;
        int currentModel = 0;
    };

    // 失真。型号：通用 / RAT 风格（运放后硬削波，滤波旋钮）/ DS-1 风格（非对称削波，中频凹陷）
    class DistortionFx final : public ClipperBase
    {
    public:
        DistortionFx() : ClipperBase ("distortion", TR ("Distortion", "失真"))
        {
            gain  = addFloat ("gain", "增益", 0.0f, 60.0f, 34.0f, "dB");
            tone  = addFloat ("tone", "音色", 0.0f, 1.0f, 0.55f);
            level = addFloat ("level", "音量", -36.0f, 6.0f, -14.0f, "dB");
            mid   = addFloat ("mid", "中频", -12.0f, 6.0f, -3.0f, "dB");
            tight = addFloat ("tight", "紧致", 60.0f, 400.0f, 180.0f, "Hz", 150.0f);
            model = addChoice ("model", "型号", { "通用", "RAT 风格", "DS-1 风格" }, 0);
        }

        juce::String modelLabel() const override
        {
            return model->getIndex() == 0 ? juce::String() : model->getCurrentChoiceName();
        }

    private:
        static float hardish (float x)
        {
            const float t = juce::jlimit (-1.5f, 1.5f, x);
            return t - 0.148148f * t * t * t;          // 三次软拐点，±1.5 以外完全削平
        }

        float shape (float x) const override
        {
            switch (currentModel)
            {
                case 1:  return hardish (x);                                                   // RAT：硅二极管对地
                case 2:  return x >= 0.0f ? hardish (x) : hardish (1.4f * x) * 0.8f;           // DS-1：非对称
                default:
                {
                    // 正半周软、负半周硬，带一点偶次谐波
                    const float y = x >= 0.0f ? std::tanh (x) : juce::jlimit (-0.8f, 0.0f, x * 0.9f) + 0.2f * std::tanh (x);
                    return std::tanh (1.5f * y);
                }
            }
        }
        float driveDb() const override { return gain->get() + (currentModel == 1 ? 4.0f : 0.0f); }
        float levelDb() const override { return level->get() + (currentModel == 0 ? 0.0f : 1.0f); }

        void updateFilters (double sr) override
        {
            currentModel = model->getIndex();
            if (! tracker.changed ({ tone->get(), mid->get(), tight->get(), (float) currentModel, (float) sr }))
                return;
            const double t = tone->get();
            pre.setHighpass (sr, tight->get(), 0.7);                        // 紧致：失真前切掉多少低频
            switch (currentModel)
            {
                case 1:   // RAT：滤波旋钮是一个从很暗到很亮的低通
                    post.setLowpass (sr, 700.0 + 9000.0 * t * t, 0.7);
                    post2.setPeak (sr, 900.0, 0.7, mid->get() + 1.5);
                    post3.setLowShelf (sr, 120.0, 1.0);
                    break;
                case 2:   // DS-1：音色旋钮在低通和高通之间混，中间位置有中频凹陷
                    post.setLowpass (sr, 2500.0 + 6500.0 * t, 0.6);
                    post2.setPeak (sr, 520.0, 0.7, mid->get() - 4.0);
                    post3.setLowShelf (sr, 140.0, 2.5 - 3.0 * t);
                    break;
                default:
                    post.setLowpass (sr, 2000.0 + 7000.0 * t, 0.7);
                    post2.setPeak (sr, 750.0, 0.8, mid->get());                 // 中频：挖空或推前
                    post3.setLowShelf (sr, 120.0, 2.0);
                    break;
            }
        }

        juce::AudioParameterFloat *gain, *tone, *level, *mid, *tight;
        juce::AudioParameterChoice* model;
        int currentModel = 0;
    };

    // =========================================================================
    // 音箱模拟：前级（2–3 级三极管）+ 音色堆栈 + 功放饱和 / 下沉 + 可选箱体模拟。
    // “型号”按经典箱头的电路特点调整：增益结构、音色堆栈位置和中频、功放余量、喇叭箱。
    // 名字只用来说明是哪种风格，和这些厂商没有关系；想要真实采样的音色用 NAM 模型。
    struct AmpVoice
    {
        float inHp, brightDb;
        float g1Lo, g1Hi, g2Lo, g2Hi, g3Lo, g3Hi;     // 每一级在增益 0 / 10 时的 dB（g3 都为 0 表示只有两级）
        float bias, interHp, interLp;
        bool stackPre;                                 // 音色堆栈在削波之前（Mark 系列）
        float lowHz, midHz, midQ, highHz, scoopDb, knobDb;
        float presHz, powerDrive, sag;
        float cabHp, cabResHz, cabResDb, cabMidHz, cabMidDb, cabLp1, cabLp2;
        float trimDb;
    };

    const AmpVoice ampVoices[] = {
        //  inHp bright  g1          g2          g3        bias  iHp   iLp    pre    low  mid  Q    high  scoop knob  pres  pwr  sag   cabHp res  resDb mid  midDb lp1   lp2   trim
        {   90, 0.0f,   0, 18,      0,  6,      0,  0,   0.10f, 140, 12000, false, 120, 500, 0.7f, 3200, 0,   2.4f, 5500, 1.3f, 0.0f,  75, 110, 3.0f, 2400, 2.5f, 5200, 6500, 0 },   // 清音（原来的）
        {   90, 0.0f,   6, 36,      0, 20,      0,  0,   0.10f, 140,  9000, false, 120, 650, 0.7f, 3200, 0,   2.4f, 5500, 1.3f, 0.0f,  75, 110, 3.0f, 2400, 2.5f, 5200, 6500, 0 },   // 英式经典（原来的）
        {  120, 0.0f,  14, 48,      0, 26,      0,  0,   0.10f, 140,  9000, false, 120, 750, 0.7f, 3200, 0,   2.4f, 5500, 1.3f, 0.0f,  75, 110, 3.0f, 2400, 2.5f, 5200, 6500, 0 },   // 高增益（原来的）
        {   70, 3.0f,  -6, 16,      0,  8,      0,  0,   0.05f,  60, 14000, false, 100, 450, 0.6f, 3500, -7, 2.6f, 6000, 0.8f, 0.10f, 70, 120, 1.5f, 2200, 2.0f, 6500, 8000, 3 },  // Twin
        {   80, 1.5f,   0, 26,      0, 12,      0,  0,   0.12f,  80, 10000, false, 110, 500, 0.6f, 3300, -5, 2.5f, 5500, 2.2f, 0.35f, 85, 130, 1.0f, 2000, 2.5f, 5500, 7000, 3 },  // Deluxe
        {   90, 2.5f,   4, 30,      2, 16,      0,  0,   0.15f, 120,  9500, false, 110, 700, 0.7f, 3000, -3, 2.4f, 5000, 3.0f, 0.30f, 75, 100, 3.0f, 2000, 3.0f, 5000, 6000, 2 },  // Plexi
        {  110, 1.0f,   8, 38,      6, 24,      0,  0,   0.25f, 160,  8500, false, 110, 800, 0.8f, 2800, -1, 2.4f, 5000, 1.8f, 0.15f, 80, 105, 3.0f, 2800, 4.0f, 5000, 5800, -3 }, // JCM800
        {  100, 3.5f,   2, 28,      0, 12,      0,  0,   0.08f, 100, 11000, false, 120, 900, 0.6f, 2600, -6, 2.4f, 5500, 2.6f, 0.40f, 80, 120, 2.0f, 3000, 4.5f, 6000, 7500, 3 },  // AC30
        {  120, 0.0f,  10, 40,      8, 28,      0, 12,   0.20f, 180,  7500, true,  110, 650, 0.8f, 3000, 0,  2.8f, 5000, 1.6f, 0.05f, 80,  95, 3.5f, 2600, 3.5f, 4800, 5600, -4 }, // Mark
        {   90, 0.0f,  12, 44,      8, 30,      2, 16,   0.20f, 120,  7000, false,  90, 600, 0.7f, 3200, -5, 2.6f, 5000, 2.0f, 0.25f, 70,  90, 4.5f, 2700, 4.0f, 4700, 5500, -1.5f }, // Recto
        {  140, 0.0f,  14, 46,     10, 30,      4, 18,   0.30f, 200,  7500, false, 110, 750, 0.9f, 3000, -1, 2.4f, 5000, 1.6f, 0.08f, 85, 105, 3.0f, 2400, 3.5f, 5000, 5800, -4 }, // 5150
    };

    class AmpSimFx final : public BuiltinEffect
    {
    public:
        AmpSimFx() : BuiltinEffect ("amp", TR ("Amp Sim", "音箱模拟"))
        {
            gain     = addFloat ("gain", "增益", 0.0f, 10.0f, 5.0f);
            bass     = addFloat ("bass", "低音", 0.0f, 10.0f, 5.0f);
            mid      = addFloat ("mid", "中音", 0.0f, 10.0f, 5.0f);
            treble   = addFloat ("treble", "高音", 0.0f, 10.0f, 5.0f);
            presence = addFloat ("presence", "临场感", 0.0f, 10.0f, 5.0f);
            master   = addFloat ("master", "总音量", -36.0f, 6.0f, -10.0f, "dB");
            cab      = addBool ("cab", "箱体模拟", true);
            // 前三项保留原来的顺序（旧预设里存的是序号）
            voicing  = addChoice ("voicing", "型号", { "清音", "英式经典", "高增益",
                                                       "美式清音 Twin 风格", "美式黑面 Deluxe 风格", "英式 Plexi 风格", "英式 JCM800 风格",
                                                       "英式 AC30 风格", "Mesa Mark 风格", "Mesa 双整流风格", "5150 风格" }, 1);
            resonance= addFloat ("resonance", "低频共鸣", 0.0f, 10.0f, 5.0f);
            bright   = addBool ("bright", "亮音", false);
        }

        juce::String modelLabel() const override
        {
            return voicing->getIndex() < 3 ? juce::String() : voicing->getCurrentChoiceName();
        }

        void prepareEffect (double sr, int block) override
        {
            oversampling.initProcessing ((size_t) block);
            oversampling.reset();
            dc.prepare (sr, 12.0);
            tracker.invalidate();
            for (auto* f : allFilters()) f->reset();
            sagEnv[0] = sagEnv[1] = 0.0f;
            masterSm.prepare (sr);
            const int v = juce::jlimit (0, (int) std::size (ampVoices) - 1, voicing->getIndex());
            masterSm.snap (dbToGain (master->get() + ampVoices[v].trimDb));
        }

        void processEffect (juce::AudioBuffer<float>& b) override
        {
            const double sr = currentSampleRate;
            const double osr = sr * 2.0;
            const int n = b.getNumSamples();
            const int v = juce::jlimit (0, (int) std::size (ampVoices) - 1, voicing->getIndex());
            const auto& m = ampVoices[v];

            if (tracker.changed ({ bass->get(), mid->get(), treble->get(), presence->get(), resonance->get(), (float) v, (float) sr }))
            {
                inHp.setHighpass (sr, m.inHp, 0.7);
                brightF.setHighShelf (sr, 2500.0, 5.0);
                voiceBright.setHighShelf (sr, 2200.0, m.brightDb);
                interHp.setHighpass (osr, m.interHp, 0.7);
                interHp2.setHighpass (osr, m.interHp * 0.8, 0.7);
                interLp.setLowpass (osr, m.interLp, 0.7);
                low.setLowShelf (sr, m.lowHz, (bass->get() - 5.0) * m.knobDb);
                middle.setPeak (sr, m.midHz, m.midQ, (mid->get() - 5.0) * m.knobDb + m.scoopDb);
                high.setHighShelf (sr, m.highHz, (treble->get() - 5.0) * m.knobDb);
                pres.setHighShelf (sr, m.presHz, (presence->get() - 5.0) * 1.4);
                reso.setLowShelf (sr, 90.0, (resonance->get() - 5.0) * 1.4);
                cabHp.setHighpass (sr, m.cabHp, 0.8);
                cabRes.setPeak (sr, m.cabResHz, 1.2, m.cabResDb);
                cabMid.setPeak (sr, m.cabMidHz, 1.0, m.cabMidDb);
                cabLp1.setLowpass (sr, m.cabLp1, 0.75);
                cabLp2.setLowpass (sr, m.cabLp2, 0.6);
                sagRelease = (float) std::exp (-1.0 / (0.12 * sr));
                sagAttack = (float) std::exp (-1.0 / (0.004 * sr));
            }

            const bool br = bright->get();
            auto toneStack = [this] (float x, int ch) { return high.process (middle.process (low.process (x, ch), ch), ch); };

            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = b.getWritePointer (ch);
                inHp.processBlock (d, n, ch);
                if (m.brightDb != 0.0f) voiceBright.processBlock (d, n, ch);
                if (br) brightF.processBlock (d, n, ch);
                if (m.stackPre)
                    for (int i = 0; i < n; ++i)
                        d[i] = toneStack (d[i], ch);
            }

            // 前级（过采样）
            const float g = gain->get() / 10.0f;
            const float g1 = dbToGain (m.g1Lo + (m.g1Hi - m.g1Lo) * g);
            const float g2 = dbToGain (m.g2Lo + (m.g2Hi - m.g2Lo) * g);
            const bool third = m.g3Hi > 0.0f;
            const float g3 = dbToGain (m.g3Lo + (m.g3Hi - m.g3Lo) * g);
            const float bias = m.bias;
            const float biasOut = std::tanh (bias);
            juce::dsp::AudioBlock<float> block (b);
            auto up = oversampling.processSamplesUp (block);
            for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
            {
                auto* d = up.getChannelPointer (ch);
                const int c = (int) ch;
                for (size_t i = 0; i < up.getNumSamples(); ++i)
                {
                    float x = std::tanh (d[i] * g1 + bias) - biasOut;   // 偏置 -> 偶次谐波
                    x = interHp.process (x, c);
                    x = std::tanh (x * g2 * 1.5f);
                    if (third)
                    {
                        x = interHp2.process (x, c);
                        x = std::tanh (x * g3 * 1.5f + 0.5f * bias) - std::tanh (0.5f * bias);
                    }
                    d[i] = interLp.process (x, c);
                }
            }
            oversampling.processSamplesDown (block);

            for (int ch = 0; ch < 2; ++ch)
                dc.processBlock (b.getWritePointer (ch), n, ch);

            const bool useCab = cab->get();
            const float pd = m.powerDrive;
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = b.getWritePointer (ch);
                float env = sagEnv[ch];
                for (int i = 0; i < n; ++i)
                {
                    float x = m.stackPre ? d[i] : toneStack (d[i], ch);
                    x = pres.process (x, ch);
                    // 功放：越推越压缩；整流管 / 阴极偏压的“下沉”
                    if (m.sag > 0.0f)
                    {
                        const float a = std::abs (x);
                        env = a > env ? sagAttack * env + (1.0f - sagAttack) * a : sagRelease * env + (1.0f - sagRelease) * a;
                        x /= 1.0f + m.sag * 3.0f * env;
                    }
                    x = std::tanh (x * pd) / pd;
                    x = reso.process (x, ch);
                    if (useCab)
                        x = cabLp2.process (cabLp1.process (cabMid.process (cabRes.process (cabHp.process (x, ch), ch), ch), ch), ch);
                    d[i] = x;
                }
                sagEnv[ch] = env;
            }

            masterSm.setTarget (dbToGain (master->get() + m.trimDb));
            for (int i = 0; i < n; ++i)
            {
                const float g = masterSm.next();
                for (int ch = 0; ch < 2; ++ch)
                    b.getWritePointer (ch)[i] *= g;
            }
        }

        void resetEffect() override { oversampling.reset(); dc.reset(); for (auto* f : allFilters()) f->reset(); sagEnv[0] = sagEnv[1] = 0.0f; }
        int effectLatencySamples() const override { return (int) std::round (oversampling.getLatencyInSamples()) + 2; }

    private:
        std::array<Biquad*, 16> allFilters()
        {
            return { &inHp, &brightF, &voiceBright, &interHp, &interHp2, &interLp, &low, &middle, &high, &pres, &reso,
                     &cabHp, &cabRes, &cabMid, &cabLp1, &cabLp2 };
        }

        juce::AudioParameterFloat *gain, *bass, *mid, *treble, *presence, *master, *resonance;
        juce::AudioParameterBool *cab, *bright;
        juce::AudioParameterChoice* voicing;
        juce::dsp::Oversampling<float> oversampling { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true };
        Biquad inHp, brightF, voiceBright, interHp, interHp2, interLp, low, middle, high, pres, reso, cabHp, cabRes, cabMid, cabLp1, cabLp2;
        DcBlocker dc;
        SmoothedGain masterSm;
        ChangeTracker tracker;
        float sagEnv[2] {}, sagAttack = 0.99f, sagRelease = 0.999f;
    };

    // =========================================================================
    class EqFx final : public BuiltinEffect
    {
    public:
        EqFx() : BuiltinEffect ("eq", TR ("EQ", "均衡"))
        {
            low     = addFloat ("low", "低频", -12.0f, 12.0f, 0.0f, "dB");
            mid     = addFloat ("mid", "中频", -12.0f, 12.0f, 0.0f, "dB");
            midFreq = addFloat ("midFreq", "中频频率", 200.0f, 3000.0f, 800.0f, "Hz", 800.0f);
            high    = addFloat ("high", "高频", -12.0f, 12.0f, 0.0f, "dB");
            level   = addFloat ("level", "音量", -12.0f, 12.0f, 0.0f, "dB");
            midQ    = addFloat ("midQ", "中频宽度", 0.3f, 4.0f, 0.9f, "Q", 1.0f);
        }

        void prepareEffect (double sr, int) override
        {
            tracker.invalidate();
            lo.prepare (sr); mi.prepare (sr); hi.prepare (sr);
            levelSm.prepare (sr); levelSm.snap (dbToGain (level->get()));
        }

        void processEffect (juce::AudioBuffer<float>& b) override
        {
            const double sr = currentSampleRate;
            if (tracker.changed ({ low->get(), mid->get(), midFreq->get(), high->get(), midQ->get(), (float) sr }))
            {
                lo.setLowShelf (100.0, low->get());
                mi.setPeak (midFreq->get(), midQ->get(), mid->get());
                hi.setHighShelf (4000.0, high->get());
            }

            levelSm.setTarget (dbToGain (level->get()));
            const int n = b.getNumSamples();
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = b.getWritePointer (ch);
                lo.processBlock (d, n, ch);
                mi.processBlock (d, n, ch);
                hi.processBlock (d, n, ch);
            }
            for (int i = 0; i < n; ++i)
            {
                const float g = levelSm.next();
                for (int ch = 0; ch < 2; ++ch)
                    b.getWritePointer (ch)[i] *= g;
            }
        }

        void resetEffect() override { lo.reset(); mi.reset(); hi.reset(); }

    private:
        juce::AudioParameterFloat *low, *mid, *midFreq, *high, *level, *midQ;
        GlidedBiquad lo, mi, hi;
        SmoothedGain levelSm;
        ChangeTracker tracker;
    };

    // =========================================================================
    class ChorusFx final : public BuiltinEffect
    {
    public:
        ChorusFx (bool flangerMode)
            : BuiltinEffect (flangerMode ? "flanger" : "chorus", flangerMode ? TR ("Flanger", "镶边") : TR ("Chorus", "合唱")),
              flanger (flangerMode)
        {
            rate  = addFloat ("rate", "速度", 0.05f, 5.0f, flanger ? 0.25f : 0.8f, "Hz", 0.8f);
            depth = addFloat ("depth", "深度", 0.0f, 1.0f, flanger ? 0.7f : 0.35f);
            if (flanger)
                feedback = addFloat ("feedback", "共振", -0.95f, 0.95f, 0.6f);
            mix    = addFloat ("mix", "混合", 0.0f, 1.0f, 0.5f);
            stereo = addBool ("stereo", "立体声", true);
            manual = flanger ? addFloat ("manual", "手动", 1.0f, 6.0f, 2.0f, "ms")
                             : addFloat ("predelay", "预延迟", 4.0f, 20.0f, 8.0f, "ms");
            tone   = addFloat ("tone", "音色", 0.0f, 1.0f, 0.8f);
        }

        void prepareEffect (double sr, int block) override
        {
            for (auto& c : chorus)
            {
                c.prepare ({ sr, (juce::uint32) block, 1 });
                c.setCentreDelay (manual->get());
                c.reset();
            }
            lastTone = -1.0f;
        }

        void processEffect (juce::AudioBuffer<float>& b) override
        {
            if (! juce::approximatelyEqual (lastTone, tone->get()))
            {
                lastTone = tone->get();
                toneFilter.setLowpass (currentSampleRate, 2500.0 + 15000.0 * lastTone * lastTone, 0.7);
            }

            for (int ch = 0; ch < 2; ++ch)
            {
                auto& c = chorus[(size_t) ch];
                // 立体声：右声道速度稍快、延迟稍长，左右错开，声场更宽
                const bool wide = stereo->get() && ch == 1;
                c.setRate (rate->get() * (wide ? 1.17f : 1.0f));
                c.setDepth (depth->get());
                c.setCentreDelay (juce::jlimit (1.0f, 99.0f, manual->get() * (wide ? 1.3f : 1.0f)));
                c.setFeedback (feedback != nullptr ? feedback->get() : 0.0f);
                c.setMix (mix->get());
                juce::dsp::AudioBlock<float> block (b);
                auto one = block.getSingleChannelBlock ((size_t) ch);
                c.process (juce::dsp::ProcessContextReplacing<float> (one));
                if (lastTone < 0.99f)
                    toneFilter.processBlock (b.getWritePointer (ch), b.getNumSamples(), ch);
            }
        }

        void resetEffect() override { for (auto& c : chorus) c.reset(); toneFilter.reset(); }

    private:
        const bool flanger;
        juce::AudioParameterFloat *rate, *depth, *mix, *manual, *tone;
        juce::AudioParameterFloat* feedback = nullptr;
        juce::AudioParameterBool* stereo;
        std::array<juce::dsp::Chorus<float>, 2> chorus;
        Biquad toneFilter;
        float lastTone = -1.0f;
    };

    // =========================================================================
    class TremoloFx final : public BuiltinEffect
    {
    public:
        TremoloFx() : BuiltinEffect ("tremolo", TR ("Tremolo", "颤音"))
        {
            rate   = addFloat ("rate", "速度", 0.5f, 15.0f, 5.0f, "Hz", 4.0f);
            depth  = addFloat ("depth", "深度", 0.0f, 1.0f, 0.5f);
            wave   = addChoice ("wave", "波形", { "正弦", "三角", "方波" }, 0);
            stereo = addBool ("pan", "左右摆动", false);
            sync   = addChoice ("sync", "同步鼓机", { "关", "1/4", "1/8", "1/8 三连音", "1/16" }, 0);
        }

        void prepareEffect (double sr, int) override
        {
            phase = 0.0;
            smoothedSquare = 0.0f;
            depthSm.prepare (sr);
            depthSm.snap (depth->get());
        }

        void processEffect (juce::AudioBuffer<float>& b) override
        {
            double hz = rate->get();
            if (const int s = sync->getIndex(); s > 0)
            {
                const double perBeat[] = { 0.0, 1.0, 2.0, 3.0, 4.0 };
                hz = HostTempo::bpm.load() / 60.0 * perBeat[s];
            }
            const double inc = hz / currentSampleRate;
            depthSm.setTarget (depth->get());
            const int w = wave->getIndex();
            const bool pan = stereo->get();
            const float sq = (float) std::exp (-1.0 / (0.002 * currentSampleRate));
            auto* l = b.getWritePointer (0);
            auto* r = b.getWritePointer (1);

            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                float lfo;   // 0..1
                if (w == 0)      lfo = 0.5f + 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * phase);
                else if (w == 1) lfo = (float) (phase < 0.5 ? phase * 2.0 : 2.0 - phase * 2.0);
                else
                {
                    // 方波稍微圆滑一下，避免咔哒声
                    smoothedSquare = sq * smoothedSquare + (1.0f - sq) * (phase < 0.5 ? 1.0f : 0.0f);
                    lfo = smoothedSquare;
                }

                const float d = depthSm.next();
                const float mL = 1.0f - d * lfo;
                const float mR = pan ? 1.0f - d * (1.0f - lfo) : mL;
                l[i] *= mL;
                r[i] *= mR;
                phase += inc;
                if (phase >= 1.0) phase -= 1.0;
            }
        }

    private:
        juce::AudioParameterFloat *rate, *depth;
        juce::AudioParameterChoice *wave, *sync;
        juce::AudioParameterBool* stereo;
        SmoothedGain depthSm;
        double phase = 0.0;
        float smoothedSquare = 0.0f;
    };

    // =========================================================================
    class DelayFx final : public BuiltinEffect
    {
    public:
        DelayFx() : BuiltinEffect ("delay", TR ("Delay", "延迟"))
        {
            time     = addFloat ("time", "时间", 20.0f, 2000.0f, 380.0f, "ms", 400.0f);
            feedback = addFloat ("feedback", "反馈", 0.0f, 0.95f, 0.35f);
            mix      = addFloat ("mix", "混合", 0.0f, 1.0f, 0.28f);
            tone     = addFloat ("tone", "音色", 800.0f, 12000.0f, 4500.0f, "Hz", 3000.0f);
            pingPong = addBool ("pingpong", "左右交替", false);
            type     = addChoice ("type", "类型", { "数字", "磁带", "模拟" }, 0);
            modDepth = addFloat ("mod", "调制", 0.0f, 1.0f, 0.0f);
            sync     = addChoice ("sync", "同步鼓机", { "关", "1/4", "附点 1/8", "1/8", "1/8 三连音", "1/16" }, 0);
        }

        void prepareEffect (double sr, int) override
        {
            size = (int) (sr * 2.1) + 4;
            for (auto& line : lines)
                line.assign ((size_t) size, 0.0f);
            writePos = 0;
            smoothedDelay.reset (sr, 0.25);
            smoothedDelay.setCurrentAndTargetValue ((float) (time->get() * 0.001 * sr));
            fbSm.prepare (sr); fbSm.snap (feedback->get());
            mixSm.prepare (sr); mixSm.snap (mix->get());
            tracker.invalidate();
            damp.reset();
            lfo = 0.0;
        }

        void processEffect (juce::AudioBuffer<float>& b) override
        {
            const double sr = currentSampleRate;
            const int t = type->getIndex();
            const float toneMul[] = { 1.0f, 0.65f, 0.45f };     // 磁带 / 模拟更暗
            if (tracker.changed ({ tone->get(), (float) t, (float) sr }))
                damp.setLowpass (sr, tone->get() * toneMul[t], 0.6);

            double ms = time->get();
            if (const int s = sync->getIndex(); s > 0)
            {
                const double beats[] = { 0.0, 1.0, 0.75, 0.5, 1.0 / 3.0, 0.25 };
                ms = juce::jlimit (20.0, 2000.0, 60000.0 / HostTempo::bpm.load() * beats[s]);
            }
            smoothedDelay.setTargetValue ((float) (ms * 0.001 * sr));

            fbSm.setTarget (feedback->get());
            mixSm.setTarget (mix->get());
            const bool pp = pingPong->get();
            const bool saturate = t > 0;
            // 磁带的“抖动”：慢速调制延迟时间
            const double modSamples = modDepth->get() * (t == 1 ? 0.004 : 0.0025) * sr;
            const double lfoInc = juce::MathConstants<double>::twoPi * (t == 1 ? 0.7 : 0.45) / sr;
            auto* l = b.getWritePointer (0);
            auto* r = b.getWritePointer (1);

            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float mod = (float) (modSamples * (0.5 + 0.5 * std::sin (lfo)));
                lfo += lfoInc;
                if (lfo > juce::MathConstants<double>::twoPi) lfo -= juce::MathConstants<double>::twoPi;

                const float d = juce::jlimit (1.0f, (float) size - 2.0f, smoothedDelay.getNextValue() + mod);
                const float yl = read (0, d), yr = read (1, d);
                const float inL = l[i], inR = r[i];
                const float fb = fbSm.next();
                float fl = damp.process (yl, 0) * fb;
                float fr = damp.process (yr, 1) * fb;
                if (saturate) { fl = std::tanh (fl * 1.2f) / 1.2f; fr = std::tanh (fr * 1.2f) / 1.2f; }

                if (pp)
                {
                    lines[0][(size_t) writePos] = 0.5f * (inL + inR) + fr;
                    lines[1][(size_t) writePos] = fl;
                }
                else
                {
                    lines[0][(size_t) writePos] = inL + fl;
                    lines[1][(size_t) writePos] = inR + fr;
                }

                const float wet = mixSm.next();
                const float dry = 1.0f - 0.5f * wet;
                l[i] = inL * dry + yl * wet;
                r[i] = inR * dry + yr * wet;
                if (++writePos >= size) writePos = 0;
            }
        }

        void resetEffect() override
        {
            for (auto& line : lines) std::fill (line.begin(), line.end(), 0.0f);
            damp.reset();
        }

    private:
        float read (int ch, float delaySamples) const
        {
            float pos = (float) writePos - delaySamples;
            while (pos < 0.0f) pos += (float) size;
            const int i0 = (int) pos;
            const int i1 = (i0 + 1) % size;
            const float frac = pos - (float) i0;
            return lines[(size_t) ch][(size_t) i0] * (1.0f - frac) + lines[(size_t) ch][(size_t) i1] * frac;
        }

        juce::AudioParameterFloat *time, *feedback, *mix, *tone, *modDepth;
        juce::AudioParameterBool* pingPong;
        juce::AudioParameterChoice *type, *sync;
        std::vector<float> lines[2];
        int size = 1, writePos = 0;
        double lfo = 0.0;
        juce::SmoothedValue<float> smoothedDelay;
        SmoothedGain fbSm, mixSm;
        Biquad damp;
        ChangeTracker tracker;
    };

    // =========================================================================
    class ReverbFx final : public BuiltinEffect
    {
    public:
        ReverbFx() : BuiltinEffect ("reverb", TR ("Reverb", "混响"))
        {
            type     = addChoice ("type", "类型", { "房间", "大厅", "板式" }, 1);
            size     = addFloat ("size", "空间", 0.0f, 1.0f, 0.5f);
            damping  = addFloat ("damping", "阻尼", 0.0f, 1.0f, 0.5f);
            predelay = addFloat ("predelay", "预延迟", 0.0f, 150.0f, 15.0f, "ms");
            tone     = addFloat ("tone", "音色", 0.0f, 1.0f, 0.6f);
            mix      = addFloat ("mix", "混合", 0.0f, 1.0f, 0.22f);
        }

        void prepareEffect (double sr, int block) override
        {
            reverb.setSampleRate (sr);
            reverb.reset();
            wetL.assign ((size_t) block, 0.0f);
            wetR.assign ((size_t) block, 0.0f);
            preSize = 1;
            while (preSize < (int) (0.16 * sr) + 2) preSize <<= 1;
            for (auto& line : pre) line.assign ((size_t) preSize, 0.0f);
            prePos = 0;
            lastTone = -1.0f;
            lowCut.reset(); highCut.reset();
            mixSm.prepare (sr); mixSm.snap (mix->get());
        }

        void processEffect (juce::AudioBuffer<float>& b) override
        {
            const int n = b.getNumSamples();
            if (n > (int) wetL.size())
                return;

            const double sr = currentSampleRate;
            const int t = type->getIndex();
            // 三种类型的空间感：房间小而暗，大厅大，板式明亮、密
            const float baseSize[]  = { 0.30f, 0.70f, 0.55f };
            const float rangeSize[] = { 0.40f, 0.28f, 0.40f };
            const float dampScale[] = { 1.0f, 0.8f, 0.45f };

            juce::Reverb::Parameters p;
            p.roomSize = baseSize[t] + rangeSize[t] * size->get();
            p.damping = damping->get() * dampScale[t];
            p.wetLevel = 1.0f;
            p.dryLevel = 0.0f;
            p.width = t == 0 ? 0.7f : 1.0f;
            reverb.setParameters (p);

            if (! juce::approximatelyEqual (lastTone, tone->get()))
            {
                lastTone = tone->get();
                lowCut.setHighpass (sr, 300.0 - 200.0 * lastTone, 0.7);
                highCut.setLowpass (sr, 2500.0 + 9500.0 * lastTone, 0.7);
            }

            auto* l = b.getWritePointer (0);
            auto* r = b.getWritePointer (1);
            const int d = juce::jlimit (0, preSize - 1, (int) (predelay->get() * 0.001 * sr));
            for (int i = 0; i < n; ++i)
            {
                pre[0][(size_t) prePos] = l[i];
                pre[1][(size_t) prePos] = r[i];
                const int rp = (prePos - d) & (preSize - 1);
                wetL[(size_t) i] = pre[0][(size_t) rp];
                wetR[(size_t) i] = pre[1][(size_t) rp];
                prePos = (prePos + 1) & (preSize - 1);
            }

            reverb.processStereo (wetL.data(), wetR.data(), n);

            mixSm.setTarget (mix->get());
            for (int i = 0; i < n; ++i)
            {
                const float m = mixSm.next();
                const float dry = 1.0f - 0.5f * m;
                const float wl = highCut.process (lowCut.process (wetL[(size_t) i], 0), 0);
                const float wr = highCut.process (lowCut.process (wetR[(size_t) i], 1), 1);
                // juce::Reverb boosts the wet path by 3; 0.5 matches the dry level.
                l[i] = l[i] * dry + wl * m * 0.5f;
                r[i] = r[i] * dry + wr * m * 0.5f;
            }
        }

        void resetEffect() override { reverb.reset(); }

    private:
        juce::AudioParameterChoice* type;
        juce::AudioParameterFloat *size, *damping, *predelay, *tone, *mix;
        juce::Reverb reverb;
        std::vector<float> wetL, wetR;
        std::array<std::vector<float>, 2> pre;
        int preSize = 1, prePos = 0;
        Biquad lowCut, highCut;
        SmoothedGain mixSm;
        float lastTone = -1.0f;
    };

    // =========================================================================
    class BoostFx final : public BuiltinEffect
    {
    public:
        BoostFx() : BuiltinEffect ("boost", TR ("Boost", "音量"))
        {
            gain   = addFloat ("gain", "增益", -24.0f, 24.0f, 6.0f, "dB");
            bass   = addFloat ("bass", "低音", -12.0f, 12.0f, 0.0f, "dB");
            treble = addFloat ("treble", "高音", -12.0f, 12.0f, 0.0f, "dB");
        }

        void prepareEffect (double sr, int) override
        {
            smoothed.reset (sr, 0.02);
            smoothed.setCurrentAndTargetValue (dbToGain (gain->get()));
            tracker.invalidate();
            lo.reset(); hi.reset();
        }

        void processEffect (juce::AudioBuffer<float>& b) override
        {
            const double sr = currentSampleRate;
            if (tracker.changed ({ bass->get(), treble->get(), (float) sr }))
            {
                lo.setLowShelf (sr, 120.0, bass->get());
                hi.setHighShelf (sr, 3000.0, treble->get());
            }
            const bool eq = std::abs (bass->get()) > 0.05f || std::abs (treble->get()) > 0.05f;

            smoothed.setTargetValue (dbToGain (gain->get()));
            auto* l = b.getWritePointer (0);
            auto* r = b.getWritePointer (1);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float g = smoothed.getNextValue();
                float x = l[i] * g, y = r[i] * g;
                if (eq)
                {
                    x = hi.process (lo.process (x, 0), 0);
                    y = hi.process (lo.process (y, 1), 1);
                }
                l[i] = x; r[i] = y;
            }
        }

    private:
        juce::AudioParameterFloat *gain, *bass, *treble;
        juce::SmoothedValue<float> smoothed;
        Biquad lo, hi;
        ChangeTracker tracker;
    };
}

std::unique_ptr<BuiltinEffect> createMoreEffect (const juce::String& id);   // MoreEffects.cpp
std::unique_ptr<BuiltinEffect> createGervanaEffect (const juce::String& id); // GervanaEffects.cpp

// =============================================================================
const std::vector<BuiltinEffectInfo>& getBuiltinEffectList()
{
    static const std::vector<BuiltinEffectInfo> list {
        { "gv_wuyin",   TR ("Wuyin Fuzz", "戊寅 Wuyin 法兹"),          Gervana::category },
        { "gv_bingzi",  TR ("Bingzi Overdrive", "丙子 Bingzi 过载"),         Gervana::category },
        { "gv_wuwu",    TR ("Wuwu Plexi", "戊午 Wuwu Plexi"),          Gervana::category },
        { "gv_yiyou",   TR ("Yiyou Brown Sound", "乙酉 Yiyou Brown Sound"),   Gervana::category },
        { "gv_yimao",   TR ("Yimao AC30", "乙卯 Yimao AC30"),          Gervana::category },
        { "gv_jiachen", TR ("Jiachen Comp Boost", "甲辰 Jiachen 压缩激励"),    Gervana::category },
        { "denoise",    TR ("Noise Reduction", "降噪"),         TR ("Dynamics", "动态") },
        { "gate",       TR ("Noise Gate", "噪声门"),       TR ("Dynamics", "动态") },
        { "comp",       TR ("Compressor", "压缩"),         TR ("Dynamics", "动态") },
        { "boost",      TR ("Boost", "音量"),         TR ("Dynamics", "动态") },
        { "overdrive",  TR ("Overdrive", "过载"),         TR ("Distortion", "失真") },
        { "distortion", TR ("Distortion", "失真"),         TR ("Distortion", "失真") },
        { "fuzz",       TR ("Fuzz", "法兹"),         TR ("Distortion", "失真") },
        { "amp",        TR ("Amp Sim", "音箱模拟"),     TR ("Amp / Cab", "箱头 / 箱体") },
        { "cab",        TR ("Cab Sim", "箱体模拟"),     TR ("Amp / Cab", "箱头 / 箱体") },
        { "eq",         TR ("EQ", "均衡"),         TR ("Tone", "音色") },
        { "geq",        TR ("Graphic EQ", "图示均衡"),     TR ("Tone", "音色") },
        { "wah",        TR ("Wah", "哇音"),         TR ("Tone", "音色") },
        { "octave",     TR ("Octave", "八度"),         TR ("Pitch", "音高") },
        { "pitch",      TR ("Pitch Shift / Harmony", "移调 / 和声"),  TR ("Pitch", "音高") },
        { "chorus",     TR ("Chorus", "合唱"),         TR ("Modulation", "调制") },
        { "flanger",    TR ("Flanger", "镶边"),         TR ("Modulation", "调制") },
        { "phaser",     TR ("Phaser", "相位"),         TR ("Modulation", "调制") },
        { "tremolo",    TR ("Tremolo", "颤音"),         TR ("Modulation", "调制") },
        { "vibrato",    TR ("Vibrato", "揉音"),         TR ("Modulation", "调制") },
        { "delay",      TR ("Delay", "延迟"),         TR ("Space", "空间") },
        { "reverb",     TR ("Reverb", "混响"),         TR ("Space", "空间") },
        { "shimmer",    TR ("Shimmer", "空灵混响"),     TR ("Space", "空间") },
    };
    return list;
}

std::unique_ptr<BuiltinEffect> createBuiltinEffect (const juce::String& id)
{
    if (Gervana::isGervana (id)) return createGervanaEffect (id);
    if (id == "gate")       return std::make_unique<NoiseGateFx>();
    if (id == "comp")       return std::make_unique<CompressorFx>();
    if (id == "boost")      return std::make_unique<BoostFx>();
    if (id == "overdrive")  return std::make_unique<OverdriveFx>();
    if (id == "distortion") return std::make_unique<DistortionFx>();
    if (id == "amp")        return std::make_unique<AmpSimFx>();
    if (id == "eq")         return std::make_unique<EqFx>();
    if (id == "chorus")     return std::make_unique<ChorusFx> (false);
    if (id == "flanger")    return std::make_unique<ChorusFx> (true);
    if (id == "tremolo")    return std::make_unique<TremoloFx>();
    if (id == "delay")      return std::make_unique<DelayFx>();
    if (id == "reverb")     return std::make_unique<ReverbFx>();
    return createMoreEffect (id);
}
