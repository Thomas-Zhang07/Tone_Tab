#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <vector>
#include "fx/EffectChain.h"
#include "dsp/DcBlocker.h"
#include "drums/DrumMachine.h"
#include "looper/Looper.h"
#include "practice/TabPlayer.h"
#include "practice/Judge.h"
#include "practice/Recorder.h"

// 宿主的音频引擎。
//
// 信号流向：
//   声卡输入（吉他通道）─┬─> 干信号环形缓冲（调音器 / 以后的弹奏判定）
//                         └─> 输入增益 ─> 效果器链（立体声）─> 吉他音量 ─┐
//                                                    鼓机 ─> 鼓音量 ─────┴─> 声卡输出
//
// 音频线程不加锁、不分配内存。效果器链以“快照”的形式交给音频线程，
// 用完的旧快照通过无锁队列还给界面线程释放。
class AudioEngine : public juce::AudioIODeviceCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    // ---- 参数（界面线程）----------------------------------------------------
    void setInputGainDb (float db)        { inputGainDb.store (db); }
    void setOutputGainDb (float db)       { outputGainDb.store (db); }
    void setMonitoring (bool on)          { monitoring.store (on); }
    void setGuitarChannel (int deviceChannel) { guitarDeviceChannel.store (deviceChannel); }
    int  getGuitarChannel() const         { return guitarDeviceChannel.load(); }

    DrumMachine& getDrums()               { return drums; }
    Looper& getLooper()                   { return looper; }
    TabPlayer& getTabPlayer()             { return tabPlayer; }
    Judge& getJudge()                     { return judge; }
    Recorder& getRecorder()               { return recorder; }

    // ---- 效果器链 -----------------------------------------------------------
    void publishChain (std::unique_ptr<ChainSnapshot> snapshot);
    void collectGarbage();
    // 退出时调用（音频回调已移除）：立即释放所有链路快照里的效果器
    void releaseAllChains();

    double getSampleRate() const          { const double sr = currentSampleRate.load(); return sr > 0 ? sr : lastSampleRate.load(); }
    int    getBlockSize() const           { return preparedBlockSize.load(); }
    int    getChainLatencySamples() const { return chainLatencySamples.load(); }

    // ---- 电平（界面线程读取后清零）------------------------------------------
    float takeInputPeak()                 { return inputPeak.exchange (0.0f); }
    float takeOutputPeak()                { return outputPeak.exchange (0.0f); }
    bool  takeInputClipped()              { return inputClipped.exchange (false); }
    bool  takeOutputClipped()             { return outputClipped.exchange (false); }
    bool  isGuitarChannelActive() const   { return guitarChannelActive.load(); }

    double copyRecentDrySamples (float* dest, int numSamples) const;

    // ---- AudioIODeviceCallback ----------------------------------------------
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    // 离线测试用：不经过声卡，直接处理一段单声道输入，输出立体声
    void prepareForOffline (double sampleRate, int blockSize);
    void processOffline (const float* monoIn, float* outL, float* outR, int numSamples);

    // VST3 host: no audio device, no practice features.
    void setPracticeEnabled (bool enabled) { practiceEnabled.store (enabled); }
    void prepareForPlugin (double sampleRate, int blockSize) { prepareInternal (sampleRate, blockSize); }
    void processPluginBlock (const float* guitarIn, float* outL, float* outR, int numSamples) noexcept;

private:
    void updateActiveInputMap (juce::AudioIODevice* device);
    void prepareInternal (double sampleRate, int blockSize);
    void processSubBlock (const float* guitarIn, float* outL, float* outR, int n) noexcept;
    void retire (ChainSnapshot* s) noexcept;

    std::atomic<float> inputGainDb  { 0.0f };
    std::atomic<float> outputGainDb { -6.0f };
    std::atomic<bool>  monitoring   { true };
    std::atomic<bool>  practiceEnabled { true };
    std::atomic<int>   guitarDeviceChannel { 0 };

    std::atomic<float> inputPeak  { 0.0f };
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<bool>  inputClipped { false }, outputClipped { false };
    std::atomic<bool>  guitarChannelActive { false };

    static constexpr int maxDeviceChannels = 64;
    std::atomic<int> activeIndexForDeviceChannel[maxDeviceChannels];

    juce::SmoothedValue<float> inputGain  { 1.0f };
    juce::SmoothedValue<float> outputGain { 0.5f };
    juce::AudioBuffer<float> chainBuffer, incomingBuffer;
    std::vector<float> monoBuffer;
    DcBlocker masterDc;

    // 链路快照：pending 由界面线程放入，音频线程取走；current 只归音频线程
    std::atomic<ChainSnapshot*> pending { nullptr };
    ChainSnapshot* current = nullptr;
    ChainSnapshot* incoming = nullptr;
    int fadeState = 0;   // 0 正常，1 正在淡出旧链路，2 正在淡入新链路

    static constexpr int garbageSize = 64;
    juce::AbstractFifo garbageFifo { garbageSize };
    std::array<ChainSnapshot*, garbageSize> garbage {};

    DrumMachine drums;
    Looper looper;
    TabPlayer tabPlayer;
    Judge judge { tabPlayer };
    Recorder recorder { tabPlayer };

    static constexpr int ringSize = 1 << 15;
    std::vector<float> dryRing;
    std::atomic<uint32_t> dryWritePos { 0 };
    std::atomic<double> currentSampleRate { 0.0 }, lastSampleRate { 48000.0 };
    std::atomic<int> preparedBlockSize { 512 };
    std::atomic<int> chainLatencySamples { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
