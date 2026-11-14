#include "Looper.h"
#include "drums/DrumMachine.h"
#include "fx/HostTempo.h"
#include <algorithm>
#include <cmath>
#include <cstring>

// ---- setup ------------------------------------------------------------------

void Looper::prepare (double newSampleRate, int blockSize, bool shouldEnable)
{
    const juce::ScopedLock sl (bufferLock);

    const double sr = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    const int newCapacity = shouldEnable ? (int) std::ceil (maxSeconds * sr) : 0;

    sampleRate = sr;
    enabled = shouldEnable;

    if (newCapacity != capacity || loopBuffer.getNumSamples() != newCapacity)
    {
        loopBuffer.setSize (2, newCapacity, false, true, false);
        loopBuffer.clear();
        capacity = newCapacity;
    }

    inputBuffer.setSize (2, juce::jmax (256, blockSize), false, true, false);
    inputBuffer.clear();

    // One envelope bucket is ~10 ms; 12000 buckets span the 120 s buffer, and
    // the live ring holds the most recent ~5 s.
    waveformBucketSamples.store (juce::jmax (64, (int) std::lround (0.010 * sampleRate)));
    if (waveform == nullptr)
        waveform = std::make_unique<WaveArray>();
    if (livePeaks == nullptr)
        livePeaks = std::make_unique<LiveArray>();
    for (auto& p : *waveform)
        p.store (0.0f);
    for (auto& p : *livePeaks)
        p.store (0.0f);
    liveWrite = 0;
    liveFill = 0;
    livePeak = 0.0f;
    liveWritePos.store (0);
    resetWaveform();
    waveformDirty.store (false);

    // prepare() runs while audio is stopped; a fresh start always begins empty.
    current = State::empty;
    loopLen = 0;
    recordFrames = 0;
    playPos = 0;
    closeTarget = 0;
    fixedTarget = 0;
    closePending = false;
    clearPending = false;
    armSamples = 0;
    fadeGain = 0.0f;
    freeBeats = 0.0;
    captured = 0;
    extOldLen = extEnd = extRecorded = 0;
    extWasPlaying = false;
    clickPhaseSamples = 0.0;
    clickAngle = 0.0;
    clickSamplesLeft = 0;
    clickLengthSamples = 0;
    clickIndex = 0;
    clickFreq = 1200.0f;

    toggleRequested.store (false);
    stopRequested.store (false);
    clearRequested.store (false);
    seekRequest.store (-1.0);
    pendingExtendFrames.store (0);

    undoBuffer.setSize (0, 0);
    undoLen = 0;
    undoAvailable.store (false);

    state.store (current);
    loopFrames.store (0);
    layers.store (0);
    lengthSeconds.store (0.0);
    position.store (0.0);
    recordedSeconds.store (0.0);
    loopBpm.store (0.0);
    loopBars.store (0.0);
    loopBeatsPerBar.store (4);
    countInRemaining.store (0);
    countInTotal.store (0);
    extendStartSeconds.store (0.0);
    extendEndSeconds.store (0.0);
    revision.store (0);
    loopId.store (0);
}

void Looper::capture (const float* left, const float* right, int numSamples) noexcept
{
    captured = 0;
    if (! enabled || numSamples <= 0 || inputBuffer.getNumSamples() < numSamples)
        return;

    juce::FloatVectorOperations::copy (inputBuffer.getWritePointer (0), left, numSamples);
    juce::FloatVectorOperations::copy (inputBuffer.getWritePointer (1), right, numSamples);
    captured = numSamples;
}

// ---- helpers ----------------------------------------------------------------

int64_t Looper::unitSamples (int beatsPerBar, double bpm) const noexcept
{
    const auto q = quantise.load();
    if (q == LoopQuantise::off)
        return 1;

    const double beats = q == LoopQuantise::bar ? (double) juce::jmax (1, beatsPerBar) : 1.0;
    return juce::jmax<int64_t> (1, (int64_t) std::llround (beats * 60.0 / bpm * sampleRate));
}

int64_t Looper::samplesToNextBoundary (double gridBeats, int beatsPerBar, double bpm) const noexcept
{
    if (quantise.load() == LoopQuantise::off)
        return 0;

    const double beats = quantise.load() == LoopQuantise::bar
                             ? (double) juce::jmax (1, beatsPerBar) : 1.0;
    const double next = (std::floor (gridBeats / beats) + 1.0) * beats;
    const double beatsPerSample = (bpm / 60.0) / sampleRate;
    const double samples = (next - gridBeats) / beatsPerSample;

    // The wait is always at most one grid unit; the clamp only guards against
    // floating-point noise at very slow tempos.
    const double maxSamples = beats * 60.0 / bpm * sampleRate + 2.0;
    return (int64_t) juce::jlimit (0.0, maxSamples, std::ceil (samples - 1.0e-6));
}

