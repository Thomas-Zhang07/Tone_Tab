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
//   idle -> (SPACE) -> arming/count-in -> recording -> (SPACE) -> playing
//                                                        \-> (SPACE) -> overdub
//
// A new take waits for the next grid line plus an adjustable count-in. The
// number of beats remaining before recording starts is published for the UI.
//
// The audio thread owns the audio buffer and all timing. The message thread
// sets request flags, and the few edits that touch the buffer (trim / undo /
// copying for save) hold bufferLock. The audio thread uses a try-lock so it
// never blocks: it simply skips one block of loop output while an edit is
// being applied.
// =============================================================================
class Looper
{
public:
    enum class State
    {
        empty,          // no loop, nothing playing
        armingRecord,   // counting in before recording
        recording,      // recording the first layer
        playing,        // looping
        armedOverdub,   // waiting for the loop start to overdub
        overdubbing,    // adding a layer to the loop
        stopped,        // a loop exists but is not playing
        armingPlay,     // counting in before playback
        extending       // recording extra bars onto the end of the loop
    };

    Looper() = default;

    // ---- message thread -----------------------------------------------------
    void setQuantise (LoopQuantise q) noexcept  { quantise.store (q); }
    LoopQuantise getQuantise() const noexcept   { return quantise.load(); }
    // 0 = auto (close with a second press), 1..64 bars = fixed length.
    void setFixedLengthBars (int bars) noexcept { fixedBars.store (juce::jlimit (0, 64, bars)); }
    int  getFixedLengthBars() const noexcept    { return fixedBars.load(); }
    // Extra beats counted in before recording. 0 still waits for the next
    // bar/beat when the drum machine is running.
    void setCountInBeats (int beats) noexcept   { countInBeats.store (juce::jlimit (0, 32, beats)); }
    int  getCountInBeats() const noexcept       { return countInBeats.load(); }
    void setLevelDb (float db) noexcept         { levelDb.store (juce::jlimit (-60.0f, 6.0f, db)); }
    float getLevelDb() const noexcept           { return levelDb.load(); }

    static constexpr double getMaxSeconds() noexcept { return maxSeconds; }

    // Space bar. stop() freezes the loop, clear() erases it.
    void toggle() noexcept                      { toggleRequested.store (true); }
    void stop() noexcept                        { stopRequested.store (true); }
    void clear() noexcept                       { clearRequested.store (true); }

    // ---- editing (message thread) -------------------------------------------
    // Keep only [start, end) of the loop. The selection must be inside the loop.
    bool trimTo (double startSeconds, double endSeconds);
    // Grow the loop by whole bars and record the new section live. The loop
    // keeps playing; recording starts when the playhead reaches the old end.
    bool extendByBars (int bars);
    bool undoEdit();
    bool canUndo() const noexcept               { return undoAvailable.load(); }
    bool canEditNow() const noexcept;           // playing or stopped, loop exists
    // Copy the current loop for saving. Returns false while recording.
    bool copyLoopTo (juce::AudioBuffer<float>& dest, double& sampleRateOut);
    // Replace the loop with an already recorded take (a saved loop from disk).
    // The take is resampled to the engine rate, and its tempo is published so
    // the bar grid and the drum machine can follow it. Refuses while a take is
    // being recorded / overdubbed / extended.
    bool loadLoop (const juce::AudioBuffer<float>& source, double sourceSampleRate,
                   double bpm, int beatsPerBar, bool startPlaying);
    // Called from the UI timer; rebuilds the published waveform after an edit.
    void updateWaveformIfDirty();
    void seek (double seconds) noexcept         { seekRequest.store (seconds); }

