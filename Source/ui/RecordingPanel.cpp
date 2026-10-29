#include "RecordingPanel.h"
#include "Text.h"
#include "app/AppPaths.h"
#include "fx/EffectChain.h"
#include <algorithm>
#include <memory>

namespace
{
    // Offline bounce with the current effect chain, on a background thread.
    class ExportJob : public juce::ThreadWithProgressWindow
    {
    public:
        ExportJob (juce::File sourceIn, juce::File outputIn, int modeIn,
                   std::shared_ptr<juce::XmlElement> chainXmlIn)
            : juce::ThreadWithProgressWindow (TR ("Exporting WAV…", "正在导出 WAV…"), true, true),
              source (std::move (sourceIn)),
              output (std::move (outputIn)),
              mode (modeIn),
              chainXml (std::move (chainXmlIn))
        {
        }

        void run() override
        {
            // The recorded mix already contains drums, backing and the loop.
            if (mode == 3)
            {
                if (! source.existsAsFile())
                {
                    error = TR ("The recorded mix.wav is missing", "找不到录音的 mix.wav");
                    return;
                }
                if (source == output)
                {
                    error = TR ("Choose a different output file", "请选择不同的输出文件");
                    return;
                }

                setStatusMessage (TR ("Copying the recorded mix…", "复制完整录音…"));
                setProgress (0.1);
                if (! source.copyFileTo (output))
                {
                    error = TR ("Cannot write ", "没法写入 ") + output.getFullPathName();
                    return;
                }
                setProgress (1.0);
                return;
            }

            setStatusMessage (TR ("Reading the take…", "读取录音…"));
            setProgress (0.02);

            Recorder::Info info;
            double rate = 0.0;
            auto dry = Recorder::loadEffectiveDry (source, error, &info, &rate);
            if (dry == nullptr)
                return;

            const int total = dry->getNumSamples();
            if (total <= 0)
            {
                error = TR ("The take is empty", "录音是空的");
                return;
            }

            if (mode == 2)
            {
                std::unique_ptr<juce::OutputStream> stream (output.createOutputStream());
                if (stream == nullptr)
                {
                    error = TR ("Cannot write ", "没法写入 ") + output.getFullPathName();
                    return;
                }

                juce::WavAudioFormat format;
                std::unique_ptr<juce::AudioFormatWriter> writer (
                    format.createWriterFor (stream.get(), rate, 1, 24, {}, 0));
                if (writer == nullptr)
                {
                    error = TR ("Cannot create the WAV writer", "没法创建 WAV 写入器");
                    return;
                }
                stream.release();

                const float* channels[] = { dry->getReadPointer (0) };
                writer->writeFromFloatArrays (channels, 1, total);
                setProgress (1.0);
                return;
            }

            setStatusMessage (TR ("Loading the current effect chain…", "加载当前效果器链…"));
            setProgress (0.05);

            AudioEngine engine;
            engine.setPracticeEnabled (false);
            engine.setOutputGainDb (0.0f);   // bounce at unity, not the monitor level
            engine.prepareForOffline (rate, 512);

            EffectChain chain (engine);
            if (chainXml != nullptr)
            {
                juce::StringArray problems;
                chain.loadFromXml (*chainXml, problems);
            }

            std::unique_ptr<juce::OutputStream> stream (output.createOutputStream());
            if (stream == nullptr)
            {
                error = TR ("Cannot write ", "没法写入 ") + output.getFullPathName();
                return;
            }

            juce::WavAudioFormat format;
            std::unique_ptr<juce::AudioFormatWriter> writer (
                format.createWriterFor (stream.get(), rate, 2, 24, {}, 0));
            if (writer == nullptr)
            {
                error = TR ("Cannot create the WAV writer", "没法创建 WAV 写入器");
                return;
            }
            stream.release();

            std::vector<float> left (512), right (512), zeros (512, 0.0f);
            int64_t skip = 0;
            int64_t written = 0;
            bool latencyKnown = false;

            for (int pos = 0; pos < total; pos += 512)
            {
                if (threadShouldExit())
                {
                    error = TR ("Export cancelled", "导出已取消");
                    return;
                }

                const int n = juce::jmin (512, total - pos);
                engine.processOffline (dry->getReadPointer (0) + pos, left.data(), right.data(), n);

                if (! latencyKnown)
                {
                    latencyKnown = true;
                    skip = juce::jlimit<int64_t> (0, total, engine.getChainLatencySamples());
                }

                int offset = 0;
                if (skip > 0)
                {
                    offset = (int) juce::jmin<int64_t> (skip, n);
                    skip -= offset;
                }

                if (offset < n)
                {
                    const float* channels[] = { left.data() + offset, right.data() + offset };
                    writer->writeFromFloatArrays (channels, 2, n - offset);
                    written += n - offset;
                }

                setProgress (0.05 + 0.94 * (double) (pos + n) / (double) total);
            }

            // Keep the exported length equal to the dry take even if the chain
            // reported latency at the start.
            while (written < total)
            {
                const int n = (int) juce::jmin<int64_t> (512, total - written);
                const float* channels[] = { zeros.data(), zeros.data() };
                writer->writeFromFloatArrays (channels, 2, n);
                written += n;
            }

            setProgress (1.0);
        }

