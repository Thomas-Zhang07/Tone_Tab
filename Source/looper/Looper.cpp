#include "Looper.h"
#include "drums/DrumMachine.h"
#include "fx/HostTempo.h"
#include <algorithm>
#include <cmath>

// ---- setup ------------------------------------------------------------------

void Looper::prepare (double newSampleRate, int blockSize, bool shouldEnable)
{
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

    // One envelope bucket is ~10 ms; 3000 buckets span the 30 s loop, and the
    // live ring holds the most recent ~5 s.
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

    toggleRequested.store (false);
    stopRequested.store (false);
    clearRequested.store (false);

    state.store (current);
    loopFrames.store (0);
    layers.store (0);
    lengthSeconds.store (0.0);
    position.store (0.0);
    recordedSeconds.store (0.0);
    loopBpm.store (0.0);
    loopBars.store (0.0);
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
    current = s;
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
    layers.store (0);
    resetWaveform();
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

    const double beats = (double) loopLen / sampleRate * recordBpm / 60.0;
    lengthSeconds.store ((double) loopLen / sampleRate);
    loopBpm.store (recordBpm);
    loopBars.store (beats / (double) recordBeatsPerBar);
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
    setCurrent (State::playing);
}

void Looper::handleToggle (bool drumsOn, int beatsPerBar, double bpm, double gridBeats, int64_t unit)
{
    if (clearPending)
        return;

    switch (current)
    {
        case State::empty:
            if (drumsOn && quantise.load() != LoopQuantise::off)
            {
                armSamples = samplesToNextBoundary (gridBeats, beatsPerBar, bpm);
                setCurrent (State::armingRecord);
            }
            else
            {
                beginRecording (bpm, beatsPerBar);
            }
            break;

        case State::stopped:
            if (loopLen <= 0)
            {
                handleToggle (drumsOn, beatsPerBar, bpm, gridBeats, unit);
            }
            else if (drumsOn && quantise.load() != LoopQuantise::off)
            {
                armSamples = samplesToNextBoundary (gridBeats, beatsPerBar, bpm);
                setCurrent (State::armingPlay);
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
    }
}

// ---- audio ------------------------------------------------------------------

void Looper::render (float* left, float* right, int numSamples, const DrumMachine& drums) noexcept
{
    if (! enabled || numSamples <= 0 || captured < numSamples)
        return;

    const bool drumsOn = drums.isPlaying();
    const int beatsPerBar = juce::jlimit (1, 12, drums.getGridBeatsPerBar());
    const double bpm = juce::jlimit (30.0, 300.0, (double) HostTempo::bpm.load());
    const double beatsPerSample = (bpm / 60.0) / sampleRate;
    const double gridBeats = drumsOn ? drums.getGridBeatsAtBlockStart() : freeBeats;
    const int64_t unit = unitSamples (beatsPerBar, bpm);
    const float outLevel = juce::Decibels::decibelsToGain (levelDb.load(), -60.0f);

    // ---- commands at the start of the block --------------------------------
    if (clearRequested.exchange (false, std::memory_order_acq_rel))
    {
        if (current == State::recording || current == State::armingRecord || loopLen <= 0)
        {
            discardRecording();
        }
        else
        {
            // Let the loop fade out, then erase it in the sample loop.
            setCurrent (State::stopped);
            clearPending = true;
        }
    }

    if (stopRequested.exchange (false, std::memory_order_acq_rel))
    {
        clearPending = false;
        switch (current)
        {
            case State::recording:
            case State::armingRecord:
                discardRecording();
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

    // ---- per-sample ---------------------------------------------------------
    const float* inL = inputBuffer.getReadPointer (0);
    const float* inR = inputBuffer.getReadPointer (1);
    float* bufL = loopBuffer.getWritePointer (0);
    float* bufR = loopBuffer.getWritePointer (1);
    const float fadeCoef = 1.0f - std::exp (-1.0f / (float) juce::jmax (1.0, 0.005 * sampleRate));
    const int bucketSamples = juce::jmax (1, waveformBucketSamples.load (std::memory_order_relaxed));

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

        if (clearPending && loopLen > 0 && fadeGain <= 1.0e-4f)
        {
            loopLen = 0;
            recordFrames = 0;
            playPos = 0;
            layers.store (0);
            lengthSeconds.store (0.0);
            loopBpm.store (0.0);
            loopBars.store (0.0);
            setCurrent (State::empty);
            clearPending = false;
        }

        const bool activePlayback = current == State::playing
                                    || current == State::armedOverdub
                                    || current == State::overdubbing;
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

        // Advance the playhead and switch from armed overdub at the loop seam.
        if (loopLen > 0
            && (current == State::playing || current == State::armedOverdub
                || current == State::overdubbing || fadeGain > 1.0e-4f))
        {
            ++playPos;
            if (playPos >= loopLen)
            {
                playPos = 0;
                if (current == State::armedOverdub)
                {
                    setCurrent (State::overdubbing);
                    layers.store (layers.load() + 1);
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

    if (drumsOn)
        freeBeats = drums.getGridBeatsAtBlockStart() + (double) numSamples * beatsPerSample;
    else
        freeBeats += (double) numSamples * beatsPerSample;

    captured = 0;
}
