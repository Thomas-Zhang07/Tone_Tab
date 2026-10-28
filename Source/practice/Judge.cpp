#include "Judge.h"
#include "TabPlayer.h"
#include <algorithm>
#include <cmath>

namespace
{
    double midiToHz (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }

    bool isHit (Verdict v) { return v == Verdict::perfect || v == Verdict::good; }

    int nextPow2 (int v) { int p = 1; while (p < v) p <<= 1; return p; }

    void fillHann (std::vector<float>& w, int n)
    {
        w.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
            w[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) n);
    }
}

// =============================================================================
JudgeSession::JudgeSession (std::shared_ptr<const Song> s, int t)
    : song (std::move (s)), track (t)
{
    if (song == nullptr || ! juce::isPositiveAndBelow (t, (int) song->tracks.size()) || song->tracks[(size_t) t].percussion)
    {
        results = std::make_unique<Result[]> (1);
        return;
    }

    const auto& notes = song->tracks[(size_t) t].notes;
    const size_t n = notes.size();
    noteToEvent.assign (n, -1);

    // 滑音的目标音（同一根弦上一个音带 slideOut）
    std::vector<bool> slideDest (n, false);
    {
        std::vector<int> lastOn (16, -1);
        for (size_t i = 0; i < n; ++i)
        {
            const int str = juce::jlimit (0, 15, notes[i].string);
            if (lastOn[(size_t) str] >= 0 && notes[(size_t) lastOn[(size_t) str]].slideOut)
                slideDest[i] = true;
            lastOn[(size_t) str] = (int) i;
        }
    }

    for (size_t i = 0; i < n; )
    {
        size_t j = i;
        while (j < n && std::abs (notes[j].start - notes[i].start) < 1.0e-6)
            ++j;

        JudgeEvent ev;
        ev.beat = notes[i].start;
        ev.bar = juce::jmax (0, song->barIndexAt (ev.beat + 1.0e-9));
        ev.firstNote = (int) i;
        ev.numNotes = (int) (j - i);
        bool allLegato = true;
        for (size_t k = i; k < j; ++k)
        {
            const auto& nn = notes[k];
            noteToEvent[k] = (int) events.size();
            allLegato = allLegato && (nn.hammerPull || slideDest[k]);
            const bool pitched = ! nn.dead && ! nn.harmonic && nn.bend <= 0.0f && ! nn.slideIn;
            if (pitched && ev.numPitches < (int) ev.midi.size()
                && std::find (ev.midi.begin(), ev.midi.begin() + ev.numPitches, nn.midi) == ev.midi.begin() + ev.numPitches)
                ev.midi[(size_t) ev.numPitches++] = nn.midi;
        }
        ev.kind = ev.numPitches == 0 ? JudgeEvent::timingOnly : (allLegato ? JudgeEvent::legato : JudgeEvent::picked);
        events.push_back (ev);
        i = j;
    }

    results = std::make_unique<Result[]> (juce::jmax ((size_t) 1, events.size()));
}

JudgeSession::Stats JudgeSession::computeStats (double fromBeat, double toBeat) const
{
    Stats s;
    double offsetSum = 0.0;
    for (size_t e = 0; e < events.size(); ++e)
    {
        if (events[e].beat < fromBeat - 1.0e-6 || events[e].beat >= toBeat - 1.0e-6)
            continue;
        switch (verdictOf ((int) e))
        {
            case Verdict::perfect:   ++s.perfect; offsetSum += offsetOf ((int) e); break;
            case Verdict::good:      ++s.good;    offsetSum += offsetOf ((int) e); break;
            case Verdict::wrongNote: ++s.wrong; break;
            case Verdict::miss:      ++s.miss; break;
            case Verdict::pending:   break;
        }
    }
    if (s.perfect + s.good > 0)
        s.meanOffsetMs = (float) (offsetSum / (s.perfect + s.good));
    return s;
}

std::vector<float> JudgeSession::perBarAccuracy() const
{
    const size_t bars = song != nullptr ? song->bars.size() : 0;
    std::vector<int> hit (bars, 0), judged (bars, 0);
    for (size_t e = 0; e < events.size(); ++e)
    {
        const auto v = verdictOf ((int) e);
        if (v == Verdict::pending || ! juce::isPositiveAndBelow (events[e].bar, (int) bars))
            continue;
        ++judged[(size_t) events[e].bar];
        if (isHit (v)) ++hit[(size_t) events[e].bar];
    }
    std::vector<float> acc (bars, -1.0f);
    for (size_t b = 0; b < bars; ++b)
        if (judged[b] > 0)
            acc[b] = (float) hit[b] / (float) judged[b];
    return acc;
}

