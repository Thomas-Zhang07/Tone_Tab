#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include "PluginProcessor.h"
#include "ui/PresetBar.h"
#include "ui/PedalBoard.h"
#include "ui/PedalEditor.h"

class ToneTabPluginEditor : public juce::AudioProcessorEditor
{
public:
    explicit ToneTabPluginEditor (ToneTabPluginProcessor& processor);
    ~ToneTabPluginEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void showOverlay (EffectSlotPtr slot);
    void closeOverlay();

    ToneTabPluginProcessor& proc;
    juce::Label title, note;
    PresetBar presetBar;
    PedalBoard board;
    std::unique_ptr<PedalEditorOverlay> overlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToneTabPluginEditor)
};
