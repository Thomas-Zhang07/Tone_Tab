#pragma once

#include "BuiltinEffect.h"
#include <memory>
#include <vector>

namespace nam { class DSP; }

// NAM（Neural Amp Modeler）箱头模型。模型文件 .nam 在界面线程加载好以后才放进效果器链。
class NamProcessor final : public BuiltinEffect
{
public:
    NamProcessor();
    ~NamProcessor() override;

    // 界面线程调用。失败返回 false，并写入 error
    bool loadModel (const juce::File& file, juce::String& error);

    juce::File getModelFile() const      { return modelFile; }
    double getExpectedSampleRate() const { return expectedSampleRate; }
    bool hasSampleRateMismatch() const;

    void prepareEffect (double sampleRate, int maxBlockSize) override;
    void processEffect (juce::AudioBuffer<float>& buffer) override;
    int effectLatencySamples() const override;

protected:
    void writeExtraState (juce::XmlElement& xml) override { xml.setAttribute ("model", modelFile.getFullPathName()); }

private:
    juce::AudioParameterFloat *inputDb, *outputDb, *bass, *mid, *treble;
    struct ToneStack;
    std::unique_ptr<ToneStack> toneStack;
    std::unique_ptr<nam::DSP> model;
    juce::File modelFile;
    double expectedSampleRate = 48000.0;
    float loudnessGain = 1.0f;
    std::vector<float> inBuf, outBuf;
    juce::SmoothedValue<float> inGainSm, outGainSm;
    bool needResample = false;
    double modelRate = 48000.0;
    int modelBlock = 0;
    juce::Interpolators::WindowedSinc resampleUp, resampleDown;
    std::vector<float> resampleHost, resampleModel, resampleModelOut;
    int preparedBlock = 0;
};

// 箱体 IR（脉冲响应 .wav）卷积
class IrProcessor final : public BuiltinEffect
{
public:
    IrProcessor();
    ~IrProcessor() override;

    bool loadIr (const juce::File& file, juce::String& error);
    juce::File getIrFile() const { return irFile; }

    void prepareEffect (double sampleRate, int maxBlockSize) override;
    void processEffect (juce::AudioBuffer<float>& buffer) override;
    void resetEffect() override;
    int effectLatencySamples() const override;

protected:
    void writeExtraState (juce::XmlElement& xml) override { xml.setAttribute ("ir", irFile.getFullPathName()); }

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    juce::AudioParameterFloat *levelDb, *lowCut, *highCut, *mix;
    juce::File irFile;
};
