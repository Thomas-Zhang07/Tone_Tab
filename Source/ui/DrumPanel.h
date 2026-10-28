#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "drums/DrumMachine.h"

class DrumPanel : public juce::Component, private juce::Timer
{
public:
    DrumPanel (DrumMachine& drumsToUse, juce::PropertiesFile& settingsToUse);
    ~DrumPanel() override;

    void togglePlay();
    void tapTempo();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void applyStyleDefaults();
    void save();
    void updatePlayButton();

    DrumMachine& drums;
    juce::PropertiesFile& settings;

    juce::Label title;
    juce::TextButton playButton, verseButton, chorusButton, fillNowButton, tapButton;
    juce::ComboBox styleBox, fillBox;
    juce::Label bpmLabel, swingLabel, volumeLabel, humanizeLabel;
    juce::Slider bpmSlider, swingSlider, volumeSlider, humanizeSlider;
    juce::ToggleButton countInToggle, metronomeToggle;

    juce::Rectangle<int> beatArea;
    juce::Array<double> tapTimes;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumPanel)
};