std::vector<int> JudgeSession::worstBars (int count, int minEvents) const
{
    const size_t bars = song != nullptr ? song->bars.size() : 0;
    std::vector<int> hit (bars, 0), judged (bars, 0);
    for (size_t e = 0; e < events.size(); ++e)
    {
        const auto v = verdictOf ((int) e);
        if (v == Verdict::pending || ! juce::isPositiveAndBelow (events[e].bar, (int) bars))
            continue;
        ++judged[(size_t) events[e].bar];
        if (isHit (v)) ++hit[(size_t) events[e].bar];
    }
    std::vector<int> idx;
    for (size_t b = 0; b < bars; ++b)
        if (judged[b] >= minEvents && hit[b] < judged[b])
            idx.push_back ((int) b);
    std::stable_sort (idx.begin(), idx.end(), [&] (int a, int b)
    {
        return (double) hit[(size_t) a] / judged[(size_t) a] < (double) hit[(size_t) b] / judged[(size_t) b];
    });
    if ((int) idx.size() > count)
        idx.resize ((size_t) count);
    return idx;
}

// =============================================================================
void JudgeFeed::push (const float* dry, int n, const Block& info) noexcept
{
    if (! enabled.load (std::memory_order_relaxed) || n <= 0)
    {
        counter += n;
        return;
    }
    if (sampleFifo.getFreeSpace() < n || blockFifo.getFreeSpace() < 1)
    {
        counter += n;     // 判定线程跟不上：丢掉这一块（判定线程会看到序号跳了）
        return;
    }

    {
        const auto w = sampleFifo.write (n);
        if (w.blockSize1 > 0) std::copy (dry, dry + w.blockSize1, samples.begin() + w.startIndex1);
        if (w.blockSize2 > 0) std::copy (dry + w.blockSize1, dry + w.blockSize1 + w.blockSize2, samples.begin() + w.startIndex2);
    }
    {
        const auto w = blockFifo.write (1);
        Block b = info;
        b.start = counter;
        b.n = n;
        if (w.blockSize1 > 0) blocks[(size_t) w.startIndex1] = b;
        else if (w.blockSize2 > 0) blocks[(size_t) w.startIndex2] = b;
    }
    counter += n;
}

bool JudgeFeed::popBlock (Block& b) noexcept
{
    if (blockFifo.getNumReady() < 1)
        return false;
    const auto r = blockFifo.read (1);
    b = r.blockSize1 > 0 ? blocks[(size_t) r.startIndex1] : blocks[(size_t) r.startIndex2];
    return true;
}

void JudgeFeed::popSamples (float* dest, int n) noexcept
{
    const auto r = sampleFifo.read (n);
    if (r.blockSize1 > 0) std::copy (samples.begin() + r.startIndex1, samples.begin() + r.startIndex1 + r.blockSize1, dest);
    if (r.blockSize2 > 0) std::copy (samples.begin() + r.startIndex2, samples.begin() + r.startIndex2 + r.blockSize2, dest + r.blockSize1);
}

// =============================================================================
Judge::Judge (TabPlayer& p) : juce::Thread ("Lianqin judge"), player (p) {}

Judge::~Judge()
{
    stopAnalysis();
}

void Judge::startAnalysis()
{
    if (! isThreadRunning())
        startThread (juce::Thread::Priority::high);
}

void Judge::stopAnalysis()
{
    stopThread (2000);
}

void Judge::run()
{
    while (! threadShouldExit())
    {
        processPending();
        wait (2);
    }
}

void Judge::setSession (std::shared_ptr<JudgeSession> s)
{
    {
        const juce::SpinLock::ScopedLockType sl (sessionLock);
        pendingSession = std::move (s);
    }
    sessionGen.fetch_add (1);
}

std::shared_ptr<JudgeSession> Judge::getSession() const
{
    const juce::SpinLock::ScopedLockType sl (sessionLock);
    return pendingSession;
}

void Judge::setEnabled (bool on)
{
    feed.setEnabled (on);
    if (! on)
        player.setWaitGate (false, 0.0);
    else
        gateDirty.store (true);
}