void Looper::setCurrent (State s)
{
    const bool wasArming = current == State::armingRecord || current == State::armingPlay;
    current = s;

    if (wasArming && s != State::armingRecord && s != State::armingPlay)
    {
        countInRemaining.store (0);
        countInTotal.store (0);
        clickSamplesLeft = 0;
        clickPhaseSamples = 0.0;
        clickIndex = 0;
    }
}

void Looper::publishLengths()
{
    loopFrames.store (loopLen);
    lengthSeconds.store (loopLen > 0 ? (double) loopLen / sampleRate : 0.0);

    const double bpm = loopBpm.load() > 1.0 ? loopBpm.load() : recordBpm;
    const int bpb = juce::jmax (1, loopBeatsPerBar.load());
    loopBars.store (loopLen > 0 ? (double) loopLen / sampleRate * bpm / 60.0 / (double) bpb : 0.0);
}

void Looper::invalidateUndo()
{
    undoAvailable.store (false);
}

void Looper::resetWaveform() noexcept
{
    waveWrite = 0;
    waveFill = 0;
    wavePeak = 0.0f;
    waveformCount.store (0, std::memory_order_relaxed);
}

void Looper::pushWaveformPeak (float peak, int bucketSamples) noexcept
{
    wavePeak = juce::jmax (wavePeak, peak);

    if (++waveFill < bucketSamples)
        return;

    if (waveWrite < waveformBuckets && waveform != nullptr)
    {
        (*waveform)[(size_t) waveWrite].store (wavePeak, std::memory_order_relaxed);
        ++waveWrite;
        waveformCount.store (waveWrite, std::memory_order_relaxed);
    }

    wavePeak = 0.0f;
    waveFill = 0;
}

void Looper::rebuildWaveformLocked()
{
    resetWaveform();
    if (loopLen <= 0 || waveform == nullptr)
        return;

    const int bucketSamples = juce::jmax (1, waveformBucketSamples.load (std::memory_order_relaxed));
    const int buckets = juce::jmin (waveformBuckets,
                                    (int) ((loopLen + bucketSamples - 1) / bucketSamples));
    const auto* l = loopBuffer.getReadPointer (0);
    const auto* r = loopBuffer.getReadPointer (1);

    int b = 0;
    int fill = 0;
    float peak = 0.0f;
    for (int64_t i = 0; i < loopLen && b < buckets; ++i)
    {
        peak = juce::jmax (peak, std::abs (l[i]), std::abs (r[i]));
        if (++fill >= bucketSamples)
        {
            (*waveform)[(size_t) b++].store (peak, std::memory_order_relaxed);
            peak = 0.0f;
            fill = 0;
        }
    }

    if (b < buckets)
        (*waveform)[(size_t) b++].store (peak, std::memory_order_relaxed);

    waveWrite = b;
    waveFill = 0;
    wavePeak = 0.0f;
    waveformCount.store (b, std::memory_order_relaxed);
}

void Looper::updateWaveformIfDirty()
{
    if (! waveformDirty.exchange (false, std::memory_order_acq_rel))
        return;

    const juce::ScopedLock sl (bufferLock);
    if (current == State::recording || current == State::overdubbing || current == State::extending)
    {
        waveformDirty.store (true, std::memory_order_relaxed);
        return;
    }

    rebuildWaveformLocked();
}

// ---- editing (message thread) ----------------------------------------------

bool Looper::canEditNow() const noexcept
{
    const auto s = getState();
    return hasLoop() && (s == State::playing || s == State::stopped);
}

