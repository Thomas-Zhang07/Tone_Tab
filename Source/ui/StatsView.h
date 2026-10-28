#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "practice/PracticeLog.h"

// 练习记录页：今天 / 最近 7 天 / 连续天数 / 累计，最近 14 天每天练了多久，
// 每首歌的累计时间和命中率，选中一首看它每一遍的命中率和速度变化。
class StatsView : public juce::Component,
                  private juce::ListBoxModel
{
public:
    explicit StatsView (PracticeLog& log);

    void refresh();
    std::function<void (const juce::File&)> onOpenSong;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int getNumRows() override                       { return (int) order.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int lastRow) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;

    void paintCards (juce::Graphics&);
    void paintDaily (juce::Graphics&);
    void paintTrend (juce::Graphics&);
    const PracticeLog::SongEntry* selectedSong() const;

    PracticeLog& log;
    std::vector<int> order;            // 按最近练习时间排序后的下标
    juce::ListBox list { {}, this };
    juce::TextButton openButton;
    juce::Rectangle<int> cardsArea, dailyArea, listArea, trendArea, listHeader;
    int seenChanges = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StatsView)
};
