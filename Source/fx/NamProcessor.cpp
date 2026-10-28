#include "NamProcessor.h"
#include "dsp/Biquad.h"
#include "dsp/ParamSmoother.h"
#include "Text.h"
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <filesystem>

#include "NAM/dsp.h"
#include "NAM/get_dsp.h"

namespace
{
    // juce::File -> std::filesystem::path，Windows 上中文路径必须按 UTF-8 走
    std::filesystem::path toPath (const juce::File& f)
    {
        const auto s = f.getFullPathName();
        return std::filesystem::path (reinterpret_cast<const char8_t*> (s.toRawUTF8()));
    }

    constexpr double targetLoudnessDb = -18.0;   // 与 NAM 官方插件一致
}

// =============================================================================
// NAM 后面的三段音色（和官方 NAM 插件一样）
struct NamProcessor::ToneStack
{
    Biquad low, mid, high;
    float last[3] { -100.0f, -100.0f, -100.0f };
};

NamProcessor::NamProcessor() : BuiltinEffect ("nam", TR ("NAM Amp", "NAM 箱头")), toneStack (std::make_unique<ToneStack>())
{
    inputDb  = addFloat ("input", "输入", -24.0f, 24.0f, 0.0f, "dB");
    outputDb = addFloat ("output", "输出", -36.0f, 12.0f, 0.0f, "dB");
    bass     = addFloat ("bass", "低音", -10.0f, 10.0f, 0.0f, "dB");
    mid      = addFloat ("mid", "中音", -10.0f, 10.0f, 0.0f, "dB");
    treble   = addFloat ("treble", "高音", -10.0f, 10.0f, 0.0f, "dB");
}

NamProcessor::~NamProcessor() = default;

bool NamProcessor::loadModel (const juce::File& file, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = TR ("Model file not found: ", "找不到模型文件：") + file.getFullPathName();
        return false;
    }

    try
    {
        auto dsp = nam::get_dsp (toPath (file));
        if (dsp == nullptr)
        {
            error = TR ("Could not read model: ", "无法读取模型：") + file.getFileName();
            return false;
        }

        if (dsp->NumInputChannels() != 1 || dsp->NumOutputChannels() != 1)
        {
            error = TR ("Only mono NAM models are supported (this model: ",
                        "只支持单声道 NAM 模型（这个模型：")
                    + juce::String (dsp->NumInputChannels()) + " in / "
                    + juce::String (dsp->NumOutputChannels()) + " out)";
            return false;
        }

        expectedSampleRate = dsp->GetExpectedSampleRate() > 0 ? dsp->GetExpectedSampleRate() : 48000.0;
        loudnessGain = dsp->HasLoudness()
                         ? (float) std::pow (10.0, (targetLoudnessDb - dsp->GetLoudness()) / 20.0)
                         : 1.0f;

        if (preparedBlock > 0)
            dsp->ResetAndPrewarm (currentSampleRate, preparedBlock);

        model = std::move (dsp);
        modelFile = file;
        return true;
    }
    catch (const std::exception& e)
    {
        error = TR ("Model failed to load: ", "模型加载失败：") + juce::String::fromUTF8 (e.what());
        return false;
    }
}

bool NamProcessor::hasSampleRateMismatch() const
{
    // When resampling is active the mismatch is already corrected.
    return model != nullptr && ! needResample
        && std::abs (currentSampleRate - expectedSampleRate) > 1.0;
}

void NamProcessor::prepareEffect (double sampleRate, int maxBlockSize)
{
    preparedBlock = maxBlockSize;
    inBuf.assign ((size_t) maxBlockSize, 0.0f);
    outBuf.assign ((size_t) maxBlockSize, 0.0f);

    inGainSm.reset (sampleRate, 0.02);
    outGainSm.reset (sampleRate, 0.02);
    inGainSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (inputDb->get()));
    outGainSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputDb->get()) * loudnessGain);

    needResample = model != nullptr && expectedSampleRate > 0.0
                   && std::abs (sampleRate - expectedSampleRate) > 1.0;
    modelRate = needResample ? expectedSampleRate : sampleRate;
    modelBlock = needResample
                     ? (int) std::ceil ((double) maxBlockSize * modelRate / sampleRate) + 8
                     : maxBlockSize;

    if (model != nullptr)
        model->ResetAndPrewarm (modelRate, modelBlock);

    if (needResample)
    {
        resampleUp.reset();
        resampleDown.reset();
        resampleHost.assign ((size_t) maxBlockSize + 8, 0.0f);
        resampleModel.assign ((size_t) modelBlock + 8, 0.0f);
        resampleModelOut.assign ((size_t) modelBlock + 8, 0.0f);
    }
}

