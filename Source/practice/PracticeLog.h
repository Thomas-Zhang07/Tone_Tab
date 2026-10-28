#pragma once

#include <juce_core/juce_core.h>
#include <map>
#include <vector>

// 练习记录：每天练了多久、每首歌练了多久 / 几次、每一遍的命中率和速度。
// 存成 JSON（%APPDATA%\LianqinHost\练习记录.json），只在界面线程用。
class PracticeLog
{
public:
    struct Pass
    {
        juce::int64 time = 0;     // 毫秒（Unix 时间）
        float accuracy = 0.0f;    // 0..1
        float speed = 1.0f;
    };

    struct SongEntry
    {
        juce::String path, title;
        double seconds = 0.0;
        int sessions = 0;
        juce::int64 lastTime = 0;
        float best = -1.0f;
        std::vector<Pass> passes;   // 最多留 400 条

        float recentAccuracy (int n = 5) const;
    };

    explicit PracticeLog (const juce::File& file);
    ~PracticeLog();

    static juce::File defaultFile();

    void addTime (const juce::String& path, const juce::String& title, double seconds);
    void addPass (const juce::String& path, const juce::String& title, float accuracy, float speed);
    void saveIfNeeded();
    void save();

    const std::vector<SongEntry>& getSongs() const   { return songs; }
    double secondsOnDay (juce::Time day) const;
    double secondsInLastDays (int days) const;       // 含今天
    double totalSeconds() const;
    int streakDays() const;                          // 连续练习天数（今天没练从昨天算起）
    int changeCount() const                          { return changes; }

    static juce::String dayKey (juce::Time t)        { return t.formatted ("%Y-%m-%d"); }

private:
    SongEntry& entryFor (const juce::String& path, const juce::String& title);
    void load();

    juce::File file;
    std::vector<SongEntry> songs;
    std::map<juce::String, double> daily;   // "2026-10-24" -> 秒
    bool dirty = false;
    int changes = 0;
};