bool Looper::trimTo (double startSeconds, double endSeconds)
{
    if (! (startSeconds >= 0.0) || endSeconds <= startSeconds)
        return false;

    const juce::ScopedLock sl (bufferLock);

    if (loopLen <= 0 || (current != State::playing && current != State::stopped))
        return false;

    int64_t a = (int64_t) std::llround (startSeconds * sampleRate);
    int64_t b = (int64_t) std::llround (endSeconds * sampleRate);
    a = juce::jlimit<int64_t> (0, loopLen - 1, a);
    b = juce::jlimit<int64_t> (a + 1, loopLen, b);

    if (b - a < (int64_t) juce::jmax (1.0, 0.02 * sampleRate))
        return false;

    // Keep the old loop for the one-level undo.
    undoBuffer.setSize (2, (int) loopLen, false, false, true);
    for (int ch = 0; ch < 2; ++ch)
        undoBuffer.copyFrom (ch, 0, loopBuffer, ch, 0, (int) loopLen);
    undoLen = loopLen;

    const int64_t newLen = b - a;
    for (int ch = 0; ch < 2; ++ch)
        std::memmove (loopBuffer.getWritePointer (ch),
                      loopBuffer.getReadPointer (ch) + a,
                      (size_t) newLen * sizeof (float));

    if (playPos < a)
        { /* stay at the same musical point */ }
    else if (playPos >= b)
        playPos -= a;
    else
        playPos = 0;

    loopLen = newLen;
    playPos = juce::jlimit<int64_t> (0, loopLen - 1, playPos);
    publishLengths();
    rebuildWaveformLocked();
    undoAvailable.store (true);
    revision.fetch_add (1);
    waveformDirty.store (false);
    return true;
}

bool Looper::extendByBars (int bars)
{
    if (bars <= 0)
        return false;

    const juce::ScopedLock sl (bufferLock);

    if (loopLen <= 0 || (current != State::playing && current != State::stopped))
        return false;

    const double bpm = loopBpm.load() > 1.0 ? loopBpm.load() : recordBpm;
    const int bpb = juce::jmax (1, loopBeatsPerBar.load());
    const int64_t barSamples = (int64_t) std::llround (60.0 / juce::jmax (30.0, bpm)
                                                       * (double) bpb * sampleRate);
    const int64_t add = (int64_t) bars * barSamples;

    if (add <= 0 || loopLen + add > (int64_t) capacity)
        return false;

    // Snapshot before the buffer grows, then silence the new tail. The audio
    // thread will not read past loopLen until it takes the extend request.
    undoBuffer.setSize (2, (int) loopLen, false, false, true);
    for (int ch = 0; ch < 2; ++ch)
        undoBuffer.copyFrom (ch, 0, loopBuffer, ch, 0, (int) loopLen);
    undoLen = loopLen;
    undoAvailable.store (true);

    for (int ch = 0; ch < 2; ++ch)
        loopBuffer.clear (ch, (int) loopLen, (int) add);

    pendingExtendFrames.store (add, std::memory_order_release);
    return true;
}

bool Looper::undoEdit()
{
    const juce::ScopedLock sl (bufferLock);

    if (! undoAvailable.load() || undoLen <= 0)
        return false;

    if (current != State::playing && current != State::stopped)
        return false;

    const int64_t len = juce::jmin<int64_t> (undoLen, (int64_t) undoBuffer.getNumSamples());
    if (len <= 0)
        return false;

    for (int ch = 0; ch < 2; ++ch)
        loopBuffer.copyFrom (ch, 0, undoBuffer, ch, 0, (int) len);

    loopLen = len;
    playPos = juce::jlimit<int64_t> (0, juce::jmax<int64_t> (0, loopLen - 1), playPos);
    publishLengths();
    rebuildWaveformLocked();
    undoLen = 0;
    undoAvailable.store (false);
    revision.fetch_add (1);
    waveformDirty.store (false);
    return true;
}

bool Looper::copyLoopTo (juce::AudioBuffer<float>& dest, double& sampleRateOut)
{
    const juce::ScopedLock sl (bufferLock);

    if (loopLen <= 0 || capacity <= 0)
        return false;

    if (current == State::recording || current == State::overdubbing
        || current == State::extending || current == State::armingRecord)
        return false;

    dest.setSize (2, (int) loopLen, false, false, true);
    for (int ch = 0; ch < 2; ++ch)
        dest.copyFrom (ch, 0, loopBuffer, ch, 0, (int) loopLen);

    sampleRateOut = sampleRate;
    return true;
}