int NamProcessor::effectLatencySamples() const
{
    if (! needResample)
        return 0;

    const double upLatency = juce::Interpolators::WindowedSinc::getBaseLatency() * modelRate / currentSampleRate;
    const double downLatency = juce::Interpolators::WindowedSinc::getBaseLatency();
    return (int) std::round (upLatency + downLatency);
}

void NamProcessor::processEffect (juce::AudioBuffer<float>& b)
{
    const int n = b.getNumSamples();
    auto* l = b.getWritePointer (0);
    auto* r = b.getWritePointer (1);

    if (model == nullptr || n > preparedBlock)
        return;   // 没有模型：直通

    inGainSm.setTargetValue (juce::Decibels::decibelsToGain (inputDb->get()));
    outGainSm.setTargetValue (juce::Decibels::decibelsToGain (outputDb->get()) * loudnessGain);

    // Guitar is mono: average L/R before the model.
    for (int i = 0; i < n; ++i)
        inBuf[(size_t) i] = 0.5f * (l[i] + r[i]) * inGainSm.getNextValue();

    if (! needResample)
    {
        float* in[] = { inBuf.data() };
        float* out[] = { outBuf.data() };
        model->process (in, out, n);
    }
    else
    {
        int modelSamples = (int) std::floor ((double) n * modelRate / currentSampleRate);
        modelSamples = juce::jlimit (1, (int) resampleModel.size() - 8, modelSamples);

        resampleUp.process (currentSampleRate / modelRate, inBuf.data(),
                            resampleModel.data(), modelSamples);
        for (int i = 0; i < 8; ++i)
            resampleModel[(size_t) modelSamples + (size_t) i] = 0.0f;

        float* in[] = { resampleModel.data() };
        float* out[] = { resampleModelOut.data() };
        model->process (in, out, modelSamples);

        for (int i = 0; i < 8; ++i)
            resampleModelOut[(size_t) modelSamples + (size_t) i] = 0.0f;

        resampleDown.process (modelRate / currentSampleRate, resampleModelOut.data(),
                              outBuf.data(), n);
    }

    auto& ts = *toneStack;
    const float vals[3] = { bass->get(), mid->get(), treble->get() };
    if (! juce::approximatelyEqual (vals[0], ts.last[0]) || ! juce::approximatelyEqual (vals[1], ts.last[1])
        || ! juce::approximatelyEqual (vals[2], ts.last[2]))
    {
        ts.low.setLowShelf (currentSampleRate, 150.0, vals[0]);
        ts.mid.setPeak (currentSampleRate, 650.0, 0.7, vals[1]);
        ts.high.setHighShelf (currentSampleRate, 3000.0, vals[2]);
        std::copy (vals, vals + 3, ts.last);
    }
    const bool eq = std::abs (vals[0]) > 0.05f || std::abs (vals[1]) > 0.05f || std::abs (vals[2]) > 0.05f;

    for (int i = 0; i < n; ++i)
    {
        float y = outBuf[(size_t) i] * outGainSm.getNextValue();
        if (eq)
            y = ts.high.process (ts.mid.process (ts.low.process (y, 0), 0), 0);
        l[i] = r[i] = y;
    }
}

// =============================================================================
struct IrProcessor::Impl
{
    juce::dsp::Convolution convolution { juce::dsp::Convolution::Latency { 0 } };
    juce::AudioBuffer<float> dry;
    Biquad lowCutFilter, highCutFilter;
    SmoothedGain levelSm, mixSm;
    float lastLow = -1.0f, lastHigh = -1.0f;
};

