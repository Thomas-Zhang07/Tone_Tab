#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include "song/Song.h"
#include "PluckSynth.h"
#include "DemoTone.h"

class DrumMachine;

// 谱子播放：走带（变速、A-B 循环、预备拍）、示范音、伴奏、谱里的鼓轨。
// 界面线程通过原子变量控制；音频线程在 render() 里推进位置、触发音符。
class TabPlayer
{
public:
    TabPlayer();

    // ---- 界面线程 -----------------------------------------------------------
    void setSong (std::shared_ptr<const Song> song);
    std::shared_ptr<const Song> getSong() const  { return currentSong; }
    void releaseRetiredSongs();                  // 定时调用

    void setTrack (int index)                    { trackIndex.store (index); }   // -1 = 全部音轨合奏（没有“我弹的”那一轨）

    // 每条音轨的音量（混音台：静音 = 0）
    static constexpr int maxTracks = 64;
    void setTrackGain (int t, float gain)        { if (juce::isPositiveAndBelow (t, maxTracks)) trackGains[(size_t) t].store (gain); }
    void resetTrackGains()                       { for (auto& g : trackGains) g.store (1.0f); }
    int  getTrack() const                        { return trackIndex.load(); }
    void setPlaying (bool shouldPlay);
    // 音频线程用（录音回放同步）：不碰界面线程的 shared_ptr
    void startFromAudioThread() noexcept         { if (songForAudio.load() != nullptr) playing.store (true); }
    void setPlayingFlag (bool p) noexcept        { playing.store (p); }
    bool isPlaying() const                       { return playing.load(); }
    void seek (double beat)                      { seekRequest.store (beat); }
    void setSpeed (float s)                      { speed.store (juce::jlimit (0.25f, 1.5f, s)); }
    float getSpeed() const                       { return speed.load(); }
    void setLoop (double a, double b, bool on)   { loopA.store (a); loopB.store (b); loopOn.store (on && b > a); }
    bool isLooping() const                       { return loopOn.load(); }
    double getLoopA() const                      { return loopA.load(); }
    double getLoopB() const                      { return loopB.load(); }
    void setCountIn (bool c)                     { countIn.store (c); }
    void setMetronome (bool m)                   { metronome.store (m); }
    void setDemoOn (bool d)                      { demoOn.store (d); }
    void setDemoVolumeDb (float db)              { demoGainDb.store (db); }
    void setBackingVolumeDb (float db)           { backingGainDb.store (db); }

    double getPosition() const                   { return positionOut.load(); }
    int getLoopWraps() const                     { return loopWraps.load(); }   // A-B 循环跳回过几次（速度阶梯用）

    // 等待模式：播放停在 gateBeat，直到判定线程把它往后挪（判定线程调用）
    void setWaitGate (bool on, double gateBeat)  { waitGate.store (gateBeat); waitOn.store (on); }

    // 音频线程：刚 render 完的这一块对应的谱子位置（给弹奏判定用）
    struct BlockPosition
    {
        double beat = 0.0, beatsPerSample = 0.0, nominalBps = 0.0;
        bool rolling = false;
        int wrapOffset = -1;          // 这一块里从 B 跳回 A 的位置（采样）
        double wrapBeat = 0.0;
        double loopA = 0.0, loopB = 0.0;   // 循环段（B <= A 表示没循环）
    };
    const BlockPosition& getLastBlockPosition() const noexcept { return lastBlock; }
    bool isCountingIn() const                    { return countingIn.load(); }

    // ---- 音频线程 -----------------------------------------------------------
    void prepare (double sampleRate);
    void render (float* left, float* right, int numSamples, DrumMachine& drums) noexcept;

private:
    void resetCursors (const Song& song, double beat) noexcept;
    void triggerRange (const Song& song, double from, double to, double beatsPerSample, DrumMachine& drums) noexcept;

    // 界面线程拥有的歌；音频线程只拿裸指针
    std::shared_ptr<const Song> currentSong;
    std::vector<std::shared_ptr<const Song>> retired;
    std::atomic<const Song*> songForAudio { nullptr };
    std::atomic<const Song*> songInUse { nullptr };

    std::atomic<int> trackIndex { 0 };
    std::array<std::atomic<float>, maxTracks> trackGains;
    std::atomic<bool> playing { false }, loopOn { false }, countIn { true }, metronome { false }, demoOn { true };
    std::atomic<float> speed { 1.0f }, demoGainDb { -6.0f }, backingGainDb { -12.0f };
    std::atomic<double> loopA { 0.0 }, loopB { 0.0 }, seekRequest { -1.0 }, positionOut { 0.0 };
    std::atomic<bool> countingIn { false };
    std::atomic<int> loopWraps { 0 };
    std::atomic<bool> waitOn { false };
    std::atomic<double> waitGate { 1.0e30 };
    BlockPosition lastBlock;

    // 音频线程状态
    double sr = 48000.0;
    const Song* activeSong = nullptr;
    double position = 0.0;
    bool wasPlaying = false;
    double countInLeft = 0.0;       // 拍
    double countInTotal = 0.0;
    std::vector<size_t> cursors;    // 每条轨下一个要触发的音
    int lastTrack = -1;
    PluckSynth demoSynth, backingSynth;
    DemoTone demoTone;
    juce::SmoothedValue<float> demoGain { 0.5f }, backingGain { 0.25f };
};
