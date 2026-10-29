#include "AudioEngine.h"

AudioEngine::AudioEngine()
    : dryRing ((size_t) ringSize, 0.0f)
{
    for (auto& a : activeIndexForDeviceChannel)
        a.store (-1);

    prepareInternal (48000.0, 512);
}

AudioEngine::~AudioEngine()
{
    // 设备回调已经被移除，这里可以安全释放所有快照
    releaseAllChains();
}

// ---- 效果器链快照 --------------------------------------------------------------
void AudioEngine::publishChain (std::unique_ptr<ChainSnapshot> snapshot)
{
    // 音频线程还没来得及取走的旧快照，直接由界面线程释放
    delete pending.exchange (snapshot.release(), std::memory_order_acq_rel);
}

void AudioEngine::releaseAllChains()
{
    delete pending.exchange (nullptr);
    delete incoming;
    delete current;
    incoming = current = nullptr;
    chainLatencySamples.store (0);
    fadeState = 0;
    collectGarbage();
}

void AudioEngine::retire (ChainSnapshot* s) noexcept
{
    if (s == nullptr)
        return;

    const auto scope = garbageFifo.write (1);
    if (scope.blockSize1 > 0)
        garbage[(size_t) scope.startIndex1] = s;
    // 队列满（界面线程长时间没回收）时只能泄漏，绝不在音频线程里 delete
}

void AudioEngine::collectGarbage()
{
    const auto scope = garbageFifo.read (garbageFifo.getNumReady());
    for (int i = 0; i < scope.blockSize1; ++i)
        delete garbage[(size_t) (scope.startIndex1 + i)];
    for (int i = 0; i < scope.blockSize2; ++i)
        delete garbage[(size_t) (scope.startIndex2 + i)];
}

// ---- 设备 ----------------------------------------------------------------------
void AudioEngine::updateActiveInputMap (juce::AudioIODevice* device)
{
    for (auto& a : activeIndexForDeviceChannel)
        a.store (-1);

    if (device == nullptr)
        return;

    auto active = device->getActiveInputChannels();
    int index = 0;
    for (int ch = 0; ch < juce::jmin (maxDeviceChannels, active.getHighestBit() + 1); ++ch)
        if (active[ch])
            activeIndexForDeviceChannel[ch].store (index++);
}

void AudioEngine::prepareInternal (double sr, int block)
{
    block = juce::jmax (16, block);
    preparedBlockSize.store (block);
    lastSampleRate.store (sr);

    chainBuffer.setSize (2, block, false, true, false);
    incomingBuffer.setSize (2, block, false, true, false);
    monoBuffer.assign ((size_t) block, 0.0f);
    masterDc.prepare (sr, 5.0);

    inputGain.reset (sr, 0.02);
    outputGain.reset (sr, 0.02);
    inputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (inputGainDb.load()));
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputGainDb.load()));

    drums.prepare (sr);
    looper.prepare (sr, block, practiceEnabled.load());
    tabPlayer.prepare (sr);
    judge.setSampleRate (sr);
    recorder.setSampleRate (sr);

    // 设备没在跑，链路里的效果器可以安全地重新准备
    for (auto* snap : { current, incoming, pending.load() })
        if (snap != nullptr)
            for (auto& s : snap->slots)
                s->prepare (sr, block);
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    updateActiveInputMap (device);
    prepareInternal (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());

    std::fill (dryRing.begin(), dryRing.end(), 0.0f);
    dryWritePos.store (0);
    currentSampleRate.store (device->getCurrentSampleRate());
}

void AudioEngine::audioDeviceStopped()
{
    currentSampleRate.store (0.0);
    guitarChannelActive.store (false);
}

// ---- 处理 ----------------------------------------------------------------------
void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                                    int numInputChannels,
                                                    float* const* outputChannelData,
                                                    int numOutputChannels,
                                                    int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;

    const int devCh = guitarDeviceChannel.load();
    const int idx = (devCh >= 0 && devCh < maxDeviceChannels) ? activeIndexForDeviceChannel[devCh].load() : -1;
    const float* in = (idx >= 0 && idx < numInputChannels) ? inputChannelData[idx] : nullptr;
    guitarChannelActive.store (in != nullptr);

    // 找前两个输出通道
    float* outL = nullptr;
    float* outR = nullptr;
    for (int ch = 0; ch < numOutputChannels; ++ch)
    {
        if (outputChannelData[ch] == nullptr)
            continue;
        if (outL == nullptr)      outL = outputChannelData[ch];
        else if (outR == nullptr) outR = outputChannelData[ch];
        else juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);
    }

    // 按准备好的块大小切开处理，保证插件拿到的样本数不超过 prepareToPlay 时的值
    const int block = preparedBlockSize.load();
    for (int pos = 0; pos < numSamples; )
    {
        const int n = juce::jmin (block, numSamples - pos);
        float* l = chainBuffer.getWritePointer (0);
        float* r = chainBuffer.getWritePointer (1);
        processSubBlock (in != nullptr ? in + pos : nullptr, l, r, n);

        if (outL != nullptr && outR != nullptr)
        {
            juce::FloatVectorOperations::copy (outL + pos, l, n);
            juce::FloatVectorOperations::copy (outR + pos, r, n);
        }
        else if (outL != nullptr)
        {
            for (int i = 0; i < n; ++i)
                outL[pos + i] = 0.5f * (l[i] + r[i]);
        }
        pos += n;
    }
}

