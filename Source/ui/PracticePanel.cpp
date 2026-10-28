#include "PracticePanel.h"
#include "drums/DrumMachine.h"
#include "app/AppPaths.h"
#include "Text.h"

namespace
{
    const juce::Colour panelColour { 0xff222222 };
    const juce::Colour mutedText   { 0xff9a9a9a };
    const juce::Colour accent      { 0xff37b24d };

    void styleLabel (juce::Label& l, float size, juce::Colour c = mutedText)
    {
        l.setFont (juce::FontOptions (size));
        l.setColour (juce::Label::textColourId, c);
    }

    void styleSlider (juce::Slider& s, double min, double max, double step, double value, const juce::String& suffix)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 22);
        s.setRange (min, max, step);
        s.setTextValueSuffix (suffix);
        s.setValue (value, juce::dontSendNotification);
    }

    juce::String midiName (int midi)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (names[((midi % 12) + 12) % 12]);
    }

    // 从粗到细写出调弦，例如 “E A D G B E”；标准调弦会额外标注
    juce::String tuningText (const SongTrack& t)
    {
        juce::StringArray parts;
        for (int i = (int) t.tuning.size(); --i >= 0;)
            parts.add (midiName (t.tuning[(size_t) i]));
        juce::String s = parts.joinIntoString (" ");
        const std::vector<int> standard { 64, 59, 55, 50, 45, 40 };
        if (t.tuning == standard) s << TR ("(Standard)", "（标准）");
        else if (t.tuning.size() == 6 && t.tuning[5] == 38 && t.tuning[4] == 45) s << " (Drop D)";
        if (t.capo > 0) s << TR ("   Capo at fret ", "   变调夹 ") << t.capo << TR ("", " 品");
        return s;
    }

    juce::String formatTime (double seconds)
    {
        const int s = (int) std::floor (juce::jmax (0.0, seconds));
        return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
    }
}