        void threadComplete (bool userPressedCancel) override
        {
            if (userPressedCancel || error.isNotEmpty())
            {
                output.deleteFile();
                if (error.isNotEmpty())
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                            TR ("Export Failed", "导出失败"), error);
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                                                        TR ("Export complete", "导出完成"),
                                                        output.getFullPathName());
            }

            delete this;
        }

        juce::String error;

    private:
        juce::File source, output;
        int mode = 1;
        std::shared_ptr<juce::XmlElement> chainXml;
    };
}

RecordingPanel::RecordingPanel (AudioEngine& engineToUse, EffectChain& chainToUse,
                                juce::PropertiesFile& settingsToUse)
    : engine (engineToUse),
      recorder (engineToUse.getRecorder()),
      player (engineToUse.getTabPlayer()),
      effectChain (chainToUse),
      settings (settingsToUse)
{
    title.setText (TR ("Recording", "录音"), juce::dontSendNotification);
    title.setFont (juce::FontOptions (21.0f, juce::Font::bold));
    addAndMakeVisible (title);

    hint.setText (TR ("Click to replay from a point; drag a region then Punch for a non-destructive re-take. SPACE plays / pauses.",
                      "点波形从该处回放；拖出选区后点“补录”做非破坏性重录。空格播放 / 暂停。"),
                  juce::dontSendNotification);
    hint.setFont (juce::FontOptions (13.0f));
    hint.setColour (juce::Label::textColourId, juce::Colour (0xff9a9a9a));
    addAndMakeVisible (hint);

    status.setFont (juce::FontOptions (14.0f));
    status.setColour (juce::Label::textColourId, juce::Colour (0xff5dcaa5));
    addAndMakeVisible (status);

    recordButton.setButtonText (TR ("● Record", "● 录音"));
    recordButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffa32d2d));
    recordButton.onClick = [this] { toggleRecord(); };
    addAndMakeVisible (recordButton);

    setWantsKeyboardFocus (true);

    replayButton.setButtonText (TR ("▶ Play", "▶ 播放"));
    replayButton.setTooltip (TR ("Play / continue from the playhead (SPACE)", "从播放头开始 / 继续播放（空格）"));
    replayButton.onClick = [this] { startReplayFrom (resumeSeconds); };
    addAndMakeVisible (replayButton);

    pauseButton.setButtonText (TR ("❚❚ Pause", "❚❚ 暂停"));
    pauseButton.setTooltip (TR ("Pause and keep the playhead (SPACE)", "暂停并保留播放头（空格）"));
    pauseButton.onClick = [this] { pauseReplay(); };
    addAndMakeVisible (pauseButton);

    stopButton.setButtonText (TR ("■ Stop", "■ 停止"));
    stopButton.setTooltip (TR ("Stop and rewind to the start", "停止并回到开头"));
    stopButton.onClick = [this] { stopReplay(); };
    addAndMakeVisible (stopButton);

    punchButton.setButtonText (TR ("Punch Selection", "补录选区"));
    punchButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff7a4a1f));
    punchButton.onClick = [this] { togglePunch(); };
    addAndMakeVisible (punchButton);

    exportModeBox.addItem (TR ("Re-amped (current chain)", "重过效果器（当前链）"), 1);
    exportModeBox.addItem (TR ("Dry DI stem", "干声 DI"), 2);
    exportModeBox.addItem (TR ("Full recorded mix (pre-punch)", "完整录音混音（不含补录）"), 3);
    exportModeBox.setSelectedId (1, juce::dontSendNotification);
    exportModeBox.setTooltip (TR ("Re-amped renders the take through the current chain; Dry writes the DI stem.",
                                  "重过效果器会把录音过一遍当前效果器链；干声只导出 DI。"));
    addAndMakeVisible (exportModeBox);

    exportButton.setButtonText (TR ("Export WAV…", "导出 WAV…"));
    exportButton.onClick = [this] { exportSelectedTake(); };
    addAndMakeVisible (exportButton);

    fullMixToggle.setButtonText (TR ("Full mix (off = dry)", "完整混音（关 = 干声）"));
    fullMixToggle.setToggleState (settings.getBoolValue ("recordFullMix", true),
                                  juce::dontSendNotification);
    fullMixToggle.setTooltip (TR ("ON: play the recorded mix.wav (drums, backing, loop). "
                                  "OFF: play the dry stem through the current chain. "
                                  "Punch edits only apply to the dry/re-amped path.",
                                  "开：播放录音的完整 mix.wav（鼓机、伴奏、循环）。"
                                  "关：干声走当前效果器链。补录只作用于干声路径。"));
    fullMixToggle.onClick = [this]
    {
        settings.setValue ("recordFullMix", fullMixToggle.getToggleState());
        if (recorder.isPlayingBack())
            stopReplay();

        updateControls();
    };
    addAndMakeVisible (fullMixToggle);

    deleteButton.setButtonText (TR ("Delete Take", "删除录音"));
    deleteButton.onClick = [this] { deleteSelected(); };
    addAndMakeVisible (deleteButton);

    refreshButton.setButtonText (TR ("Refresh", "刷新"));
    refreshButton.onClick = [this] { refreshTakes(); };
    addAndMakeVisible (refreshButton);

    folderButton.setButtonText (TR ("Open Folder", "打开文件夹"));
    folderButton.onClick = [this] { openFolder(); };
    addAndMakeVisible (folderButton);

    chooseFolderButton.setButtonText (TR ("Change Folder…", "更改文件夹…"));
    chooseFolderButton.onClick = [this] { chooseFolder(); };
    addAndMakeVisible (chooseFolderButton);

    list.setModel (this);
    list.setRowHeight (42);
    list.setMultipleSelectionEnabled (false);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff1d1d1d));
    addAndMakeVisible (list);

    addAndMakeVisible (timeline);
    timeline.onSeek = [this] (double seconds) { seekReplayTo (seconds); };
    timeline.onSelectionChanged = [this] (double start, double end)
    {
        selectionStart = start;
        selectionEnd = end;
        updateControls();
    };

    refreshTakes();
    updateControls();
    startTimerHz (30);
}