void Judge::setWaitMode (bool on)
{
    waitMode.store (on);
    gateDirty.store (true);
    if (! on || ! feed.isEnabled())
        player.setWaitGate (false, 0.0);
}

void Judge::resetResults()
{
    resetRequested.store (true);
}

// ---- 判定线程 --------------------------------------------------------------------
void Judge::reinit()
{
    hop = sr > 60000.0 ? 512 : 256;
    const int fluxN = hop * 4;
    fluxFft = std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 (fluxN)));
    fillHann (fluxWindow, fluxN);
    fluxBuf.assign ((size_t) fluxN * 2, 0.0f);
    prevLog.assign ((size_t) fluxN / 2 + 1, 0.0f);
    fluxHistory.clear();
    fluxPrev1 = fluxPrev2 = 0.0f;
    rmsPrev1 = -100.0f;

    const int winLen = nextPow2 ((int) (sr * 0.085));
    pitchFft = std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 (winLen * 2)));
    fillHann (pitchWindow, winLen);
    pitchBuf.assign ((size_t) winLen * 4, 0.0f);
    scratch.assign ((size_t) winLen * 2, 0.0f);

    nextHopEnd = histEnd + fluxN;
    blockMap.clear();
    checks.clear();
}

void Judge::processPending()
{
    // 换会话
    if (const auto g = sessionGen.load(); g != seenGen)
    {
        seenGen = g;
        {
            const juce::SpinLock::ScopedLockType sl (sessionLock);
            session = pendingSession;
        }
        checks.clear();
        state.assign (session != nullptr ? session->events.size() : 0, 0);
        sweepCursor = 0;
        gateEvent = -1;
        gateDirty.store (true);
        haveLastBlock = false;
        resetPending = false;
        lastResetSample = std::numeric_limits<int64_t>::min();
    }

    if (const double s = sampleRate.load(); s > 0.0 && s != sr)
    {
        sr = s;
        reinit();
    }
    if (sr <= 0.0)
        return;

    if (resetRequested.exchange (false))
        resetFrom (-1.0e9, false);

    JudgeFeed::Block b;
    while (feed.popBlock (b))
    {
        // 序号跳了（判定关着或者跟不上）：补零，重新对齐
        if (b.start > histEnd)
        {
            const int64_t gap = juce::jmin ((int64_t) histSize, b.start - histEnd);
            for (int64_t i = 0; i < gap; ++i)
                hist[(size_t) ((b.start - gap + i) & (histSize - 1))] = 0.0f;
            histEnd = b.start;
            nextHopEnd = juce::jmax (nextHopEnd, histEnd);
        }
        if ((int) scratch.size() < b.n) scratch.resize ((size_t) b.n);
        feed.popSamples (scratch.data(), b.n);
        for (int i = 0; i < b.n; ++i)
            hist[(size_t) ((histEnd + i) & (histSize - 1))] = scratch[(size_t) i];
        histEnd += b.n;

        blockMap.push_back (b);
        while (! blockMap.empty() && blockMap.front().start + blockMap.front().n < histEnd - histSize / 2)
            blockMap.pop_front();

        // 跳转 / 循环
        if (b.rolling && session != nullptr)
        {
            const double startBeat = b.wrapOffset >= 0 ? b.wrapBeat : b.beat;
            if (haveLastBlock && (b.wrapOffset >= 0 || b.beat < lastBlockBeat - 0.05))
            {
                // 循环跳回或者往回跳：从那里开始重新计分（循环时记下上一遍的成绩）。
                // 等分析时间走到跳转点（analyseHop 里）再清空。
                const bool loop = b.loopB > b.loopA;
                if (resetPending)      // 上一次还没执行就又跳了：先执行掉
                    resetFrom (resetBeat, true, resetPassEnd);
                resetPending = true;
                resetSample = b.start + juce::jmax (0, b.wrapOffset);
                resetBeat = startBeat;
                resetPassEnd = loop ? b.loopB : lastBlockBeat;
            }
            else if (! haveLastBlock || b.beat > lastBlockBeat + 2.0)
            {
                const double win = matchWindowMs.load() / 1000.0 * b.nominalBps * sr;
                const auto& ev = session->events;
                const int first = (int) (std::lower_bound (ev.begin(), ev.end(), b.beat - win,
                                                           [] (const JudgeEvent& e, double x) { return e.beat < x; }) - ev.begin());
                sweepCursor = juce::jmax (haveLastBlock ? sweepCursor : 0, first);
                probeCursor = juce::jmax (haveLastBlock ? probeCursor : 0, first);
                gateDirty.store (true);
            }
            lastBlockBeat = b.wrapOffset >= 0 ? b.wrapBeat + b.beatsPerSample * (b.n - b.wrapOffset)
                                              : b.beat + b.beatsPerSample * b.n;
            haveLastBlock = true;
        }

        while (nextHopEnd <= histEnd)
        {
            analyseHop (nextHopEnd);
            nextHopEnd += hop;
        }
    }

    if (session != nullptr && feed.isEnabled() && waitMode.load() && gateDirty.exchange (false))
    {
        double nowBeat = player.getPosition(), nb = 0.0;
        if (! blockMap.empty())
            mapBeat (histEnd - 1 - juce::roundToInt (latencyMs.load() * 0.001 * sr), nowBeat, nb);
        updateGate (nowBeat);
    }
}