PracticePanel::PracticePanel (TabPlayer& p, DrumMachine& d, Judge& j, Recorder& rec, PracticeLog& lg, juce::PropertiesFile& s)
    : player (p), drums (d), judge (j), recorder (rec), log (lg), settings (s), highway (p)
{
    openButton.setButtonText (TR ("Open Tab…", "打开谱子…"));
    openButton.onClick = [this] { chooseFile(); };
    recentButton.setButtonText (TR ("Recent ▾", "最近 ▾"));
    recentButton.onClick = [this] { showRecentMenu(); };
    searchButton.setButtonText (TR ("Find Tabs Online…", "在线找谱…"));
    searchButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2f6fdb));
    searchButton.onClick = [this] { showOnlineSearch(); };
    stopWatchButton.setButtonText (TR ("Stop Waiting", "停止等待"));
    stopWatchButton.onClick = [this] { watcher.stop(); refreshInfoLabel(); };

    titleLabel.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    titleLabel.setText (TR ("No tab open", "还没有打开谱子"), juce::dontSendNotification);
    styleLabel (infoLabel, 13.0f);
    styleLabel (trackLabel, 13.0f);
    trackLabel.setText (TR ("Track", "音轨"), juce::dontSendNotification);
    mixerButton.setButtonText (TR ("Mixer…", "混音…"));
    mixerButton.setTooltip (TR ("Volume, mute and solo for each track", "每条音轨的音量、静音、独奏"));
    mixerButton.onClick = [this] { showMixer(); };
    trackBox.onChange = [this]
    {
        const int id = trackBox.getSelectedId();
        const int t = id == allTracksId ? -1 : id - 1;
        if (song == nullptr || (t >= 0 && ! juce::isPositiveAndBelow (t, (int) song->tracks.size()))) return;
        player.setTrack (t);
        highway.setTrack (t >= 0 ? t : displayTrack());
        rebuildJudgeSession();
        refreshInfoLabel();
    };

    playButton.setButtonText (TR ("▶ Play", "▶ 播放"));
    playButton.onClick = [this] { togglePlay(); };
    playButton.setColour (juce::TextButton::buttonColourId, accent.darker (0.3f));
    rewindButton.setButtonText (T ("⏮"));
    rewindButton.setTooltip (TR ("Back to the start (or loop point A when looping)", "回到开头（有循环时回到 A 点）"));
    rewindButton.onClick = [this] { player.seek (player.isLooping() ? player.getLoopA() : 0.0); };
    prevBarButton.setButtonText (TR ("◀ Bar", "◀ 小节"));
    prevBarButton.onClick = [this]
    {
        if (song == nullptr) return;
        const int bi = currentBarIndex();
        if (bi >= 0) player.seek (song->bars[(size_t) juce::jmax (0, bi - 1)].start);
    };
    nextBarButton.setButtonText (TR ("Bar ▶", "小节 ▶"));
    nextBarButton.onClick = [this]
    {
        if (song == nullptr) return;
        const int bi = currentBarIndex();
        if (bi >= 0 && bi + 1 < (int) song->bars.size()) player.seek (song->bars[(size_t) bi + 1].start);
    };

    loopAButton.setButtonText ("A");
    loopAButton.setTooltip (TR ("Set the current bar as loop start", "把当前小节设为循环起点"));
    loopAButton.onClick = [this] { setLoopPoint (true); };
    loopBButton.setButtonText ("B");
    loopBButton.setTooltip (TR ("Set the current bar as loop end (or drag on the seek bar below)", "把当前小节设为循环终点（也可以在下面进度条上拖动框选）"));
    loopBButton.onClick = [this] { setLoopPoint (false); };
    loopClearButton.setButtonText (TR ("Clear Loop", "取消循环"));
    loopClearButton.onClick = [this] { loopA = loopB = -1.0; player.setLoop (0.0, 0.0, false); refreshLoopButtons(); };
    highway.onLoopDragged = [this] (double a, double b)
    {
        loopA = a; loopB = b;
        player.setLoop (a, b, true);
        player.seek (a);
        refreshLoopButtons();
    };

    countInToggle.setButtonText (TR ("Count-in", "预备拍"));
    countInToggle.setToggleState (settings.getBoolValue ("tabCountIn", true), juce::dontSendNotification);
    countInToggle.onClick = [this] { player.setCountIn (countInToggle.getToggleState()); };
    metronomeToggle.setButtonText (TR ("Metronome", "节拍器"));
    metronomeToggle.setToggleState (settings.getBoolValue ("tabMetronome", false), juce::dontSendNotification);
    metronomeToggle.onClick = [this] { player.setMetronome (metronomeToggle.getToggleState()); };
    demoToggle.setButtonText (TR ("Demo", "示范音"));
    demoToggle.setTooltip (TR ("Play the demo for the selected track; turn it off to hear only the backing and play along", "播放你所选音轨的示范音；关掉后只剩伴奏，由你来弹"));
    demoToggle.setToggleState (settings.getBoolValue ("tabDemo", true), juce::dontSendNotification);
    demoToggle.onClick = [this] { player.setDemoOn (demoToggle.getToggleState()); };
    noteNameToggle.setButtonText (TR ("Note Names", "音名"));
    noteNameToggle.setTooltip (TR ("Show each note name (e.g. E4, C#3); shortcut N", "在每个音上面写出它是什么音（例如 E4、C#3）；快捷键 N"));
    noteNameToggle.setToggleState (settings.getBoolValue ("tabNoteNames", true), juce::dontSendNotification);
    noteNameToggle.onClick = [this] { highway.setShowNoteNames (noteNameToggle.getToggleState()); };

    styleLabel (speedLabel, 13.0f);       speedLabel.setText (TR ("Speed", "速度"), juce::dontSendNotification);
    styleLabel (demoVolLabel, 13.0f);     demoVolLabel.setText (TR ("Demo", "示范"), juce::dontSendNotification);
    styleLabel (backingVolLabel, 13.0f);  backingVolLabel.setText (TR ("Backing", "伴奏"), juce::dontSendNotification);
    styleLabel (positionLabel, 13.0f, juce::Colours::white);
    positionLabel.setJustificationType (juce::Justification::centredRight);

    styleSlider (speedSlider, 25.0, 150.0, 5.0, settings.getDoubleValue ("tabSpeed", 100.0), "%");
    speedSlider.setDoubleClickReturnValue (true, 100.0);
    speedSlider.onValueChange = [this] { player.setSpeed ((float) speedSlider.getValue() / 100.0f); };
    styleSlider (demoVolSlider, -40.0, 6.0, 1.0, settings.getDoubleValue ("tabDemoDb", -6.0), " dB");
    demoVolSlider.onValueChange = [this] { player.setDemoVolumeDb ((float) demoVolSlider.getValue()); };
    styleSlider (backingVolSlider, -40.0, 6.0, 1.0, settings.getDoubleValue ("tabBackingDb", -12.0), " dB");
    backingVolSlider.onValueChange = [this] { player.setBackingVolumeDb ((float) backingVolSlider.getValue()); };
    highway.onZoomChanged = [this] (float z) { settings.setValue ("tabZoom", z); };

    // ---- 弹奏判定 -----------------------------------------------------------
    judgeToggle.setButtonText (TR ("Playing Judgement", "弹奏判定"));
    judgeToggle.setTooltip (TR ("Judge your dry signal for timing and pitch (Perfect / Good / Wrong / Miss)", "听你弹的干信号，判断有没有按时弹对（Perfect / Good / 错音 / Miss）"));
    judgeToggle.setToggleState (settings.getBoolValue ("judgeOn", true), juce::dontSendNotification);
    judgeToggle.onClick = [this]
    {
        if (! judgeToggle.getToggleState() && waitToggle.getToggleState())
        {
            waitToggle.setToggleState (false, juce::dontSendNotification);
            judge.setWaitMode (false);
        }
        judge.setEnabled (judgeToggle.getToggleState());
        highway.setJudgeVisible (judgeToggle.getToggleState());
        highway.setJudgeSession (judgeSession, waitToggle.getToggleState());
    };
    waitToggle.setButtonText (TR ("Wait Mode", "等待模式"));
    waitToggle.setTooltip (TR ("The score waits until you play the right note. Great for learning fingerings (shortcut W).", "弹对了谱子才往下走，适合刚开始记指法（快捷键 W）"));
    waitToggle.onClick = [this]
    {
        const bool on = waitToggle.getToggleState();
        if (on && ! judgeToggle.getToggleState())
        {
            judgeToggle.setToggleState (true, juce::dontSendNotification);
            judge.setEnabled (true);
            highway.setJudgeVisible (true);
        }
        judge.setWaitMode (on);
        judge.resetResults();
        highway.setJudgeSession (judgeSession, on);
    };
    calibrateButton.setButtonText (TR ("Calibrate Latency…", "延迟校准…"));
    calibrateButton.setTooltip (TR ("Play 16 notes with the metronome to measure audio and system latency for more accurate judging", "跟着节拍器弹 16 下，自动算出声卡和系统的延迟，让判定更准"));
    calibrateButton.onClick = [this] { startCalibration(); };

    // ---- 录音 / 回放 / 速度阶梯 ----------------------------------------------
    recordButton.setButtonText (TR ("● Record", "● 录音"));
    recordButton.setTooltip (TR ("Record your dry signal (before effects) and replay it with a different tone. Press Record, then Play; playback stays in sync with the score.", "把你弹的干信号录下来（效果器之前），之后可以换音色回放；先按录音再按播放，回放时谱子会对上"));
    recordButton.onClick = [this] { toggleRecording(); };
    replayButton.setButtonText (TR ("Replay…", "回放…"));
    replayButton.setTooltip (TR ("Replay: the recording replaces the guitar input through the current effect chain, so you can audition tones as you listen", "回放录音：录音会代替吉他输入走现在的效果器链，可以边听边换音色"));
    replayButton.onClick = [this] { if (replaying) finishReplay(); else showReplayMenu(); };
    trainerButton.setButtonText (TR ("Trainer…", "阶梯…"));
    trainerButton.setTooltip (TR ("Speed trainer: automatically speeds up when you play an A-B loop cleanly", "速度阶梯：A-B 循环里弹稳了就自动提速"));
    trainerButton.onClick = [this] { showTrainerSettings(); };
    styleLabel (toolsStatus, 12.5f, juce::Colour (0xff9cc4ff));
    toolsStatus.setMinimumHorizontalScale (0.8f);
    for (auto* c : std::initializer_list<juce::Component*> { &recordButton, &replayButton, &trainerButton, &toolsStatus })
        addAndMakeVisible (*c);

    trainer.start = (float) settings.getDoubleValue ("trainerStart", 0.6);
    trainer.step = (float) settings.getDoubleValue ("trainerStep", 0.05);
    trainer.target = (float) settings.getDoubleValue ("trainerTarget", 1.0);
    trainer.needAccuracy = (float) settings.getDoubleValue ("trainerAccuracy", 0.9);
    trainer.passesPerStep = settings.getIntValue ("trainerPasses", 2);
    seenWraps = player.getLoopWraps();
    lastTickMs = juce::Time::getMillisecondCounterHiRes();

    addChildComponent (summary);
    summary.onClose = [this] { summary.setVisible (false); };
    summary.onReplay = [this]
    {
        summary.setVisible (false);
        player.seek (player.isLooping() ? player.getLoopA() : 0.0);
        player.setPlaying (true);
    };
    summary.onPracticeBar = [this] (int bar)
    {
        summary.setVisible (false);
        if (song == nullptr || ! juce::isPositiveAndBelow (bar, (int) song->bars.size())) return;
        const auto& b = song->bars[(size_t) bar];
        loopA = b.start; loopB = b.start + b.length;
        player.setLoop (loopA, loopB, true);
        player.seek (loopA);
        refreshLoopButtons();
        player.setPlaying (true);
    };

    for (auto* c : std::initializer_list<juce::Component*> {
             &openButton, &recentButton, &searchButton, &titleLabel, &infoLabel, &trackLabel, &trackBox, &mixerButton,
             &playButton, &rewindButton, &prevBarButton, &nextBarButton,
             &loopAButton, &loopBButton, &loopClearButton,
             &countInToggle, &metronomeToggle, &demoToggle, &noteNameToggle, &judgeToggle, &waitToggle, &calibrateButton,
             &speedLabel, &speedSlider, &demoVolLabel, &demoVolSlider, &backingVolLabel, &backingVolSlider,
             &positionLabel, &highway })
        addAndMakeVisible (*c);

    // 把保存的设置推给播放器
    player.setSpeed ((float) speedSlider.getValue() / 100.0f);
    player.setDemoVolumeDb ((float) demoVolSlider.getValue());
    player.setBackingVolumeDb ((float) backingVolSlider.getValue());
    player.setCountIn (countInToggle.getToggleState());
    player.setMetronome (metronomeToggle.getToggleState());
    player.setDemoOn (demoToggle.getToggleState());
    highway.setZoom ((float) settings.getDoubleValue ("tabZoom", 240.0));
    judge.setEnabled (judgeToggle.getToggleState());
    highway.setJudgeVisible (judgeToggle.getToggleState());
    highway.setShowNoteNames (noteNameToggle.getToggleState());
    applyJudgeLatency();

    addChildComponent (stopWatchButton);
    refreshLoopButtons();
    setWantsKeyboardFocus (true);
    startTimerHz (15);

    // 自动打开上次的谱子
    const juce::File last (settings.getValue ("tabLastFile"));
    if (last.existsAsFile())
    {
        juce::String err;
        if (auto loaded = loadSongFile (last, err))
        {
            song = std::shared_ptr<const Song> (loaded.release());
            player.setSong (song);
            const int savedTrack = settings.getIntValue ("tabLastTrack", song->defaultTrack());
            const int t = savedTrack < 0 ? -1 : juce::jlimit (0, juce::jmax (0, (int) song->tracks.size() - 1), savedTrack);
            mix.assign (song->tracks.size(), {});
            applyMix();
            player.setTrack (t);
            highway.setSong (song, t >= 0 ? t : displayTrack());
            refreshTrackBox();
            selectTrack (t);
        }
    }
}

PracticePanel::~PracticePanel()
{
    stopTimer();
    if (recorder.isRecording()) recorder.stopRecording();
    if (replaying) recorder.stopPlayback();
    log.saveIfNeeded();
    if (calibrating)
    {
        // 校准没做完就退出：先恢复原来的谱子，免得记住校准用的练习
        player.setPlaying (false);
        song = saved.song;
        calibrating = false;
    }
    saveSettings();
    judge.setSession (nullptr);
}