bool Looper::loadLoop (const juce::AudioBuffer<float>& source, double sourceSampleRate,
                       double bpm, int beatsPerBar, bool startPlaying)
{
    if (source.getNumSamples() <= 0)
        return false;

    if (! (sourceSampleRate > 0.0))
        sourceSampleRate = sampleRate;

    const juce::ScopedLock sl (bufferLock);

    if (capacity <= 0)
        return false;

    // Replacing a take while it is being recorded or extended would fight the
    // audio thread's write position. The UI stops those first.
    if (current != State::empty && current != State::playing && current != State::stopped)
        return false;

    const int64_t sourceSamples = (int64_t) source.getNumSamples();
    const double ratio = sourceSampleRate / juce::jmax (1.0, sampleRate);
    int64_t wanted = (int64_t) std::llround ((double) sourceSamples / juce::jmax (1.0e-9, ratio));
    wanted = juce::jlimit<int64_t> (1, (int64_t) capacity, wanted);

    const int srcChannels = source.getNumChannels();
    if (srcChannels <= 0)
        return false;

    for (int ch = 0; ch < 2; ++ch)
    {
        auto* dst = loopBuffer.getWritePointer (ch);
        const auto* src = source.getReadPointer (juce::jmin (ch, srcChannels - 1));

        if (std::abs (sampleRate - sourceSampleRate) < 0.01)
        {
            const int copy = (int) juce::jmin<int64_t> (sourceSamples, wanted);
            juce::FloatVectorOperations::copy (dst, src, copy);
            if (wanted > copy)
                juce::FloatVectorOperations::clear (dst + copy, (int) (wanted - copy));
        }
        else
        {
            juce::LagrangeInterpolator interpolator;
            interpolator.process (sourceSampleRate / juce::jmax (1.0, sampleRate),
                                  src, dst, (int) wanted, (int) sourceSamples, 0);
        }

        // Short fades keep the seam click-free without changing the take.
        const int64_t fade = juce::jmin<int64_t> (wanted / 4, (int64_t) (0.003 * sampleRate));
        if (fade > 1)
        {
            for (int64_t i = 0; i < fade; ++i)
            {
                const float g = (float) i / (float) fade;
                dst[i] *= g;
                dst[wanted - 1 - i] *= g;
            }
        }
    }

    loopLen = wanted;
    recordFrames = 0;
    playPos = 0;
    closeTarget = 0;
    fixedTarget = 0;
    closePending = false;
    clearPending = false;
    armSamples = 0;
    while (toggleRequested.exchange (false)) {}
    while (stopRequested.exchange (false)) {}
    while (clearRequested.exchange (false)) {}
    seekRequest.store (-1.0);
    extOldLen = extEnd = extRecorded = 0;
    extWasPlaying = false;
    extendStartSeconds.store (0.0);
    extendEndSeconds.store (0.0);
    pendingExtendFrames.store (0);
    countInRemaining.store (0);
    countInTotal.store (0);

    const double loadedBpm = bpm > 1.0 ? juce::jlimit (30.0, 300.0, bpm) : recordBpm;
    recordBpm = loadedBpm;
    recordBeatsPerBar = juce::jlimit (1, 12, beatsPerBar);
    loopBpm.store (loadedBpm);
    loopBeatsPerBar.store (recordBeatsPerBar);

    layers.store (1);
    fadeGain = 0.0f;
    undoBuffer.setSize (0, 0);
    undoLen = 0;
    undoAvailable.store (false);

    publishLengths();
    rebuildWaveformLocked();
    revision.fetch_add (1);
    loopId.fetch_add (1);
    waveformDirty.store (false);
    recordedSeconds.store (0.0);
    position.store (0.0);

    setCurrent (startPlaying ? State::playing : State::stopped);
    state.store (current);
    loopFrames.store (loopLen);
    return true;
}

void Looper::discardRecording()
{
    loopLen = 0;
    recordFrames = 0;
    playPos = 0;
    closeTarget = 0;
    fixedTarget = 0;
    closePending = false;
    clearPending = false;
    armSamples = 0;
    fadeGain = 0.0f;
    extOldLen = extEnd = extRecorded = 0;
    extWasPlaying = false;
    extendStartSeconds.store (0.0);
    extendEndSeconds.store (0.0);
    pendingExtendFrames.store (0);
    layers.store (0);
    lengthSeconds.store (0.0);
    loopBars.store (0.0);
    resetWaveform();
    invalidateUndo();
    setCurrent (State::empty);
}

void Looper::beginRecording (double bpm, int beatsPerBar)
{
    recordFrames = 0;
    playPos = 0;
    loopLen = 0;
    closePending = false;
    clearPending = false;
    layers.store (0);
    resetWaveform();
    invalidateUndo();
    pendingExtendFrames.store (0);
    extOldLen = extEnd = extRecorded = 0;
    extWasPlaying = false;
    extendStartSeconds.store (0.0);
    extendEndSeconds.store (0.0);
    lengthSeconds.store (0.0);
    loopBars.store (0.0);
    loopFrames.store (0);
    position.store (0.0);
    loopId.fetch_add (1);

    recordBpm = bpm;
    recordBeatsPerBar = juce::jmax (1, beatsPerBar);

    const double barSeconds = 60.0 / bpm * (double) recordBeatsPerBar;
    const int wanted = fixedBars.load();
    fixedTarget = wanted > 0
                      ? juce::jlimit<int64_t> (1, (int64_t) capacity,
                                               (int64_t) std::llround (wanted * barSeconds * sampleRate))
                      : 0;

    fadeGain = 0.0f;
    setCurrent (State::recording);
}

