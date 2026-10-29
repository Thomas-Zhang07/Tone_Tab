#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "AudioEngine.h"
#include "TakeTimeline.h"

// Dedicated recording view: record the full mix + dry stem, browse takes, replay
// the dry stem through the current chain.
class RecordingPanel : public juce::Component,
                       private juce::ListBoxModel,
                       private juce::Timer
{
public:
    RecordingPanel (AudioEngine& engineToUse, EffectChain& chainToUse,
                    juce::PropertiesFile& settingsToUse);
    ~RecordingPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void focusLost (FocusChangeType) override;

    TakeTimeline& getTimeline() { return timeline; }
    void togglePlayPause();

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void selectedRowsChanged (int) override;

private:
    struct Take
    {
        juce::File mixFile;
        Recorder::Info info;
    };

    void timerCallback() override;
    void refreshTakes();
    void loadSelectedTake();
    void toggleRecord();
    void startReplayFrom (double seconds);
    void pauseReplay();
    void stopReplay();
    void togglePunch();
    void seekReplayTo (double seconds);
    void exportSelectedTake();
    void deleteSelected();
    void openFolder();
    void chooseFolder();
    void updateControls();

    AudioEngine& engine;
    Recorder& recorder;
    TabPlayer& player;
    EffectChain& effectChain;
    juce::PropertiesFile& settings;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::FileChooser> exportChooser;

    std::vector<Take> takes;

    juce::Label title, status, hint;
    juce::TextButton recordButton, replayButton, pauseButton, stopButton, punchButton,
                     deleteButton, refreshButton, folderButton, chooseFolderButton, exportButton;
    juce::ComboBox exportModeBox;
    juce::ToggleButton fullMixToggle;
    juce::ListBox list;
    TakeTimeline timeline;
    double selectionStart = -1.0, selectionEnd = -1.0;
    double resumeSeconds = 0.0;   // playhead kept by Pause
    bool spaceHeld = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecordingPanel)
};