void PracticePanel::saveSettings()
{
    settings.setValue ("tabSpeed", speedSlider.getValue());
    settings.setValue ("tabDemoDb", demoVolSlider.getValue());
    settings.setValue ("tabBackingDb", backingVolSlider.getValue());
    settings.setValue ("tabZoom", highway.getZoom());
    settings.setValue ("judgeOn", judgeToggle.getToggleState());
    settings.setValue ("tabNoteNames", noteNameToggle.getToggleState());
    settings.setValue ("trainerStart", trainer.start);
    settings.setValue ("trainerStep", trainer.step);
    settings.setValue ("trainerTarget", trainer.target);
    settings.setValue ("trainerAccuracy", trainer.needAccuracy);
    settings.setValue ("trainerPasses", trainer.passesPerStep);
    settings.setValue ("tabCountIn", countInToggle.getToggleState());
    settings.setValue ("tabMetronome", metronomeToggle.getToggleState());
    settings.setValue ("tabDemo", demoToggle.getToggleState());
    if (song != nullptr && ! calibrating)
    {
        settings.setValue ("tabLastFile", song->file.getFullPathName());
        settings.setValue ("tabLastTrack", player.getTrack());
    }
    settings.saveIfNeeded();
}

// -----------------------------------------------------------------------------
bool PracticePanel::loadFile (const juce::File& file)
{
    juce::String err;
    auto loaded = loadSongFile (file, err);
    if (loaded == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, TR ("Could not open this tab", "打不开这个谱子"),
                                                file.getFileName() + "\n\n" + err);
        return false;
    }

    song = std::shared_ptr<const Song> (loaded.release());
    loopA = loopB = -1.0;
    player.setSong (song);
    const int t = song->defaultTrack();
    mix.assign (song->tracks.size(), {});
    applyMix();
    player.setTrack (t);
    highway.setSong (song, t);
    refreshTrackBox();
    selectTrack (t);
    refreshLoopButtons();

    // 最近打开
    juce::StringArray recent;
    recent.addTokens (settings.getValue ("tabRecent"), "|", {});
    recent.removeEmptyStrings();
    recent.removeString (file.getFullPathName());
    recent.insert (0, file.getFullPathName());
    while (recent.size() > 12) recent.remove (recent.size() - 1);
    settings.setValue ("tabRecent", recent.joinIntoString ("|"));
    saveSettings();
    return true;
}

void PracticePanel::chooseFile()
{
    juce::File start (settings.getValue ("tabLastFolder"));
    if (! start.isDirectory())
    {
        const auto tabs = getTabsFolder();
        start = tabs.isDirectory() ? tabs : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    }

    chooser = std::make_unique<juce::FileChooser> (TR ("Open Guitar Tab", "打开吉他谱"), start, "*.gp3;*.gp4;*.gp5;*.gpx;*.gp");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile())
                              {
                                  settings.setValue ("tabLastFolder", f.getParentDirectory().getFullPathName());
                                  loadFile (f);
                              }
                          });
}

void PracticePanel::showRecentMenu()
{
    juce::StringArray recent;
    recent.addTokens (settings.getValue ("tabRecent"), "|", {});
    recent.removeEmptyStrings();

    juce::PopupMenu m;
    for (int i = 0; i < recent.size(); ++i)
    {
        const juce::File f (recent[i]);
        m.addItem (i + 1, f.getFileName(), f.existsAsFile());
    }
    if (recent.isEmpty())
        m.addItem (-1, TR ("(none yet)", "（还没有）"), false);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (recentButton),
                     [this, recent] (int r)
                     {
                         if (r > 0 && r <= recent.size())
                             loadFile (juce::File (recent[r - 1]));
                     });
}

// ---- 在线找谱 -------------------------------------------------------------------
juce::File PracticePanel::getWatchFolder() const
{
    const juce::File saved (settings.getValue ("tabWatchFolder"));
    return saved.isDirectory() ? saved : getSystemDownloadsFolder();
}

juce::File PracticePanel::getTabsFolder() const
{
    return AppPaths::tabsDir();
}

void PracticePanel::showOnlineSearch()
{
    if (searchCallout != nullptr)
    {
        searchCallout->dismiss();
        return;
    }

    juce::String q;
    if (song != nullptr)
        q = (song->title + " " + song->artist).trim();

    auto box = std::make_unique<TabSearchBox> (q, getWatchFolder());
    auto* raw = box.get();
    raw->onSearch = [this] (const juce::String& url)
    {
        watcher.start (getWatchFolder());
        refreshInfoLabel();
        juce::URL (url).launchInDefaultBrowser();
        if (searchCallout != nullptr)
            searchCallout->dismiss();
    };
    raw->onChangeFolder = [this, raw]
    {
        auto safeBox = juce::Component::SafePointer<TabSearchBox> (raw);
        chooser = std::make_unique<juce::FileChooser> (TR ("Where does your browser save downloads?", "浏览器把文件下载到哪个文件夹？"), getWatchFolder());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this, safeBox] (const juce::FileChooser& fc)
                              {
                                  const auto dir = fc.getResult();
                                  if (! dir.isDirectory()) return;
                                  settings.setValue ("tabWatchFolder", dir.getFullPathName());
                                  if (safeBox != nullptr) safeBox->setWatchFolder (dir);
                                  if (watcher.isActive()) watcher.start (dir);
                              });
    };

    searchCallout = &juce::CallOutBox::launchAsynchronously (std::move (box), searchButton.getScreenBounds(), nullptr);
}

void PracticePanel::importDownloadedTab (const juce::File& f)
{
    watcher.stop();

    // 拷进“吉他谱”文件夹（同名就加编号），以后在“最近”和打开对话框里都找得到
    auto target = getTabsFolder().getChildFile (f.getFileName());
    if (target.existsAsFile() && target.getSize() == f.getSize())
        ; // 同一个文件已经导入过
    else
    {
        if (target.existsAsFile())
            target = target.getNonexistentSibling (false);
        if (! f.copyFileTo (target))
            target = f;
    }

    if (loadFile (target))
        if (auto* top = getTopLevelComponent())
            top->toFront (true);     // 把窗口从浏览器后面拉回来
    refreshInfoLabel();
}

void PracticePanel::refreshInfoLabel()
{
    stopWatchButton.setVisible (watcher.isActive());
    if (watcher.isActive())
    {
        infoLabel.setColour (juce::Label::textColourId, juce::Colour (0xff6fa8ff));
        infoLabel.setText (TR ("Waiting for a tab… GP files downloaded in your browser will open automatically (", "等待下载谱子… 在浏览器里下载 GP 文件后会自动打开（") + watcher.getFolder().getFullPathName() + TR (")", "）"),
                           juce::dontSendNotification);
        return;
    }

    infoLabel.setColour (juce::Label::textColourId, mutedText);
    const int t = player.getTrack();
    if (song != nullptr && t < 0)
    {
        int n = 0;
        for (auto& tr : song->tracks) if (! tr.notes.empty()) ++n;
        const int d = displayTrack();
        infoLabel.setText (TR ("Ensemble: ", "合奏：") + juce::String (n) + TR (" tracks play together (adjust with Backing volume and Mixer…, no judging)    Showing: ", " 条音轨一起播放（用“伴奏”音量和“混音…”调大小，不判定）    谱面显示：")
                               + (juce::isPositiveAndBelow (d, (int) song->tracks.size()) ? song->tracks[(size_t) d].name : juce::String()),
                           juce::dontSendNotification);
        return;
    }
    if (song == nullptr || ! juce::isPositiveAndBelow (t, (int) song->tracks.size()))
    {
        infoLabel.setText ({}, juce::dontSendNotification);
        return;
    }
    const auto& tr = song->tracks[(size_t) t];
    infoLabel.setText (TR ("Tuning: ", "调弦：") + tuningText (tr) + "    " + juce::String (tr.notes.size()) + TR (" notes", " 个音"),
                       juce::dontSendNotification);
}

