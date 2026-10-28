// Gervana 系列效果器（按官方介绍里的旋钮 / 拨档和音色定位做的数字模拟）
//   戊寅 Wuyin   法兹（Big Muff 系）：Volume / Sustain / Tone（可直通）/ Balance，模式、时代、低切
//   戊午 Wuwu    英式 Plexi 双通道：Bright / Normal 两路增益并联（跳线玩法），风格、削波，效果器 / 前级两用
//   丙子 Bingzi  多风格过载：Level / Tone / Gain / Blend / Tight，三种风格 × 三种削波 = 九种音色，动感增幅
//   甲辰 Jiachen OTA 压缩 + 清音激励：Level / Sustain / Attack / Blend，高音激励、电子管模拟
//   乙酉 Yiyou   美式 Brown Sound 前级：清音 / 失真双通道，三段均衡 + 临场感，共鸣拨档，效果器 / 前级两用
//   乙卯 Yimao   AC30 前级：普通 / Top Boost / 两通道并联，低音 / 高音 / 削高，效果器 / 前级两用
// 这些都是照着功能描述做的模拟，不是电路采样。
#include "BuiltinEffects.h"
#include "dsp/Biquad.h"
#include "dsp/DcBlocker.h"
#include "Text.h"
#include <juce_dsp/juce_dsp.h>
#include <map>

namespace
{
    inline float dbg (float db) { return juce::Decibels::decibelsToGain (db, -100.0f); }

    // 包络跟随（取绝对值，起音 / 释放分开）
    struct Envelope
    {
        float a = 0.0f, r = 0.0f, v = 0.0f;
        void set (double sr, float attackMs, float releaseMs)
        {
            a = 1.0f - std::exp (-1.0f / (float) (sr * attackMs * 0.001));
            r = 1.0f - std::exp (-1.0f / (float) (sr * releaseMs * 0.001));
        }
        float process (float x)
        {
            const float ax = std::abs (x);
            v += (ax > v ? a : r) * (ax - v);
            return v;
        }
    };

    // 带 2 倍过采样的 Gervana 基类：子类实现 pre（原采样率）、shapeOs（过采样里逐点）、post（原采样率）
    class GervanaBase : public BuiltinEffect
    {
    public:
        using BuiltinEffect::BuiltinEffect;

        void prepareEffect (double sr, int block) override
        {
            os.initProcessing ((size_t) block);
            os.reset();
            dc.prepare (sr, 12.0);
            dry.setSize (2, block, false, true, false);
            dirty = true;
            prepareGv (sr, block);
        }

        void processEffect (juce::AudioBuffer<float>& b) override
        {
            const int n = b.getNumSamples();
            if (n > dry.getNumSamples()) return;
            update (currentSampleRate);
            for (int ch = 0; ch < 2; ++ch)
                dry.copyFrom (ch, 0, b, ch, 0, n);

            pre (b);
            juce::dsp::AudioBlock<float> block (b);
            auto up = os.processSamplesUp (block);
            for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
                shapeOs (up.getChannelPointer (ch), (int) up.getNumSamples(), (int) ch);
            os.processSamplesDown (block);
            for (int ch = 0; ch < 2; ++ch)
                dc.processBlock (b.getWritePointer (ch), n, ch);
            post (b, dry);
        }

        void resetEffect() override { os.reset(); dc.reset(); dirty = true; }
        int effectLatencySamples() const override { return (int) std::round (os.getLatencyInSamples()) + 2; }

    protected:
        virtual void prepareGv (double, int) {}
        virtual void update (double sr) = 0;
        virtual void pre (juce::AudioBuffer<float>&) {}
        virtual void shapeOs (float* d, int n, int ch) = 0;
        virtual void post (juce::AudioBuffer<float>& b, const juce::AudioBuffer<float>& dryBuf) = 0;

        static void run (Biquad& f, juce::AudioBuffer<float>& b)
        {
            for (int ch = 0; ch < 2; ++ch) f.processBlock (b.getWritePointer (ch), b.getNumSamples(), ch);
        }

        // 参数变了才重算滤波器
        bool changed (float hash)
        {
            if (! dirty && juce::approximatelyEqual (hash, lastHash)) return false;
            lastHash = hash; dirty = false; return true;
        }