RecordingPanel::~RecordingPanel()
{
    stopTimer();
    if (recorder.isPlayingBack())
        recorder.stopPlayback();
}

void RecordingPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff151515));
}

void RecordingPanel::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto top = area.removeFromTop (34);
    title.setBounds (top.removeFromLeft (200));
    recordButton.setBounds (top.removeFromRight (150).reduced (0, 1));
    top.removeFromRight (10);
    status.setBounds (top);

    auto hintRow = area.removeFromTop (38);
    fullMixToggle.setBounds (hintRow.removeFromRight (200).reduced (0, 6));
    hint.setBounds (hintRow);
    area.removeFromTop (6);

    const int timelineH = juce::jlimit (150, 240, area.getHeight() * 2 / 5);
    timeline.setBounds (area.removeFromTop (timelineH));
    area.removeFromTop (8);

    auto bottom = area.removeFromBottom (34);
    replayButton.setBounds (bottom.removeFromLeft (66).reduced (0, 1));
    bottom.removeFromLeft (8);
    pauseButton.setBounds (bottom.removeFromLeft (74).reduced (0, 1));
    bottom.removeFromLeft (8);
    stopButton.setBounds (bottom.removeFromLeft (62).reduced (0, 1));
    bottom.removeFromLeft (8);
    punchButton.setBounds (bottom.removeFromLeft (105).reduced (0, 1));
    bottom.removeFromLeft (8);
    deleteButton.setBounds (bottom.removeFromLeft (85).reduced (0, 1));
    bottom.removeFromLeft (8);
    refreshButton.setBounds (bottom.removeFromLeft (75).reduced (0, 1));
    bottom.removeFromLeft (8);
    exportButton.setBounds (bottom.removeFromLeft (105).reduced (0, 1));
    bottom.removeFromLeft (8);
    exportModeBox.setBounds (bottom.removeFromLeft (170).reduced (0, 3));
    chooseFolderButton.setBounds (bottom.removeFromRight (120).reduced (0, 1));
    bottom.removeFromRight (8);
    folderButton.setBounds (bottom.removeFromRight (100).reduced (0, 1));

    area.removeFromBottom (8);
    list.setBounds (area);
}