void PracticePanel::refreshTrackBox()
{
    trackBox.clear (juce::dontSendNotification);
    if (song == nullptr) return;

    titleLabel.setText (song->title.isNotEmpty() ? song->title : song->file.getFileNameWithoutExtension(), juce::dontSendNotification);
    int withNotes = 0;
    for (auto& tr : song->tracks) if (! tr.notes.empty()) ++withNotes;
    if (withNotes >= 2)
    {
        trackBox.addItem (TR ("All Tracks (Ensemble)", "全部音轨（合奏）"), allTracksId);
        trackBox.addSeparator();
    }
    mixerButton.setEnabled (! song->tracks.empty());
    for (size_t i = 0; i < song->tracks.size(); ++i)
    {
        const auto& t = song->tracks[i];
        juce::String name = t.name.isNotEmpty() ? t.name : (TR ("Track ", "音轨 ") + juce::String ((int) i + 1));
        if (t.percussion) name << TR (" (Drums)", "（鼓）");
        else if (t.isBass()) name << TR (" (Bass)", "（贝斯）");
        trackBox.addItem (name, (int) i + 1);
        trackBox.setItemEnabled ((int) i + 1, ! t.percussion && ! t.notes.empty());
    }
}

int PracticePanel::currentBarIndex() const
{
    return song != nullptr ? song->barIndexAt (player.getPosition() + 1.0e-6) : -1;
}

void PracticePanel::setLoopPoint (bool isA)
{
    if (song == nullptr) return;
    const int bi = currentBarIndex();
    if (bi < 0) return;
    const auto& bar = song->bars[(size_t) bi];
    if (isA) { loopA = bar.start; if (loopB <= loopA) loopB = bar.start + bar.length; }
    else     { loopB = bar.start + bar.length; if (loopA < 0.0 || loopA >= loopB) loopA = bar.start; }
    player.setLoop (loopA, loopB, true);
    refreshLoopButtons();
}

void PracticePanel::refreshLoopButtons()
{
    const bool on = player.isLooping();
    for (auto* b : { &loopAButton, &loopBButton })
        b->setColour (juce::TextButton::buttonColourId, on ? accent.darker (0.4f) : juce::Colour (0xff3a3a3a));
    loopClearButton.setEnabled (on);
}

void PracticePanel::togglePlay()
{
    if (song == nullptr) { chooseFile(); return; }
    summary.setVisible (false);
    const bool play = ! player.isPlaying();
    if (play && drums.isPlaying())
        drums.setPlaying (false);           // 谱子自带节奏，先停掉鼓机
    player.setPlaying (play);
}

bool PracticePanel::keyPressed (const juce::KeyPress& key)
{
    if (key.getTextCharacter() == 'w' || key.getTextCharacter() == 'W') { waitToggle.triggerClick(); return true; }
    if (key.getTextCharacter() == 'n' || key.getTextCharacter() == 'N') { noteNameToggle.triggerClick(); return true; }
    if (key == juce::KeyPress::spaceKey)       { togglePlay(); return true; }
    if (key == juce::KeyPress::leftKey)        { prevBarButton.triggerClick(); return true; }
    if (key == juce::KeyPress::rightKey)       { nextBarButton.triggerClick(); return true; }
    if (key == juce::KeyPress::homeKey)        { rewindButton.triggerClick(); return true; }
    if (key.getTextCharacter() == 'a' || key.getTextCharacter() == 'A') { setLoopPoint (true); return true; }
    if (key.getTextCharacter() == 'b' || key.getTextCharacter() == 'B') { setLoopPoint (false); return true; }
    if (key == juce::KeyPress::upKey)          { speedSlider.setValue (speedSlider.getValue() + 5.0); return true; }
    if (key == juce::KeyPress::downKey)        { speedSlider.setValue (speedSlider.getValue() - 5.0); return true; }
    return false;
}

void PracticePanel::timerCallback()
{
    player.releaseRetiredSongs();
    recorder.releaseRetired();

    // ---- 练习时间 -----------------------------------------------------------
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    const double dt = (nowMs - lastTickMs) * 0.001;
    lastTickMs = nowMs;
    if (song != nullptr && player.isPlaying() && ! player.isCountingIn() && ! calibrating && ! replaying && dt > 0.0 && dt < 1.0)
        log.addTime (song->file.getFullPathName(), song->title, dt);
    if (++saveCounter % (15 * 30) == 0)
        log.saveIfNeeded();

    // ---- 循环每过一遍：记成绩、速度阶梯 -------------------------------------------
    if (const int wraps = player.getLoopWraps(); wraps != seenWraps)
    {
        seenWraps = wraps;
        if (! calibrating && ! replaying)
        {
            passPending = true;
            passPendingSince = nowMs;
        }
    }
    if (passPending)
    {
        const bool judging = judgeToggle.getToggleState() && ! waitToggle.getToggleState() && judgeSession != nullptr;
        if (! judging)
        {
            passPending = false;
            onLoopPass (-1.0f);
        }
        else if (judgeSession->passes.load() != seenJudgePasses)
        {
            seenJudgePasses = judgeSession->passes.load();
            passPending = false;
            onLoopPass (judgeSession->lastPassAccuracy.load());
        }
        else if (nowMs - passPendingSince > 1500.0)
        {
            passPending = false;          // 这一段没有能判的音
            onLoopPass (-1.0f);
        }
    }

    // ---- 回放结束 -----------------------------------------------------------
    if (replaying && ! recorder.isPlayingBack())
        finishReplay();
    refreshToolsStatus();

    if (watcher.isActive() && ++pollCounter % 15 == 0)    // 约每秒查一次下载文件夹
    {
        const auto f = watcher.poll();
        if (f.existsAsFile())
            importDownloadedTab (f);
        else if (! watcher.isActive())
            refreshInfoLabel();                            // 超时了
    }

    const bool playing = player.isPlaying();
    if (playing != wasPlaying)
    {
        wasPlaying = playing;
        playButton.setButtonText (playing ? TR ("❚❚ Pause", "❚❚ 暂停") : TR ("▶ Play", "▶ 播放"));
        if (playing)
            summary.setVisible (false);

        // 停下来了：校准结束 / 弹到结尾出成绩卡
        if (! playing && song != nullptr)
        {
            const bool atEnd = player.getPosition() >= song->lengthBeats - 1.0e-3;
            if (calibrating)
                finishCalibration();
            else if (atEnd && judgeToggle.getToggleState() && ! waitToggle.getToggleState())
                showSummary();
        }
    }

    if (song != nullptr)
    {
        const double pos = player.getPosition();
        const int bi = currentBarIndex();
        const int total = song->bars.empty() ? 0 : song->bars.back().sourceIndex + 1;
        const double bpm = song->tempoAt (pos) * speedSlider.getValue() / 100.0;
        positionLabel.setText (formatTime (song->beatToSeconds (pos)) + " / " + formatTime (song->lengthSeconds())
                                   + TR ("   Bar ", "   第 ") + juce::String (bi >= 0 ? song->bars[(size_t) bi].sourceIndex + 1 : 0)
                                   + " / " + juce::String (total) + TR ("   ", " 小节   ") + juce::String (juce::roundToInt (bpm)) + " BPM",
                               juce::dontSendNotification);
    }
}

// -----------------------------------------------------------------------------
void PracticePanel::paint (juce::Graphics& g)
{
    g.setColour (panelColour);
    g.fillRoundedRectangle (controlsArea.toFloat(), 10.0f);
}

