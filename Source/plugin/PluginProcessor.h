#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_data_structures/juce_data_structures.h>
#include <vector>
#include "AudioEngine.h"
#include "fx/EffectChain.h"

// VST3 wrapper around the tone chain. Practice, tabs, drums and recording live in
// the standalone app; this processor runs the effect chain and presets only.
class ToneTabPluginProcessor : public juce::AudioProcessor,
                               private juce::Timer
{
public:
    ToneTabPluginProcessor();
    ~ToneTabPluginProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                       { return true; }

    const juce::String getName() const override           { return "Tone_Tab"; }
    bool acceptsMidi() const override                     { return false; }
    bool producesMidi() const override                    { return false; }
    bool isMidiEffect() const override                    { return false; }
    double getTailLengthSeconds() const override          { return 0.0; }

    int getNumPrograms() override                         { return 1; }
    int getCurrentProgram() override                      { return 0; }
    void setCurrentProgram (int) override                 {}
    const juce::String getProgramName (int) override      { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    AudioEngine& getEngine()                 { return engine; }
    EffectChain& getChain()                  { return chain; }
    juce::PropertiesFile& getSettings()      { return *settings; }

private:
    void timerCallback() override;

    juce::ApplicationProperties appProperties;
    juce::PropertiesFile* settings = nullptr;

    AudioEngine engine;
    EffectChain chain { engine };

    std::vector<float> mono, tempRight;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToneTabPluginProcessor)
};
