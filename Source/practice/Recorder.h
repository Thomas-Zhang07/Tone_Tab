#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
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
//   clips/    optional non-destructive punch layers (dry), listed in take.json
//   take.json song/track/speed/loop/count-in/sync + chain latency + punch list
//
// The audio thread only copies samples and touches atomics. Disk writing happens
// on two background writer threads.
// =============================================================================
class Recorder
{
public:
    // A non-destructive punch recorded over a region of the take. The base
    // dry.wav is never modified; clips are applied in order when playing and
    // exporting (later clips win).
    struct PunchClip
    {
        juce::File file;            // resolved absolute path
        juce::String relativePath;  // stored in take.json (relative to the take folder)
        double startSeconds = 0.0;
        double endSeconds = 0.0;
        double sampleRate = 0.0;
    };

    struct Info
    {
        int version = 3;
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
        std::vector<PunchClip> punches;

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

    // fullMix plays the recorded mix.wav directly (drums + backing + loop),
    // instead of the dry stem through the current chain.
    bool startPlayback (const juce::File& wav, juce::String& error,
                        Info* infoOut = nullptr, double startSeconds = 0.0,
                        bool fullMix = false);
    // Adds the recorded full mix to the output during full-mix playback.
    void renderFullMix (float* left, float* right, int n) noexcept;
    void stopPlayback();
    // Move the replay head while playing. Safe to call from the message thread.
    void setPlaybackPosition (double seconds) noexcept;
    bool isPlayingBack() const                  { return playbackOn.load(); }
    double getPlaybackSeconds() const;
    double getPlaybackLength() const            { return playbackLength; }
    void releaseRetired();

    // ---- non-destructive punch ---------------------------------------------
    // Replays the take from two seconds before startSeconds; the live input is
    // recorded from startSeconds to endSeconds and the base is muted during the
    // punch so you hear yourself, not the old part.
    bool startPunchRecording (const juce::File& takeFile, double startSeconds,
                              double endSeconds, juce::String& error);
    void stopPunchRecording();                  // stop early; keep what was recorded
    bool isPunchRecording() const               { return punchRecording.load(); }
    bool hasPunchFinished() const               { return punchFinished.load(); }
    double getPunchRecordedSeconds() const;
    double getPunchStartSeconds() const         { return punchStartSec; }
    double getPunchEndSeconds() const           { return punchEndSec; }
    // Finalize the clip and write it into take.json. Call after hasPunchFinished().
    juce::File finishPunchRecording();

    // Load a take's dry stem with all punch clips applied. Used by replay and
    // export so both hear exactly the same non-destructive edit.
    static std::shared_ptr<juce::AudioBuffer<float>> loadEffectiveDry (
        const juce::File& takeFile, juce::String& error,
        Info* infoOut = nullptr, double* rateOut = nullptr);

    // ---- live input waveform (the Record view's real-time lane) ------------
    static constexpr int liveWaveBuckets = 2048;   // ~20 s at 10 ms
    uint32_t getLiveWaveWrite() const noexcept  { return liveWaveWrite.load (std::memory_order_relaxed); }
    float getLiveWavePeak (uint32_t absoluteIndex) const noexcept
    {
        return liveWave != nullptr
                   ? (*liveWave)[(size_t) (absoluteIndex & (uint32_t) (liveWaveBuckets - 1))]
                         .load (std::memory_order_relaxed)
                   : 0.0f;
    }
    double getLiveWaveBucketSeconds() const noexcept
    {
        return (double) liveBucketSamples.load (std::memory_order_relaxed) / juce::jmax (1.0, deviceRate.load());
    }

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

    // Live input envelope for the real-time record lane.
    using LiveWaveArray = std::array<std::atomic<float>, liveWaveBuckets>;
    std::unique_ptr<LiveWaveArray> liveWave;
    std::atomic<uint32_t> liveWaveWrite { 0 };
    std::atomic<int> liveBucketSamples { 480 };
    uint32_t liveWriteIndex = 0;
    int liveFill = 0;
    float livePeak = 0.0f;
    void updateLiveEnvelope (const float* mono, int n) noexcept;

    // Playback.
    std::shared_ptr<juce::AudioBuffer<float>> playbackData;
    std::vector<std::shared_ptr<juce::AudioBuffer<float>>> retired;
    std::atomic<juce::AudioBuffer<float>*> playbackPtr { nullptr }, playbackInUse { nullptr };
    std::atomic<bool> playbackOn { false };
    std::atomic<bool> fullMixPlaying { false };
    std::atomic<int64_t> playbackPos { 0 }, pendingSeek { -1 }, fullMixBlockStart { 0 };
    std::atomic<bool> playbackSync { false };
    std::atomic<int64_t> playbackSyncSample { 0 };
    std::atomic<double> playbackSyncBeat { 0.0 };
    double playbackRate = 48000.0, playbackLength = 0.0;

    // Punch recording.
    std::atomic<bool> punchRecording { false }, punchFinished { false };
    std::atomic<int64_t> punchStartSample { 0 }, punchEndSample { 0 }, punchSamplesWritten { 0 };
    std::atomic<int> punchWriteUsers { 0 };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> punchWriter;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> activePunch { nullptr };
    std::vector<float> punchScratch;
    juce::File punchFile;
    Info punchInfo;
    double punchStartSec = 0.0, punchEndSec = 0.0, punchClipRate = 48000.0;

    void handlePunchBlock (float* mono, juce::AudioBuffer<float>& base, int n) noexcept;
};