void Judge::copyHistory (int64_t start, int n, float* dest) const
{
    for (int i = 0; i < n; ++i)
    {
        const int64_t s = start + i;
        dest[i] = (s >= histEnd - histSize && s < histEnd) ? hist[(size_t) (s & (histSize - 1))] : 0.0f;
    }
}

const JudgeFeed::Block* Judge::mapBeat (int64_t sample, double& beat, double& nominalBps) const
{
    if (blockMap.empty())
        return nullptr;
    auto it = std::upper_bound (blockMap.begin(), blockMap.end(), sample,
                                [] (int64_t s, const JudgeFeed::Block& b) { return s < b.start; });
    if (it != blockMap.begin())
        --it;
    else if (blockMap.front().start - sample > 4096)
        return nullptr;          // 比记录还早太多
    if (! it->rolling || sample >= it->start + it->n + 4096)
        return nullptr;
    const int64_t off = sample - it->start;
    if (it->wrapOffset >= 0 && off >= it->wrapOffset)
        beat = it->wrapBeat + it->beatsPerSample * (double) (off - it->wrapOffset);
    else
        beat = it->beat + it->beatsPerSample * (double) off;
    nominalBps = it->nominalBps;
    return nominalBps > 0.0 ? &*it : nullptr;
}

void Judge::analyseHop (int64_t e)
{
    const int N = hop * 4;

    // ---- 频谱通量 ------------------------------------------------------------
    copyHistory (e - N, N, fluxBuf.data());
    double sumSq = 0.0;
    for (int i = N - hop; i < N; ++i)
        sumSq += (double) fluxBuf[(size_t) i] * fluxBuf[(size_t) i];
    const float rmsDb = juce::Decibels::gainToDecibels ((float) std::sqrt (sumSq / hop), -100.0f);

    for (int i = 0; i < N; ++i)
        fluxBuf[(size_t) i] *= fluxWindow[(size_t) i];
    std::fill (fluxBuf.begin() + N, fluxBuf.end(), 0.0f);
    fluxFft->performFrequencyOnlyForwardTransform (fluxBuf.data(), true);

    const int kMax = juce::jmin (N / 2, (int) (6000.0 / sr * N));
    float flux = 0.0f;
    const float norm = 2.0f / (float) N;
    for (int k = 2; k <= kMax; ++k)
    {
        const float l = std::log1p (200.0f * fluxBuf[(size_t) k] * norm);
        flux += juce::jmax (0.0f, l - prevLog[(size_t) k]);
        prevLog[(size_t) k] = l;
    }

    // ---- 峰值检测（看上一帧是不是局部最大）---------------------------------
    float mean = 0.0f;
    for (auto f : fluxHistory) mean += f;
    mean = fluxHistory.empty() ? 0.0f : mean / (float) fluxHistory.size();
    const float threshold = mean * 1.5f + 1.2f;
    const int64_t peakEnd = e - hop;
    if (fluxPrev1 > threshold && fluxPrev1 >= fluxPrev2 && fluxPrev1 >= flux
        && juce::jmax (rmsPrev1, rmsDb) > -50.0f
        && peakEnd - lastOnsetSample > (int64_t) (0.045 * sr))
    {
        // 精确定位：在这一帧里找能量上升最快的位置
        const int chunk = 32;
        const int span = N + hop;
        const int nChunks = span / chunk;
        scratch.resize ((size_t) juce::jmax ((int) scratch.size(), span));
        copyHistory (peakEnd - N, span, scratch.data());
        float bestRise = 0.0f;
        int bestChunk = nChunks / 2;
        float env1 = 0.0f, env2 = 0.0f;
        for (int c = 0; c < nChunks; ++c)
        {
            float m = 0.0f;
            for (int i = 0; i < chunk; ++i)
                m = juce::jmax (m, std::abs (scratch[(size_t) (c * chunk + i)]));
            const float rise = m - juce::jmax (env1, env2);
            if (c >= 2 && rise > bestRise)
            {
                bestRise = rise;
                bestChunk = c;
            }
            env2 = env1;
            env1 = m;
        }
        const int64_t onset = peakEnd - N + (int64_t) bestChunk * chunk;
        lastOnsetSample = onset;
        handleOnset (onset);
    }

    fluxHistory.push_back (fluxPrev1);
    while (fluxHistory.size() > 12) fluxHistory.pop_front();
    fluxPrev2 = fluxPrev1;
    fluxPrev1 = flux;
    rmsPrev1 = rmsDb;

    if (session == nullptr || session->events.empty())
        return;

    // ---- 漏弹 / 击勾弦核对 / 等待模式 -----------------------------------------
    const int64_t lat = juce::roundToInt (latencyMs.load() * 0.001 * sr);

    // 分析时间（扣掉延迟后）走到了跳转点：上一遍到此为止，重新计分
    if (resetPending && e - lat >= resetSample)
    {
        resetPending = false;
        lastResetSample = resetSample;
        resetFrom (resetBeat, true, resetPassEnd);
    }
    double nowBeat, nb;
    if (mapBeat (e - lat, nowBeat, nb) != nullptr)
        sweep (nowBeat, nb, e);

    // ---- 到时间的音高核对 ----------------------------------------------------
    const int winLen = (int) pitchWindow.size();
    for (size_t i = 0; i < checks.size(); )
    {
        const auto c = checks[i];
        if (c.start + c.length > histEnd)
        {
            ++i;
            continue;
        }
        checks.erase (checks.begin() + (long) i);
        if (! juce::isPositiveAndBelow (c.event, (int) state.size()) || state[(size_t) c.event] != 1)
            continue;

        const bool ok = checkPitches (session->events[(size_t) c.event], c.start, c.length);
        const bool wait = waitMode.load();
        if (ok)
        {
            finalise (c.event, wait || std::abs (c.offsetMs) <= perfectMs ? Verdict::perfect : Verdict::good,
                      wait ? 0.0f : c.offsetMs);
        }
        else if (wait)
        {
            state[(size_t) c.event] = 0;     // 等待模式：弹错了可以再来
            if (c.fromOnset)
            {
                session->lastEvent.store (c.event);
                session->lastVerdict.store ((uint8_t) Verdict::wrongNote);
                session->lastOffsetMs.store (0.0f);
                session->lastSeq.fetch_add (1);
            }
        }
        else
        {
            // 没拨弦的击勾弦：声音太小算漏弹，否则算错音
            Verdict v = Verdict::wrongNote;
            if (! c.fromOnset)
            {
                copyHistory (c.start, c.length, pitchBuf.data());
                double s2 = 0.0;
                for (int k = 0; k < c.length; ++k) s2 += (double) pitchBuf[(size_t) k] * pitchBuf[(size_t) k];
                if (juce::Decibels::gainToDecibels ((float) std::sqrt (s2 / c.length), -100.0f) < -50.0f)
                    v = Verdict::miss;
            }
            finalise (c.event, v, c.offsetMs);
        }
    }
}

