#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include "drums/DrumMachine.h"

// Full drum page: visible style grid, step-pattern display, transport, feel
// controls and a per-piece mixer.
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
    void setStyle (int index);
    void applyStyleDefaults();
    void save();
    void updatePlayButton();
    void updateStyleButtons();
    void applyMixer();
    void paintPattern (juce::Graphics&, juce::Rectangle<int> area);

    DrumMachine& drums;
    juce::PropertiesFile& settings;
    int selectedStyle = 0;

    juce::Label title, status;
    juce::Label styleHeader, rockPopLabel, grooveLabel, metalLabel, otherLabel;
    juce::Label patternHeader, feelHeader, mixerHeader;

    juce::TextButton playButton, fillNowButton, tapButton, verseButton, chorusButton;
    juce::OwnedArray<juce::TextButton> styleButtons;
    juce::ComboBox fillBox;
    juce::ToggleButton countInToggle, metronomeToggle;

    juce::Label bpmLabel, swingLabel, volumeLabel, humanizeLabel;
    juce::Slider bpmSlider, swingSlider, volumeSlider, humanizeSlider;

    juce::OwnedArray<juce::Label> mixLabelOwned;
    juce::OwnedArray<juce::Slider> mixSliderOwned;
    std::array<juce::Label*, 5> mixLabels {};
    std::array<juce::Slider*, 5> mixSliders {};

    juce::Rectangle<int> patternArea;
    juce::Array<double> tapTimes;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumPanel)
};
