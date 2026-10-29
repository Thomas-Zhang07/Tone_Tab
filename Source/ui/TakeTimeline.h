#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <functional>
#include <utility>
#include <vector>

// Waveform timeline for one take. Click to seek, drag to select a region.
// The WAV is scanned on a background thread so long takes do not freeze the UI.
class TakeTimeline : public juce::Component,
                     private juce::Thread
{
public:
    TakeTimeline();
    ~TakeTimeline() override;

    void setFile (const juce::File& wav, double fallbackLengthSeconds = 0.0);
    void clearFile();
    void setPlayPosition (double seconds, bool isRecording = false, double recordedSeconds = 0.0);

    double getLengthSeconds() const noexcept   { return lengthSeconds; }
    bool hasSelection() const noexcept         { return regionEnd > regionStart + 0.01; }
    double getSelectionStart() const noexcept  { return regionStart; }
    double getSelectionEnd() const noexcept    { return regionEnd; }
    void setSelection (double startSeconds, double endSeconds);
    void clearSelection();

    // Non-destructive punch layers stored in take.json.
    void setPunchRegions (const std::vector<std::pair<double, double>>& regions);
    // The punch currently being recorded (drawn in red with a live head).
    void setPunchState (bool active, double startSeconds, double endSeconds);

    // Real-time input waveform while a new take is recording. The provider
    // reads the Recorder's live envelope ring.
    using PeakProvider = std::function<float (uint32_t)>;
    void setRecordingLive (bool active, double recordedSeconds, uint32_t writePos,
                           double bucketSeconds, PeakProvider peakAt);

    std::function<void (double)> onSeek;
    std::function<void (double, double)> onSelectionChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void run() override;
    double xToSeconds (float x) const noexcept;
    float secondsToX (double seconds, double displayLength) const noexcept;
    void applyLoad (std::vector<float>&& newPeaks, double length, const juce::String& error,
                    int generation);

    juce::File file;
    std::vector<float> peaks;
    double lengthSeconds = 0.0;
    double playSeconds = 0.0;
    double recordSeconds = 0.0;
    double regionStart = -1.0, regionEnd = -1.0;
    bool recording = false, loading = false;
    bool dragging = false;
    float dragStartX = 0.0f;
    juce::String message;
    std::atomic<int> loadGeneration { 0 };

    std::vector<std::pair<double, double>> punchRegions;
    bool punchActive = false;
    double punchStart = 0.0, punchEnd = 0.0;

    bool liveActive = false;
    uint32_t liveWrite = 0;
    double liveBucketSeconds = 0.01;
    PeakProvider livePeakAt;

    juce::Rectangle<int> headerArea, rulerArea, plotArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TakeTimeline)
};