        juce::dsp::Oversampling<float> os { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true };
        juce::AudioBuffer<float> dry;
        DcBlocker dc;
        bool dirty = true;
        float lastHash = 0.0f;
    };

    // 前级用的“带箱体”输出：内部挂一个箱体模拟
    struct CabOut
    {
        std::unique_ptr<BuiltinEffect> cab;
        juce::MidiBuffer midi;
        void prepare (double sr, int block, int type, float bright)
        {
            cab = createBuiltinEffect ("cab");
            cab->setParam ("type", (float) type);
            cab->setParam ("bright", bright);
            cab->setParam ("mic", 0.3f);
            cab->setPlayConfigDetails (2, 2, sr, block);
            cab->prepareToPlay (sr, block);
        }
        void process (juce::AudioBuffer<float>& b) { if (cab != nullptr) cab->processBlock (b, midi); }
    };

    // =========================================================================
    // 戊寅 Wuyin 法兹
    class WuyinFuzz final : public GervanaBase
    {
    public:
        WuyinFuzz() : GervanaBase ("gv_wuyin", TR ("Wuyin Fuzz", "戊寅 Wuyin 法兹"))
        {
            volume  = addFloat ("volume", "音量", -36.0f, 6.0f, -10.0f, "dB");
            sustain = addFloat ("sustain", "延音", 0.0f, 1.0f, 0.6f);
            tone    = addFloat ("tone", "音色", 0.0f, 1.0f, 0.5f);
            balance = addFloat ("balance", "平衡", 0.0f, 1.0f, 0.6f);
            mode    = addChoice ("mode", "模式", { "经典 Muff", "Gervana 动态", "复古 Vox" }, 0);
            voice   = addChoice ("voice", "时代", { "俄产", "Pi", "三角" }, 1);
            filter  = addBool ("filter", "低切", false);
            toneBypass = addBool ("toneBypass", "音色直通", false);
        }

    private:
        void prepareGv (double sr, int) override { env.set (sr, 2.0f, 140.0f); }

        void update (double sr) override
        {
            const int v = voice->getIndex(), m = mode->getIndex();
            if (! changed (tone->get() * 3.0f + balance->get() * 7.0f + (float) v * 11.0f + (float) m * 13.0f
                           + (filter->get() ? 17.0f : 0.0f) + (float) sr * 1.0e-3f))
                return;
            const double osr = sr * 2.0;
            inHp.setHighpass (sr, filter->get() ? 170.0 : 35.0, 0.7);
            // 平衡：输入阻抗。拧小像接在长线 / 无缓冲效果器后面（高频被吃一点、推得没那么猛）
            inLp.setLowpass (sr, 2500.0 + 14000.0 * balance->get(), 0.6);
            inter.setLowpass (osr, 6000.0, 0.6);
            interHp.setHighpass (osr, v == 0 ? 60.0 : 90.0, 0.6);
            // 时代：俄产更暗更厚、Pi 经典中频凹陷、三角更亮中频多
            const double fl[] = { 320.0, 460.0, 680.0 }, fh[] = { 950.0, 1150.0, 1500.0 };
            toneLp.setLowpass (sr, fl[v], 0.6);
            toneHp.setHighpass (sr, fh[v], 0.6);
            vox.setPeak (sr, 1600.0, 0.9, m == 2 ? 6.0 : 0.0);
            voxLp.setLowpass (sr, m == 2 ? 6000.0 : 8000.0, 0.6);
            flatLp.setLowpass (sr, 5500.0, 0.6);
            low.setLowShelf (sr, 120.0, v == 0 ? 3.0 : 1.0);
        }

        void pre (juce::AudioBuffer<float>& b) override
        {
            run (inHp, b); run (inLp, b);
            const float inGain = 0.55f + 0.9f * balance->get();
            const float trim[] = { 0.0f, 2.5f, 4.0f };
            const float g = dbg (16.0f + 30.0f * sustain->get() + trim[voice->getIndex()]);
            const bool dynamic = mode->getIndex() == 1;
            // Gervana 动态模式：轻弹时增益下来，失真变少，手上的力度能听出来
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float e = env.process (0.5f * (b.getSample (0, i) + b.getSample (1, i)));
                const float dyn = dynamic ? 0.25f + 0.75f * juce::jlimit (0.0f, 1.0f, e / 0.12f) : 1.0f;
                for (int ch = 0; ch < 2; ++ch)
                    b.setSample (ch, i, b.getSample (ch, i) * inGain * g * dyn);
            }
        }

