#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

class DrumMachine;

// Where a loop starts and ends. The space bar never has to be frame-accurate:
// the audio thread snaps the action to the musical grid at the current tempo.
enum class LoopQuantise { bar = 0, beat = 1, off = 2 };

// =============================================================================
// Live loop station.
//
//   idle -> (SPACE) -> recording -> (SPACE) -> playing -> (SPACE) -> overdubbing
//                                                   \-> (SPACE) -> playing
//
// When the drum machine is playing the musical grid is the drum machine's bar
// grid, so a loop recorded over the drums lines up with them. Without drums the
// looper free-runs: recording starts on the press but the length is rounded to
// a whole number of bars, so a late / early closing press still gives a clean
// loop.
//
// The audio thread owns the audio buffer and all timing. The message thread
// only sets request flags and reads atomics.
// =============================================================================
class Looper
{
public:
    enum class State
    {
        empty,          // no loop, nothing playing
        armingRecord,   // waiting for the next bar before recording
        recording,      // recording the first layer
        playing,        // looping
        armedOverdub,   // waiting for the loop start to overdub
        overdubbing,    // adding a layer to the loop
        stopped,        // a loop exists but is not playing
        armingPlay      // waiting for the next bar to start playback
    };

    Looper() = default;

    // ---- message thread -----------------------------------------------------
    void setQuantise (LoopQuantise q) noexcept  { quantise.store (q); }
    LoopQuantise getQuantise() const noexcept   { return quantise.load(); }
    void setFixedLengthBars (int bars) noexcept { fixedBars.store (juce::jlimit (0, 8, bars)); }
    int  getFixedLengthBars() const noexcept    { return fixedBars.load(); }
    void setLevelDb (float db) noexcept         { levelDb.store (juce::jlimit (-60.0f, 6.0f, db)); }
    float getLevelDb() const noexcept           { return levelDb.load(); }

    // Space bar. stop() freezes the loop, clear() erases it.
    void toggle() noexcept                      { toggleRequested.store (true); }
    void stop() noexcept                        { stopRequested.store (true); }
    void clear() noexcept                       { clearRequested.store (true); }

    // ---- UI state -----------------------------------------------------------
    State  getState() const noexcept            { return state.load(); }
    bool   hasLoop() const noexcept             { return loopFrames.load() > 0; }
    int    getLayerCount() const noexcept       { return layers.load(); }
    double getLengthSeconds() const noexcept    { return lengthSeconds.load(); }
    double getPosition() const noexcept         { return position.load(); }
    double getRecordedSeconds() const noexcept  { return recordedSeconds.load(); }
    double getLoopBpm() const noexcept          { return loopBpm.load(); }
    double getLoopBars() const noexcept         { return loopBars.load(); }

    // Downsampled recording envelope for the UI waveform (one peak per ~10 ms).
    static constexpr int waveformBuckets = 3000;
    int   getWaveformCount() const noexcept     { return waveformCount.load (std::memory_order_relaxed); }
    float getWaveformPeak (int index) const noexcept
    {
        return waveform != nullptr && juce::isPositiveAndBelow (index, waveformBuckets)
                   ? (*waveform)[(size_t) index].load (std::memory_order_relaxed) : 0.0f;
    }
    double getWaveformBucketSeconds() const noexcept
    {
        return (double) waveformBucketSamples.load (std::memory_order_relaxed) / sampleRate;
    }

    // Continuously running input envelope (the "what am I playing" lane).
    static constexpr int liveBuckets = 512;
    uint32_t getLiveWritePosition() const noexcept { return liveWritePos.load (std::memory_order_relaxed); }
    float getLivePeakAt (uint32_t absoluteIndex) const noexcept
    {
        return livePeaks != nullptr
                   ? (*livePeaks)[(size_t) (absoluteIndex & (uint32_t) (liveBuckets - 1))]
                         .load (std::memory_order_relaxed)
                   : 0.0f;
    }
    bool   isWaiting() const noexcept
    {
        const auto s = getState();
        return s == State::armingRecord || s == State::armingPlay;
    }

    // ---- audio thread -------------------------------------------------------
    // Called while audio is stopped (device start / offline setup).
    void prepare (double newSampleRate, int blockSize, bool shouldEnable);
    // Copy the guitar (post-effects, post-guitar-level) for this block.
    void capture (const float* left, const float* right, int numSamples) noexcept;
    // Mix the loop into the output. Must be called after DrumMachine::render
    // for the same block so the drum grid phase is current.
    void render (float* left, float* right, int numSamples, const DrumMachine& drums) noexcept;

private:
    void handleToggle (bool drumsOn, int beatsPerBar, double bpm, double gridBeats, int64_t unit);
    void beginRecording (double bpm, int beatsPerBar);
    void requestClose (int64_t unit, bool drumsOn);
    void finishRecording (int64_t wantedFrames, bool drumsOn);
    void startPlayback (int64_t fromFrame);
    void discardRecording();
    void setCurrent (State s);
    void publishState();
    int64_t unitSamples (int beatsPerBar, double bpm) const noexcept;
    int64_t samplesToNextBoundary (double gridBeats, int beatsPerBar, double bpm) const noexcept;
    void resetWaveform() noexcept;
    void pushWaveformPeak (float peak, int bucketSamples) noexcept;

    static constexpr double maxSeconds = 30.0;

    // Message-thread settings and requests.
    std::atomic<LoopQuantise> quantise { LoopQuantise::bar };
    std::atomic<int>   fixedBars { 0 };
    std::atomic<float> levelDb { -3.0f };
    std::atomic<bool>  toggleRequested { false }, stopRequested { false }, clearRequested { false };

    // Published for the UI.
    std::atomic<State>  state { State::empty };
    std::atomic<int64_t> loopFrames { 0 };
    std::atomic<int>    layers { 0 };
    std::atomic<double> lengthSeconds { 0.0 }, position { 0.0 }, recordedSeconds { 0.0 };
    std::atomic<double> loopBpm { 0.0 }, loopBars { 0.0 };
    // Heap-owned so AudioEngine stays small enough to live on a stack frame
    // (the offline tests allocate it as a local).
    using WaveArray = std::array<std::atomic<float>, waveformBuckets>;
    using LiveArray = std::array<std::atomic<float>, liveBuckets>;
    std::unique_ptr<WaveArray> waveform;
    std::unique_ptr<LiveArray> livePeaks;
    std::atomic<int> waveformCount { 0 };
    std::atomic<int> waveformBucketSamples { 480 };
    std::atomic<uint32_t> liveWritePos { 0 };

    // Audio buffers (owned by the audio thread while running).
    juce::AudioBuffer<float> loopBuffer, inputBuffer;
    double sampleRate = 48000.0;
    int capacity = 0;
    bool enabled = false;

    // Audio-thread state.
    State current = State::empty;
    int64_t loopLen = 0;
    int64_t recordFrames = 0;
    int64_t closeTarget = 0;
    int64_t fixedTarget = 0;
    bool closePending = false;
    bool clearPending = false;
    int64_t playPos = 0;
    int64_t armSamples = 0;
    double recordBpm = 120.0;
    int recordBeatsPerBar = 4;
    double freeBeats = 0.0;
    float fadeGain = 0.0f;
    int captured = 0;
    int waveWrite = 0, waveFill = 0;
    float wavePeak = 0.0f;
    uint32_t liveWrite = 0;
    int liveFill = 0;
    float livePeak = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Looper)
};