void Judge::handleOnset (int64_t onsetSample)
{
    if (session == nullptr || session->events.empty())
        return;

    const int64_t comp = onsetSample - juce::roundToInt (latencyMs.load() * 0.001 * sr);
    // 跳转之前弹的音、但跳转后才检测到：属于上一遍，不再算
    if (comp < lastResetSample || (resetPending && comp >= resetSample))
    {
        if (! (resetPending && comp >= resetSample))
            return;
        // 跳转点之后的起音先到了（重新计分还没执行）：先执行重新计分
        resetPending = false;
        lastResetSample = resetSample;
        resetFrom (resetBeat, true, resetPassEnd);
    }
    double beat, nb;
    const auto* blk = mapBeat (comp, beat, nb);
    if (blk == nullptr)
        return;

    const auto& ev = session->events;
    const double win = matchWindowMs.load() / 1000.0 * nb * sr;
    int best = -1;
    double bestDist = 1.0e9, bestBeat = beat;

    if (waitMode.load() && juce::isPositiveAndBelow (gateEvent, (int) ev.size())
        && state[(size_t) gateEvent] == 0 && beat >= ev[(size_t) gateEvent].beat - win)
    {
        best = gateEvent;
        bestDist = 0.0;
    }
    else
    {
        // 循环时，循环段开头的音可能在跳回之前一点点就弹了：把位置折回循环段里再找
        const bool loop = blk->loopB > blk->loopA;
        double candidates[2] = { beat, beat };
        int numCand = 1;
        if (loop)
        {
            const double len = blk->loopB - blk->loopA;
            if (beat > blk->loopB - win)       candidates[numCand++] = beat - len;
            else if (beat < blk->loopA + win)  candidates[numCand++] = beat + len;
        }
        for (int c = 0; c < numCand; ++c)
        {
            const double x = candidates[c];
            auto it = std::lower_bound (ev.begin(), ev.end(), x - win, [] (const JudgeEvent& e, double y) { return e.beat < y; });
            for (; it != ev.end() && it->beat <= x + win; ++it)
            {
                const int i = (int) (it - ev.begin());
                if (state[(size_t) i] != 0)
                    continue;
                if (loop && (it->beat < blk->loopA - 1.0e-6 || it->beat >= blk->loopB - 1.0e-6))
                    continue;
                const double d = std::abs (it->beat - x);
                if (d < bestDist)
                {
                    bestDist = d;
                    best = i;
                    bestBeat = x;
                }
            }
        }
    }
    beat = bestBeat;

    if (best < 0)
    {
        session->extraOnsets.fetch_add (1);
        return;
    }

    const auto& e = ev[(size_t) best];
    const float offsetMs = waitMode.load() ? 0.0f : (float) ((beat - e.beat) / nb / sr * 1000.0);
    if (e.kind == JudgeEvent::timingOnly)
    {
        finalise (best, waitMode.load() || std::abs (offsetMs) <= perfectMs ? Verdict::perfect : Verdict::good, offsetMs);
        return;
    }
    state[(size_t) best] = 1;
    // 核对窗口：拨弦后 25 ms 开始，最长 85 ms；快的段落缩短到下一个音之前
    const int L = (int) pitchWindow.size();
    const double gap = gapSamples (best, nb);
    const int skip = (int) juce::jmin (0.025 * sr, gap * 0.2);
    const int len = (int) juce::jlimit (0.03 * sr, (double) L, gap - skip - 0.004 * sr);
    checks.push_back ({ best, onsetSample + skip, len, offsetMs, true });
}