IrProcessor::IrProcessor() : BuiltinEffect ("ir", TR ("Cab IR", "箱体 IR")), impl (std::make_unique<Impl>())
{
    levelDb = addFloat ("level", "音量", -24.0f, 12.0f, 0.0f, "dB");
    lowCut  = addFloat ("lowcut", "低切", 20.0f, 400.0f, 70.0f, "Hz", 80.0f);
    highCut = addFloat ("highcut", "高切", 2000.0f, 20000.0f, 12000.0f, "Hz", 8000.0f);
    mix     = addFloat ("mix", "混合", 0.0f, 1.0f, 1.0f);
}

IrProcessor::~IrProcessor() = default;

bool IrProcessor::loadIr (const juce::File& file, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = TR ("IR file not found: ", "找不到 IR 文件：") + file.getFullPathName();
        return false;
    }

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    if (std::unique_ptr<juce::AudioFormatReader> reader { fm.createReaderFor (file) }; reader == nullptr)
    {
        error = TR ("Could not read IR (WAV required): ", "无法读取 IR 文件（需要 wav 格式）：") + file.getFileName();
        return false;
    }

    // Convolution 会在后台线程里加载并安全替换，不会打断音频
    impl->convolution.loadImpulseResponse (file, juce::dsp::Convolution::Stereo::no,
                                           juce::dsp::Convolution::Trim::yes, 0,
                                           juce::dsp::Convolution::Normalise::yes);
    irFile = file;
    return true;
}

void IrProcessor::prepareEffect (double sampleRate, int maxBlockSize)
{
    impl->convolution.prepare ({ sampleRate, (juce::uint32) maxBlockSize, 2 });
    impl->dry.setSize (2, maxBlockSize, false, true, false);
    impl->lastLow = impl->lastHigh = -1.0f;
    impl->lowCutFilter.reset();
    impl->highCutFilter.reset();
    impl->levelSm.prepare (sampleRate);
    impl->levelSm.snap (juce::Decibels::decibelsToGain (levelDb->get()));
    impl->mixSm.prepare (sampleRate);
    impl->mixSm.snap (mix->get());
}

void IrProcessor::resetEffect()
{
    impl->convolution.reset();
}

int IrProcessor::effectLatencySamples() const
{
    return impl->convolution.getLatency();
}

void IrProcessor::processEffect (juce::AudioBuffer<float>& b)
{
    if (! juce::approximatelyEqual (impl->lastLow, lowCut->get()) || ! juce::approximatelyEqual (impl->lastHigh, highCut->get()))
    {
        impl->lastLow = lowCut->get();
        impl->lastHigh = highCut->get();
        impl->lowCutFilter.setHighpass (currentSampleRate, impl->lastLow, 0.7);
        impl->highCutFilter.setLowpass (currentSampleRate, impl->lastHigh, 0.7);
    }

    const int n = b.getNumSamples();
    const bool canBlend = n <= impl->dry.getNumSamples();
    if (canBlend)
        for (int ch = 0; ch < 2; ++ch)
            impl->dry.copyFrom (ch, 0, b, ch, 0, n);

    juce::dsp::AudioBlock<float> block (b);
    impl->convolution.process (juce::dsp::ProcessContextReplacing<float> (block));

    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = b.getWritePointer (ch);
        impl->lowCutFilter.processBlock (d, b.getNumSamples(), ch);
        impl->highCutFilter.processBlock (d, b.getNumSamples(), ch);
    }

    impl->levelSm.setTarget (juce::Decibels::decibelsToGain (levelDb->get()));
    impl->mixSm.setTarget (mix->get());

    for (int i = 0; i < n; ++i)
    {
        const float lev = impl->levelSm.next();
        const float m = impl->mixSm.next();
        for (int ch = 0; ch < 2; ++ch)
        {
            const float wet = b.getWritePointer (ch)[i] * lev;
            b.getWritePointer (ch)[i] = canBlend
                ? wet * m + impl->dry.getReadPointer (ch)[i] * (1.0f - m)
                : wet;
        }
    }
}