void AudioEngine::processSubBlock (const float* guitarIn, float* outL, float* outR, int n) noexcept
{
    float* mono = monoBuffer.data();

    // 1. 吉他输入 + 削波检测
    if (guitarIn != nullptr)
    {
        const auto range = juce::FloatVectorOperations::findMinAndMax (guitarIn, n);
        if (juce::jmax (std::abs (range.getStart()), std::abs (range.getEnd())) >= 0.98f)
            inputClipped.store (true);
        juce::FloatVectorOperations::copy (mono, guitarIn, n);
    }
    else
    {
        juce::FloatVectorOperations::clear (mono, n);
    }

    // 1b. Playback substitutes the input; recording captures the raw input.
    if (practiceEnabled.load())
    {
        recorder.replaceInput (mono, n);
        recorder.capture (mono, n, tabPlayer.isPlaying(), tabPlayer.getPosition());
    }

    // 2. 干信号写入环形缓冲
    {
        const uint32_t w = dryWritePos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
            dryRing[(size_t) ((w + (uint32_t) i) & (ringSize - 1))] = mono[i];
        dryWritePos.store (w + (uint32_t) n, std::memory_order_release);
    }

    // 3. 输入增益
    inputGain.setTargetValue (juce::Decibels::decibelsToGain (inputGainDb.load()));
    for (int i = 0; i < n; ++i)
        mono[i] *= inputGain.getNextValue();
    {
        const auto r = juce::FloatVectorOperations::findMinAndMax (mono, n);
        const float p = juce::jmax (std::abs (r.getStart()), std::abs (r.getEnd()));
        float prev = inputPeak.load();
        while (p > prev && ! inputPeak.compare_exchange_weak (prev, p)) {}
    }

    juce::FloatVectorOperations::copy (outL, mono, n);
    juce::FloatVectorOperations::copy (outR, mono, n);

    // 4. Effect chain. A structural change runs the old and new chains in
    // parallel for one block and crossfades between them, so there is no mute gap.
    bool crossfade = false;
    if (auto* p = pending.exchange (nullptr, std::memory_order_acq_rel))
    {
        incoming = p;
        if (current == nullptr)
        {
            current = incoming;
            incoming = nullptr;
            chainLatencySamples.store (current->latencySamples);
        }
        else
        {
            crossfade = true;
            incomingBuffer.copyFrom (0, 0, outL, n);
            incomingBuffer.copyFrom (1, 0, outR, n);
        }
    }

    juce::AudioBuffer<float> io (chainBuffer.getArrayOfWritePointers(), 2, n);
    if (current != nullptr)
        for (auto& s : current->slots)
            s->process (io, n);

    if (crossfade && incoming != nullptr)
    {
        juce::AudioBuffer<float> next (incomingBuffer.getArrayOfWritePointers(), 2, n);
        for (auto& s : incoming->slots)
            s->process (next, n);

        auto* out0 = io.getWritePointer (0);
        auto* out1 = io.getWritePointer (1);
        const auto* next0 = next.getReadPointer (0);
        const auto* next1 = next.getReadPointer (1);
        for (int i = 0; i < n; ++i)
        {
            const float t = (float) (i + 1) / (float) n;
            const float oldGain = std::cos (t * juce::MathConstants<float>::halfPi);
            const float newGain = std::sin (t * juce::MathConstants<float>::halfPi);
            out0[i] = out0[i] * oldGain + next0[i] * newGain;
            out1[i] = out1[i] * oldGain + next1[i] * newGain;
        }

        retire (current);
        current = incoming;
        incoming = nullptr;
        chainLatencySamples.store (current->latencySamples);
    }

    // Remove any DC left by asymmetric effects or a third-party plug-in.
    masterDc.processBlock (outL, n, 0);
    masterDc.processBlock (outR, n, 1);

    // The loop station records the tone after the effects chain but before the
    // guitar level / monitor control, so muting the live guitar still captures
    // the loop and the loop keeps its own level.
    if (practiceEnabled.load())
        looper.capture (outL, outR, n);

    // 5. 吉他音量 / 监听开关
    outputGain.setTargetValue (monitoring.load() ? juce::Decibels::decibelsToGain (outputGainDb.load()) : 0.0f);
    for (int i = 0; i < n; ++i)
    {
        const float g = outputGain.getNextValue();
        outL[i] *= g;
        outR[i] *= g;
    }

    // 6. Tab playback (demo / backing / tab drums), drum machine, recording mix.
    if (practiceEnabled.load())
    {
        tabPlayer.render (outL, outR, n, drums);
        drums.render (outL, outR, n);

        // Render after the drums so the looper sees this block's drum grid.
        looper.render (outL, outR, n, drums);

        // Recorded full-mix playback (drums + backing + loop) for the Record view.
        recorder.renderFullMix (outL, outR, n);

        // Full mix: guitar + effects + backing + drums + loop. The dry stem is
        // captured earlier, before input gain.
        recorder.captureMix (outL, outR, n);

        // Judging: dry signal (post input gain, pre effects) + the tab position.
        const auto& bp = tabPlayer.getLastBlockPosition();
        JudgeFeed::Block info;
        info.beat = bp.beat;
        info.beatsPerSample = bp.beatsPerSample;
        info.nominalBps = bp.nominalBps;
        info.rolling = bp.rolling;
        info.wrapOffset = bp.wrapOffset;
        info.wrapBeat = bp.wrapBeat;
        info.loopA = bp.loopA;
        info.loopB = bp.loopB;
        judge.getFeed().push (mono, n, info);
    }

    // 7. 输出电平
    {
        const auto rl = juce::FloatVectorOperations::findMinAndMax (outL, n);
        const auto rr = juce::FloatVectorOperations::findMinAndMax (outR, n);
        const float p = juce::jmax (std::abs (rl.getStart()), std::abs (rl.getEnd()), std::abs (rr.getStart()), std::abs (rr.getEnd()));
        if (p >= 0.99f)
            outputClipped.store (true);
        float prev = outputPeak.load();
        while (p > prev && ! outputPeak.compare_exchange_weak (prev, p)) {}
    }
}

