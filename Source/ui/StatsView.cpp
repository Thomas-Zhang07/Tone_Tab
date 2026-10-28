#include "StatsView.h"
#include "Text.h"

namespace
{
    const juce::Colour panel   { 0xff222222 };
    const juce::Colour muted   { 0xff9a9a9a };
    const juce::Colour blue    { 0xff3d8ff0 };
    const juce::Colour green   { 0xff4ccf6b };
    const juce::Colour yellow  { 0xffffc94d };

    juce::String duration (double seconds)
    {
        const int m = juce::roundToInt (seconds / 60.0);
        if (m < 60) return juce::String (m) + TR (" min", " 分钟");
        return juce::String (m / 60) + TR (" h ", " 小时 ") + juce::String (m % 60) + TR (" min", " 分");
    }

    juce::String whenText (juce::int64 ms)
    {
        if (ms <= 0) return "-";
        const auto t = juce::Time (ms);
        const auto now = juce::Time::getCurrentTime();
        if (PracticeLog::dayKey (t) == PracticeLog::dayKey (now)) return TR ("Today ", "今天 ") + t.formatted ("%H:%M");
        if (PracticeLog::dayKey (t) == PracticeLog::dayKey (now - juce::RelativeTime::days (1))) return TR ("Yesterday ", "昨天 ") + t.formatted ("%H:%M");
        return t.formatted ("%m-%d");
    }

    juce::Colour accColour (float a)
    {
        return a < 0.7f ? juce::Colour (0xffff6b5a) : a < 0.9f ? yellow : green;
    }
}

StatsView::StatsView (PracticeLog& l) : log (l)
{
    list.setRowHeight (30);
    list.setColour (juce::ListBox::backgroundColourId, panel);
    list.setOutlineThickness (0);
    addAndMakeVisible (list);

    openButton.setButtonText (TR ("Practice This Song", "去练这首"));
    openButton.onClick = [this]
    {
        if (auto* s = selectedSong(); s != nullptr && onOpenSong)
            onOpenSong (juce::File (s->path));
    };
    addAndMakeVisible (openButton);
    refresh();
}

void StatsView::refresh()
{
    const auto& songs = log.getSongs();
    const juce::String keep = selectedSong() != nullptr ? selectedSong()->path : juce::String();
    order.resize (songs.size());
    for (size_t i = 0; i < songs.size(); ++i) order[i] = (int) i;
    std::sort (order.begin(), order.end(), [&] (int a, int b) { return songs[(size_t) a].lastTime > songs[(size_t) b].lastTime; });
    list.updateContent();

    int sel = order.empty() ? -1 : 0;
    for (size_t i = 0; i < order.size(); ++i)
        if (songs[(size_t) order[i]].path == keep) sel = (int) i;
    if (sel >= 0) list.selectRow (sel, false, true);
    openButton.setEnabled (sel >= 0);
    seenChanges = log.changeCount();
    repaint();
}

const PracticeLog::SongEntry* StatsView::selectedSong() const
{
    const int r = list.getSelectedRow();
    if (! juce::isPositiveAndBelow (r, (int) order.size())) return nullptr;
    return &log.getSongs()[(size_t) order[(size_t) r]];
}

void StatsView::selectedRowsChanged (int)
{
    openButton.setEnabled (selectedSong() != nullptr);
    repaint();
}

void StatsView::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (juce::isPositiveAndBelow (row, (int) order.size()) && onOpenSong)
        onOpenSong (juce::File (log.getSongs()[(size_t) order[(size_t) row]].path));
}

// =============================================================================
void StatsView::paint (juce::Graphics& g)
{
    if (seenChanges != log.changeCount())
        refresh();

    g.setColour (panel);
    for (auto r : { dailyArea, listArea.withTop (listHeader.getY()), trendArea })
        g.fillRoundedRectangle (r.toFloat(), 10.0f);

    paintCards (g);
    paintDaily (g);

    // 列表表头
    g.setColour (muted);
    g.setFont (juce::FontOptions (12.5f));
    auto h = listHeader.reduced (12, 0);
    g.drawText (TR ("Song", "歌"), h.removeFromLeft (h.getWidth() - 330), juce::Justification::centredLeft);
    g.drawText (TR ("Total", "累计"), h.removeFromLeft (90), juce::Justification::centredLeft);
    g.drawText (TR ("Runs", "次数"), h.removeFromLeft (50), juce::Justification::centredLeft);
    g.drawText (TR ("Best 100%", "原速最好"), h.removeFromLeft (80), juce::Justification::centredLeft);
    g.drawText (TR ("Last", "上次"), h, juce::Justification::centredLeft);

    paintTrend (g);

    if (log.getSongs().empty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.setFont (juce::FontOptions (15.0f));
        g.drawText (TR ("No practice history yet. Load a tab in Practice; time practiced and every run will be logged here.", "还没有练习记录。在“练习谱面”里放谱子练，练的时间和每一遍的成绩会记在这里。"),
                    listArea.reduced (20), juce::Justification::centred);
    }
}

