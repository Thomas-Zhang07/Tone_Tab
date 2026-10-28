#include "TabPlayer.h"
#include "drums/DrumMachine.h"
#include "fx/HostTempo.h"
#include <algorithm>

namespace
{
    // GM 鼓号 -> 合成鼓件
    DrumInstrument drumForGm (int note)
    {
        switch (note)
        {
            case 35: case 36:                       return kick;
            case 37:                                return sideStick;
            case 38: case 39: case 40:              return snare;
            case 42: case 44:                       return closedHat;
            case 46:                                return openHat;
            case 41: case 43:                       return tomLow;
            case 45: case 47:                       return tomMid;
            case 48: case 50:                       return tomHigh;
            case 49: case 52: case 55: case 57:     return crash;
            case 51: case 53: case 59:              return ride;
            default:                                return note < 35 ? sideStick : closedHat;
        }
    }
}

TabPlayer::TabPlayer()
{
    resetTrackGains();
}

// ---- 界面线程 ------------------------------------------------------------------
void TabPlayer::setSong (std::shared_ptr<const Song> song)
{
    playing.store (false);
    if (currentSong != nullptr)
        retired.push_back (currentSong);
    currentSong = std::move (song);
    songForAudio.store (currentSong.get());
    seekRequest.store (0.0);
    loopOn.store (false);
}

void TabPlayer::releaseRetiredSongs()
{
    // 音频线程已经不再用的旧歌才释放
    const Song* inUse = songInUse.load();
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [inUse] (const std::shared_ptr<const Song>& s) { return s.get() != inUse; }),
                   retired.end());
}

void TabPlayer::setPlaying (bool shouldPlay)
{
    playing.store (shouldPlay && currentSong != nullptr);
}

// ---- 音频线程 ------------------------------------------------------------------
void TabPlayer::prepare (double sampleRate)
{
    sr = sampleRate;
    demoSynth.prepare (sampleRate);
    backingSynth.prepare (sampleRate);
    demoTone.prepare (sampleRate);
    demoGain.reset (sampleRate, 0.05);
    backingGain.reset (sampleRate, 0.05);
}

void TabPlayer::resetCursors (const Song& song, double beat) noexcept
{
    cursors.resize (song.tracks.size());   // 只有换歌时才会变大小（首次进入时可能分配一次）
    for (size_t t = 0; t < song.tracks.size(); ++t)
    {
        const auto& notes = song.tracks[t].notes;
        auto it = std::lower_bound (notes.begin(), notes.end(), beat - 1.0e-9,
                                    [] (const SongNote& n, double b) { return n.start < b; });
        cursors[t] = (size_t) std::distance (notes.begin(), it);
    }
}

void TabPlayer::triggerRange (const Song& song, double from, double to, double beatsPerSample, DrumMachine& drums) noexcept
{
    const int selected = trackIndex.load();
    const bool demo = demoOn.load();

    for (size_t t = 0; t < song.tracks.size() && t < cursors.size(); ++t)
    {
        const auto& track = song.tracks[t];
        const bool isSelected = (int) t == selected;
        const float trackGain = t < (size_t) maxTracks ? trackGains[t].load (std::memory_order_relaxed) : 1.0f;
        auto& c = cursors[t];

        while (c < track.notes.size() && track.notes[c].start < to)
        {
            const auto& n = track.notes[c];
            ++c;
            if (n.start < from - 1.0e-9)
                continue;

            const int delay = juce::jmax (0, (int) ((n.start - from) / beatsPerSample));

            if (track.percussion)
            {
                if (trackGain > 0.001f)
                    drums.triggerFromSong (drumForGm (n.midi), juce::jmin (1.0f, (n.accent ? 1.0f : 0.8f) * trackGain));   // 鼓只能在块开头触发
                continue;
            }
            if ((isSelected && ! demo) || trackGain <= 0.001f)
                continue;

            PluckSynth::NoteOn on;
            on.midi = n.midi;
            on.velocity = n.accent ? 1.0f : 0.8f;
            on.lengthSeconds = song.beatToSeconds (n.start + n.length) - song.beatToSeconds (n.start);
            on.lengthSeconds /= juce::jmax (0.25f, speed.load());
            on.delaySamples = delay;
            on.bendSemitones = n.bend;
            on.palmMute = n.palmMute;
            on.dead = n.dead;
            on.harmonic = n.harmonic;
            on.vibrato = n.vibrato;
            on.hammer = n.hammerPull;
            on.letRing = n.letRing;
            on.bass = track.isBass();
            on.pan = isSelected ? 0.0f : ((t % 2) == 0 ? -0.35f : 0.35f);
            on.gain = trackGain;
            (isSelected ? demoSynth : backingSynth).noteOn (on);
        }
    }
}

