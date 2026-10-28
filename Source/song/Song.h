#pragma once

#include <juce_core/juce_core.h>
#include <memory>
#include <vector>

// =============================================================================
// 谱子的内部格式：所有导入器（GP3/4/5、GPX、GP7/8）都先转成 RawScore，
// 再由 buildSong() 展开反复记号、合并连音线，得到按演奏顺序排好的 Song。
// 时间单位统一用“四分音符拍”（beat）。
// 弦的编号：0 = 最细的弦（高音 E），和吉他谱上从上到下的顺序一致。
// =============================================================================

struct SongNote
{
    double start = 0.0;          // 拍
    double length = 0.0;         // 拍
    int string = 0;              // 0 = 最细的弦
    int fret = 0;
    int midi = 0;                // 实际音高（含变调夹）
    float bend = 0.0f;           // 推弦最大音高（半音），0 = 不推
    bool hammerPull = false;     // 这个音是击弦 / 勾弦弹出来的（不用拨）
    bool slideOut = false;       // 滑到下一个音
    bool slideIn = false;
    bool palmMute = false;
    bool dead = false;           // 闷音 ×
    bool harmonic = false;
    bool vibrato = false;
    bool letRing = false;
    bool accent = false;
};

struct SongTrack
{
    juce::String name;
    bool percussion = false;
    std::vector<int> tuning;     // MIDI 音高，[0] = 最细的弦
    int capo = 0;
    int program = 25;            // GM 音色
    std::vector<SongNote> notes; // 按 start 排序

    bool isBass() const { return ! percussion && program >= 32 && program <= 39; }
    int stringCount() const { return (int) tuning.size(); }
};

struct SongBar
{
    double start = 0.0;          // 拍（展开反复以后）
    double length = 4.0;         // 拍
    int numerator = 4, denominator = 4;
    int sourceIndex = 0;         // 在原谱里是第几小节（从 0 开始）
    juce::String section;        // 段落名（前奏、主歌……）
};

struct TempoPoint
{
    double beat = 0.0;
    double bpm = 120.0;
};

struct Song
{
    juce::String title, artist, sourceFormat;
    juce::File file;
    std::vector<SongBar> bars;
    std::vector<TempoPoint> tempos;      // 至少一个，按拍排序
    std::vector<double> tempoStartSeconds; // 每个速度点开始时的秒数（buildSong 里算好）
    std::vector<SongTrack> tracks;
    double lengthBeats = 0.0;

    double tempoAt (double beat) const;
    double beatToSeconds (double beat) const;
    double secondsToBeat (double seconds) const;
    double lengthSeconds() const { return beatToSeconds (lengthBeats); }
    int barIndexAt (double beat) const;
    int defaultTrack() const;            // 第一条有音符的吉他轨
};

// =============================================================================
// 导入器的中间格式（未展开反复）
struct RawNote
{
    int string = 0;              // 0 = 最细的弦
    int fret = 0;
    bool tieDestination = false;
    bool dead = false, hammerPull = false, palmMute = false, harmonic = false;
    bool vibrato = false, letRing = false, accent = false;
    bool slideOut = false, slideIn = false;
    float bend = 0.0f;           // 半音
};

struct RawBeat
{
    double duration = 1.0;       // 拍
    bool grace = false;          // 装饰音：不占时值
    std::vector<RawNote> notes;
};

struct RawVoice { std::vector<RawBeat> beats; };
struct RawBar   { std::vector<RawVoice> voices; };

struct RawTempo
{
    double ratio = 0.0;          // 小节内的位置 0..1
    double bpm = 120.0;
};

struct RawMasterBar
{
    int numerator = 4, denominator = 4;
    bool repeatStart = false;
    int repeatCount = 0;         // >0 表示这里是反复结束，总共弹几遍
    int alternateEndings = 0;    // 位掩码：第 1 遍 = bit0 …
    juce::String section;
    std::vector<RawTempo> tempos;
};

struct RawTrack
{
    juce::String name;
    bool percussion = false;
    std::vector<int> tuning;     // [0] = 最细的弦
    int capo = 0;
    int program = 25;
    std::vector<RawBar> bars;
};

struct RawScore
{
    juce::String title, artist, format;
    double initialTempo = 120.0;
    std::vector<RawMasterBar> masterBars;
    std::vector<RawTrack> tracks;
};

// 展开反复、合并连音线、生成速度表
std::unique_ptr<Song> buildSong (const RawScore& raw);

// 读取吉他谱文件（.gp3 .gp4 .gp5 .gpx .gp），失败时返回 nullptr 并写入 error
std::unique_ptr<Song> loadSongFile (const juce::File& file, juce::String& error);

// 内部使用（也给测试用）
bool importGp3To5 (const juce::MemoryBlock& data, RawScore& out, juce::String& error);
bool importGpif (const juce::String& xml, RawScore& out, juce::String& error);
bool extractGpxScore (const juce::MemoryBlock& data, juce::String& xmlOut, juce::String& error);
