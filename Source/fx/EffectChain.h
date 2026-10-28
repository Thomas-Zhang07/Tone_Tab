#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <memory>
#include <vector>

// =============================================================================
// 效果器链里的一个位置（一块“单块”）
class EffectSlot
{
public:
    enum class Kind { builtin, nam, ir, vst3 };

    EffectSlot (Kind k, std::unique_ptr<juce::AudioProcessor> p, juce::String typeIdOrPath);
    ~EffectSlot();

    const Kind kind;
    juce::AudioProcessor& getProcessor() const { return *processor; }

    // builtin: 效果器 id；nam / ir: 文件路径；vst3: 插件唯一标识
    const juce::String typeId;
    juce::PluginDescription pluginDescription;   // 仅 vst3

    juce::String getDisplayName() const;
    juce::String getKindLabel() const;
    int getLatencySamples() const { return processor != nullptr ? processor->getLatencySamples() : 0; }

    std::atomic<bool> bypassed { false };

    // 界面线程或设备启动时调用（音频没有在处理这一块时）
    void prepare (double sampleRate, int maxBlockSize);

    // 音频线程：对 2 声道 io 处理前 numSamples 个样本，带开关淡入淡出
    void process (juce::AudioBuffer<float>& io, int numSamples) noexcept;

private:
    std::unique_ptr<juce::AudioProcessor> processor;
    juce::AudioBuffer<float> scratch;
    juce::MidiBuffer midi;
    int numProcChannels = 2;
    int preparedBlock = 0;
    float mix = 1.0f;   // 0 = 全旁通，1 = 全效果

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectSlot)
};

using EffectSlotPtr = std::shared_ptr<EffectSlot>;

// 音频线程看到的只读链路快照
struct ChainSnapshot
{
    std::vector<EffectSlotPtr> slots;
    int latencySamples = 0;
};

class AudioEngine;

// =============================================================================
// 界面线程上的效果器链：增删、排序、存取预设、加载插件
class EffectChain : public juce::ChangeBroadcaster
{
public:
    explicit EffectChain (AudioEngine& engineToUse);
    ~EffectChain() override;

    const std::vector<EffectSlotPtr>& getSlots() const { return slots; }

    // 程序旁边的“音色库”文件夹。预设里在这个文件夹内的文件存成相对路径（@app/…），
    // 整个文件夹拷到别的电脑上也能找到模型。
    static juce::File getAppFolder();
    static juce::File getToneLibrary();
    static juce::String makePortablePath (const juce::File& f);
    static juce::File resolvePortablePath (const juce::String& path);

    // ---- 创建模块（失败时 error 非空、返回 nullptr） -------------------------
    EffectSlotPtr createBuiltin (const juce::String& id);
    EffectSlotPtr createNam (const juce::File& file, juce::String& error);
    EffectSlotPtr createIr (const juce::File& file, juce::String& error);
    EffectSlotPtr createPlugin (const juce::PluginDescription& desc, juce::String& error);

    // ---- 编辑链路（每次都会生成新快照交给音频线程） -------------------------
    void insert (EffectSlotPtr slot, int index = -1);
    void remove (int index);
    void move (int from, int to);
    void replace (int index, EffectSlotPtr slot);
    void setBypassed (int index, bool shouldBypass);
    void clear();

    // ---- 预设 ---------------------------------------------------------------
    std::unique_ptr<juce::XmlElement> toXml() const;
    // 读取预设；找不到的文件 / 插件会跳过，原因写进 problems
    void loadFromXml (const juce::XmlElement& xml, juce::StringArray& problems);

    // ---- VST3 ---------------------------------------------------------------
    juce::AudioPluginFormatManager& getFormatManager()   { return formatManager; }
    juce::KnownPluginList& getKnownPlugins()             { return knownPlugins; }

    // 在界面线程上定时调用：回收音频线程用完的旧快照
    void collectGarbage();

    // 编辑器窗口需要在模块被删除前关闭
    std::function<void (EffectSlot*)> onSlotRemoved;

private:
    void commit();
    EffectSlotPtr finishCreate (EffectSlotPtr slot);

    AudioEngine& engine;
    std::vector<EffectSlotPtr> slots;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectChain)
};
