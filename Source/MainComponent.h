#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "AudioEngine.h"
#include "PitchDetector.h"
#include "Widgets.h"
#include "fx/EffectChain.h"
#include "ui/PedalBoard.h"
#include "ui/PresetBar.h"
#include "ui/DrumPanel.h"
#include "ui/PedalEditor.h"
#include "ui/PracticePanel.h"
#include "ui/RecordingPanel.h"
#include "ui/StatsView.h"

class MainComponent : public juce::Component,
                      public juce::FileDragAndDropTarget,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    enum class View { tone, practice, drums, record, stats };
    void setView (View v);
    PracticePanel& getPracticePanel()    { return *practicePanel; }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void openAudioSettings();
    void showPedalEditor (EffectSlotPtr slot);
    void closePedalEditor();
    void openPluginManager();
    void refreshInputChannelList();
    void refreshStatus();
    void saveSettings();

    juce::ApplicationProperties appProperties;
    juce::AudioDeviceManager deviceManager;
    AudioEngine engine;
    EffectChain chain { engine };
    PitchDetector pitchDetector;
    std::vector<float> tunerBuffer;
    juce::Array<int> channelForComboId;

    juce::Label titleLabel, authorLabel, statusLabel, latencyLabel, hintLabel;
    juce::TextButton settingsButton;
    juce::ComboBox languageBox;
    juce::Label channelLabel, inputGainLabel, outputGainLabel;
    juce::ComboBox channelBox;
    juce::Slider inputGainSlider, outputGainSlider;
    juce::ToggleButton monitorToggle;
    LevelMeter inputMeter, outputMeter;
    TunerDisplay tuner;

    std::unique_ptr<PresetBar> presetBar;
    std::unique_ptr<PedalBoard> pedalBoard;
    std::unique_ptr<DrumPanel> drumPanel;
    std::unique_ptr<PedalEditorOverlay> pedalOverlay;
    std::unique_ptr<PracticePanel> practicePanel;
    std::unique_ptr<RecordingPanel> recordingPanel;
    juce::TextButton toneViewButton, practiceViewButton, drumsViewButton, recordViewButton, statsViewButton;
    PracticeLog practiceLog { PracticeLog::defaultFile() };
    std::unique_ptr<StatsView> statsView;
    View view = View::tone;
    juce::Label chainLabel;

    juce::Rectangle<int> leftPanel, fxPanel, drumPanelArea, practiceArea;
    int frameCounter = 0;
    bool chainDirty = false;

    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
