#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include "TabHighway.h"
#include "TabSearch.h"
#include "practice/Recorder.h"
#include "practice/SpeedTrainer.h"
#include "practice/PracticeLog.h"

class DrumMachine;

// 一遍弹完后的成绩卡
class JudgeSummaryCard : public juce::Component
{
public:
    JudgeSummaryCard();
    void show (const JudgeSession& session);
    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void (int bar)> onPracticeBar;   // 循环练这一小节
    std::function<void()> onReplay, onClose;

private:
    JudgeSession::Stats stats;
    int bestCombo = 0;
    juce::String offsetText;
    juce::OwnedArray<juce::TextButton> barButtons;
    juce::TextButton replayButton, closeButton;
};

// 混音台：每条音轨的音量、静音、独奏
class TrackMixer : public juce::Component
{
public:
    struct Strip { float db = 0.0f; bool mute = false, solo = false; };
    TrackMixer (const Song& song, std::vector<Strip>& strips, std::function<void()> onChange);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    std::vector<Strip>& strips;
    std::function<void()> changed;
    juce::OwnedArray<juce::Label> names;
    juce::OwnedArray<juce::TextButton> muteButtons, soloButtons;
    juce::OwnedArray<juce::Slider> sliders;
    juce::TextButton resetButton;
};

// 练习页：打开谱子、选音轨、走带控制（变速 / A-B 循环 / 预备拍 / 节拍器 / 示范音）+ 音游谱面
class PracticePanel : public juce::Component,
                      private juce::Timer
{
public:
    PracticePanel (TabPlayer& player, DrumMachine& drums, Judge& judge, Recorder& recorder,
                   PracticeLog& log, juce::PropertiesFile& settings);
    ~PracticePanel() override;

    bool loadFile (const juce::File& file);      // 返回是否成功（失败会弹提示）
    void togglePlay();
    void saveSettings();

    TabHighway& getHighway()                     { return highway; }
    void setDeviceLatencyMs (float ms);          // 声卡报告的往返延迟（主界面定时更新）

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void chooseFile();
    void showRecentMenu();
    void showOnlineSearch();
    void importDownloadedTab (const juce::File& f);
    juce::File getWatchFolder() const;
    juce::File getTabsFolder() const;
    void refreshInfoLabel();
    void rebuildJudgeSession();
    void applyJudgeLatency();
    void showSummary();
    void startCalibration();
    void finishCalibration();
    void refreshTrackBox();
    void selectTrack (int t);                    // -1 = 全部音轨合奏
    int displayTrack() const;                    // 谱面上显示哪一轨
    void showMixer();
    void applyMix();
    void refreshLoopButtons();
    // 录音 / 回放 / 速度阶梯 / 练习记录
    void toggleRecording();
    void showReplayMenu();
    void startReplay (const juce::File& wav);
    void finishReplay();
    void showTrainerSettings();
    void enableTrainer (bool on);
    void onLoopPass (float accuracy);
    void refreshToolsStatus();
    void setLoopPoint (bool isA);
    int currentBarIndex() const;

    TabPlayer& player;
    DrumMachine& drums;
    Judge& judge;
    Recorder& recorder;
    PracticeLog& log;
    juce::PropertiesFile& settings;
    std::shared_ptr<const Song> song;

    juce::TextButton openButton, recentButton, searchButton, stopWatchButton, playButton, rewindButton, prevBarButton, nextBarButton;
    juce::TextButton loopAButton, loopBButton, loopClearButton;
    juce::ToggleButton countInToggle, metronomeToggle, demoToggle, judgeToggle, waitToggle, noteNameToggle;
    juce::TextButton calibrateButton, recordButton, replayButton, trainerButton;
    juce::Label toolsStatus;
    SpeedTrainer trainer;
    int seenWraps = 0, seenJudgePasses = 0, saveCounter = 0;
    bool passPending = false, replaying = false;
    double passPendingSince = 0.0, lastTickMs = 0.0;
    juce::String toolsMessage;           // 录音结束之类的一次性提示
    struct ReplaySaved { bool countIn = true, metronome = false, wait = false; } replaySaved;
    juce::Label titleLabel, infoLabel, trackLabel, speedLabel, demoVolLabel, backingVolLabel, positionLabel;
    juce::ComboBox trackBox;
    juce::TextButton mixerButton;
    std::vector<TrackMixer::Strip> mix;
    static constexpr int allTracksId = 1000;
    juce::Slider speedSlider, demoVolSlider, backingVolSlider;

    TabHighway highway;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> controlsArea;
    double loopA = -1.0, loopB = -1.0;
    bool wasPlaying = false;
    DownloadWatcher watcher;
    int pollCounter = 0;
    juce::Component::SafePointer<juce::CallOutBox> searchCallout;

    std::shared_ptr<JudgeSession> judgeSession;
    JudgeSummaryCard summary;
    float deviceLatencyMs = 0.0f;
    bool sawPlaying = false;

    // 延迟校准：临时换成一段节拍器练习，结束后恢复
    struct Saved { std::shared_ptr<const Song> song; int track = 0; bool countIn = true, metronome = false, demo = true, wait = false; float speed = 1.0f; };
    bool calibrating = false;
    Saved saved;
    std::shared_ptr<JudgeSession> calibrationSession;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PracticePanel)
};