void Judge::sweep (double nowBeat, double nb, int64_t hopEnd)
{
    const auto& ev = session->events;
    const int n = (int) ev.size();
    const double win = matchWindowMs.load() / 1000.0 * nb * sr;

    if (waitMode.load())
    {
        if (gateDirty.exchange (false))
            updateGate (nowBeat);

        // 卡在击勾弦音上：不需要拨弦，每 50 ms 看一下音高对不对
        if (juce::isPositiveAndBelow (gateEvent, n) && state[(size_t) gateEvent] == 0
            && ev[(size_t) gateEvent].kind == JudgeEvent::legato
            && nowBeat >= ev[(size_t) gateEvent].beat - 1.0e-6
            && hopEnd - lastLegatoProbe > (int64_t) (0.05 * sr))
        {
            lastLegatoProbe = hopEnd;
            const int winLen = (int) pitchWindow.size();
            if (checkPitches (ev[(size_t) gateEvent], hopEnd - winLen, winLen))
                finalise (gateEvent, Verdict::perfect, 0.0f);
        }
        return;
    }

    // 击勾弦 / 滑音目标音：到时间就核对音高
    while (probeCursor < n)
    {
        const int i = probeCursor;
        const double gap = gapSamples (i, nb);
        const double delaySamples = juce::jmin (0.03 * sr, gap * 0.25);
        if (ev[(size_t) i].beat + delaySamples * nb > nowBeat)
            break;
        ++probeCursor;
        if (state[(size_t) i] == 0 && ev[(size_t) i].kind == JudgeEvent::legato)
        {
            state[(size_t) i] = 1;
            const int len = (int) juce::jlimit (0.03 * sr, (double) pitchWindow.size(), gap - delaySamples - 0.004 * sr);
            checks.push_back ({ i, hopEnd, len, 0.0f, false });
        }
    }

    // 过了窗口还没弹：漏弹
    while (sweepCursor < n && ev[(size_t) sweepCursor].beat + win < nowBeat)
    {
        const int i = sweepCursor++;
        if (state[(size_t) i] == 0)
            finalise (i, Verdict::miss, 0.0f);
    }
}

