#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <deque>
#include <limits>
#include <memory>
#include <vector>
#include "song/Song.h"
#include "PitchDetector.h"

class TabPlayer;

// =============================================================================
// 弹奏判定
//
//   音频线程：每块把（输入增益后、效果器前的）干信号和这一块对应的谱子位置推进 JudgeFeed。
//   判定线程：起音检测（频谱通量）→ 找到时间最近的谱上音符 → 拨弦后 25 ms 取一段做音高核对。
//             击弦 / 勾弦 / 滑音的目标音没有拨弦，到时间直接核对音高。
//             推弦、泛音、闷音只判时间。
//   界面：读 JudgeSession 里每个音符组的结果（原子变量），画到谱面上。
//
// 只核对“谱上该出现的音在不在”，不去识别你弹了什么和弦，所以和弦也能判。
// 光靠声音分不出同一个音在哪根弦上弹，因此不判弦。
// =============================================================================

enum class Verdict : uint8_t { pending = 0, perfect, good, wrongNote, miss };

struct JudgeEvent
{
    enum Kind : uint8_t { picked, legato, timingOnly };

    double beat = 0.0;
    int bar = 0;                 // song->bars 的下标
    int firstNote = 0, numNotes = 0;   // 在音轨 notes 里的范围（同一时刻的音是连续的）
    std::array<int, 8> midi {};  // 要核对的音高（去重）
    int numPitches = 0;
    Kind kind = picked;
};

// 一首歌一条音轨的判定结果。事件表建好以后不再改；结果是原子变量，界面线程随时可以读。
class JudgeSession
{
public:
    JudgeSession (std::shared_ptr<const Song> song, int trackIndex);

    const std::shared_ptr<const Song> song;
    const int track;
    std::vector<JudgeEvent> events;      // 按拍排序
    std::vector<int> noteToEvent;        // 音轨里第 i 个音属于哪个事件（-1 = 不判）

    struct Result
    {
        std::atomic<uint8_t> verdict { 0 };
        std::atomic<float> offsetMs { 0.0f };   // 正 = 弹晚了
    };
    std::unique_ptr<Result[]> results;

    Verdict verdictOf (int e) const      { return (Verdict) results[(size_t) e].verdict.load (std::memory_order_relaxed); }
    float offsetOf (int e) const         { return results[(size_t) e].offsetMs.load (std::memory_order_relaxed); }

    // 实时信息（判定线程写）
    std::atomic<int> combo { 0 }, bestCombo { 0 }, passes { 0 }, extraOnsets { 0 };
    std::atomic<float> lastPassAccuracy { -1.0f };
    // 最近一次判定（谱面上飘字用）：seq 每次加一
    std::atomic<uint32_t> lastSeq { 0 };
    std::atomic<int> lastEvent { -1 };
    std::atomic<uint8_t> lastVerdict { 0 };
    std::atomic<float> lastOffsetMs { 0.0f };

    struct Stats
    {
        int perfect = 0, good = 0, wrong = 0, miss = 0;
        float meanOffsetMs = 0.0f;       // 只算命中的
        int judged() const               { return perfect + good + wrong + miss; }
        float accuracy() const           { return judged() > 0 ? (float) (perfect + good) / (float) judged() : 0.0f; }
    };
    Stats computeStats (double fromBeat = -1.0e9, double toBeat = 1.0e9) const;
    // 每个小节的命中率（-1 = 这一节没有判过的音）
    std::vector<float> perBarAccuracy() const;
    // 命中率最低的几个小节（至少 minEvents 个判过的音）
    std::vector<int> worstBars (int count, int minEvents = 2) const;
};

// -----------------------------------------------------------------------------
// 音频线程 -> 判定线程（无锁，单生产者单消费者）
class JudgeFeed
{
public:
    struct Block
    {
        int64_t start = 0;       // 绝对采样序号
        int n = 0;
        double beat = 0.0;       // 块开头对应的谱子位置
        double beatsPerSample = 0.0;     // 实际前进速度（等待模式卡住时为 0）
        double nominalBps = 0.0;         // 按速度表的前进速度（换算毫秒用）
        bool rolling = false;            // 正在播放（不含预备拍）
        int wrapOffset = -1;             // A-B 循环在这一块里跳回的位置
        double wrapBeat = 0.0;
        double loopA = 0.0, loopB = 0.0;
    };

    void setEnabled (bool e)             { enabled.store (e); }
    bool isEnabled() const               { return enabled.load(); }

    // 音频线程
    void push (const float* dry, int n, const Block& info) noexcept;   // info.start / n 由这里填

