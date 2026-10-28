#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "song/Song.h"
#include "practice/TabPlayer.h"
#include "practice/Judge.h"

// 音游式谱面：每根弦一条横向轨道，音符从右往左滚向判定线。
// 底部是全曲进度条：点击跳转，拖动框出 A-B 循环段。
class TabHighway : public juce::Component
{
public:
    explicit TabHighway (TabPlayer& playerToUse);
    ~TabHighway() override;

    void setSong (std::shared_ptr<const Song> song, int trackIndex);
    void setTrack (int trackIndex);
    void setZoom (float pixelsPerSecond)     { pxPerSec = pixelsPerSecond; repaint(); }
    float getZoom() const                    { return pxPerSec; }

    // 只用于离线截图：固定显示某个位置
    void setFrozenPosition (double beat)     { frozenBeat = beat; repaint(); }

    std::function<void (double, double)> onLoopDragged;   // 在进度条上框选了 A-B
    std::function<void (float)> onZoomChanged;           // 滚轮调了间距

    // 弹奏判定结果（nullptr = 不显示）
    void setJudgeSession (std::shared_ptr<JudgeSession> s, bool waitMode);
    void setJudgeVisible (bool v)            { judgeVisible = v; repaint(); }

    // 在每个音上面写音名（例如 E4、C#3）
    void setShowNoteNames (bool v)           { showNoteNames = v; repaint(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void analyseTrack();
    double currentBeat() const;
    float xForSeconds (double s, double nowSec) const { return hitX + (float) ((s - nowSec) * pxPerSec); }
    float yForString (int s) const;
    void paintLanes (juce::Graphics&, double nowSec, double nowBeat);
    void paintTimeline (juce::Graphics&, double nowBeat);
    double beatAtTimelineX (float x) const;
    void paintJudgeHud (juce::Graphics&, double nowBeat);

    TabPlayer& player;
    std::shared_ptr<const Song> song;
    int track = 0;
    float pxPerSec = 240.0f;
    double frozenBeat = -1.0;

    // 当前轨预先算好的信息
    std::vector<char> hopoLabel;        // 'H' 击弦，'P' 勾弦，0 无
    std::vector<int> nextOnString;      // 同一根弦的下一个音（画滑音线）
    std::vector<double> startSec, endSec;

    juce::Rectangle<int> laneArea, timelineArea;
    float hitX = 150.0f;
    bool draggingLoop = false;
    double dragStartBeat = 0.0, dragEndBeat = 0.0;

    std::unique_ptr<juce::VBlankAttachment> vblank;

    std::shared_ptr<JudgeSession> judge;
    bool judgeVisible = true, waitMode = false, showNoteNames = true;
    uint32_t seenSeq = 0;
    struct Flash { juce::String text, detail; juce::Colour colour; int string = 0; double time = -10.0; };
    Flash flash;
    std::vector<float> barAccuracy;
    int framesSinceBarUpdate = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TabHighway)
};