void Judge::updateGate (double nowBeat)
{
    const auto& ev = session->events;
    auto it = std::lower_bound (ev.begin(), ev.end(), nowBeat - 1.0e-6, [] (const JudgeEvent& e, double x) { return e.beat < x; });
    gateEvent = -1;
    for (; it != ev.end(); ++it)
    {
        const int i = (int) (it - ev.begin());
        if (state[(size_t) i] != 2)
        {
            gateEvent = i;
            break;
        }
    }
    if (feed.isEnabled() && waitMode.load())
        player.setWaitGate (true, gateEvent >= 0 ? ev[(size_t) gateEvent].beat : 1.0e30);
}

void Judge::finalise (int e, Verdict v, float offsetMs)
{
    state[(size_t) e] = 2;
    auto& r = session->results[(size_t) e];
    r.offsetMs.store (offsetMs);
    r.verdict.store ((uint8_t) v);

    if (isHit (v))
    {
        const int c = session->combo.fetch_add (1) + 1;
        if (c > session->bestCombo.load())
            session->bestCombo.store (c);
    }
    else
    {
        session->combo.store (0);
    }

    session->lastEvent.store (e);
    session->lastVerdict.store ((uint8_t) v);
    session->lastOffsetMs.store (offsetMs);
    session->lastSeq.fetch_add (1);

    if (waitMode.load())
    {
        gateDirty.store (true);
        double nowBeat = session->events[(size_t) e].beat;
        updateGate (nowBeat);
        gateDirty.store (false);
    }
}

void Judge::resetFrom (double beat, bool countPass, double passEnd)
{
    if (session == nullptr)
        return;
    const auto& ev = session->events;

    if (countPass)
    {
        int judged = 0, hits = 0;
        for (size_t i = 0; i < ev.size(); ++i)
            if (ev[i].beat >= beat - 1.0e-6 && ev[i].beat < passEnd - 1.0e-6 && state[i] == 2)
            {
                ++judged;
                if (isHit (session->verdictOf ((int) i))) ++hits;
            }
        if (judged > 0)
        {
            session->lastPassAccuracy.store ((float) hits / (float) judged);
            session->passes.fetch_add (1);
        }
    }

    for (size_t i = 0; i < ev.size(); ++i)
        if (ev[i].beat >= beat - 1.0e-6)
        {
            state[i] = 0;
            session->results[i].verdict.store (0);
            session->results[i].offsetMs.store (0.0f);
        }
    checks.erase (std::remove_if (checks.begin(), checks.end(),
                                  [&] (const PendingCheck& c) { return ev[(size_t) c.event].beat >= beat - 1.0e-6; }),
                  checks.end());

    const int first = (int) (std::lower_bound (ev.begin(), ev.end(), beat - 1.0e-6,
                                               [] (const JudgeEvent& e, double x) { return e.beat < x; }) - ev.begin());
    sweepCursor = juce::jmin (sweepCursor, first);
    probeCursor = juce::jmin (probeCursor, first);
    if (beat < -1.0e8) { sweepCursor = probeCursor = 0; session->passes.store (0); session->lastPassAccuracy.store (-1.0f); }
    session->combo.store (0);
    gateDirty.store (true);
}

// ---- 音高核对 --------------------------------------------------------------------
double Judge::gapSamples (int e, double nominalBps) const
{
    const auto& ev = session->events;
    if (e + 1 >= (int) ev.size() || nominalBps <= 0.0)
        return 1.0e9;
    return (ev[(size_t) e + 1].beat - ev[(size_t) e].beat) / nominalBps;
}