void Looper::requestClose (int64_t unit, bool drumsOn)
{
    if (quantise.load() == LoopQuantise::off)
    {
        finishRecording (recordFrames, drumsOn);
        return;
    }

    // Round to the nearest whole unit (bar / beat). Pressing a little late
    // trims the tail; pressing a little early keeps recording until the line.
    const double ratio = (double) recordFrames / (double) juce::jmax<int64_t> (1, unit);
    const int64_t units = juce::jmax<int64_t> (1, (int64_t) std::llround (ratio));
    const int64_t target = juce::jmin<int64_t> ((int64_t) capacity, units * unit);

    if (target <= recordFrames)
    {
        finishRecording (target, drumsOn);
    }
    else
    {
        closePending = true;
        closeTarget = target;
    }
}

void Looper::finishRecording (int64_t wantedFrames, bool drumsOn)
{
    const int64_t len = juce::jlimit<int64_t> (1, (int64_t) capacity,
                                               juce::jmin (wantedFrames, recordFrames));
    const int64_t excess = drumsOn ? juce::jmax<int64_t> (0, recordFrames - len) : 0;

    loopLen = len;

    // Short fades at both ends so the seam does not click every pass.
    const int64_t fade = juce::jmin<int64_t> (loopLen / 4, (int64_t) (0.003 * sampleRate));
    if (fade > 1)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = loopBuffer.getWritePointer (ch);
            for (int64_t i = 0; i < fade; ++i)
            {
                const float g = (float) i / (float) fade;
                d[i] *= g;
                d[loopLen - 1 - i] *= g;
            }
        }
    }

    layers.store (1);
    playPos = drumsOn && loopLen > 0 ? excess % loopLen : 0;
    fadeGain = 0.0f;
    closePending = false;

    loopBpm.store (recordBpm);
    loopBeatsPerBar.store (recordBeatsPerBar);
    publishLengths();
    revision.fetch_add (1);
    setCurrent (State::playing);

    // Close the envelope at the trimmed loop length, dropping peaks from the
    // part that was cut off by a late closing press.
    if (waveFill > 0 && waveWrite < waveformBuckets && waveform != nullptr)
    {
        (*waveform)[(size_t) waveWrite].store (wavePeak, std::memory_order_relaxed);
        ++waveWrite;
    }
    const int bucketSamples = juce::jmax (1, waveformBucketSamples.load (std::memory_order_relaxed));
    const int wantedBuckets = (int) ((loopLen + bucketSamples - 1) / bucketSamples);
    waveformCount.store (juce::jlimit (0, juce::jmin (waveWrite, waveformBuckets), wantedBuckets),
                         std::memory_order_relaxed);
    waveformDirty.store (false);
}

void Looper::startPlayback (int64_t fromFrame)
{
    if (loopLen <= 0)
    {
        setCurrent (State::empty);
        return;
    }

    playPos = fromFrame % loopLen;
    fadeGain = 0.0f;
    clearPending = false;
    extendStartSeconds.store (0.0);
    extendEndSeconds.store (0.0);
    setCurrent (State::playing);
}