void PracticePanel::resized()
{
    auto area = getLocalBounds();
    controlsArea = area.removeFromTop (178);
    area.removeFromTop (10);
    highway.setBounds (area);
    summary.setBounds (area.withSizeKeepingCentre (juce::jmin (460, area.getWidth() - 20), juce::jmin (240, area.getHeight() - 20)));

    auto c = controlsArea.reduced (14, 10);

    // 第 1 行：文件、标题、音轨
    auto row1 = c.removeFromTop (30);
    openButton.setBounds (row1.removeFromLeft (100));
    row1.removeFromLeft (6);
    recentButton.setBounds (row1.removeFromLeft (70));
    row1.removeFromLeft (6);
    searchButton.setBounds (row1.removeFromLeft (100));
    row1.removeFromLeft (14);
    auto trackArea = row1.removeFromRight (300);
    trackLabel.setBounds (trackArea.removeFromLeft (40));
    mixerButton.setBounds (trackArea.removeFromRight (62));
    trackArea.removeFromRight (6);
    trackBox.setBounds (trackArea);
    row1.removeFromRight (12);
    titleLabel.setBounds (row1);
    c.removeFromTop (4);

    // 第 2 行：调弦信息 / 当前位置
    auto row2 = c.removeFromTop (20);
    positionLabel.setBounds (row2.removeFromRight (380));
    stopWatchButton.setBounds (row2.removeFromRight (72).expanded (0, 2));
    infoLabel.setBounds (row2);
    c.removeFromTop (6);

    // 第 3 行：走带、循环、开关
    auto row3 = c.removeFromTop (30);
    playButton.setBounds (row3.removeFromLeft (92));
    row3.removeFromLeft (6);
    rewindButton.setBounds (row3.removeFromLeft (36));
    row3.removeFromLeft (4);
    prevBarButton.setBounds (row3.removeFromLeft (62));
    row3.removeFromLeft (4);
    nextBarButton.setBounds (row3.removeFromLeft (62));
    row3.removeFromLeft (18);
    loopAButton.setBounds (row3.removeFromLeft (30));
    row3.removeFromLeft (4);
    loopBButton.setBounds (row3.removeFromLeft (30));
    row3.removeFromLeft (4);
    loopClearButton.setBounds (row3.removeFromLeft (76));
    row3.removeFromLeft (18);
    countInToggle.setBounds (row3.removeFromLeft (80));
    metronomeToggle.setBounds (row3.removeFromLeft (80));
    demoToggle.setBounds (row3.removeFromLeft (80));
    noteNameToggle.setBounds (row3.removeFromLeft (70));
    c.removeFromTop (6);

    // 第 4 行：速度（+ 速度阶梯）、音量（谱面间距用鼠标滚轮调）
    auto row4 = c.removeFromTop (28);
    const int w = row4.getWidth() / 3;
    {
        auto cell = row4.removeFromLeft (w);
        speedLabel.setBounds (cell.removeFromLeft (36));
        trainerButton.setBounds (cell.removeFromRight (62).reduced (0, 1));
        speedSlider.setBounds (cell.withTrimmedRight (6));
    }
    auto place = [&row4, w] (juce::Label& l, juce::Slider& s)
    {
        auto cell = row4.removeFromLeft (w);
        l.setBounds (cell.removeFromLeft (36).withTrimmedLeft (10));
        s.setBounds (cell.withTrimmedRight (10));
    };
    place (demoVolLabel, demoVolSlider);
    place (backingVolLabel, backingVolSlider);
    c.removeFromTop (6);

    // 第 5 行：判定、录音回放、状态
    auto row5 = c.removeFromTop (28);
    judgeToggle.setBounds (row5.removeFromLeft (92));
    waitToggle.setBounds (row5.removeFromLeft (92));
    calibrateButton.setBounds (row5.removeFromLeft (90).reduced (0, 1));
    row5.removeFromLeft (16);
    recordButton.setBounds (row5.removeFromLeft (84).reduced (0, 1));
    row5.removeFromLeft (6);
    replayButton.setBounds (row5.removeFromLeft (92).reduced (0, 1));
    row5.removeFromLeft (12);
    toolsStatus.setBounds (row5);
}

// ---- 弹奏判定 ----------------------------------------------------------------------
void PracticePanel::rebuildJudgeSession()
{
    const int t = player.getTrack();
    if (song != nullptr && juce::isPositiveAndBelow (t, (int) song->tracks.size()))
        judgeSession = std::make_shared<JudgeSession> (song, t);
    else
        judgeSession = nullptr;
    judge.setSession (judgeSession);
    highway.setJudgeSession (judgeSession, waitToggle.getToggleState());
    summary.setVisible (false);
    seenJudgePasses = 0;
    passPending = false;
}

void PracticePanel::setDeviceLatencyMs (float ms)
{
    if (std::abs (ms - deviceLatencyMs) > 0.05f)
    {
        deviceLatencyMs = ms;
        applyJudgeLatency();
    }
}

void PracticePanel::applyJudgeLatency()
{
    const float calibration = calibrating ? 0.0f : (float) settings.getDoubleValue ("judgeCalibrationMs", 0.0);
    judge.setLatencyMs (deviceLatencyMs + calibration);
}

void PracticePanel::showSummary()
{
    if (judgeSession == nullptr || judgeSession->computeStats().judged() < 5)
        return;
    if (song != nullptr)
        log.addPass (song->file.getFullPathName(), song->title, judgeSession->computeStats().accuracy(), player.getSpeed());
    summary.show (*judgeSession);
    summary.setVisible (true);
    summary.toFront (false);
}

void PracticePanel::startCalibration()
{
    if (calibrating)
        return;

    // 一段 90 BPM、4 小节的四分音符（闷音 = 只判时间）
    RawScore raw;
    raw.title = TR ("Latency Calibration", "延迟校准");
    raw.initialTempo = 90.0;
    RawTrack t;
    t.name = TR ("Play with the metronome", "跟着节拍器弹");
    t.tuning = { 64, 59, 55, 50, 45, 40 };
    for (int b = 0; b < 4; ++b)
    {
        raw.masterBars.push_back ({});
        RawBar bar;
        RawVoice v;
        for (int i = 0; i < 4; ++i)
        {
            RawBeat beat;
            beat.duration = 1.0;
            RawNote n;
            n.string = 4;
            n.fret = 0;
            n.dead = true;
            beat.notes.push_back (n);
            v.beats.push_back (beat);
        }
        bar.voices.push_back (v);
        t.bars.push_back (bar);
    }
    raw.tracks.push_back (t);
    std::shared_ptr<const Song> cal (buildSong (raw).release());
    if (cal == nullptr)
        return;

    saved = { song, player.getTrack(), countInToggle.getToggleState(), metronomeToggle.getToggleState(),
              demoToggle.getToggleState(), waitToggle.getToggleState(), player.getSpeed() };
    calibrating = true;
    summary.setVisible (false);
    if (drums.isPlaying()) drums.setPlaying (false);

    player.setPlaying (false);
    player.setLoop (0.0, 0.0, false);
    player.setSong (cal);
    player.resetTrackGains();
    player.setTrack (0);
    player.setCountIn (true);
    player.setMetronome (true);
    player.setDemoOn (false);
    player.setSpeed (1.0f);
    judge.setWaitMode (false);
    judge.setEnabled (true);
    judge.setMatchWindowMs (300.0f);
    applyJudgeLatency();      // 校准时只扣声卡报告的延迟
    calibrationSession = std::make_shared<JudgeSession> (cal, 0);
    judge.setSession (calibrationSession);
    highway.setSong (cal, 0);
    highway.setJudgeVisible (true);
    highway.setJudgeSession (calibrationSession, false);
    titleLabel.setText (TR ("Latency Calibration: listen to 4 count-in clicks, then play one note on each beat (16 notes total)", "延迟校准：先听 4 下预备拍，然后跟着节拍器每一拍弹一下（共 16 下）"), juce::dontSendNotification);
    player.setPlaying (true);
}