    // ---- UI state -----------------------------------------------------------
    State  getState() const noexcept            { return state.load(); }
    bool   hasLoop() const noexcept             { return loopFrames.load() > 0; }
    int    getLayerCount() const noexcept       { return layers.load(); }
    double getLengthSeconds() const noexcept    { return lengthSeconds.load(); }
    double getPosition() const noexcept         { return position.load(); }
    double getRecordedSeconds() const noexcept  { return recordedSeconds.load(); }
    double getLoopBpm() const noexcept          { return loopBpm.load(); }
    double getLoopBars() const noexcept         { return loopBars.load(); }
    int    getLoopBeatsPerBar() const noexcept  { return loopBeatsPerBar.load(); }
    double getSampleRate() const noexcept       { return sampleRate; }

    // Count-in: beats left until recording starts, and how many it started at.
    int    getCountInRemaining() const noexcept { return countInRemaining.load(); }
    int    getCountInTotal() const noexcept     { return countInTotal.load(); }

    // Extension: the newly added region, for the timeline overlay.
    double getExtendStartSeconds() const noexcept { return extendStartSeconds.load(); }
    double getExtendEndSeconds() const noexcept   { return extendEndSeconds.load(); }

    // Changes whenever the loop content changes; the save UI uses it to show
    // "unsaved changes".
    int64_t getRevision() const noexcept        { return revision.load(); }
    // Changes when a brand-new take is started.
    int64_t getLoopId() const noexcept          { return loopId.load(); }

    // Downsampled recording envelope for the UI waveform (one peak per ~10 ms).
    static constexpr int waveformBuckets = 12000;
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
    void publishLengths();
    void invalidateUndo();
    void rebuildWaveformLocked();
    int64_t unitSamples (int beatsPerBar, double bpm) const noexcept;
    int64_t samplesToNextBoundary (double gridBeats, int beatsPerBar, double bpm) const noexcept;
    void resetWaveform() noexcept;
    void pushWaveformPeak (float peak, int bucketSamples) noexcept;

    // Extension helpers (audio thread).
    void startPendingExtension (int64_t addFrames, double bpm, int beatsPerBar);
    void finishExtension();
    void cancelExtension (bool stopPlayback);

    static constexpr double maxSeconds = 120.0;

    // Message-thread settings and requests.
    std::atomic<LoopQuantise> quantise { LoopQuantise::bar };
    std::atomic<int>   fixedBars { 0 };
    std::atomic<int>   countInBeats { 0 };
    std::atomic<float> levelDb { -3.0f };
    std::atomic<bool>  toggleRequested { false }, stopRequested { false }, clearRequested { false };
    std::atomic<double> seekRequest { -1.0 };

    // Editing requests.
    std::atomic<int64_t> pendingExtendFrames { 0 };
    std::atomic<bool> waveformDirty { false };

    // Published for the UI.
    std::atomic<State>  state { State::empty };
    std::atomic<int64_t> loopFrames { 0 };
    std::atomic<int>    layers { 0 };
    std::atomic<double> lengthSeconds { 0.0 }, position { 0.0 }, recordedSeconds { 0.0 };
    std::atomic<double> loopBpm { 0.0 }, loopBars { 0.0 };
    std::atomic<int>    loopBeatsPerBar { 4 };
    std::atomic<int>    countInRemaining { 0 }, countInTotal { 0 };
    std::atomic<double> extendStartSeconds { 0.0 }, extendEndSeconds { 0.0 };
    std::atomic<int64_t> revision { 0 }, loopId { 0 };
    std::atomic<bool>   undoAvailable { false };

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

    // Message-thread edits and the audio thread's buffer access are serialised
    // by this lock; the audio thread only ever tries it.
    juce::CriticalSection bufferLock;
    juce::AudioBuffer<float> undoBuffer;
    int64_t undoLen = 0;

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

    // Count-in click (only when the drum machine is not providing the beat).
    double clickPhaseSamples = 0.0;
    double clickAngle = 0.0;
    int    clickSamplesLeft = 0;
    int    clickLengthSamples = 0;
    int    clickIndex = 0;
    float  clickFreq = 1200.0f;

    // Extension (audio thread).
    int64_t extOldLen = 0;
    int64_t extEnd = 0;
    int64_t extRecorded = 0;
    bool    extWasPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Looper)
};