void StatsView::paintCards (juce::Graphics& g)
{
    struct Card { juce::String label, value, sub; juce::Colour colour; };
    const Card cards[] = {
        { TR ("Today", "今天"), duration (log.secondsOnDay (juce::Time::getCurrentTime())), {}, blue },
        { TR ("Last 7 days", "最近 7 天"), duration (log.secondsInLastDays (7)), TR ("Daily average ", "平均每天 ") + duration (log.secondsInLastDays (7) / 7.0), blue },
        { TR ("Streak", "连续练习"), juce::String (log.streakDays()) + TR (" days", " 天"), log.streakDays() >= 3 ? TR ("Keep it up!", "保持住！") : juce::String(), yellow },
        { TR ("Total", "累计"), duration (log.totalSeconds()), juce::String ((int) log.getSongs().size()) + TR (" songs", " 首歌"), green },
    };
    const int n = (int) std::size (cards);
    const int gap = 12;
    const int w = (cardsArea.getWidth() - gap * (n - 1)) / n;
    auto r = cardsArea;
    for (auto& c : cards)
    {
        auto box = r.removeFromLeft (w);
        r.removeFromLeft (gap);
        g.setColour (panel);
        g.fillRoundedRectangle (box.toFloat(), 10.0f);
        auto in = box.reduced (14, 10);
        g.setColour (muted);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (c.label, in.removeFromTop (18), juce::Justification::centredLeft);
        g.setColour (c.colour);
        g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
        g.drawFittedText (c.value, in.removeFromTop (32), juce::Justification::centredLeft, 1, 0.7f);
        g.setColour (muted);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (c.sub, in, juce::Justification::centredLeft);
    }
}

void StatsView::paintDaily (juce::Graphics& g)
{
    auto r = dailyArea.reduced (14, 10);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (TR ("Last 14 days", "最近 14 天"), r.removeFromTop (20), juce::Justification::centredLeft);
    r.removeFromTop (6);
    auto labels = r.removeFromBottom (16);

    const auto now = juce::Time::getCurrentTime();
    double minutes[14];
    double maxM = 10.0;
    for (int i = 0; i < 14; ++i)
    {
        minutes[i] = log.secondsOnDay (now - juce::RelativeTime::days (13 - i)) / 60.0;
        maxM = juce::jmax (maxM, minutes[i]);
    }
    const float slot = (float) r.getWidth() / 14.0f;
    for (int i = 0; i < 14; ++i)
    {
        const float x = (float) r.getX() + slot * (float) i;
        const float h = (float) (minutes[i] / maxM) * (float) (r.getHeight() - 16);
        const auto bar = juce::Rectangle<float> (x + slot * 0.18f, (float) r.getBottom() - h, slot * 0.64f, h);
        g.setColour (i == 13 ? blue : blue.withAlpha (0.55f));
        if (h > 0.5f) g.fillRoundedRectangle (bar, 3.0f);
        else { g.setColour (juce::Colours::white.withAlpha (0.08f)); g.fillRect (bar.withY ((float) r.getBottom() - 2.0f).withHeight (2.0f)); }
        if (minutes[i] >= 1.0)
        {
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (juce::String (juce::roundToInt (minutes[i])), bar.withY (bar.getY() - 15.0f).withHeight (14.0f).expanded (6.0f, 0.0f),
                        juce::Justification::centred);
        }
        const auto day = now - juce::RelativeTime::days (13 - i);
        g.setColour (muted);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (i == 13 ? TR ("Today", "今天") : day.formatted ("%d"),
                    juce::Rectangle<float> (x, (float) labels.getY(), slot, (float) labels.getHeight()), juce::Justification::centred);
    }
    g.setColour (muted);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (TR ("Minutes", "分钟"), dailyArea.reduced (14, 10).removeFromTop (20), juce::Justification::centredRight);
}