void PracticePanel::finishCalibration()
{
    calibrating = false;

    std::vector<float> offsets;
    if (calibrationSession != nullptr)
        for (size_t e = 0; e < calibrationSession->events.size(); ++e)
        {
            const auto v = calibrationSession->verdictOf ((int) e);
            if (v == Verdict::perfect || v == Verdict::good)
                offsets.push_back (calibrationSession->offsetOf ((int) e));
        }
    const bool completed = player.getPosition() >= 15.0;   // 弹到了最后一小节
    calibrationSession = nullptr;

    // 恢复原来的谱子和设置
    judge.setMatchWindowMs (Judge::goodMs);
    player.setSong (saved.song);
    player.setTrack (saved.track);
    player.setCountIn (saved.countIn);
    player.setMetronome (saved.metronome);
    player.setDemoOn (saved.demo);
    player.setSpeed (saved.speed);
    song = saved.song;
    saved.song = nullptr;
    if (song != nullptr)
    {
        highway.setSong (song, player.getTrack() >= 0 ? player.getTrack() : displayTrack());
        refreshTrackBox();
        applyMix();
        selectTrack (player.getTrack());
    }
    else
    {
        highway.setSong (nullptr, 0);
        titleLabel.setText (TR ("No tab open", "还没有打开谱子"), juce::dontSendNotification);
        rebuildJudgeSession();
    }
    judge.setEnabled (judgeToggle.getToggleState());
    judge.setWaitMode (waitToggle.getToggleState());
    highway.setJudgeVisible (judgeToggle.getToggleState());

    if (! completed)
    {
        applyJudgeLatency();
        return;     // 中途停了：当作取消
    }

    if (offsets.size() < 8)
    {
        applyJudgeLatency();
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, TR ("Calibration Failed", "校准没有成功"),
            TR ("Only ", "只听到 ") + juce::String ((int) offsets.size()) + TR (" plucks detected (need at least 8).\n\n", " 下拨弦（至少要 8 下）。\n\n")
            + TR ("Check that the input meter on the left moves when you play and that the guitar input is correct.\nThen try again, plucking an open string firmly on each beat.", "检查一下：左边的输入电平表弹的时候有没有跳？吉他输入通道选对了吗？\n然后再试一次，每一拍用力拨一下空弦。"));
        return;
    }

    std::sort (offsets.begin(), offsets.end());
    const float median = offsets[offsets.size() / 2];
    float spread = 0.0f;
    for (auto o : offsets) spread += std::abs (o - median);
    spread /= (float) offsets.size();

    settings.setValue ("judgeCalibrationMs", median);
    settings.saveIfNeeded();
    applyJudgeLatency();

    juce::String msg;
    msg << TR ("Round-trip latency reported by the device: ", "声卡报告的往返延迟：") << juce::String (deviceLatencyMs, 1) << " ms\n"
        << TR ("Measured separately: ", "另外测得：") << (median >= 0 ? "+" : "") << juce::String (median, 1) << " ms\n"
        << TR ("Judging will compensate ", "以后判定会一共扣掉 ") << juce::String (deviceLatencyMs + median, 1) << " ms。\n\n"
        << TR ("Timing consistency across ", "这 ") << (int) offsets.size() << TR (" plucks: average deviation ", " 下的节奏稳定度：平均偏差 ") << juce::String (spread, 1) << " ms"
        << (spread > 25.0f ? TR ("(large spread; you can recalibrate)", "（偏差有点大，可以再校准一次）") : juce::String());
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, TR ("Calibration Complete", "校准完成"), msg);
}

// ---- 成绩卡 --------------------------------------------------------------------------
JudgeSummaryCard::JudgeSummaryCard()
{
    replayButton.setButtonText (TR ("Try Again", "再来一遍"));
    replayButton.setColour (juce::TextButton::buttonColourId, accent.darker (0.3f));
    replayButton.onClick = [this] { if (onReplay) onReplay(); };
    closeButton.setButtonText (TR ("Close", "关闭"));
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (replayButton);
    addAndMakeVisible (closeButton);
}

void JudgeSummaryCard::show (const JudgeSession& s)
{
    stats = s.computeStats();
    bestCombo = s.bestCombo.load();
    const float off = stats.meanOffsetMs;
    if (stats.perfect + stats.good == 0)    offsetText = {};
    else if (std::abs (off) < 8.0f)         offsetText = TR ("Great timing (average offset ", "节奏很准（平均差 ") + juce::String (juce::roundToInt (std::abs (off))) + " ms）";
    else                                    offsetText = (off < 0 ? TR ("Overall early ", "整体偏早 ") : TR ("Overall late ", "整体偏晚 ")) + juce::String (juce::roundToInt (std::abs (off))) + " ms";

    barButtons.clear();
    for (int bar : s.worstBars (3))
    {
        const int number = s.song != nullptr ? s.song->bars[(size_t) bar].sourceIndex + 1 : bar + 1;
        auto* b = barButtons.add (new juce::TextButton (TR ("Practice Bar ", "练第 ") + juce::String (number) + TR ("", " 小节")));
        b->setTooltip (TR ("Loop this bar", "循环播放这一小节"));
        b->onClick = [this, bar] { if (onPracticeBar) onPracticeBar (bar); };
        addAndMakeVisible (b);
    }
    resized();
    repaint();
}

void JudgeSummaryCard::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xf0202329));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);

    auto c = getLocalBounds().reduced (20, 14);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    g.drawText (TR ("Run Results", "这一遍的成绩"), c.removeFromTop (24), juce::Justification::centredLeft);
    c.removeFromTop (6);

    auto row = c.removeFromTop (54);
    const float acc = stats.accuracy();
    g.setFont (juce::FontOptions (44.0f, juce::Font::bold));
    g.setColour (acc >= 0.9f ? juce::Colour (0xff4ccf6b) : acc >= 0.7f ? juce::Colour (0xffffc94d) : juce::Colour (0xffff6b5a));
    g.drawText (juce::String (juce::roundToInt (acc * 100.0f)) + "%", row.removeFromLeft (150), juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (14.0f));
    auto counts = row;
    auto line = [&] (const juce::String& label, int v, juce::Colour col, juce::Rectangle<int> area)
    {
        g.setColour (col);
        g.drawText (label + "  " + juce::String (v), area, juce::Justification::centredLeft);
    };
    auto top = counts.removeFromTop (27);
    line ("Perfect", stats.perfect, juce::Colour (0xffffd34d), top.removeFromLeft (top.getWidth() / 2));
    line ("Good", stats.good, juce::Colour (0xff5dd879), top);
    line (TR ("Wrong", "错音"), stats.wrong, juce::Colour (0xffff8a3d), counts.removeFromLeft (counts.getWidth() / 2));
    line ("Miss", stats.miss, juce::Colour (0xffff4d4d), counts);

    c.removeFromTop (6);
    g.setColour (juce::Colours::white.withAlpha (0.8f));
    g.setFont (juce::FontOptions (13.5f));
    juce::String info = TR ("Best combo ", "最大连击 ") + juce::String (bestCombo);
    if (offsetText.isNotEmpty()) info << "    " << offsetText;
    g.drawText (info, c.removeFromTop (20), juce::Justification::centredLeft);
    c.removeFromTop (8);
    if (! barButtons.isEmpty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawText (TR ("Focus on: ", "最需要练的地方："), c.removeFromTop (20), juce::Justification::centredLeft);
    }
}

void JudgeSummaryCard::resized()
{
    auto c = getLocalBounds().reduced (20, 14);
    auto bottom = c.removeFromBottom (30);
    closeButton.setBounds (bottom.removeFromRight (80));
    bottom.removeFromRight (8);
    replayButton.setBounds (bottom.removeFromRight (100));

    c.removeFromBottom (10);
    auto bars = c.removeFromBottom (28);
    for (auto* b : barButtons)
    {
        b->setBounds (bars.removeFromLeft (118));
        bars.removeFromLeft (8);
    }
}

// ---- 音轨：合奏 / 混音 ----------------------------------------------------------------
void PracticePanel::selectTrack (int t)
{
    trackBox.setSelectedId (t < 0 && trackBox.indexOfItemId (allTracksId) >= 0 ? allTracksId : juce::jmax (0, t) + 1,
                            juce::sendNotificationSync);
}

int PracticePanel::displayTrack() const
{
    if (song == nullptr) return 0;
    const int t = player.getTrack();
    return t >= 0 ? t : juce::jmax (0, song->defaultTrack());
}

void PracticePanel::applyMix()
{
    bool anySolo = false;
    for (auto& m : mix) anySolo = anySolo || m.solo;
    player.resetTrackGains();
    for (size_t t = 0; t < mix.size(); ++t)
    {
        const auto& m = mix[t];
        const bool audible = ! m.mute && (! anySolo || m.solo);
        player.setTrackGain ((int) t, audible ? juce::Decibels::decibelsToGain (m.db) : 0.0f);
    }
}

void PracticePanel::showMixer()
{
    if (song == nullptr || song->tracks.empty())
        return;
    if (mix.size() != song->tracks.size())
        mix.assign (song->tracks.size(), {});
    auto box = std::make_unique<TrackMixer> (*song, mix, [this] { applyMix(); });
    juce::CallOutBox::launchAsynchronously (std::move (box), mixerButton.getScreenBounds(), nullptr);
}