    // 判定线程：取出新的块和采样
    bool popBlock (Block& b) noexcept;
    void popSamples (float* dest, int n) noexcept;

private:
    static constexpr int sampleCapacity = 1 << 17;
    static constexpr int blockCapacity = 4096;
    std::atomic<bool> enabled { false };
    juce::AbstractFifo sampleFifo { sampleCapacity };
    juce::AbstractFifo blockFifo { blockCapacity };
    std::vector<float> samples = std::vector<float> ((size_t) sampleCapacity);
    std::vector<Block> blocks = std::vector<Block> ((size_t) blockCapacity);
    int64_t counter = 0;
};

// -----------------------------------------------------------------------------
class Judge : private juce::Thread
{
public:
    explicit Judge (TabPlayer& player);
    ~Judge() override;

    JudgeFeed& getFeed()                 { return feed; }

    // 界面线程
    void startAnalysis();                // 启动判定线程
    void stopAnalysis();
    void setSession (std::shared_ptr<JudgeSession> s);
    std::shared_ptr<JudgeSession> getSession() const;
    void setEnabled (bool on);
    bool isEnabled() const               { return feed.isEnabled(); }
    void setWaitMode (bool on);
    bool isWaitMode() const              { return waitMode.load(); }
    void setLatencyMs (float ms)         { latencyMs.store (ms); }    // 声卡往返延迟 + 校准
    float getLatencyMs() const           { return latencyMs.load(); }
    void setMatchWindowMs (float ms)     { matchWindowMs.store (ms); }   // 校准时放宽
    void resetResults();                 // 清空当前会话的成绩
    void setSampleRate (double sr)       { sampleRate.store (sr); }

    // 判定线程循环体（测试里直接调用）
    void processPending();

    static constexpr float perfectMs = 40.0f;
    static constexpr float goodMs = 150.0f;      // 默认匹配窗口

private:
    void run() override;
    void reinit();
    void analyseHop (int64_t hopEnd);
    const JudgeFeed::Block* mapBeat (int64_t sample, double& beat, double& nominalBps) const;
    void handleOnset (int64_t onsetSample);
    void finalise (int e, Verdict v, float offsetMs);
    void sweep (double nowBeat, double nominalBps, int64_t hopEnd);
    void updateGate (double nowBeat);
    bool checkPitches (const JudgeEvent& ev, int64_t start, int length);
    double gapSamples (int event, double nominalBps) const;   // 到下一个音还有多少采样
    void copyHistory (int64_t start, int n, float* dest) const;
    void resetFrom (double beat, bool countPass, double passEnd = 1.0e9);

    TabPlayer& player;
    JudgeFeed feed;

    mutable juce::SpinLock sessionLock;
    std::shared_ptr<JudgeSession> pendingSession;
    std::atomic<uint32_t> sessionGen { 0 };
    std::atomic<bool> waitMode { false }, resetRequested { false }, gateDirty { true };
    std::atomic<float> latencyMs { 0.0f }, matchWindowMs { goodMs };
    std::atomic<double> sampleRate { 48000.0 };

    // ---- 只在判定线程里用 ----------------------------------------------------
    std::shared_ptr<JudgeSession> session;
    uint32_t seenGen = 0;
    double sr = 0.0;
    int hop = 256;

    static constexpr int histSize = 1 << 17;
    std::vector<float> hist = std::vector<float> ((size_t) histSize);
    int64_t histEnd = 0;                  // 下一个要写入的绝对序号
    std::deque<JudgeFeed::Block> blockMap;
    double lastBlockBeat = 0.0;
    bool haveLastBlock = false;

    // 循环跳回 / 往回跳时的重新计分要等分析时间追上跳转点再做：
    // 跳转前最后几个音的起音、音高核对、漏弹判断都比音频晚几十毫秒，
    // 在跳转那一刻就清空的话，这些迟到的结果会写进新的一遍，而且下一遍弹对了也改不过来。
    bool resetPending = false;
    int64_t resetSample = 0, lastResetSample = std::numeric_limits<int64_t>::min();
    double resetBeat = 0.0, resetPassEnd = 0.0;

    // 起音检测
    std::unique_ptr<juce::dsp::FFT> fluxFft;
    std::vector<float> fluxWindow, fluxBuf, prevLog;
    std::deque<float> fluxHistory;
    float fluxPrev1 = 0.0f, fluxPrev2 = 0.0f;
    float rmsPrev1 = -100.0f;
    int64_t nextHopEnd = 0, lastOnsetSample = -1000000;

    // 音高核对
    std::unique_ptr<juce::dsp::FFT> pitchFft;
    std::vector<float> pitchWindow, pitchBuf, scratch;
    PitchDetector yin;

    struct PendingCheck { int event; int64_t start; int length; float offsetMs; bool fromOnset; };
    std::vector<PendingCheck> checks;
    std::vector<uint8_t> state;           // 0 空闲，1 核对中，2 已判
    int sweepCursor = 0, probeCursor = 0;
    int gateEvent = -1;
    int64_t gateSince = 0, lastLegatoProbe = 0;
};