        void shapeOs (float* d, int n, int ch) override
        {
            // 两级晶体管削波，中间有耦合电容（高通）和米勒电容（低通）
            for (int i = 0; i < n; ++i)
            {
                float x = std::tanh (d[i] * 0.35f);
                x = inter.process (interHp.process (x, ch), ch) * 6.0f;
                x = x >= 0.0f ? std::tanh (x) : std::tanh (1.25f * x) / 1.25f;
                d[i] = x;
            }
        }

        void post (juce::AudioBuffer<float>& b, const juce::AudioBuffer<float>&) override
        {
            const int n = b.getNumSamples();
            if (toneBypass->get())
                run (flatLp, b);                        // 一键直通：不经过音色电路，中频完整
            else
            {
                const float t = tone->get();
                for (int ch = 0; ch < 2; ++ch)
                {
                    auto* d = b.getWritePointer (ch);
                    for (int i = 0; i < n; ++i)
                        d[i] = (1.0f - t) * toneLp.process (d[i], ch) * 1.4f + t * toneHp.process (d[i], ch) * 1.1f;
                }
            }
            run (low, b); run (vox, b); run (voxLp, b);
            b.applyGain (dbg (volume->get() + (toneBypass->get() ? -3.0f : 1.0f)));
        }

        juce::AudioParameterFloat *volume, *sustain, *tone, *balance;
        juce::AudioParameterChoice *mode, *voice;
        juce::AudioParameterBool *filter, *toneBypass;
        Envelope env;
        Biquad inHp, inLp, inter, interHp, toneLp, toneHp, vox, voxLp, flatLp, low;
    };

    // =========================================================================
    // 丙子 Bingzi 多风格过载
    class BingziDrive final : public GervanaBase
    {
    public:
        BingziDrive() : GervanaBase ("gv_bingzi", TR ("Bingzi Overdrive", "丙子 Bingzi 过载"))
        {
            level = addFloat ("level", "音量", -24.0f, 12.0f, -2.0f, "dB");
            tone  = addFloat ("tone", "音色", 0.0f, 1.0f, 0.5f);
            gain  = addFloat ("gain", "增益", 0.0f, 1.0f, 0.4f);
            blend = addFloat ("blend", "混合", 0.0f, 1.0f, 1.0f);
            tight = addFloat ("tight", "紧致", 40.0f, 600.0f, 140.0f, "Hz", 150.0f);
            style = addChoice ("style", "风格", { "奶油 TS808", "尖锐 SD", "温暖 Timmy" }, 0);
            clip  = addChoice ("clip", "削波", { "硅管对称", "非对称", "LED 开放" }, 0);
        }

    private:
        void update (double sr) override
        {
            const int s = style->getIndex();
            if (! changed (tone->get() * 3.0f + tight->get() * 0.01f + (float) s * 7.0f + (float) sr * 1.0e-3f))
                return;
            tightHp.setHighpass (sr, tight->get(), 0.6);
            const double t = tone->get();
            switch (s)
            {
                case 0:  // 奶油 TS808：中频隆起，高频圆
                    mid.setPeak (sr, 900.0, 0.8, 4.0);
                    lp.setLowpass (sr, 900.0 + 4800.0 * t * t, 0.6);
                    shelf.setHighShelf (sr, 3000.0, 0.0);
                    break;
                case 1:  // 尖锐 SD：中高频更靠前
                    mid.setPeak (sr, 1800.0, 0.9, 4.0);
                    lp.setLowpass (sr, 1400.0 + 6000.0 * t * t, 0.7);
                    shelf.setHighShelf (sr, 3000.0, 1.5);
                    break;
                default: // 温暖 Timmy：平直通透，音色旋钮是削高
                    mid.setPeak (sr, 700.0, 0.7, 0.5);
                    lp.setLowpass (sr, 9000.0, 0.6);
                    shelf.setHighShelf (sr, 2200.0, (t - 0.6) * 14.0);
                    break;
            }
        }

        void pre (juce::AudioBuffer<float>& b) override
        {
            run (tightHp, b);
            // 增益范围大约是传统 TS 的两倍
            b.applyGain (dbg (4.0f + 50.0f * gain->get()));
        }

        void shapeOs (float* d, int n, int) override
        {
            const int c = clip->getIndex();
            for (int i = 0; i < n; ++i)
            {
                const float x = d[i];
                float y;
                if (c == 0)      y = std::tanh (x);
                else if (c == 1) y = x >= 0.0f ? std::tanh (x) : std::tanh (1.7f * x) / 1.7f;
                else             y = 1.8f * std::tanh (x / 1.8f);         // LED：削波点高，更开放、更响
                // 动感增幅：保留一部分没被削平的信号，压缩感更低
                d[i] = 0.82f * y + 0.18f * x / (1.0f + 0.35f * std::abs (x));
            }
        }

        void post (juce::AudioBuffer<float>& b, const juce::AudioBuffer<float>& dryBuf) override
        {
            run (mid, b); run (lp, b); run (shelf, b);
            const float bl = blend->get();
            const float trim[] = { 0.0f, -1.0f, -3.0f };
            const float wetG = dbg (level->get() - 4.0f + trim[clip->getIndex()]);
            const float cleanG = dbg (level->get() + 10.0f * gain->get());   // Blend 关掉 = 纯净提升
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = b.getWritePointer (ch);
                const auto* x = dryBuf.getReadPointer (ch);
                for (int i = 0; i < b.getNumSamples(); ++i)
                    d[i] = bl * d[i] * wetG + (1.0f - bl) * x[i] * cleanG;
            }
        }

        juce::AudioParameterFloat *level, *tone, *gain, *blend, *tight;
        juce::AudioParameterChoice *style, *clip;
        Biquad tightHp, mid, lp, shelf;
    };

    // =========================================================================
    // 甲辰 Jiachen OTA 压缩 + 激励
    class JiachenComp final : public BuiltinEffect
    {
    public:
        JiachenComp() : BuiltinEffect ("gv_jiachen", TR ("Jiachen Comp Boost", "甲辰 Jiachen 压缩激励"))
        {
            level   = addFloat ("level", "激励", 0.0f, 20.0f, 2.0f, "dB");
            sustain = addFloat ("sustain", "延音", 0.0f, 1.0f, 0.5f);
            attack  = addFloat ("attack", "起音", 0.0f, 1.0f, 0.5f);
            blend   = addFloat ("blend", "混合", 0.0f, 1.0f, 0.7f);
            treble  = addBool ("trebleBoost", "高音激励", false);
            tube    = addBool ("tubeSim", "电子管模拟", false);
        }

        void prepareEffect (double sr, int) override
        {
            last = -1.0f;
            gr = 1.0f;
            dc.setHighpass (sr, 15.0, 0.7);
        }

        void processEffect (juce::AudioBuffer<float>& b) override
        {
            const double sr = currentSampleRate;
            const float h = attack->get() * 3.0f + (treble->get() ? 5.0f : 0.0f) + (tube->get() ? 7.0f : 0.0f);
            if (! juce::approximatelyEqual (h, last))
            {
                last = h;
                // 起音旋钮越大，压缩器越晚抓住音头，拨弦越“脆”
                env.set (sr, 0.3f + 25.0f * attack->get(), 160.0f);
                hi.setPeak (sr, 3200.0, 0.8, treble->get() ? 6.0 : 0.0);
                air.setHighShelf (sr, 6000.0, treble->get() ? 2.5 : 0.0);
                warm.setLowpass (sr, tube->get() ? 9000.0 : 20000.0, 0.6);
            }
            const float s = sustain->get();
            const float thr = dbg (-8.0f - 32.0f * s);
            const float ratio = 2.0f + 10.0f * s;
            const float makeup = dbg (0.4f * (8.0f + 32.0f * s) * (1.0f - 1.0f / ratio));
            const float bl = blend->get();
            const float out = dbg (level->get());
            const bool tb = tube->get();
            const float grCoef = 1.0f - std::exp (-1.0f / (float) (sr * 0.002));

            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float l = b.getSample (0, i), r = b.getSample (1, i);
                const float e = env.process (0.5f * (l + r));
                float target = 1.0f;
                if (e > thr)
                    target = std::pow (e / thr, 1.0f / ratio - 1.0f);
                gr += grCoef * (target - gr);
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float x = ch == 0 ? l : r;
                    float y = x * gr * makeup;
                    y = bl * y + (1.0f - bl) * x;                       // 干湿混合
                    if (tb)                                             // 电子管：偶次谐波 + 温和饱和
                        y = dc.process (std::tanh (1.4f * (y + 0.12f * y * y)) / 1.4f, ch);
                    y = air.process (hi.process (warm.process (y, ch), ch), ch);
                    b.setSample (ch, i, y * out);
                }
            }
        }

    private:
        juce::AudioParameterFloat *level, *sustain, *attack, *blend;
        juce::AudioParameterBool *treble, *tube;
        Envelope env;
        Biquad hi, air, warm, dc;
        float gr = 1.0f, last = -1.0f;
    };

    // =========================================================================
    // 戊午 Wuwu 英式 Plexi
    class WuwuPlexi final : public GervanaBase
    {
    public:
        WuwuPlexi() : GervanaBase ("gv_wuwu", TR ("Wuwu Plexi", "戊午 Wuwu Plexi"))
        {
            level      = addFloat ("level", "音量", -36.0f, 6.0f, -10.0f, "dB");
            tone       = addFloat ("tone", "音色", 0.0f, 1.0f, 0.5f);
            presence   = addFloat ("presence", "临场感", 0.0f, 1.0f, 0.5f);
            brightGain = addFloat ("brightGain", "亮通道增益", 0.0f, 1.0f, 0.6f);
            normalGain = addFloat ("normalGain", "普通通道增益", 0.0f, 1.0f, 0.3f);
            voice      = addChoice ("voice", "风格", { "标准英式", "GERVANA 原声", "混合" }, 0);
            clip       = addChoice ("clip", "削波", { "现代", "无削波", "复古" }, 2);
            output     = addChoice ("output", "输出", { "效果器（进音箱）", "前级（带箱体）" }, 0);
        }

    private:
        void prepareGv (double sr, int block) override
        {
            cabOut.prepare (sr, block, 2, 0.55f);
            brightBuf.setSize (2, block, false, true, false);
        }

        void update (double sr) override
        {
            const int v = voice->getIndex();
            if (! changed (tone->get() * 3.0f + presence->get() * 5.0f + brightGain->get() * 7.0f + (float) v * 11.0f
                           + (float) output->getIndex() * 13.0f + (float) sr * 1.0e-3f))
                return;
            const double osr = sr * 2.0;
            // 亮通道：亮音电容，增益越小高频越突出
            brightCap.setHighShelf (sr, 2200.0, 3.0 + 7.0 * (1.0 - brightGain->get()));
            normalLp.setLowpass (sr, 4500.0, 0.6);
            normalLow.setLowShelf (sr, 140.0, 3.0);
            interHp.setHighpass (osr, 140.0, 0.6);
            interLp.setLowpass (osr, 9000.0, 0.6);
            const double t = tone->get();
            stackLow.setLowShelf (sr, 180.0, (0.5 - t) * 8.0 + (v == 1 ? 2.5 : v == 2 ? 1.2 : 0.0));
            stackMid.setPeak (sr, 700.0, 0.8, v == 0 ? 3.0 : v == 1 ? -0.5 : 1.5);
            stackHigh.setHighShelf (sr, 2000.0, (t - 0.5) * 12.0 + (v == 1 ? 1.5 : 0.0));
            pres.setHighShelf (sr, 3600.0, presence->get() * 10.0 - 3.0);
            pedalLp.setLowpass (sr, output->getIndex() == 0 ? 7500.0 : 20000.0, 0.6);
        }

        void pre (juce::AudioBuffer<float>& b) override
        {
            // 两个通道并联（像 Plexi 跳线），各自的增益旋钮控制失真量
            const int n = b.getNumSamples();
            for (int ch = 0; ch < 2; ++ch) brightBuf.copyFrom (ch, 0, b, ch, 0, n);
            const float bg = brightGain->get(), ng = normalGain->get();
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* nb = b.getWritePointer (ch);
                auto* bb = brightBuf.getWritePointer (ch);
                brightCap.processBlock (bb, n, ch);
                normalLp.processBlock (nb, n, ch);
                normalLow.processBlock (nb, n, ch);
                const float gB = dbg (40.0f * bg * bg + 2.0f) * (bg > 0.01f ? 1.0f : 0.0f);
                const float gN = dbg (36.0f * ng * ng + 2.0f) * (ng > 0.01f ? 1.0f : 0.0f);
                for (int i = 0; i < n; ++i) nb[i] = nb[i] * gN + bb[i] * gB;
            }
        }

        float stage (float x) const
        {
            switch (clip->getIndex())
            {
                case 0:  return x >= 0.0f ? std::tanh (1.6f * x) / 1.2f : std::tanh (2.0f * x) / 1.5f;   // 现代：更紧更硬
                case 1:  return x / (1.0f + 0.12f * std::abs (x));                                       // 无削波：几乎干净，只有一点压缩
                default: return x >= 0.0f ? 1.0f - std::exp (-x) : -(1.0f - std::exp (1.3f * x)) / 1.3f; // 复古：电子管式不对称
            }
        }

        void shapeOs (float* d, int n, int ch) override
        {
            const float g2 = clip->getIndex() == 1 ? 1.0f : 3.0f;
            for (int i = 0; i < n; ++i)
            {
                float x = stage (d[i] * 0.5f);
                x = interLp.process (interHp.process (x, ch), ch);
                d[i] = stage (x * g2);
            }
        }

        void post (juce::AudioBuffer<float>& b, const juce::AudioBuffer<float>&) override
        {
            run (stackLow, b); run (stackMid, b); run (stackHigh, b); run (pres, b); run (pedalLp, b);
            if (output->getIndex() == 1) cabOut.process (b);
            const float trim[] = { -2.0f, 4.0f, 0.0f };
            b.applyGain (dbg (level->get() + trim[clip->getIndex()]));
        }

        juce::AudioParameterFloat *level, *tone, *presence, *brightGain, *normalGain;
        juce::AudioParameterChoice *voice, *clip, *output;
        juce::AudioBuffer<float> brightBuf;
        Biquad brightCap, normalLp, normalLow, interHp, interLp, stackLow, stackMid, stackHigh, pres, pedalLp;
        CabOut cabOut;
    };

    // =========================================================================
    // 乙酉 Yiyou 美式 Brown Sound 前级
    class YiyouBrown final : public GervanaBase
    {
    public:
        YiyouBrown() : GervanaBase ("gv_yiyou", TR ("Yiyou Brown Sound", "乙酉 Yiyou Brown Sound"))
        {
            channel  = addChoice ("channel", "通道", { "清音", "失真" }, 1);
            gain     = addFloat ("gain", "增益", 0.0f, 10.0f, 6.0f);
            volume   = addFloat ("volume", "音量", -36.0f, 6.0f, -10.0f, "dB");
            low      = addFloat ("low", "低音", 0.0f, 10.0f, 5.0f);
            mid      = addFloat ("mid", "中音", 0.0f, 10.0f, 6.0f);
            high     = addFloat ("high", "高音", 0.0f, 10.0f, 5.5f);
            presence = addFloat ("presence", "临场感", 0.0f, 10.0f, 5.0f);
            reso     = addChoice ("resonance", "共鸣", { "紧", "标准", "松" }, 1);
            output   = addChoice ("output", "输出", { "效果器（进音箱）", "前级（带箱体）" }, 1);
        }

    private:
        void prepareGv (double sr, int block) override { cabOut.prepare (sr, block, 2, 0.5f); }

        void update (double sr) override
        {
            const int r = reso->getIndex(), c = channel->getIndex();
            if (! changed (low->get() * 3.0f + mid->get() * 5.0f + high->get() * 7.0f + presence->get() * 11.0f
                           + (float) r * 13.0f + (float) c * 17.0f + (float) output->getIndex() * 19.0f + (float) sr * 1.0e-3f))
                return;
            const double osr = sr * 2.0;
            inHp.setHighpass (sr, r == 0 ? 130.0 : 90.0, 0.6);
            brown.setPeak (sr, 800.0, 0.7, c == 1 ? 4.0 : 1.0);        // Brown Sound 的中频
            i1.setHighpass (osr, 160.0, 0.6);
            i2.setLowpass (osr, 7000.0, 0.6);
            i3.setHighpass (osr, 110.0, 0.6);
            eqLow.setLowShelf (sr, 120.0, (low->get() - 5.0f) * 2.4f);
            eqMid.setPeak (sr, 750.0, 0.8, (mid->get() - 5.0f) * 2.4f);
            eqHigh.setHighShelf (sr, 3000.0, (high->get() - 5.0f) * 2.4f);
            pres.setHighShelf (sr, 5000.0, (presence->get() - 5.0f) * 1.8f);
            resPeak.setPeak (sr, 90.0, 1.1, r == 0 ? 0.0 : r == 1 ? 3.0 : 6.0);
            pedalLp.setLowpass (sr, output->getIndex() == 0 ? 7000.0 : 20000.0, 0.6);
        }

        void pre (juce::AudioBuffer<float>& b) override
        {
            run (inHp, b); run (brown, b);
            const float g = gain->get() / 10.0f;
            b.applyGain (channel->getIndex() == 1 ? dbg (8.0f + 34.0f * g) : dbg (-2.0f + 18.0f * g));
        }

        void shapeOs (float* d, int n, int ch) override
        {
            const bool dist = channel->getIndex() == 1;
            for (int i = 0; i < n; ++i)
            {
                float x = d[i];
                if (dist)
                {
                    // 三级：前两级不对称、中间去低频，听起来紧而“棕”
                    x = x >= 0.0f ? std::tanh (x) : std::tanh (1.3f * x) / 1.3f;
                    x = i1.process (x, ch) * 4.0f;
                    x = std::tanh (x);
                    x = i2.process (i3.process (x, ch), ch) * 2.5f;
                    x = x >= 0.0f ? 1.0f - std::exp (-x) : -(1.0f - std::exp (1.2f * x)) / 1.2f;
                }
                else
                {
                    x = std::tanh (0.7f * x) / 0.7f;
                }
                d[i] = x;
            }
        }

        void post (juce::AudioBuffer<float>& b, const juce::AudioBuffer<float>&) override
        {
            run (eqLow, b); run (eqMid, b); run (eqHigh, b); run (pres, b); run (resPeak, b); run (pedalLp, b);
            if (output->getIndex() == 1) cabOut.process (b);
            b.applyGain (dbg (volume->get() + (channel->getIndex() == 1 ? -1.0f : 2.0f)));
        }

        juce::AudioParameterChoice *channel, *reso, *output;
        juce::AudioParameterFloat *gain, *volume, *low, *mid, *high, *presence;
        Biquad inHp, brown, i1, i2, i3, eqLow, eqMid, eqHigh, pres, resPeak, pedalLp;
        CabOut cabOut;
    };

    // =========================================================================
    // 乙卯 Yimao AC30 前级
    class YimaoAc30 final : public GervanaBase
    {
    public:
        YimaoAc30() : GervanaBase ("gv_yimao", TR ("Yimao AC30", "乙卯 Yimao AC30"))
        {
            channel   = addChoice ("channel", "通道", { "普通", "Top Boost", "两通道并联" }, 1);
            normal    = addFloat ("normal", "普通音量", 0.0f, 10.0f, 5.0f);
            brilliant = addFloat ("brilliant", "Top Boost 音量", 0.0f, 10.0f, 6.0f);
            bass      = addFloat ("bass", "低音", 0.0f, 10.0f, 5.0f);
            treble    = addFloat ("treble", "高音", 0.0f, 10.0f, 6.0f);
            tonecut   = addFloat ("tonecut", "削高", 0.0f, 10.0f, 3.0f);
            master    = addFloat ("master", "总音量", -36.0f, 6.0f, -10.0f, "dB");
            output    = addChoice ("output", "输出", { "效果器（进音箱）", "前级（带箱体）" }, 1);
        }

    private:
        void prepareGv (double sr, int block) override
        {
            cabOut.prepare (sr, block, 1, 0.65f);
            topBuf.setSize (2, block, false, true, false);
        }

        void update (double sr) override
        {
            if (! changed (bass->get() * 3.0f + treble->get() * 5.0f + tonecut->get() * 7.0f
                           + (float) output->getIndex() * 11.0f + (float) sr * 1.0e-3f))
                return;
            const double osr = sr * 2.0;
            nLp.setLowpass (sr, 3800.0, 0.6);
            tBass.setLowShelf (sr, 160.0, (bass->get() - 5.0f) * 2.6f);
            tTreble.setHighShelf (sr, 1800.0, (treble->get() - 5.0f) * 3.0f);
            chime.setPeak (sr, 2600.0, 1.0, 3.5);              // AC30 的“玻璃感”
            interHp.setHighpass (osr, 90.0, 0.6);
            // 削高：功放级之后的低通，越往右越暗
            cut.setLowpass (sr, 12000.0 * std::pow (0.12, tonecut->get() / 10.0), 0.6);
            pedalLp.setLowpass (sr, output->getIndex() == 0 ? 8000.0 : 20000.0, 0.6);
        }

        void pre (juce::AudioBuffer<float>& b) override
        {
            const int n = b.getNumSamples(), c = channel->getIndex();
            for (int ch = 0; ch < 2; ++ch) topBuf.copyFrom (ch, 0, b, ch, 0, n);
            const float gN = c == 1 ? 0.0f : dbg (-6.0f + 3.2f * normal->get());
            const float gT = c == 0 ? 0.0f : dbg (-4.0f + 3.6f * brilliant->get());
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* nb = b.getWritePointer (ch);
                auto* tb = topBuf.getWritePointer (ch);
                nLp.processBlock (nb, n, ch);
                tBass.processBlock (tb, n, ch);
                tTreble.processBlock (tb, n, ch);
                chime.processBlock (tb, n, ch);
                for (int i = 0; i < n; ++i) nb[i] = nb[i] * gN + tb[i] * gT;
            }
        }

        void shapeOs (float* d, int n, int ch) override
        {
            for (int i = 0; i < n; ++i)
            {
                // 甲类前级：对称、软；EL84 功放：压缩感明显
                float x = std::tanh (0.6f * d[i]) / 0.6f;
                x = interHp.process (x, ch);
                d[i] = x >= 0.0f ? std::tanh (0.9f * x) : std::tanh (1.1f * x) / 1.1f;
            }
        }

        void post (juce::AudioBuffer<float>& b, const juce::AudioBuffer<float>&) override
        {
            run (cut, b); run (pedalLp, b);
            if (output->getIndex() == 1) cabOut.process (b);
            b.applyGain (dbg (master->get()));
        }

        juce::AudioParameterChoice *channel, *output;
        juce::AudioParameterFloat *normal, *brilliant, *bass, *treble, *tonecut, *master;
        juce::AudioBuffer<float> topBuf;
        Biquad nLp, tBass, tTreble, chime, interHp, cut, pedalLp;
        CabOut cabOut;
    };
}