TrackMixer::TrackMixer (const Song& song, std::vector<Strip>& s, std::function<void()> onChange)
    : strips (s), changed (std::move (onChange))
{
    for (size_t i = 0; i < song.tracks.size() && i < strips.size(); ++i)
    {
        const auto& tr = song.tracks[i];
        juce::String name = tr.name.isNotEmpty() ? tr.name : (TR ("Track ", "音轨 ") + juce::String ((int) i + 1));
        if (tr.percussion) name << TR (" (Drums)", "（鼓）");
        else if (tr.isBass()) name << TR (" (Bass)", "（贝斯）");
        if (tr.notes.empty()) name << TR (" (empty)", "（空）");

        auto* l = names.add (new juce::Label ({}, name));
        l->setFont (juce::FontOptions (13.5f));
        l->setColour (juce::Label::textColourId, tr.notes.empty() ? juce::Colours::grey : juce::Colours::white);
        l->setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (l);

        auto* m = muteButtons.add (new juce::TextButton ("M"));
        m->setTooltip (TR ("Mute", "静音"));
        m->setClickingTogglesState (true);
        m->setToggleState (strips[i].mute, juce::dontSendNotification);
        m->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd9534f));
        m->onClick = [this, i, m] { strips[i].mute = m->getToggleState(); if (changed) changed(); };
        addAndMakeVisible (m);

        auto* so = soloButtons.add (new juce::TextButton ("S"));
        so->setTooltip (TR ("Solo: hear only tracks with S enabled", "独奏：只听开了 S 的音轨"));
        so->setClickingTogglesState (true);
        so->setToggleState (strips[i].solo, juce::dontSendNotification);
        so->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe0a800));
        so->onClick = [this, i, so] { strips[i].solo = so->getToggleState(); if (changed) changed(); };
        addAndMakeVisible (so);

        auto* sl = sliders.add (new juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight));
        sl->setRange (-40.0, 6.0, 1.0);
        sl->setTextValueSuffix (" dB");
        sl->setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
        sl->setDoubleClickReturnValue (true, 0.0);
        sl->setValue (strips[i].db, juce::dontSendNotification);
        sl->onValueChange = [this, i, sl] { strips[i].db = (float) sl->getValue(); if (changed) changed(); };
        addAndMakeVisible (sl);
    }

    resetButton.setButtonText (TR ("Reset All", "全部恢复"));
    resetButton.onClick = [this]
    {
        for (int i = 0; i < sliders.size(); ++i)
        {
            strips[(size_t) i] = {};
            sliders[i]->setValue (0.0, juce::dontSendNotification);
            muteButtons[i]->setToggleState (false, juce::dontSendNotification);
            soloButtons[i]->setToggleState (false, juce::dontSendNotification);
        }
        if (changed) changed();
    };
    addAndMakeVisible (resetButton);

    setSize (440, 44 + 32 * juce::jmin (12, sliders.size()) + 40);
}

void TrackMixer::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff262626));
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText (TR ("Track Mixer", "音轨混音"), 12, 8, 200, 22, juce::Justification::centredLeft);
}

void TrackMixer::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    r.removeFromTop (30);
    resetButton.setBounds (r.removeFromBottom (28).removeFromRight (90));
    r.removeFromBottom (6);
    const int rowH = sliders.isEmpty() ? 32 : juce::jmin (32, r.getHeight() / sliders.size());
    for (int i = 0; i < sliders.size(); ++i)
    {
        auto row = r.removeFromTop (rowH).reduced (0, 3);
        names[i]->setBounds (row.removeFromLeft (150));
        muteButtons[i]->setBounds (row.removeFromLeft (28));
        row.removeFromLeft (4);
        soloButtons[i]->setBounds (row.removeFromLeft (28));
        row.removeFromLeft (8);
        sliders[i]->setBounds (row);
    }
}

// ---- 录音 / 回放 --------------------------------------------------------------------
void PracticePanel::toggleRecording()
{
    if (recorder.isRecording())
    {
        const auto wav = recorder.stopRecording();
        const auto info = Recorder::Info::loadFor (wav);
        toolsMessage = TR ("Recorded: ", "录好了：") + juce::String (juce::roundToInt (info.lengthSeconds)) + TR (" s. Click “Replay…” to audition with a different tone.", " 秒，点“回放…”可以换音色重新听");
        refreshToolsStatus();
        return;
    }
    if (replaying)
        return;

    Recorder::Info info;
    if (song != nullptr)
    {
        info.songFile = song->file;
        info.songTitle = song->title.isNotEmpty() ? song->title : song->file.getFileNameWithoutExtension();
        info.track = player.getTrack();
        info.speed = player.getSpeed();
        info.loopOn = player.isLooping();
        info.loopA = player.getLoopA();
        info.loopB = player.getLoopB();
        info.countIn = countInToggle.getToggleState();
        info.metronome = metronomeToggle.getToggleState();
        info.waitMode = waitToggle.getToggleState();
    }
    juce::String err;
    if (! recorder.startRecording (info, err))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, TR ("Cannot Record", "没法录音"), err);
        return;
    }
    toolsMessage = {};
    refreshToolsStatus();
}

void PracticePanel::showReplayMenu()
{
    auto files = Recorder::getFolder().findChildFiles (juce::File::findFiles, true, "mix.wav");
    for (auto& legacy : Recorder::getFolder().findChildFiles (juce::File::findFiles, false, "*.wav"))
        if (! legacy.getFileName().equalsIgnoreCase ("mix.wav"))
            files.add (legacy);
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getLastModificationTime() > b.getLastModificationTime(); });

    juce::PopupMenu m;
    m.addSectionHeader (TR ("Replay Recording (Current Effect Chain)", "回放录音（走现在的效果器链）"));
    const int n = juce::jmin (20, files.size());
    for (int i = 0; i < n; ++i)
    {
        const auto info = Recorder::Info::loadFor (files[i]);
        const int secs = juce::roundToInt (info.lengthSeconds);
        juce::String label = info.created.formatted ("%m-%d %H:%M") + "   "
                             + (info.songTitle.isNotEmpty() ? info.songTitle : TR ("(no tab open)", "（没开谱子）"))
                             + "   " + juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2);
        if (info.synced && info.speed < 0.999f) label << "   " << juce::roundToInt (info.speed * 100.0f) << "%";
        if (! info.synced) label << TR ("   Audio only", "   只有声音");
        m.addItem (i + 1, label);
    }
    if (n == 0)
        m.addItem (-1, TR ("No recording yet (click ● Record)", "还没有录音（先点“● 录音”）"), false);
    m.addSeparator();
    m.addItem (1000, TR ("Open Recordings Folder", "打开录音文件夹"));

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (replayButton), [this, files] (int r)
    {
        if (r == 1000)
        {
            Recorder::getFolder().createDirectory();
            Recorder::getFolder().startAsProcess();
        }
        else if (r > 0 && r <= files.size())
        {
            startReplay (files[r - 1]);
        }
    });
}

void PracticePanel::startReplay (const juce::File& wav)
{
    if (recorder.isRecording())
        return;
    const auto info = Recorder::Info::loadFor (wav);

    // 录的时候在放谱子：把谱子和设置恢复成录音时的样子
    if (info.synced && ! info.waitMode && info.songFile.existsAsFile())
    {
        if (song == nullptr || song->file != info.songFile)
            if (! loadFile (info.songFile))
                return;
        if (song != nullptr && (info.track < 0 || juce::isPositiveAndBelow (info.track, (int) song->tracks.size())))
            selectTrack (info.track);
        speedSlider.setValue (info.speed * 100.0, juce::sendNotificationSync);
        loopA = info.loopA; loopB = info.loopB;
        player.setLoop (info.loopA, info.loopB, info.loopOn);
        refreshLoopButtons();
    }

    replaySaved = { countInToggle.getToggleState(), metronomeToggle.getToggleState(), waitToggle.getToggleState() };
    player.setPlaying (false);
    player.setCountIn (info.countIn);
    player.setMetronome (info.metronome);
    if (replaySaved.wait)
        judge.setWaitMode (false);
    if (info.synced)
        player.seek (info.syncBeat);
    judge.resetResults();
    summary.setVisible (false);

    juce::String err;
    if (! recorder.startPlayback (wav, err))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, TR ("Cannot Replay", "没法回放"), err);
        finishReplay();
        return;
    }
    replaying = true;
    replayButton.setButtonText (TR ("■ Stop Replay", "■ 停止回放"));
    recordButton.setEnabled (false);
    toolsMessage = {};
    refreshToolsStatus();
}

