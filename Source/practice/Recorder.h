#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <memory>
#include <vector>

class TabPlayer;

// =============================================================================
// Recording and replay.
//
// A take is a folder:
//   mix.wav   stereo, guitar + effects + backing + drums (what you hear)
//   dry.wav   mono, raw guitar before input gain (for re-amping)
//   take.json song/track/speed/loop/count-in/sync + chain latency
//
// The audio thread only copies samples and touches atomics. Disk writing happens
// on two background writer threads.
// =============================================================================
class Recorder
{
public:
    struct Info
    {
        int version = 2;
        juce::File songFile;
        juce::String songTitle;
        int track = 0;
        float speed = 1.0f;
        bool loopOn = false, countIn = true, metronome = false, waitMode = false;
        double loopA = 0.0, loopB = 0.0;
        bool synced = false;
        int64_t syncSample = 0;
        double syncBeat = 0.0;
        double sampleRate = 48000.0;
        double lengthSeconds = 0.0;
        int chainLatencySamples = 0;
        juce::File mixFile, dryFile;
        juce::Time created;

        juce::var toVar() const;
        static Info fromVar (const juce::var& v);
        static Info loadFor (const juce::File& wav);
    };

    explicit Recorder (TabPlayer& player);
    ~Recorder();

    static juce::File getFolder();

    // ---- message thread -----------------------------------------------------
    void setSampleRate (double sr);
    void setChainLatencySamples (int samples) noexcept;

    bool startRecording (const Info& info, juce::String& error);
    juce::File stopRecording();                 // returns mix.wav
    bool isRecording() const                    { return recording.load(); }
    double getRecordedSeconds() const;

    bool startPlayback (const juce::File& wav, juce::String& error, Info* infoOut = nullptr);
    void stopPlayback();
    bool isPlayingBack() const                  { return playbackOn.load(); }
    double getPlaybackSeconds() const;
    double getPlaybackLength() const            { return playbackLength; }
    void releaseRetired();

    // ---- audio thread -------------------------------------------------------
    bool replaceInput (float* mono, int n) noexcept;
    void capture (const float* mono, int n, bool tabPlaying, double tabBeatBeforeRender) noexcept;
    void captureMix (const float* left, const float* right, int n) noexcept;

private:
    TabPlayer& player;

    juce::TimeSliceThread mixThread { "Tone_Tab mix writer" };
    juce::TimeSliceThread dryThread { "Tone_Tab dry writer" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> mixWriter, dryWriter;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> activeMix { nullptr }, activeDry { nullptr };
    std::atomic<int> captureUsers { 0 };

    std::atomic<bool> recording { false };
    std::atomic<int64_t> samplesRecorded { 0 };
    std::atomic<bool> syncCaptured { false };
    std::atomic<int64_t> syncSample { 0 };
    std::atomic<double> syncBeat { 0.0 };
    std::atomic<int> chainLatency { 0 };

    bool lastTabPlaying = false;
    Info pendingInfo;
    juce::File takeFolder, mixFile, dryFile;
    double recordRate = 48000.0;
    std::atomic<double> deviceRate { 0.0 };

    std::vector<float> dryDelay, dryScratch;
    uint32_t dryDelayPos = 0;

    // Playback.
    std::shared_ptr<juce::AudioBuffer<float>> playbackData;
    std::vector<std::shared_ptr<juce::AudioBuffer<float>>> retired;
    std::atomic<juce::AudioBuffer<float>*> playbackPtr { nullptr }, playbackInUse { nullptr };
    std::atomic<bool> playbackOn { false };
    std::atomic<int64_t> playbackPos { 0 };
    std::atomic<bool> playbackSync { false };
    std::atomic<int64_t> playbackSyncSample { 0 };
    std::atomic<double> playbackSyncBeat { 0.0 };
    double playbackRate = 48000.0, playbackLength = 0.0;
};
