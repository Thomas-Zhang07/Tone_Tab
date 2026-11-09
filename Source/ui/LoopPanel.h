#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "looper/Looper.h"
#include "drums/DrumMachine.h"

// Loop station page.
//
// A large waveform timeline is the main surface: click to move the playhead,
// drag to make a selection, then trim to it. The loop can also be extended by
// any number of whole bars, which records the new section live when the
// playhead reaches the old end. A configurable count-in with a beat countdown
// starts recording on the grid, and saved loops keep a text note.
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

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    void toggleLoop();
    void stopLoop();
    void refresh();

private:
    void timerCallback() override;
    void updateControls();
    void saveSettings();
    void applyQuantise();
    void applyFixedLength();
    void applyCountIn();
    void applyExtendBars();
    void updateLengthHint();

    void paintTimeline (juce::Graphics&, juce::Rectangle<int> area);
    void paintCountIn (juce::Graphics&, juce::Rectangle<int> area);
    static juce::Colour stateColour (Looper::State s);

    void trimSelection();
    void extendLoop();
    void undoEdit();
    void saveLoop();
    void updateNotesFile();
    void openLoopsFolder();
    void clearSelection();

    bool hasSelection() const noexcept;
    double selectionStart() const noexcept;
    double selectionEnd() const noexcept;
    double xToSeconds (int x) const noexcept;
    float secondsToX (double seconds) const noexcept;
    double snapTime (double seconds, bool free) const noexcept;
    void updateSelectionLabel();

    Looper& looper;
    DrumMachine& drums;
    juce::PropertiesFile& settings;

    juce::Label title, status, tempoLabel, quantiseLabel, lengthLabel, lengthHint,
                countInLabel, levelLabel, continueLabel, notesLabel,
                savedLabel, selectionLabel;
    juce::TextButton mainButton, stopButton, clearButton, undoButton, trimButton,
                     extendButton, saveButton, openFolderButton;
    juce::ComboBox quantiseBox, lengthBox;
    juce::Slider tempoSlider, levelSlider, countInSlider, extendBarsSlider;
    juce::TextEditor notesEditor;

    juce::Rectangle<int> headerArea, transportArea, timelineArea, editArea, hintArea,
                         controlsArea, notesArea;
    juce::Rectangle<int> loopLane, liveLane, rulerArea;
    int laneX = 0, laneW = 1;
    double displaySecs = 1.0;

    double selStart = -1.0, selEnd = -1.0;
    double dragAnchor = 0.0;
    int dragMode = 0;          // 0 none, 1 new selection, 2 left edge, 3 right edge
    bool dragged = false;
    int mouseDownX = 0;

    juce::File savedFolder;
    juce::String lastSavedNotes;
    int64_t savedRevision = -1;
    bool notesDirty = false;
    double notesDirtyAt = 0.0;
    bool updatingNotes = false;

    Looper::State lastState = Looper::State::empty;
    int64_t lastLoopId = -1;
    int lastCountIn = -1;
    juce::String statusText;
    bool spaceHeld = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopPanel)
};