int RecordingPanel::getNumRows()
{
    return (int) takes.size();
}

void RecordingPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) takes.size()))
        return;

    const auto& t = takes[(size_t) row];
    if (selected)
        g.fillAll (juce::Colour (0xff2d5c4a));
    else if ((row & 1) != 0)
        g.fillAll (juce::Colour (0xff222222));

    auto r = juce::Rectangle<int> (0, 0, width, height).reduced (10, 4);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (14.0f));
    const auto name = t.info.songTitle.isNotEmpty() ? t.info.songTitle
                                                    : TR ("(no tab)", "（没有谱子）");
    g.drawText (name, r.removeFromTop (20), juce::Justification::centredLeft, true);

    const int seconds = juce::roundToInt (t.info.lengthSeconds);
    juce::String details = t.info.created.formatted ("%Y-%m-%d %H:%M")
                         + "   " + juce::String (seconds / 60) + ":" + juce::String (seconds % 60).paddedLeft ('0', 2)
                         + "   " + TR ("mix + dry", "混音 + 干声");
    if (t.info.synced)
        details << "   " << TR ("synced to tab", "和谱子同步");
    if (t.info.speed < 0.999f)
        details << "   " << juce::roundToInt (t.info.speed * 100.0f) << "%";

    g.setColour (juce::Colour (0xff9a9a9a));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (details, r, juce::Justification::centredLeft, true);
}

void RecordingPanel::listBoxItemDoubleClicked (int, const juce::MouseEvent&)
{
    startReplayFrom (0.0);
}

void RecordingPanel::selectedRowsChanged (int)
{
    if (recorder.isPlayingBack())
        recorder.stopPlayback();

    resumeSeconds = 0.0;
    loadSelectedTake();
    updateControls();
}

void RecordingPanel::loadSelectedTake()
{
    resumeSeconds = 0.0;
    const int row = list.getSelectedRow();
    if (juce::isPositiveAndBelow (row, (int) takes.size()))
    {
        const auto& take = takes[(size_t) row];
        const auto wav = take.info.dryFile.existsAsFile() ? take.info.dryFile : take.info.mixFile;
        timeline.setFile (wav, take.info.lengthSeconds);

        std::vector<std::pair<double, double>> regions;
        for (const auto& p : take.info.punches)
            if (p.endSeconds > p.startSeconds)
                regions.emplace_back (p.startSeconds, p.endSeconds);
        timeline.setPunchRegions (regions);
    }
    else
    {
        timeline.clearFile();
    }

    timeline.setPunchState (false, 0.0, 0.0);
}