std::unique_ptr<BuiltinEffect> createGervanaEffect (const juce::String& id)
{
    if (id == "gv_wuyin")   return std::make_unique<WuyinFuzz>();
    if (id == "gv_wuwu")    return std::make_unique<WuwuPlexi>();
    if (id == "gv_bingzi")  return std::make_unique<BingziDrive>();
    if (id == "gv_jiachen") return std::make_unique<JiachenComp>();
    if (id == "gv_yiyou")   return std::make_unique<YiyouBrown>();
    if (id == "gv_yimao")   return std::make_unique<YimaoAc30>();
    return nullptr;
}

// ---- artwork ------------------------------------------------------------------
#include "EffectChain.h"
#include "app/AppPaths.h"

juce::File Gervana::imageFileFor (const juce::String& id)
{
    const auto dir = AppPaths::gervanaDir();
    for (auto ext : { ".png", ".jpg", ".jpeg" })
        if (auto f = dir.getChildFile (id + ext); f.existsAsFile())
            return f;
    return {};
}

juce::Image Gervana::imageFor (const juce::String& id)
{
    // 按文件修改时间缓存：换了图片不用重启
    struct Entry { juce::Time mod; juce::Image img; };
    static std::map<juce::String, Entry> cache;
    const auto f = imageFileFor (id);
    if (! f.existsAsFile()) { cache.erase (id); return {}; }
    auto& e = cache[id];
    if (e.img.isNull() || e.mod != f.getLastModificationTime())
    {
        e.mod = f.getLastModificationTime();
        e.img = juce::ImageFileFormat::loadFrom (f);
    }
    return e.img;
}
