#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "app/AppPaths.h"
#include <juce_audio_basics/juce_audio_basics.h>

ToneTabPluginProcessor::ToneTabPluginProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    juce::PropertiesFile::Options opts;
    opts.applicationName = "plugin";
    opts.folderName = "Tone_Tab";
    opts.filenameSuffix = ".xml";
    opts.osxLibrarySubFolder = "Application Support";
    appProperties.setStorageParameters (opts);
    settings = appProperties.getUserSettings();

    if (const auto root = settings->getValue ("library.root"); root.isNotEmpty())
        AppPaths::setLibraryRoot (juce::File (root));
    else if (const auto perUser = AppPaths::userDataDir().getChildFile ("assets"); perUser.isDirectory())
        AppPaths::setLibraryRoot (perUser);

    engine.setPracticeEnabled (false);
    engine.setMonitoring (true);

    startTimerHz (20);
}

ToneTabPluginProcessor::~ToneTabPluginProcessor()
{
    stopTimer();
    engine.releaseAllChains();
}

void ToneTabPluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int maxBlock = juce::jmax (samplesPerBlock, 8192);
    mono.assign ((size_t) maxBlock, 0.0f);
    tempRight.assign ((size_t) maxBlock, 0.0f);

    engine.setPracticeEnabled (false);
    engine.prepareForPlugin (sampleRate, samplesPerBlock);
    setLatencySamples (0);
}

void ToneTabPluginProcessor::releaseResources()
{
}

bool ToneTabPluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();

    if (out != juce::AudioChannelSet::stereo())
        return false;

    return in == juce::AudioChannelSet::stereo()
        || in == juce::AudioChannelSet::mono()
        || in == juce::AudioChannelSet::disabled();
}

void ToneTabPluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    if (n <= 0)
        return;

    if ((int) mono.size() < n) mono.resize ((size_t) n);
    if ((int) tempRight.size() < n) tempRight.resize ((size_t) n);

    const float* inL = numIn > 0 ? buffer.getReadPointer (0) : nullptr;
    const float* inR = numIn > 1 ? buffer.getReadPointer (1) : inL;

    for (int i = 0; i < n; ++i)
    {
        if (inL == nullptr)
            mono[(size_t) i] = 0.0f;
        else if (inR == nullptr || inR == inL)
            mono[(size_t) i] = inL[i];
        else
            mono[(size_t) i] = 0.5f * (inL[i] + inR[i]);
    }

    if (numOut > 0)
    {
        auto* outL = buffer.getWritePointer (0);
        auto* outR = numOut > 1 ? buffer.getWritePointer (1) : tempRight.data();
        engine.processPluginBlock (mono.data(), outL, outR, n);
    }

    for (int ch = 2; ch < numOut; ++ch)
        buffer.clear (ch, 0, n);
}

void ToneTabPluginProcessor::timerCallback()
{
    chain.collectGarbage();

    const int latency = engine.getChainLatencySamples();
    if (latency != getLatencySamples())
    {
        setLatencySamples (latency);
        updateHostDisplay();
    }
}

juce::AudioProcessorEditor* ToneTabPluginProcessor::createEditor()
{
    return new ToneTabPluginEditor (*this);
}

void ToneTabPluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = chain.toXml())
        copyXmlToBinary (*xml, destData);
}

void ToneTabPluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        juce::StringArray problems;
        chain.loadFromXml (*xml, problems);
    }
}

// This is the entry point JUCE uses to create the plug-in instance.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ToneTabPluginProcessor();
}