void RecordingPanel::timerCallback()
{
    if (recorder.hasPunchFinished())
    {
        const auto clip = recorder.finishPunchRecording();
        if (clip.existsAsFile())
        {
            refreshTakes();
            loadSelectedTake();
        }
    }

    if (recorder.isPunchRecording())
    {
        timeline.setRecordingLive (false, 0.0, 0, 0.01, nullptr);
        timeline.setPunchState (true, recorder.getPunchStartSeconds(), recorder.getPunchEndSeconds());
        timeline.setPlayPosition (recorder.getPlaybackSeconds(), false, 0.0);
    }
    else if (recorder.isRecording())
    {
        timeline.setPunchState (false, 0.0, 0.0);
        timeline.setRecordingLive (true, recorder.getRecordedSeconds(), recorder.getLiveWaveWrite(),
                                   recorder.getLiveWaveBucketSeconds(),
                                   [this] (uint32_t i) { return recorder.getLiveWavePeak (i); });
    }
    else
    {
        timeline.setRecordingLive (false, 0.0, 0, 0.01, nullptr);
        timeline.setPunchState (false, 0.0, 0.0);

        if (recorder.isPlayingBack())
        {
            resumeSeconds = recorder.getPlaybackSeconds();
        }
        else
        {
            const auto length = recorder.getPlaybackLength();
            if (resumeSeconds > 0.0 && length > 0.0
                && recorder.getPlaybackSeconds() >= length - 0.05)
                resumeSeconds = 0.0;   // playback reached the end, not a pause
        }

        timeline.setPlayPosition (resumeSeconds, false, 0.0);
    }

    updateControls();
}

void RecordingPanel::refreshTakes()
{
    const int previousRow = list.getSelectedRow();
    const auto previousFile = juce::isPositiveAndBelow (previousRow, (int) takes.size())
                                  ? takes[(size_t) previousRow].mixFile
                                  : juce::File();

    takes.clear();

    auto folder = Recorder::getFolder();
    folder.createDirectory();

    auto files = folder.findChildFiles (juce::File::findFiles, true, "mix.wav");
    for (auto& legacy : folder.findChildFiles (juce::File::findFiles, false, "*.wav"))
        if (! legacy.getFileName().equalsIgnoreCase ("mix.wav"))
            files.add (legacy);

    for (auto& f : files)
        takes.push_back ({ f, Recorder::Info::loadFor (f) });

    std::sort (takes.begin(), takes.end(), [] (const Take& a, const Take& b)
               { return a.info.created > b.info.created; });

    list.updateContent();

    int select = -1;
    for (int i = 0; i < (int) takes.size(); ++i)
        if (takes[(size_t) i].mixFile == previousFile)
            select = i;

    if (select < 0 && ! takes.empty())
        select = 0;

    if (select >= 0)
        list.selectRow (select);
    else
        list.deselectAllRows();

    // The selected row may be the same index but now point at a different
    // take (the list is sorted by time), so always reload the waveform.
    loadSelectedTake();

    list.repaint();
    updateControls();
}

void RecordingPanel::toggleRecord()
{
    if (recorder.isPunchRecording())
        return;

    if (recorder.isRecording())
    {
        recorder.stopRecording();
        refreshTakes();
        if (! takes.empty())
        {
            list.selectRow (0);   // the new take is the newest row
            loadSelectedTake();
        }
        return;
    }

    recorder.stopPlayback();
    resumeSeconds = 0.0;
    timeline.clearSelection();
    selectionStart = selectionEnd = -1.0;

    Recorder::Info info;
    if (auto song = player.getSong())
    {
        info.songFile = song->file;
        info.songTitle = song->title.isNotEmpty() ? song->title : song->file.getFileNameWithoutExtension();
        info.track = player.getTrack();
        info.speed = player.getSpeed();
        info.loopOn = player.isLooping();
        info.loopA = player.getLoopA();
        info.loopB = player.getLoopB();
    }

    juce::String error;
    if (! recorder.startRecording (info, error))
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                TR ("Cannot Record", "没法录音"), error);

    updateControls();
}

void RecordingPanel::startReplayFrom (double seconds)
{
    if (recorder.isRecording() || recorder.isPunchRecording())
        return;

    const int row = list.getSelectedRow();
    if (! juce::isPositiveAndBelow (row, (int) takes.size()))
        return;

    player.setPlaying (false);

    if (recorder.isPlayingBack())
    {
        recorder.setPlaybackPosition (seconds);
        resumeSeconds = seconds;
        updateControls();
        return;
    }

    const double length = takes[(size_t) row].info.lengthSeconds > 0.0
                              ? takes[(size_t) row].info.lengthSeconds
                              : timeline.getLengthSeconds();
    if (length > 0.0 && seconds >= length - 0.01)
        seconds = 0.0;   // pressing Play at the very end starts again

    juce::String error;
    if (! recorder.startPlayback (takes[(size_t) row].mixFile, error, nullptr, seconds,
                                  fullMixToggle.getToggleState()))
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                TR ("Cannot Replay", "没法回放"), error);
    else
        resumeSeconds = seconds;

    updateControls();
}

