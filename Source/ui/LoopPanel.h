#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "looper/Looper.h"
#include "drums/DrumMachine.h"

// Loop station page. Space is the one switch: record -> close -> overdub,
// with every boundary snapped to the bar / beat grid.
class LoopPanel : public juce::Component,
                  private juce::Timer
{
public:
    LoopPanel (Looper& looperToUse, DrumMachine& drumsToUse, juce::PropertiesFile& settingsToUse);
    ~LoopPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void focusLost (FocusChangeType) override;

    void toggleLoop();
    void stopLoop();
    void refresh();

private:
    void timerCallback() override;
    void updateControls();
    void save();
    void applyQuantise();
    void applyFixedLength();
    void paintLoopDisplay (juce::Graphics&, juce::Rectangle<int> area);
    void paintTimeline (juce::Graphics&, juce::Rectangle<int> area);
    static juce::Colour stateColour (Looper::State s);

    Looper& looper;
    DrumMachine& drums;
    juce::PropertiesFile& settings;

    juce::Label title, status, tempoLabel, quantiseLabel, lengthLabel, levelLabel;
    juce::TextButton mainButton, stopButton, clearButton;
    juce::ComboBox quantiseBox, lengthBox;
    juce::Slider tempoSlider, levelSlider;

    juce::Rectangle<int> loopDisplay, ringArea, timelineArea, controlsArea, hintArea, tipsArea;
    juce::String statusText, hintText;
    Looper::State lastState = Looper::State::empty;
    bool spaceHeld = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopPanel)
};