void Looper::handleToggle (bool drumsOn, int beatsPerBar, double bpm, double gridBeats, int64_t unit)
{
    if (clearPending)
        return;

    const double beatSamples = 60.0 / juce::jmax (1.0, bpm) * sampleRate;
    auto scheduleArm = [this, beatSamples] (int64_t samples, State armed)
    {
        armSamples = juce::jmax<int64_t> (0, samples);
        clickPhaseSamples = 0.0;
        clickSamplesLeft = 0;
        clickIndex = 0;
        clickLengthSamples = (int) juce::jmax (64, (int) std::llround (0.020 * sampleRate));
        const int total = armSamples > 0
                              ? (int) std::ceil ((double) armSamples / beatSamples) : 0;
        countInTotal.store (total);
        countInRemaining.store (total);
        setCurrent (armed);
    };

    switch (current)
    {
        case State::empty:
        {
            const int64_t boundary = (drumsOn && quantise.load() != LoopQuantise::off)
                                         ? samplesToNextBoundary (gridBeats, beatsPerBar, bpm) : 0;
            const int64_t countIn = (int64_t) std::llround ((double) countInBeats.load() * beatSamples);
            const int64_t total = boundary + countIn;
            if (total > 0)
                scheduleArm (total, State::armingRecord);
            else
                beginRecording (bpm, beatsPerBar);
            break;
        }

        case State::stopped:
            if (loopLen <= 0)
            {
                handleToggle (drumsOn, beatsPerBar, bpm, gridBeats, unit);
            }
            else if (drumsOn && quantise.load() != LoopQuantise::off)
            {
                const auto boundary = samplesToNextBoundary (gridBeats, beatsPerBar, bpm);
                if (boundary > 0)
                    scheduleArm (boundary, State::armingPlay);
                else
                    startPlayback (0);
            }
            else
            {
                startPlayback (0);
            }
            break;

        case State::recording:
            requestClose (unit, drumsOn);
            break;

        case State::playing:
            setCurrent (State::armedOverdub);
            break;

        case State::overdubbing:
            // Finishing an overdub is immediate: the loop itself does not change
            // length, so there is nothing to quantise.
            setCurrent (State::playing);
            revision.fetch_add (1);
            waveformDirty.store (true, std::memory_order_release);
            break;

        case State::armingRecord:
            discardRecording();
            break;

        case State::armedOverdub:
            setCurrent (State::playing);
            break;

        case State::armingPlay:
            setCurrent (State::stopped);
            break;

        case State::extending:
            cancelExtension (false);
            break;
    }
}

// ---- extension --------------------------------------------------------------

void Looper::startPendingExtension (int64_t addFrames, double bpm, int beatsPerBar)
{
    juce::ignoreUnused (beatsPerBar);

    if (! (current == State::playing || current == State::stopped)
        || addFrames <= 0 || loopLen <= 0 || loopLen + addFrames > (int64_t) capacity)
    {
        invalidateUndo();
        return;
    }

    const double loopTempo = loopBpm.load() > 1.0 ? loopBpm.load() : bpm;
    const int bpb = juce::jmax (1, loopBeatsPerBar.load());
    const int64_t barSamples = juce::jmax<int64_t> (
        1, (int64_t) std::llround (60.0 / juce::jmax (30.0, loopTempo) * (double) bpb * sampleRate));

    extWasPlaying = current == State::playing;
    extOldLen = loopLen;
    extEnd = loopLen + addFrames;
    extRecorded = 0;
    loopLen = extEnd;

    // Start one bar before the seam when possible, so the new section is
    // played in context instead of out of nowhere.
    const int64_t preRoll = juce::jmin (extOldLen, barSamples);
    playPos = extOldLen - preRoll;
    fadeGain = 1.0f;

    extendStartSeconds.store ((double) extOldLen / sampleRate);
    extendEndSeconds.store ((double) extEnd / sampleRate);
    publishLengths();
    setCurrent (State::extending);
}

void Looper::finishExtension()
{
    if (loopLen <= 0)
    {
        setCurrent (State::stopped);
        return;
    }

    extRecorded = extEnd - extOldLen;
    publishLengths();
    setCurrent (State::playing);
    waveformDirty.store (true, std::memory_order_release);
    revision.fetch_add (1);
}

void Looper::cancelExtension (bool stopPlayback)
{
    if (loopLen <= 0)
    {
        setCurrent (stopPlayback ? State::stopped : State::empty);
        return;
    }

    loopLen = extOldLen;
    if (loopLen <= 0)
        loopLen = 1;

    if (playPos >= loopLen)
        playPos = 0;

    extendStartSeconds.store (0.0);
    extendEndSeconds.store (0.0);
    publishLengths();
    invalidateUndo();

    setCurrent (stopPlayback ? State::stopped : (extWasPlaying ? State::playing : State::stopped));
}

// ---- audio ------------------------------------------------------------------