void AudioEngine::processPluginBlock (const float* guitarIn, float* outL, float* outR, int numSamples) noexcept
{
    const int block = juce::jmax (1, preparedBlockSize.load());
    for (int pos = 0; pos < numSamples; )
    {
        const int chunk = juce::jmin (block, numSamples - pos);
        float* l = chainBuffer.getWritePointer (0);
        float* r = chainBuffer.getWritePointer (1);
        processSubBlock (guitarIn != nullptr ? guitarIn + pos : nullptr, l, r, chunk);

        if (outL != nullptr)
            juce::FloatVectorOperations::copy (outL + pos, l, chunk);
        if (outR != nullptr)
            juce::FloatVectorOperations::copy (outR + pos, r, chunk);

        pos += chunk;
    }
}

double AudioEngine::copyRecentDrySamples (float* dest, int numSamples) const
{
    const double sr = currentSampleRate.load();
    if (sr <= 0.0 || numSamples > ringSize)
        return 0.0;

    const uint32_t end = dryWritePos.load (std::memory_order_acquire);
    const uint32_t start = end - (uint32_t) numSamples;
    for (int i = 0; i < numSamples; ++i)
        dest[i] = dryRing[(size_t) ((start + (uint32_t) i) & (ringSize - 1))];
    return sr;
}

// ---- 离线测试 ------------------------------------------------------------------
void AudioEngine::prepareForOffline (double sampleRate, int blockSize)
{
    prepareInternal (sampleRate, blockSize);
    currentSampleRate.store (sampleRate);
}

void AudioEngine::processOffline (const float* monoIn, float* outL, float* outR, int numSamples)
{
    const int block = preparedBlockSize.load();
    for (int pos = 0; pos < numSamples; )
    {
        const int n = juce::jmin (block, numSamples - pos);
        float* l = chainBuffer.getWritePointer (0);
        float* r = chainBuffer.getWritePointer (1);
        processSubBlock (monoIn + pos, l, r, n);
        juce::FloatVectorOperations::copy (outL + pos, l, n);
        juce::FloatVectorOperations::copy (outR + pos, r, n);
        pos += n;
    }
}
