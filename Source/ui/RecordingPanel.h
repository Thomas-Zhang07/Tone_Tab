#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "AudioEngine.h"

// Dedicated recording view: record the full mix + dry stem, browse takes, replay
// the dry stem through the current chain.
class RecordingPanel : public juce::Component,
                       private juce::ListBoxModel,
                       private juce::Timer
{
public:
    RecordingPanel (AudioEngine& engineToUse, juce::PropertiesFile& settingsToUse);
    ~RecordingPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

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
    void toggleRecord();
    void toggleReplay();
    void deleteSelected();
    void openFolder();
    void chooseFolder();
    void updateControls();

    AudioEngine& engine;
    Recorder& recorder;
    TabPlayer& player;
    juce::PropertiesFile& settings;
    std::unique_ptr<juce::FileChooser> chooser;

    std::vector<Take> takes;

    juce::Label title, status, hint;
    juce::TextButton recordButton, replayButton, deleteButton, refreshButton, folderButton, chooseFolderButton;
    juce::ListBox list;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecordingPanel)
};