void TabPlayer::render (float* left, float* right, int numSamples, DrumMachine& drums) noexcept
{
    // 换歌
    const Song* wanted = songForAudio.load();
    if (wanted != activeSong)
    {
        activeSong = wanted;
        songInUse.store (activeSong);
        demoSynth.allNotesOff(); backingSynth.allNotesOff();
        position = 0.0;
        cursors.clear();
        if (activeSong != nullptr)
            resetCursors (*activeSong, 0.0);
    }

    demoGain.setTargetValue (juce::Decibels::decibelsToGain (demoGainDb.load(), -60.0f));
    backingGain.setTargetValue (juce::Decibels::decibelsToGain (backingGainDb.load(), -60.0f));

    if (activeSong != nullptr)
    {
        const auto& song = *activeSong;

        // 跳转
        const double seekTo = seekRequest.exchange (-1.0);
        if (seekTo >= 0.0)
        {
            position = juce::jlimit (0.0, song.lengthBeats, seekTo);
            resetCursors (song, position);
            demoSynth.allNotesOff(); backingSynth.allNotesOff();
        }

        const bool play = playing.load();
        if (play && ! wasPlaying)
        {
            if (position >= song.lengthBeats - 1.0e-6)
            {
                position = loopOn.load() ? loopA.load() : 0.0;
                resetCursors (song, position);
            }
            // 预备拍：一小节，按四分音符敲
            const int bi = song.barIndexAt (position);
            countInTotal = countIn.load() && bi >= 0 ? song.bars[(size_t) bi].length : 0.0;
            countInLeft = countInTotal;
        }
        if (! play && wasPlaying)
        {
            demoSynth.allNotesOff();
            backingSynth.allNotesOff();
        }
        wasPlaying = play;

        if (play)
        {
            const float spd = speed.load();
            const double bpm = song.tempoAt (position);
            HostTempo::bpm.store ((float) (bpm * spd));
            const double beatsPerSample = bpm / 60.0 / sr * spd;
            double blockBeats = beatsPerSample * numSamples;

            lastBlock = {};
            lastBlock.beat = position;
            if (countInLeft > 0.0)
            {
                countingIn.store (true);
                const double before = countInTotal - countInLeft;
                const double after = before + blockBeats;
                for (double b = std::ceil (before - 1.0e-9); b < after; b += 1.0)
                    if (b >= before - 1.0e-9 && b < countInTotal - 1.0e-9)
                        drums.triggerFromSong (b < 0.5 ? clickHigh : clickLow, 1.0f);
                countInLeft -= blockBeats;
                blockBeats = 0.0;
            }
            else
            {
                countingIn.store (false);
                double from = position;
                double to = position + blockBeats;

                // 等待模式：停在下一个还没弹对的音前面
                if (waitOn.load() && to > waitGate.load())
                    to = juce::jmax (from, waitGate.load());

                lastBlock.beat = from;
                lastBlock.beatsPerSample = numSamples > 0 ? (to - from) / numSamples : 0.0;
                lastBlock.nominalBps = beatsPerSample;
                lastBlock.rolling = true;
                lastBlock.wrapOffset = -1;
                lastBlock.loopA = loopOn.load() ? loopA.load() : 0.0;
                lastBlock.loopB = loopOn.load() ? loopB.load() : 0.0;

                // A-B 循环
                if (loopOn.load() && to >= loopB.load() && from < loopB.load() + 1.0e-6)
                {
                    triggerRange (song, from, loopB.load(), beatsPerSample, drums);
                    if (lastBlock.beatsPerSample > 0.0)
                        lastBlock.wrapOffset = (int) ((loopB.load() - from) / lastBlock.beatsPerSample);
                    lastBlock.wrapBeat = loopA.load();
                    loopWraps.fetch_add (1);
                    from = loopA.load();
                    to = from + (to - loopB.load());
                    resetCursors (song, from);
                }

                // 节拍器：小节第一拍重音，其余每个四分音符一下
                if (metronome.load())
                {
                    for (double b = std::ceil (from - 1.0e-9); b < to; b += 1.0)
                    {
                        const int bi = song.barIndexAt (b);
                        const bool downbeat = bi >= 0 && std::abs (song.bars[(size_t) bi].start - b) < 1.0e-6;
                        drums.triggerFromSong (downbeat ? clickHigh : clickLow, 0.9f);
                    }
                }

                triggerRange (song, from, to, beatsPerSample, drums);
                position = to;

                if (position >= song.lengthBeats)
                {
                    position = song.lengthBeats;
                    playing.store (false);
                }
            }
        }
        else
        {
            countingIn.store (false);
            lastBlock = {};
            lastBlock.beat = position;
        }

        positionOut.store (position);
    }
    else
    {
        lastBlock = {};
    }

    // 合成器：示范和伴奏各自的音量
    float dL[512], dR[512], bL[512], bR[512], mixL[512], mixR[512];
    for (int pos = 0; pos < numSamples; )
    {
        const int n = juce::jmin (512, numSamples - pos);
        std::fill (dL, dL + n, 0.0f); std::fill (dR, dR + n, 0.0f);
        std::fill (bL, bL + n, 0.0f); std::fill (bR, bR + n, 0.0f);
        demoSynth.render (dL, dR, n);
        backingSynth.render (bL, bR, n);
        for (int i = 0; i < n; ++i)
        {
            const float gd = demoGain.getNextValue(), gb = backingGain.getNextValue();
            mixL[i] = dL[i] * gd + bL[i] * gb;
            mixR[i] = dR[i] * gd + bR[i] * gb;
        }

        demoTone.process (mixL, mixR, n);

        for (int i = 0; i < n; ++i)
        {
            left[pos + i] += mixL[i];
            right[pos + i] += mixR[i];
        }
        pos += n;
    }
}