bool Judge::checkPitches (const JudgeEvent& ev, int64_t start, int length)
{
    const int full = (int) pitchWindow.size();
    const int L = juce::jlimit (256, full, length) & ~1;
    copyHistory (start, L, pitchBuf.data());

    double s2 = 0.0;
    for (int i = 0; i < L; ++i) s2 += (double) pitchBuf[(size_t) i] * pitchBuf[(size_t) i];
    if (juce::Decibels::gainToDecibels ((float) std::sqrt (s2 / L), -100.0f) < -55.0f)
        return false;

    const bool bass = session != nullptr && session->song != nullptr
                      && juce::isPositiveAndBelow (session->track, (int) session->song->tracks.size())
                      && session->song->tracks[(size_t) session->track].isBass();

    // 单音：先用 YIN（八度误判也算对）
    if (ev.numPitches == 1)
    {
        // 窗口短（快的段落）时用整段，长的时候用一半就够
        const int yinLen = (bass || L < full) ? L : L / 2;
        const float minHz = juce::jmax (bass ? 28.0f : 60.0f, (float) (sr / (yinLen / 2) * 1.05));
        const auto r = yin.detect (pitchBuf.data(), yinLen, sr, minHz, 1500.0f, 0.15f);
        if (r.frequency > 0.0f)
        {
            const double m = 69.0 + 12.0 * std::log2 (r.frequency / 440.0);
            const double target = ev.midi[0];
            for (double oct : { 0.0, 12.0, -12.0 })
                if (std::abs (m - (target + oct)) < 0.6)
                    return true;
            // 很确定是相邻的音（弹错一两品）：直接判错；其他情况可能是前一个音还在响，交给下面的谐波核对
            if (r.clarity > 0.9f && std::abs (m - target) < 2.6 && std::abs (m - target) >= 0.6)
                return false;
        }
    }

    // 谐波显著度：谱上的音和它上下半音比，谁的泛音更明显
    for (int i = 0; i < L; ++i)
        pitchBuf[(size_t) i] *= 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) L);
    std::fill (pitchBuf.begin() + L, pitchBuf.end(), 0.0f);
    pitchFft->performFrequencyOnlyForwardTransform (pitchBuf.data(), true);
    const int fftN = full * 2;
    const double binHz = sr / fftN;
    const int kTop = juce::jmin (fftN / 2 - 2, (int) (5000.0 / binHz));

    // 底噪：60–5000 Hz 的中位数
    scratch.resize ((size_t) juce::jmax ((int) scratch.size(), kTop + 1));
    const int kLow = juce::jmax (1, (int) (60.0 / binHz));
    int cnt = 0;
    for (int k = kLow; k <= kTop; ++k)
        scratch[(size_t) cnt++] = juce::Decibels::gainToDecibels (pitchBuf[(size_t) k] + 1.0e-9f, -160.0f);
    if (cnt == 0) return false;
    std::nth_element (scratch.begin(), scratch.begin() + cnt / 2, scratch.begin() + cnt);
    const float floorDb = scratch[(size_t) cnt / 2];

    auto score = [&] (int midi)
    {
        const double f0 = midiToHz (midi);
        float sum = 0.0f;
        int harmonics = 0;
        for (int h = 1; h <= 8; ++h)
        {
            const double f = f0 * h;
            if (f > 5000.0) break;
            const int k0 = juce::jmax (1, (int) std::floor (f * 0.982 / binHz));
            const int k1 = juce::jmin (kTop, (int) std::ceil (f * 1.018 / binHz));
            float peak = 0.0f;
            for (int k = k0; k <= k1; ++k)
                peak = juce::jmax (peak, pitchBuf[(size_t) k]);
            sum += juce::jmax (0.0f, juce::Decibels::gainToDecibels (peak + 1.0e-9f, -160.0f) - floorDb);
            ++harmonics;
        }
        return harmonics > 0 ? sum / (float) harmonics : 0.0f;
    };

    int present = 0;
    for (int p = 0; p < ev.numPitches; ++p)
    {
        const int m = ev.midi[(size_t) p];
        const float s = score (m);
        const float neighbours = juce::jmax (score (m - 1), score (m + 1));
        if (s > 12.0f && s > neighbours * 1.12f)
            ++present;
    }
    const int need = ev.numPitches == 1 ? 1 : juce::jmax (1, (int) std::ceil (ev.numPitches * 0.6));
    return present >= need;
}