void StatsView::paintTrend (juce::Graphics& g)
{
    auto r = trendArea.reduced (14, 10);
    const auto* s = selectedSong();
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawFittedText (s != nullptr ? s->title : TR ("Run Results", "每一遍的成绩"), r.removeFromTop (20), juce::Justification::centredLeft, 1, 0.7f);
    r.removeFromTop (4);
    r.removeFromBottom (40);   // 按钮

    if (s == nullptr || s->passes.empty())
    {
        g.setColour (muted);
        g.setFont (juce::FontOptions (12.5f));
        g.drawFittedText (TR ("Turn on Playing Judgement and finish a run (each A-B loop pass or the whole song) to log your accuracy.", "开着“弹奏判定”弹完一遍（A-B 循环每一遍、或者整首弹完）就会记一次命中率。"),
                          r, juce::Justification::centred, 3);
        return;
    }

    // 最近 40 遍：命中率折线 + 速度（底部灰条）
    const int count = juce::jmin (40, (int) s->passes.size());
    const auto first = s->passes.end() - count;
    g.setColour (muted);
    g.setFont (juce::FontOptions (11.5f));
    const float recent = s->recentAccuracy();
    g.drawText (TR ("Last ", "最近 ") + juce::String (count) + TR (" runs    Last 5 avg ", " 遍    最近 5 遍平均 ") + juce::String (juce::roundToInt (recent * 100.0f)) + "%",
                r.removeFromTop (16), juce::Justification::centredLeft);
    r.removeFromTop (6);
    auto plot = r.toFloat();

    for (float lv : { 0.5f, 0.7f, 0.9f, 1.0f })
    {
        const float y = plot.getBottom() - lv * plot.getHeight();
        g.setColour (juce::Colours::white.withAlpha (lv == 0.9f ? 0.18f : 0.07f));
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (muted);
        g.drawText (juce::String (juce::roundToInt (lv * 100.0f)) + "%", (int) plot.getRight() - 34, (int) y - 13, 34, 12, juce::Justification::centredRight);
    }

    const float step = count > 1 ? plot.getWidth() / (float) (count - 1) : 0.0f;
    juce::Path line;
    for (int i = 0; i < count; ++i)
    {
        const auto& p = *(first + i);
        const float x = plot.getX() + step * (float) i;
        // 速度条
        const float sh = juce::jlimit (0.0f, 1.5f, p.speed) / 1.5f * 26.0f;
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRect (x - 2.0f, plot.getBottom() - sh, 4.0f, sh);
        const float y = plot.getBottom() - p.accuracy * plot.getHeight();
        if (i == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
    }
    g.setColour (blue);
    g.strokePath (line, juce::PathStrokeType (2.0f));
    for (int i = 0; i < count; ++i)
    {
        const auto& p = *(first + i);
        const float x = plot.getX() + step * (float) i;
        const float y = plot.getBottom() - p.accuracy * plot.getHeight();
        g.setColour (accColour (p.accuracy));
        g.fillEllipse (x - 3.0f, y - 3.0f, 6.0f, 6.0f);
    }
    const auto& last = s->passes.back();
    g.setColour (muted);
    g.drawText (TR ("Grey bar = tempo at the time (last run ", "灰条 = 当时的速度（最后一遍 ") + juce::String (juce::roundToInt (last.speed * 100.0f)) + "%）",
                trendArea.reduced (14, 10).removeFromBottom (40).removeFromTop (16), juce::Justification::centredLeft);
}

void StatsView::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) order.size())) return;
    const auto& s = log.getSongs()[(size_t) order[(size_t) row]];
    if (selected)
    {
        g.setColour (blue.withAlpha (0.25f));
        g.fillRoundedRectangle (juce::Rectangle<float> (4.0f, 1.0f, (float) w - 8.0f, (float) h - 2.0f), 5.0f);
    }
    auto r = juce::Rectangle<int> (0, 0, w, h).reduced (12, 0);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (14.0f));
    g.drawFittedText (s.title, r.removeFromLeft (w - 24 - 330), juce::Justification::centredLeft, 1, 0.8f);
    g.setColour (juce::Colours::white.withAlpha (0.8f));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (duration (s.seconds), r.removeFromLeft (90), juce::Justification::centredLeft);
    g.drawText (juce::String (s.sessions), r.removeFromLeft (50), juce::Justification::centredLeft);
    auto best = r.removeFromLeft (80);
    if (s.best >= 0.0f)
    {
        g.setColour (accColour (s.best));
        g.drawText (juce::String (juce::roundToInt (s.best * 100.0f)) + "%", best, juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (muted);
        g.drawText ("-", best, juce::Justification::centredLeft);
    }
    g.setColour (muted);
    g.drawText (whenText (s.lastTime), r, juce::Justification::centredLeft);
}

void StatsView::resized()
{
    auto r = getLocalBounds();
    cardsArea = r.removeFromTop (92);
    r.removeFromTop (12);
    dailyArea = r.removeFromTop (170);
    r.removeFromTop (12);
    trendArea = r.removeFromRight (juce::jmin (360, r.getWidth() / 2));
    r.removeFromRight (12);
    listArea = r;
    listHeader = listArea.removeFromTop (30);
    list.setBounds (listArea.reduced (0, 4));
    openButton.setBounds (trendArea.reduced (14, 10).removeFromBottom (30).removeFromRight (110));
}