void RecordingPanel::pauseReplay()
{
    if (! recorder.isPlayingBack() || recorder.isPunchRecording())
        return;

    resumeSeconds = recorder.getPlaybackSeconds();
    recorder.stopPlayback();
    updateControls();
}

void RecordingPanel::stopReplay()
{
    if (recorder.isPlayingBack())
        recorder.stopPlayback();

    resumeSeconds = 0.0;
    updateControls();
}

void RecordingPanel::seekReplayTo (double seconds)
{
    startReplayFrom (seconds);
}

void RecordingPanel::togglePlayPause()
{
    if (recorder.isRecording() || recorder.isPunchRecording())
        return;

    if (recorder.isPlayingBack())
        pauseReplay();
    else
        startReplayFrom (resumeSeconds);
}

bool RecordingPanel::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        if (spaceHeld && juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::spaceKey))
            return true;

        spaceHeld = true;
        togglePlayPause();
        return true;
    }

    return false;
}

bool RecordingPanel::keyStateChanged (bool)
{
    if (! juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::spaceKey))
        spaceHeld = false;
    return false;
}

void RecordingPanel::focusLost (FocusChangeType)
{
    spaceHeld = false;
}

void RecordingPanel::togglePunch()
{
    if (recorder.isPunchRecording())
    {
        recorder.stopPunchRecording();
        updateControls();
        return;
    }

    if (recorder.isRecording() || recorder.isPlayingBack())
        return;

    const int row = list.getSelectedRow();
    if (! juce::isPositiveAndBelow (row, (int) takes.size()) || ! timeline.hasSelection())
        return;

    player.setPlaying (false);
    resumeSeconds = 0.0;
    juce::String error;
    if (! recorder.startPunchRecording (takes[(size_t) row].mixFile,
                                       timeline.getSelectionStart(),
                                       timeline.getSelectionEnd(), error))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                TR ("Cannot Punch", "没法补录"), error);
    }

    updateControls();
}

void RecordingPanel::exportSelectedTake()
{
    if (recorder.isRecording() || recorder.isPunchRecording())
        return;

    const int row = list.getSelectedRow();
    if (! juce::isPositiveAndBelow (row, (int) takes.size()))
        return;

    const auto take = takes[(size_t) row];
    const int mode = juce::jlimit (1, 3, exportModeBox.getSelectedId());
    const auto folder = take.mixFile.getParentDirectory();
    const auto suffix = mode == 1 ? "-reamp.wav" : mode == 2 ? "-dry.wav" : "-full.wav";
    const auto suggested = folder.getChildFile (folder.getFileName() + suffix);

    auto chainXml = effectChain.toXml();
    auto chainPtr = std::shared_ptr<juce::XmlElement> (chainXml.release());

    exportChooser = std::make_unique<juce::FileChooser> (TR ("Export WAV", "导出 WAV"), suggested, "*.wav");
    exportChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                    | juce::FileBrowserComponent::canSelectFiles
                                    | juce::FileBrowserComponent::warnAboutOverwriting,
                                [this, take, mode, chainPtr] (const juce::FileChooser& fc)
    {
        auto output = fc.getResult();
        if (output == juce::File())
            return;
        if (! output.hasFileExtension ("wav"))
            output = output.withFileExtension ("wav");

        // ThreadWithProgressWindow owns itself through threadComplete().
        (new ExportJob (take.mixFile, output, mode, chainPtr))->launchThread();
    });
}

void RecordingPanel::deleteSelected()
{
    const int row = list.getSelectedRow();
    if (! juce::isPositiveAndBelow (row, (int) takes.size()))
        return;

    const auto take = takes[(size_t) row];
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon,
                                        TR ("Delete Take", "删除录音"),
                                        TR ("Delete this take and both stems?", "删除这次录音和两个文件？"),
                                        TR ("Delete", "删除"), TR ("Cancel", "取消"), this,
                                        juce::ModalCallbackFunction::create ([this, take] (int result)
    {
        if (result != 1)
            return;

        const auto folder = take.mixFile.getParentDirectory();
        if (folder.getChildFile ("take.json").existsAsFile())
            folder.deleteRecursively();
        else
        {
            take.mixFile.deleteFile();
            take.mixFile.withFileExtension ("json").deleteFile();
        }

        refreshTakes();
    }));
}