void PracticePanel::finishReplay()
{
    if (recorder.isPlayingBack())
        recorder.stopPlayback();
    if (replaying)
        player.setPlaying (false);
    replaying = false;
    player.setCountIn (replaySaved.countIn);
    player.setMetronome (replaySaved.metronome);
    judge.setWaitMode (waitToggle.getToggleState());
    replayButton.setButtonText (TR ("Replay…", "回放…"));
    recordButton.setEnabled (true);
    refreshToolsStatus();
}

// ---- 速度阶梯 --------------------------------------------------------------------
namespace
{
    class TrainerSettings : public juce::Component
    {
    public:
        TrainerSettings (SpeedTrainer& t, std::function<void (bool)> onEnable, std::function<void()> onChange)
            : trainer (t), enableChanged (std::move (onEnable)), changed (std::move (onChange))
        {
            enable.setButtonText (TR ("Enable Speed Trainer", "开启速度阶梯"));
            enable.setToggleState (trainer.enabled, juce::dontSendNotification);
            enable.onClick = [this] { if (enableChanged) enableChanged (enable.getToggleState()); };
            addAndMakeVisible (enable);

            auto add = [this] (const juce::String& name, double lo, double hi, double stepV, double value,
                               const juce::String& suffix, std::function<void (double)> set)
            {
                auto* l = labels.add (new juce::Label ({}, name));
                l->setFont (juce::FontOptions (13.0f));
                addAndMakeVisible (l);
                auto* s = sliders.add (new juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight));
                s->setRange (lo, hi, stepV);
                s->setTextValueSuffix (suffix);
                s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
                s->setValue (value, juce::dontSendNotification);
                s->onValueChange = [this, s, set] { set (s->getValue()); if (changed) changed(); };
                addAndMakeVisible (s);
            };
            add (TR ("Start speed", "起始速度"), 25, 100, 5, trainer.start * 100.0, "%", [this] (double v) { trainer.start = (float) v / 100.0f; });
            add (TR ("Step up", "每档提速"), 1, 20, 1, trainer.step * 100.0, "%", [this] (double v) { trainer.step = (float) v / 100.0f; });
            add (TR ("Target speed", "目标速度"), 50, 150, 5, trainer.target * 100.0, "%", [this] (double v) { trainer.target = (float) v / 100.0f; });
            add (TR ("Passes per step", "每档过几遍"), 1, 5, 1, trainer.passesPerStep, TR (" passes", " 遍"), [this] (double v) { trainer.passesPerStep = (int) v; });
            add (TR ("Required accuracy", "命中率要求"), 50, 100, 5, trainer.needAccuracy * 100.0, "%", [this] (double v) { trainer.needAccuracy = (float) v / 100.0f; });

            note.setText (TR ("Use with an A-B loop (or the whole song if no loop is set). With Playing Judgement on, each pass must reach the target accuracy; otherwise passes are just counted.",
                              "在 A-B 循环里用（没设循环就整首循环）。开着“弹奏判定”时，一遍的命中率达到要求才算过；没开判定就只数遍数。"), juce::dontSendNotification);
            note.setFont (juce::FontOptions (12.0f));
            note.setColour (juce::Label::textColourId, juce::Colour (0xffaaaaaa));
            note.setJustificationType (juce::Justification::topLeft);
            note.setMinimumHorizontalScale (1.0f);
            addAndMakeVisible (note);
            setSize (400, 40 + 30 * sliders.size() + 50);
        }

        void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff262626)); }
        void resized() override
        {
            auto r = getLocalBounds().reduced (12, 8);
            enable.setBounds (r.removeFromTop (28));
            for (int i = 0; i < sliders.size(); ++i)
            {
                auto row = r.removeFromTop (30).reduced (0, 3);
                labels[i]->setBounds (row.removeFromLeft (90));
                sliders[i]->setBounds (row);
            }
            note.setBounds (r.withTrimmedTop (4));
        }

    private:
        SpeedTrainer& trainer;
        std::function<void (bool)> enableChanged;
        std::function<void()> changed;
        juce::ToggleButton enable;
        juce::OwnedArray<juce::Label> labels;
        juce::OwnedArray<juce::Slider> sliders;
        juce::Label note;
    };
}

void PracticePanel::showTrainerSettings()
{
    auto box = std::make_unique<TrainerSettings> (trainer, [this] (bool on) { enableTrainer (on); },
                                                  [this] { saveSettings(); refreshToolsStatus(); });
    juce::CallOutBox::launchAsynchronously (std::move (box), trainerButton.getScreenBounds(), nullptr);
}

void PracticePanel::enableTrainer (bool on)
{
    trainer.enabled = on;
    if (on)
    {
        if (song != nullptr && ! player.isLooping())
        {
            loopA = 0.0; loopB = song->lengthBeats;
            player.setLoop (loopA, loopB, true);
            refreshLoopButtons();
        }
        trainer.restart();
        speedSlider.setValue (trainer.current * 100.0, juce::sendNotificationSync);
        if (player.isLooping())
            player.seek (player.getLoopA());
    }
    trainerButton.setColour (juce::TextButton::buttonColourId, on ? accent.darker (0.3f) : juce::Colour (0xff3a3a3a));
    refreshToolsStatus();
}

void PracticePanel::onLoopPass (float accuracy)
{
    if (song != nullptr && accuracy >= 0.0f)
        log.addPass (song->file.getFullPathName(), song->title, accuracy, player.getSpeed());

    if (trainer.enabled)
    {
        const bool wasFinished = trainer.finished();
        if (trainer.onPass (accuracy))
        {
            speedSlider.setValue (trainer.current * 100.0, juce::sendNotificationSync);
            toolsMessage = trainer.finished() ? TR ("Target speed reached!", "到目标速度了！") : TR ("Speed up to ", "提速到 ") + juce::String (juce::roundToInt (trainer.current * 100.0f)) + "%";
        }
        else if (accuracy >= 0.0f && accuracy < trainer.needAccuracy && ! wasFinished)
        {
            toolsMessage = TR ("This run ", "这一遍 ") + juce::String (juce::roundToInt (accuracy * 100.0f)) + TR ("%, needed ", "%，没到 ")
                           + juce::String (juce::roundToInt (trainer.needAccuracy * 100.0f)) + TR ("%. Step restarts.", "%，这一档重新数");
        }
    }
    refreshToolsStatus();
}

void PracticePanel::refreshToolsStatus()
{
    auto fmt = [] (double s) { const int t = (int) s; return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2); };
    juce::String text;
    juce::Colour colour (0xff9cc4ff);

    if (recorder.isRecording())
    {
        text = TR ("● Recording ", "● 录音中 ") + fmt (recorder.getRecordedSeconds()) + TR (" (click again to stop)", "（再点一下停止）");
        colour = juce::Colour (0xffff6b6b);
        recordButton.setButtonText (TR ("■ Stop Recording", "■ 停止录音"));
        recordButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffb03a3a));
    }
    else
    {
        recordButton.setButtonText (TR ("● Record", "● 录音"));
        recordButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3a3a3a));
        if (replaying)
            text = TR ("Playing ", "回放中 ") + fmt (recorder.getPlaybackSeconds()) + " / " + fmt (recorder.getPlaybackLength())
                   + TR (": the recording replaces the guitar input; switch tones as you listen", "：录音代替了吉他输入，可以边听边换音色");
        else if (trainer.enabled)
        {
            text = TR ("Speed trainer: ", "速度阶梯：") + juce::String (juce::roundToInt (trainer.current * 100.0f)) + "% → "
                   + juce::String (juce::roundToInt (trainer.target * 100.0f)) + "%";
            if (! trainer.finished())
            {
                const int left = juce::jmax (1, trainer.passesPerStep - trainer.goodPasses);
                text << TR (", ", "，这一档还要过 ") << left << TR (" more passes at this step", " 遍");
                if (judgeToggle.getToggleState()) text << TR (" (accuracy ≥ ", "（命中率 ≥ ") << juce::roundToInt (trainer.needAccuracy * 100.0f) << "%）";
            }
            if (toolsMessage.isNotEmpty()) text << "    " << toolsMessage;
        }
        else
            text = toolsMessage;
    }
    toolsStatus.setColour (juce::Label::textColourId, colour);
    if (toolsStatus.getText() != text)
        toolsStatus.setText (text, juce::dontSendNotification);
}
