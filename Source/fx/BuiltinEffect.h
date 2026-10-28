#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Text.h"

// 所有内置效果器（以及 NAM、IR 模块）的基类。
// 统一做成 juce::AudioProcessor，这样和 VST3 插件走同一条链路、同一种编辑器窗口。
class BuiltinEffect : public juce::AudioProcessor
{
public:
    BuiltinEffect (juce::String typeIdToUse, juce::String displayNameToUse)
        : juce::AudioProcessor (BusesProperties()
                                    .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                    .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          typeId (std::move (typeIdToUse)),
          displayName (std::move (displayNameToUse))
    {
    }

    const juce::String typeId;

    // ---- 子类实现 -----------------------------------------------------------
    virtual void prepareEffect (double sampleRate, int maxBlockSize) = 0;
    virtual void processEffect (juce::AudioBuffer<float>& buffer) = 0;
    virtual void resetEffect() {}

    // ---- 参数 ---------------------------------------------------------------
    // 按参数 id 设置（实际值，不是 0..1），用于默认预设
    void setParam (const juce::String& id, float value)
    {
        for (auto* p : getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (r->getParameterID() == id)
                    r->setValueNotifyingHost (r->convertTo0to1 (value));
    }

    // ---- AudioProcessor -----------------------------------------------------
    const juce::String getName() const override
    {
        const auto m = modelLabel();
        return m.isEmpty() ? displayName : displayName + juce::String::fromUTF8 (" \xc2\xb7 ") + m;
    }
    // 有“型号”选择的效果器在名字后面加上型号（例如 “过载 · TS808 风格”）
    virtual juce::String modelLabel() const             { return {}; }
    // Latency introduced by this effect (oversampling, convolution, ...).
    virtual int effectLatencySamples() const            { return 0; }

    void prepareToPlay (double sr, int block) override
    {
        currentSampleRate = sr;
        prepareEffect (sr, block);
        setLatencySamples (effectLatencySamples());
    }
    void releaseResources() override                    {}
    void reset() override                               { resetEffect(); }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        processEffect (buffer);
    }

    bool isBusesLayoutSupported (const BusesLayout& layout) const override
    {
        return layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
            && layout.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
    }

    double getTailLengthSeconds() const override        { return 0.0; }
    bool acceptsMidi() const override                   { return false; }
    bool producesMidi() const override                  { return false; }
    bool hasEditor() const override                     { return true; }
    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor (*this); }

    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    const juce::String getProgramName (int) override    { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        juce::XmlElement xml ("Params");
        for (auto* p : getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
                xml.setAttribute (r->getParameterID(), (double) r->convertFrom0to1 (r->getValue()));
        writeExtraState (xml);
        copyXmlToBinary (xml, dest);
    }

    void setStateInformation (const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary (data, size))
        {
            for (auto* p : getParameters())
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
                    if (xml->hasAttribute (r->getParameterID()))
                        r->setValueNotifyingHost (r->convertTo0to1 ((float) xml->getDoubleAttribute (r->getParameterID())));
            readExtraState (*xml);
        }
    }

protected:
    virtual void writeExtraState (juce::XmlElement&) {}
    virtual void readExtraState (const juce::XmlElement&) {}

    juce::AudioParameterFloat* addFloat (const juce::String& id, const char* name,
                                         float min, float max, float def,
                                         const char* unit = "", float skewCentre = 0.0f)
    {
        juce::NormalisableRange<float> range (min, max);
        if (skewCentre > min && skewCentre < max)
            range.setSkewForCentre (skewCentre);

        auto* p = new juce::AudioParameterFloat (juce::ParameterID { id, 1 }, L (name), range, def,
                                                 juce::AudioParameterFloatAttributes().withLabel (T (unit)));
        addParameter (p);
        return p;
    }

    juce::AudioParameterBool* addBool (const juce::String& id, const char* name, bool def)
    {
        auto* p = new juce::AudioParameterBool (juce::ParameterID { id, 1 }, L (name), def);
        addParameter (p);
        return p;
    }

    juce::AudioParameterInt* addInt (const juce::String& id, const char* name, int min, int max, int def, const char* unit = "")
    {
        auto* p = new juce::AudioParameterInt (juce::ParameterID { id, 1 }, L (name), min, max, def,
                                               juce::AudioParameterIntAttributes().withLabel (T (unit)));
        addParameter (p);
        return p;
    }

    juce::AudioParameterChoice* addChoice (const juce::String& id, const char* name,
                                           std::initializer_list<const char*> choices, int def = 0)
    {
        juce::StringArray items;
        for (auto* c : choices)
            items.add (L (c));
        auto* p = new juce::AudioParameterChoice (juce::ParameterID { id, 1 }, L (name), items, def);
        addParameter (p);
        return p;
    }

    double currentSampleRate = 48000.0;

private:
    const juce::String displayName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BuiltinEffect)
};