void RecordingPanel::openFolder()
{
    auto folder = Recorder::getFolder();
    folder.createDirectory();
    folder.startAsProcess();
}

void RecordingPanel::chooseFolder()
{
    auto folder = Recorder::getFolder();
    folder.createDirectory();

    chooser = std::make_unique<juce::FileChooser> (TR ("Choose recordings folder", "选择录音文件夹"), folder);
    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
    {
        const auto dir = fc.getResult();
        if (! dir.isDirectory())
            return;

        AppPaths::setRecordingsRoot (dir);
        settings.setValue ("recording.root", dir.getFullPathName());
        settings.saveIfNeeded();
        refreshTakes();
        updateControls();
    });
}

void RecordingPanel::updateControls()
{
    const bool recording = recorder.isRecording();
    const bool punching = recorder.isPunchRecording();
    const bool replaying = recorder.isPlayingBack();
    const bool hasTake = juce::isPositiveAndBelow (list.getSelectedRow(), (int) takes.size());
    const bool paused = ! recording && ! punching && ! replaying && resumeSeconds > 0.0;

    recordButton.setButtonText (recording ? TR ("■ Stop Recording", "■ 停止录音")
                                          : TR ("● Record", "● 录音"));
    recordButton.setEnabled (! replaying && ! punching);

    replayButton.setEnabled (! recording && ! punching && ! replaying && hasTake);
    pauseButton.setEnabled (replaying && ! punching);
    stopButton.setEnabled ((replaying && ! punching) || paused);

    punchButton.setButtonText (punching ? TR ("■ Stop Punch", "■ 停止补录")
                                        : TR ("Punch Selection", "补录选区"));
    punchButton.setEnabled (punching
                            || (! recording && ! replaying && hasTake && timeline.hasSelection()));

    deleteButton.setEnabled (! recording && ! punching && hasTake);
    exportButton.setEnabled (! recording && ! punching && ! replaying && hasTake);
    exportModeBox.setEnabled (exportButton.isEnabled());
    list.setEnabled (! recording && ! punching);
    timeline.setEnabled (! recording && ! punching);
    fullMixToggle.setEnabled (! recording && ! punching);

    if (punching)
    {
        status.setText (TR ("Punch ", "补录 ") + juce::String (recorder.getPunchStartSeconds(), 2)
                            + TR ("–", "–") + juce::String (recorder.getPunchEndSeconds(), 2)
                            + TR (" s  ·  play along; it stops at the end", " 秒  ·  跟着弹，到结尾自动停"),
                        juce::dontSendNotification);
    }
    else if (recording)
        status.setText (TR ("Recording ", "录音中 ") + juce::String (recorder.getRecordedSeconds(), 1)
                            + TR (" s  ·  mix.wav + dry.wav", " 秒  ·  mix.wav + dry.wav"),
                        juce::dontSendNotification);
    else if (replaying)
        status.setText (TR ("Playing ", "播放中 ") + juce::String (recorder.getPlaybackSeconds(), 2)
                            + TR (" s  ·  ", " 秒  ·  ")
                            + (fullMixToggle.getToggleState() ? TR ("full mix", "完整混音")
                                                              : TR ("dry + chain", "干声 + 效果器"))
                            + TR ("  ·  SPACE pauses; click the waveform to jump",
                                  "  ·  空格暂停；点波形跳转"),
                        juce::dontSendNotification);
    else if (paused)
        status.setText (TR ("Paused at ", "暂停在 ") + juce::String (resumeSeconds, 2)
                            + TR (" s  ·  SPACE continues", " 秒  ·  空格继续"),
                        juce::dontSendNotification);
    else
    {
        juce::String s = juce::String ((int) takes.size()) + TR (" takes", " 次录音");
        if (timeline.hasSelection())
            s << TR ("  ·  selection ", "  ·  选区 ")
              << juce::String (selectionEnd - selectionStart, 2) + " s";
        else
            s << "  ·  " << Recorder::getFolder().getFullPathName();

        status.setText (s, juce::dontSendNotification);
    }
}
