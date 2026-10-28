#include "RecordingPanel.h"
#include "Text.h"
#include "app/AppPaths.h"
#include <algorithm>

RecordingPanel::RecordingPanel (AudioEngine& engineToUse, juce::PropertiesFile& settingsToUse)
    : engine (engineToUse),
      recorder (engineToUse.getRecorder()),
      player (engineToUse.getTabPlayer()),
      settings (settingsToUse)
{
    title.setText (TR ("Recording", "录音"), juce::dontSendNotification);
    title.setFont (juce::FontOptions (21.0f, juce::Font::bold));
    addAndMakeVisible (title);

    hint.setText (TR ("Each take stores mix.wav (guitar + FX + drums + backing, stereo) and dry.wav (raw guitar, mono).",
                      "每次录音会保存 mix.wav（吉他 + 效果器 + 鼓机 + 伴奏，立体声）和 dry.wav（干吉他，单声道）。"),
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

    replayButton.setButtonText (TR ("Replay dry through chain", "干声走当前效果器链回放"));
    replayButton.onClick = [this] { toggleReplay(); };
    addAndMakeVisible (replayButton);

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

    refreshTakes();
    updateControls();
    startTimerHz (10);
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

    hint.setBounds (area.removeFromTop (38));
    area.removeFromTop (6);

    auto bottom = area.removeFromBottom (34);
    replayButton.setBounds (bottom.removeFromLeft (230).reduced (0, 1));
    bottom.removeFromLeft (8);
    deleteButton.setBounds (bottom.removeFromLeft (120).reduced (0, 1));
    bottom.removeFromLeft (8);
    refreshButton.setBounds (bottom.removeFromLeft (100).reduced (0, 1));
    chooseFolderButton.setBounds (bottom.removeFromRight (130).reduced (0, 1));
    bottom.removeFromRight (8);
    folderButton.setBounds (bottom.removeFromRight (120).reduced (0, 1));

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
    toggleReplay();
}

void RecordingPanel::selectedRowsChanged (int)
{
    updateControls();
}

void RecordingPanel::timerCallback()
{
    updateControls();
}

void RecordingPanel::refreshTakes()
{
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
    list.repaint();
    updateControls();
}

void RecordingPanel::toggleRecord()
{
    if (recorder.isRecording())
    {
        recorder.stopRecording();
        refreshTakes();
        updateControls();
        return;
    }

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

void RecordingPanel::toggleReplay()
{
    if (recorder.isPlayingBack())
    {
        recorder.stopPlayback();
        updateControls();
        return;
    }

    const int row = list.getSelectedRow();
    if (! juce::isPositiveAndBelow (row, (int) takes.size()))
        return;

    player.setPlaying (false);
    juce::String error;
    if (! recorder.startPlayback (takes[(size_t) row].mixFile, error))
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                TR ("Cannot Replay", "没法回放"), error);

    updateControls();
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
    const bool replaying = recorder.isPlayingBack();

    recordButton.setButtonText (recording ? TR ("■ Stop Recording", "■ 停止录音")
                                          : TR ("● Record", "● 录音"));
    recordButton.setEnabled (! replaying);

    replayButton.setButtonText (replaying ? TR ("■ Stop Replay", "■ 停止回放")
                                          : TR ("Replay dry through chain", "干声走当前效果器链回放"));
    replayButton.setEnabled (! recording && list.getSelectedRow() >= 0);
    deleteButton.setEnabled (! recording && list.getSelectedRow() >= 0);

    if (recording)
        status.setText (TR ("Recording ", "录音中 ") + juce::String (recorder.getRecordedSeconds(), 1)
                            + TR (" s  ·  mix.wav + dry.wav", " 秒  ·  mix.wav + dry.wav"),
                        juce::dontSendNotification);
    else if (replaying)
        status.setText (TR ("Replaying dry stem through the current chain", "干声正在走当前效果器链回放"),
                        juce::dontSendNotification);
    else
        status.setText (juce::String ((int) takes.size()) + TR (" takes  ·  ", " 次录音  ·  ")
                            + Recorder::getFolder().getFullPathName(),
                        juce::dontSendNotification);
}