void Looper::render (float* left, float* right, int numSamples, const DrumMachine& drums) noexcept
{
    if (! enabled || numSamples <= 0 || captured < numSamples)
        return;

    // Edits happen on the message thread. Never block the audio thread for
    // them: if an edit is in progress, skip this block of loop processing.
    const juce::CriticalSection::ScopedTryLockType tryLock (bufferLock);
    if (! tryLock.isLocked())
    {
        captured = 0;
        return;
    }

    const bool drumsOn = drums.isPlaying();
    const int beatsPerBar = juce::jlimit (1, 12, drums.getGridBeatsPerBar());
    const double bpm = juce::jlimit (30.0, 300.0, (double) HostTempo::bpm.load());
    const double beatsPerSample = (bpm / 60.0) / sampleRate;
    const double beatSamples = 60.0 / bpm * sampleRate;
    const double gridBeats = drumsOn ? drums.getGridBeatsAtBlockStart() : freeBeats;
    const int64_t unit = unitSamples (beatsPerBar, bpm);
    const float outLevel = juce::Decibels::decibelsToGain (levelDb.load(), -60.0f);

    // ---- commands at the start of the block --------------------------------
    if (clearRequested.exchange (false, std::memory_order_acq_rel))
    {
        if (pendingExtendFrames.exchange (0) > 0)
            invalidateUndo();

        if (current == State::extending)
            cancelExtension (false);

        if (current == State::recording || current == State::armingRecord || loopLen <= 0)
        {
            discardRecording();
        }
        else
        {
            // Let the loop fade out, then erase it in the sample loop.
            setCurrent (State::stopped);
            clearPending = true;
            invalidateUndo();
        }
    }

    if (stopRequested.exchange (false, std::memory_order_acq_rel))
    {
        clearPending = false;
        if (pendingExtendFrames.exchange (0) > 0)
            invalidateUndo();
        switch (current)
        {
            case State::recording:
            case State::armingRecord:
                discardRecording();
                break;

            case State::extending:
                cancelExtension (true);
                break;

            case State::playing:
            case State::armedOverdub:
            case State::overdubbing:
            case State::armingPlay:
                setCurrent (loopLen > 0 ? State::stopped : State::empty);
                break;

            default:
                break;
        }
    }

    if (toggleRequested.exchange (false, std::memory_order_acq_rel))
        handleToggle (drumsOn, beatsPerBar, bpm, gridBeats, unit);

    // A queued seek moves the playhead without changing the transport state.
    const double seekSeconds = seekRequest.exchange (-1.0, std::memory_order_acq_rel);
    if (seekSeconds >= 0.0 && loopLen > 0
        && (current == State::playing || current == State::stopped))
    {
        playPos = juce::jlimit<int64_t> (0, loopLen - 1,
                                         (int64_t) std::llround (seekSeconds * sampleRate)) % loopLen;
        position.store (loopLen > 0 ? (double) playPos / (double) loopLen : 0.0,
                        std::memory_order_relaxed);
    }

    // A queued edit grows the loop and starts recording at the seam.
    const int64_t extendFrames = pendingExtendFrames.exchange (0, std::memory_order_acq_rel);
    if (extendFrames > 0)
    {
        if (! clearPending && (current == State::playing || current == State::stopped))
            startPendingExtension (extendFrames, bpm, beatsPerBar);
        else
            invalidateUndo();
    }

    // ---- per-sample ---------------------------------------------------------
    const float* inL = inputBuffer.getReadPointer (0);
    const float* inR = inputBuffer.getReadPointer (1);
    float* bufL = loopBuffer.getWritePointer (0);
    float* bufR = loopBuffer.getWritePointer (1);
    const float fadeCoef = 1.0f - std::exp (-1.0f / (float) juce::jmax (1.0, 0.005 * sampleRate));
    const int bucketSamples = juce::jmax (1, waveformBucketSamples.load (std::memory_order_relaxed));
    const int clickBeatsPerBar = juce::jmax (1, beatsPerBar);

    for (int i = 0; i < numSamples; ++i)
    {
        // Always keep a live envelope of the guitar, so the UI can show what
        // is being played even when the looper is not recording.
        const float inputPeak = juce::jmax (std::abs (inL[i]), std::abs (inR[i]));
        livePeak = juce::jmax (livePeak, inputPeak);
        if (++liveFill >= bucketSamples)
        {
            if (livePeaks != nullptr)
                (*livePeaks)[(size_t) (liveWrite & (uint32_t) (liveBuckets - 1))]
                    .store (livePeak, std::memory_order_relaxed);
            ++liveWrite;
            liveWritePos.store (liveWrite, std::memory_order_relaxed);
            livePeak = 0.0f;
            liveFill = 0;
        }

        // Start a queued action on the musical boundary.
        if (current == State::armingRecord || current == State::armingPlay)
        {
            // A soft click makes the count-in usable without the drum machine;
            // if the drums are running they already provide the beat.
            if (! drumsOn && clickLengthSamples > 0)
            {
                clickPhaseSamples += 1.0;
                if (clickPhaseSamples >= beatSamples)
                {
                    clickPhaseSamples -= beatSamples;
                    clickSamplesLeft = clickLengthSamples;
                    clickAngle = 0.0;
                    clickFreq = (clickIndex % clickBeatsPerBar == 0) ? 1800.0f : 1200.0f;
                    ++clickIndex;
                }
            }

            if (armSamples <= 0)
            {
                if (current == State::armingRecord)
                    beginRecording (bpm, beatsPerBar);
                else
                    startPlayback (0);
            }
            else
            {
                --armSamples;
            }
        }

        // Count-in click envelope.
        if (clickSamplesLeft > 0)
        {
            const float env = (float) clickSamplesLeft / (float) juce::jmax (1, clickLengthSamples);
            const float s = (float) std::sin (clickAngle) * env * 0.20f;
            left[i] += s;
            right[i] += s;
            clickAngle += juce::MathConstants<double>::twoPi * (double) clickFreq / sampleRate;
            --clickSamplesLeft;
        }

        if (clearPending && loopLen > 0 && fadeGain <= 1.0e-4f)
        {
            loopLen = 0;
            recordFrames = 0;
            playPos = 0;
            layers.store (0);
            lengthSeconds.store (0.0);
            loopBpm.store (0.0);
            loopBars.store (0.0);
            loopFrames.store (0);
            setCurrent (State::empty);
            clearPending = false;
            revision.fetch_add (1);
        }

        const bool activePlayback = current == State::playing
                                    || current == State::armedOverdub
                                    || current == State::overdubbing
                                    || current == State::extending;
        const int64_t p = playPos;

        // Loop playback (also runs while fading out after a stop).
        if (loopLen > 0 && (activePlayback || fadeGain > 1.0e-4f))
        {
            const float target = activePlayback ? 1.0f : 0.0f;
            fadeGain += (target - fadeGain) * fadeCoef;

            if (fadeGain <= 1.0e-4f && target == 0.0f)
            {
                fadeGain = 0.0f;
            }
            else
            {
                left[i]  += bufL[p] * fadeGain * outLevel;
                right[i] += bufR[p] * fadeGain * outLevel;
            }
        }

        // First-layer recording.
        if (current == State::recording)
        {
            if (recordFrames < capacity)
            {
                bufL[recordFrames] = inL[i];
                bufR[recordFrames] = inR[i];
            }
            ++recordFrames;
            pushWaveformPeak (inputPeak, bucketSamples);
        }
        else if (current == State::overdubbing)
        {
            // Add the new pass on top of the existing loop.
            bufL[p] = juce::jlimit (-2.0f, 2.0f, bufL[p] + inL[i] * 0.85f);
            bufR[p] = juce::jlimit (-2.0f, 2.0f, bufR[p] + inR[i] * 0.85f);
        }
        else if (current == State::extending)
        {
            // The first pass through the new bars records the live input.
            if (p >= extOldLen && p < extEnd)
            {
                bufL[p] = inL[i];
                bufR[p] = inR[i];
                ++extRecorded;
            }
        }

        // Advance the playhead and switch from armed overdub at the loop seam.
        if (loopLen > 0
            && (current == State::playing || current == State::armedOverdub
                || current == State::overdubbing || current == State::extending
                || fadeGain > 1.0e-4f))
        {
            ++playPos;
            if (playPos >= loopLen)
            {
                playPos = 0;
                if (current == State::armedOverdub)
                {
                    setCurrent (State::overdubbing);
                    layers.store (layers.load() + 1);
                    invalidateUndo();   // the undo snapshot no longer matches
                }
                else if (current == State::extending)
                {
                    finishExtension();
                }
            }
        }

        // Close the first layer at the chosen boundary / fixed length.
        if (current == State::recording)
        {
            if (closePending && recordFrames >= closeTarget)
                finishRecording (closeTarget, drumsOn);
            else if (fixedTarget > 0 && recordFrames >= fixedTarget)
                finishRecording (fixedTarget, drumsOn);
            else if (recordFrames >= capacity)
                finishRecording (recordFrames, drumsOn);
        }
    }

    // ---- publish ------------------------------------------------------------
    state.store (current, std::memory_order_relaxed);
    loopFrames.store (loopLen, std::memory_order_relaxed);
    position.store (loopLen > 0 ? (double) playPos / (double) loopLen : 0.0,
                    std::memory_order_relaxed);
    recordedSeconds.store ((double) recordFrames / sampleRate, std::memory_order_relaxed);

    if (current == State::armingRecord || current == State::armingPlay)
    {
        const int remaining = armSamples > 0
                                  ? (int) std::ceil ((double) armSamples / beatSamples) : 0;
        countInRemaining.store (remaining, std::memory_order_relaxed);
    }

    if (drumsOn)
        freeBeats = drums.getGridBeatsAtBlockStart() + (double) numSamples * beatsPerSample;
    else
        freeBeats += (double) numSamples * beatsPerSample;

    captured = 0;
}
