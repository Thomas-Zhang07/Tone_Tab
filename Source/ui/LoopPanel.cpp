#include "LoopPanel.h"
#include "looper/LoopStore.h"
#include "Text.h"
#include <cmath>

namespace
{
    const juce::Colour bg        { 0xff151515 };
    const juce::Colour panel     { 0xff1b1b1b };
    const juce::Colour panelLine { 0xff2a2a2a };
    const juce::Colour muted     { 0xff9a9a9a };
    const juce::Colour accent    { 0xff5dcaa5 };
    const juce::Colour green     { 0xff1d9e75 };
    const juce::Colour red       { 0xffc0392b };
    const juce::Colour blue      { 0xff2f6fdb };
    const juce::Colour amber     { 0xffc9851a };

    void styleLabel (juce::Label& l, float size, juce::Colour c)
    {
        l.setFont (juce::FontOptions (size));
        l.setColour (juce::Label::textColourId, c);
    }

    void noKeyboardFocus (juce::Component& c)
    {
        c.setWantsKeyboardFocus (false);
        c.setMouseClickGrabsKeyboardFocus (false);
    }

    juce::String barsText (double bars)
    {
        if (bars <= 0.0)
            return TR ("0 bars", "0 小节");

        if (std::abs (bars - std::round (bars)) < 0.01)
            return juce::String ((int) std::lround (bars)) + TR (" bars", " 小节");
        return juce::String (bars, 2) + TR (" bars", " 小节");
    }

    juce::String timeText (double seconds)
    {
        return juce::String (seconds, 2) + " s";
    }
}

// =============================================================================
// Saved-loop browser. Slides in over the right-hand controls: every loop.wav
// folder under <recordings>/loops is listed with its title, tempo, length and
// notes. Double-click (or Load) puts the take back in the loop station, fitted
// to the tempo that is running now, and starts playing.
// =============================================================================
class LoopPanel::SavedLoopsPanel final : public juce::Component,
                                         private juce::ListBoxModel
{
public:
    SavedLoopsPanel()
    {
        setOpaque (true);
        setWantsKeyboardFocus (true);

        titleLabel.setText (TR ("Saved loops", "已保存循环"), juce::dontSendNotification);
        titleLabel.setFont (juce::FontOptions (19.0f, juce::Font::bold));
        titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible (titleLabel);

        hintLabel.setText (TR ("Double-click a loop to load and play it. It is fitted to the current tempo.",
                               "双击一个循环即可载入并播放，它会匹配当前速度。"),
                           juce::dontSendNotification);
        hintLabel.setFont (juce::FontOptions (11.5f));
        hintLabel.setColour (juce::Label::textColourId, muted);
        hintLabel.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (hintLabel);

        list.setModel (this);
        list.setRowHeight (44);
        list.setMultipleSelectionEnabled (false);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff101010));
        list.setColour (juce::ListBox::outlineColourId, panelLine);
        list.setOutlineThickness (1);
        addAndMakeVisible (list);

        refreshButton.setButtonText (TR ("Refresh", "刷新"));
        refreshButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff303030));
        noKeyboardFocus (refreshButton);
        refreshButton.onClick = [this] { refresh(); };
        addAndMakeVisible (refreshButton);

        loadButton.setButtonText (TR ("Load", "载入"));
        loadButton.setColour (juce::TextButton::buttonColourId, green);
        loadButton.setEnabled (false);
        noKeyboardFocus (loadButton);
        loadButton.onClick = [this] { loadRow (list.getSelectedRow()); };
        addAndMakeVisible (loadButton);

        closeButton.setButtonText (TR ("Close", "关闭"));
        closeButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff303030));
        noKeyboardFocus (closeButton);
        closeButton.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (closeButton);

        status.setFont (juce::FontOptions (11.0f));
        status.setColour (juce::Label::textColourId, muted);
        status.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (status);

        refresh();
    }

    std::function<void (const juce::File&)> onLoad;
    std::function<void()> onClose;

    void refresh()
    {
        loops = LoopStore::scanLoops();
        list.deselectAllRows();
        list.updateContent();
        loadButton.setEnabled (false);

        const int count = (int) loops.size();
        status.setText (juce::String (count)
                            + (count == 1 ? TR (" saved loop", " 个已保存循环")
                                          : TR (" saved loops", " 个已保存循环")),
                        juce::dontSendNotification);
        repaint();
    }

    void focusList() { list.grabKeyboardFocus(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (bg);

        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (panel);
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (panelLine);
        g.drawRoundedRectangle (r, 12.0f, 1.0f);
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (! loops.empty())
            return;

        auto r = list.getBounds().reduced (12);
        g.setColour (muted);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (TR ("No saved loops yet.\nRecord a take, then press Save loop — "
                              "its notes and tempo will appear here.",
                              "还没有保存的循环。\n录一段后按“保存循环”，备注和速度会显示在这里。"),
                          r, juce::Justification::centred, 3);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (14);

        titleLabel.setBounds (area.removeFromTop (28));
        hintLabel.setBounds (area.removeFromTop (38));
        area.removeFromTop (8);

        auto bottom = area.removeFromBottom (30);
        closeButton.setBounds (bottom.removeFromRight (82).reduced (0, 1));
        bottom.removeFromRight (8);
        loadButton.setBounds (bottom.removeFromRight (82).reduced (0, 1));
        bottom.removeFromRight (8);
        refreshButton.setBounds (bottom.removeFromRight (90).reduced (0, 1));

        area.removeFromBottom (8);
        status.setBounds (area.removeFromBottom (18));
        area.removeFromBottom (6);
        list.setBounds (area);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            if (onClose)
                onClose();
            return true;
        }
        return false;
    }

    int getNumRows() override { return (int) loops.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, (int) loops.size()))
            return;

        const auto& info = loops[(size_t) row];

        if (selected)
            g.fillAll (juce::Colour (0xff2d5c4a));
        else
            g.fillAll ((row & 1) != 0 ? juce::Colour (0xff161616) : juce::Colour (0xff111111));

        auto r = juce::Rectangle<int> (0, 0, width, height).reduced (9, 4);
        auto top = r.removeFromTop (18);

        auto meta = juce::String();
        if (info.hasTempo())
            meta << juce::String (juce::roundToInt (info.bpm)) << " BPM";
        else
            meta << TR ("tempo ?", "速度未知");

        if (info.bars > 0.01)
            meta << "  ·  " << juce::String (info.bars, 2) << TR (" bars", " 小节");
        if (info.length > 0.0)
            meta << "  ·  " << juce::String (info.length, 1) << " s";

        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (info.title.isNotEmpty() ? info.title : info.folder.getFileName(),
                    top.removeFromLeft (juce::jmax (80, top.getWidth() - 170)),
                    juce::Justification::centredLeft, true);

        g.setColour (muted);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (meta, top, juce::Justification::centredRight, true);

        auto notes = info.notes.replaceCharacter ('\n', ' ').replaceCharacter ('\r', ' ').trim();
        g.setColour (info.notes.isEmpty() ? juce::Colour (0xff5c5c5c) : muted.withAlpha (0.9f));
        g.setFont (juce::FontOptions (11.5f));
        g.drawFittedText (notes.isEmpty() ? TR ("No notes", "没有备注") : notes,
                          r, juce::Justification::centredLeft, 1);
    }

    void selectedRowsChanged (int) override
    {
        loadButton.setEnabled (list.getSelectedRow() >= 0);
    }

    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
    {
        loadRow (row);
    }

    void returnKeyPressed (int row) override
    {
        loadRow (row);
    }

private:
    void loadRow (int row)
    {
        if (! juce::isPositiveAndBelow (row, (int) loops.size()))
            return;
        if (onLoad)
            onLoad (loops[(size_t) row].folder);
    }

    std::vector<LoopStore::LoopInfo> loops;

    juce::Label titleLabel, hintLabel, status;
    juce::ListBox list;
    juce::TextButton refreshButton, loadButton, closeButton;
};

LoopPanel::LoopPanel (Looper& l, DrumMachine& d, juce::PropertiesFile& s)
    : looper (l), drums (d), settings (s)
{
    setWantsKeyboardFocus (true);
    setOpaque (true);

    title.setText (TR ("Loop Station", "循环站"), juce::dontSendNotification);
    title.setFont (juce::FontOptions (21.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (title);

    styleLabel (status, 13.0f, accent);
    status.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (status);

    // ---- transport ----------------------------------------------------------
    mainButton.setWantsKeyboardFocus (false);
    mainButton.setMouseClickGrabsKeyboardFocus (false);
    mainButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    mainButton.onClick = [this] { toggleLoop(); grabKeyboardFocus(); };
    mainButton.setTooltip (TR ("Space bar does the same thing", "空格键功能相同"));
    addAndMakeVisible (mainButton);

    stopButton.setButtonText (TR ("Stop", "停止"));
    stopButton.setTooltip (TR ("Stop playback. Escape also works.", "停止播放（Esc 也可以）"));
    noKeyboardFocus (stopButton);
    stopButton.onClick = [this] { stopLoop(); grabKeyboardFocus(); };
    addAndMakeVisible (stopButton);

    clearButton.setButtonText (TR ("Clear", "清除"));
    clearButton.setTooltip (TR ("Erase the loop. The next press records a new one.",
                                "删除循环，下一次按空格重新录音"));
    noKeyboardFocus (clearButton);
    clearButton.onClick = [this] { looper.clear(); grabKeyboardFocus(); };
    addAndMakeVisible (clearButton);

    undoButton.setButtonText (TR ("Undo", "撤销"));
    undoButton.setTooltip (TR ("Undo the last trim or extension.", "撤销上一次裁剪或延长"));
    noKeyboardFocus (undoButton);
    undoButton.onClick = [this] { undoEdit(); grabKeyboardFocus(); };
    addAndMakeVisible (undoButton);

    // ---- selection / editing -----------------------------------------------
    styleLabel (selectionLabel, 12.5f, juce::Colours::white.withAlpha (0.8f));
    selectionLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (selectionLabel);

    trimButton.setButtonText (TR ("Trim to selection", "裁剪到选区"));
    trimButton.setTooltip (TR ("Keep only the highlighted part of the loop.",
                               "只保留波形上高亮的部分"));
    noKeyboardFocus (trimButton);
    trimButton.onClick = [this] { trimSelection(); grabKeyboardFocus(); };
    addAndMakeVisible (trimButton);

    // ---- grid / length / count-in / level ----------------------------------
    styleLabel (quantiseLabel, 12.5f, muted);
    quantiseLabel.setText (TR ("Snap to", "对齐到"), juce::dontSendNotification);
    addAndMakeVisible (quantiseLabel);

    quantiseBox.addItem (TR ("Bar (recommended)", "小节（推荐）"), 1);
    quantiseBox.addItem (TR ("Beat", "拍"), 2);
    quantiseBox.addItem (TR ("Off — free timing", "关 — 自由时间"), 3);
    quantiseBox.setTooltip (TR ("Start, close, trim and count-in land on the nearest bar. "
                                "You do not have to hit SPACE exactly on time.",
                                "开始、结束、裁剪和预备拍都会落在最近的小节线上，空格按早晚一点都没关系"));
    noKeyboardFocus (quantiseBox);
    quantiseBox.onChange = [this] { applyQuantise(); saveSettings(); grabKeyboardFocus(); };
    addAndMakeVisible (quantiseBox);

    styleLabel (tempoLabel, 12.5f, muted);
    tempoLabel.setText (TR ("Tempo (shared with the drum machine)", "速度（与鼓机共用）"),
                        juce::dontSendNotification);
    addAndMakeVisible (tempoLabel);

    tempoSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    tempoSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 22);
    tempoSlider.setRange (30.0, 300.0, 1.0);
    tempoSlider.setTextValueSuffix (" BPM");
    tempoSlider.setValue (drums.getBpm(), juce::dontSendNotification);
    tempoSlider.setTooltip (TR ("The loop snaps to this tempo, with or without drums",
                                "循环会按这个速度对齐，开不开鼓机都一样"));
    noKeyboardFocus (tempoSlider);
    tempoSlider.onValueChange = [this]
    {
        drums.setBpm ((float) tempoSlider.getValue());
        updateLengthHint();
    };
    tempoSlider.onDragEnd = [this] { saveSettings(); grabKeyboardFocus(); };
    addAndMakeVisible (tempoSlider);

    styleLabel (lengthLabel, 12.5f, muted);
    lengthLabel.setText (TR ("New loop length", "新循环长度"), juce::dontSendNotification);
    addAndMakeVisible (lengthLabel);

    lengthBox.clear (juce::dontSendNotification);
    lengthBox.addItem (TR ("Auto — press again to close", "自动 — 再按一次结束"), 1);
    for (int bar = 1; bar <= 64; ++bar)
        lengthBox.addItem (juce::String (bar) + TR (" bars", " 小节"), bar + 1);
    lengthBox.setTooltip (TR ("Every whole number of bars is available. A fixed length "
                              "records with one press and starts looping by itself.",
                              "所有整数小节都可以。固定长度时只需按一次空格，录完自动开始循环"));
    noKeyboardFocus (lengthBox);
    lengthBox.onChange = [this] { applyFixedLength(); saveSettings(); grabKeyboardFocus(); };
    addAndMakeVisible (lengthBox);

    styleLabel (lengthHint, 11.0f, muted);
    lengthHint.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (lengthHint);

    styleLabel (countInLabel, 12.5f, muted);
    countInLabel.setText (TR ("Count-in before recording", "录音前的预备拍"),
                          juce::dontSendNotification);
    addAndMakeVisible (countInLabel);

    countInSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    countInSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 74, 22);
    countInSlider.setRange (0.0, 32.0, 1.0);
    countInSlider.textFromValueFunction = [] (double v)
    {
        const int n = (int) std::lround (v);
        return juce::String (n) + (n == 1 ? TR (" beat", " 拍") : TR (" beats", " 拍"));
    };
    countInSlider.setTooltip (TR ("How many beats are counted before recording starts. "
                                  "The number is shown in the middle of the timeline.",
                                  "录音开始前数多少拍，数字会显示在时间线中间"));
    noKeyboardFocus (countInSlider);
    countInSlider.onValueChange = [this] { applyCountIn(); };
    countInSlider.onDragEnd = [this] { saveSettings(); grabKeyboardFocus(); };
    addAndMakeVisible (countInSlider);

    styleLabel (levelLabel, 12.5f, muted);
    levelLabel.setText (TR ("Loop level", "循环音量"), juce::dontSendNotification);
    addAndMakeVisible (levelLabel);

    levelSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    levelSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 22);
    levelSlider.setRange (-40.0, 6.0, 0.5);
    levelSlider.setTextValueSuffix (" dB");
    levelSlider.setDoubleClickReturnValue (true, -3.0);
    noKeyboardFocus (levelSlider);
    levelSlider.onValueChange = [this] { looper.setLevelDb ((float) levelSlider.getValue()); };
    levelSlider.onDragEnd = [this] { saveSettings(); grabKeyboardFocus(); };
    addAndMakeVisible (levelSlider);

    // ---- continue / extend ---------------------------------------------------
    styleLabel (continueLabel, 11.0f, juce::Colours::white.withAlpha (0.55f));
    continueLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    continueLabel.setText (TR ("CONTINUE LOOP  ·  BARS TO ADD", "延长循环  ·  增加小节数"),
                           juce::dontSendNotification);
    addAndMakeVisible (continueLabel);

    extendBarsSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    extendBarsSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 22);
    extendBarsSlider.setRange (1.0, 64.0, 1.0);
    extendBarsSlider.setTooltip (TR ("Any whole number of bars. The loop keeps playing, "
                                     "and the new bars are recorded when the playhead reaches the end.",
                                     "可以填任意整数小节。循环会继续播放，播放头到结尾时开始录制新增的小节"));
    noKeyboardFocus (extendBarsSlider);
    extendBarsSlider.onValueChange = [this] { applyExtendBars(); };
    extendBarsSlider.onDragEnd = [this] { saveSettings(); grabKeyboardFocus(); };
    addAndMakeVisible (extendBarsSlider);

    extendButton.setButtonText (TR ("Extend & record", "延长并录音"));
    extendButton.setTooltip (TR ("Adds the bars above and records them live at the end of the loop.",
                                 "增加上面的小节数，并在循环结尾实时录制"));
    noKeyboardFocus (extendButton);
    extendButton.onClick = [this] { extendLoop(); grabKeyboardFocus(); };
    addAndMakeVisible (extendButton);

    // ---- save & notes --------------------------------------------------------
    styleLabel (notesLabel, 11.0f, juce::Colours::white.withAlpha (0.55f));
    notesLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    notesLabel.setText (TR ("SAVE & NOTES", "保存与备注"), juce::dontSendNotification);
    addAndMakeVisible (notesLabel);

    notesEditor.setMultiLine (true, true);
    notesEditor.setReturnKeyStartsNewLine (true);
    notesEditor.setScrollbarsShown (true);
    notesEditor.setFont (juce::FontOptions (13.0f));
    notesEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff101010));
    notesEditor.setColour (juce::TextEditor::textColourId, juce::Colours::white.withAlpha (0.85f));
    notesEditor.setColour (juce::TextEditor::outlineColourId, panelLine);
    notesEditor.setColour (juce::TextEditor::focusedOutlineColourId, accent);
    notesEditor.setTextToShowWhenEmpty (TR ("Notes for this loop…", "给这个循环写点备注…"), muted);
    notesEditor.onTextChange = [this]
    {
        if (updatingNotes)
            return;
        notesDirty = true;
        notesDirtyAt = juce::Time::getMillisecondCounterHiRes() * 0.001;
    };
    addAndMakeVisible (notesEditor);

    saveButton.setButtonText (TR ("Save loop", "保存循环"));
    saveButton.setTooltip (TR ("Writes loop.wav and loop.json (with these notes) to the loops folder.",
                               "把 loop.wav 和 loop.json（连同备注）写到 loops 文件夹"));
    noKeyboardFocus (saveButton);
    saveButton.onClick = [this] { saveLoop(); grabKeyboardFocus(); };
    addAndMakeVisible (saveButton);

    openFolderButton.setButtonText (TR ("Loops folder", "loops 文件夹"));
    openFolderButton.setTooltip (TR ("Open the folder containing saved loops.",
                                     "打开保存循环的文件夹"));
    noKeyboardFocus (openFolderButton);
    openFolderButton.onClick = [this] { openLoopsFolder(); grabKeyboardFocus(); };
    addAndMakeVisible (openFolderButton);

    savedLoopsButton.setButtonText (TR ("Saved loops", "已保存循环"));
    savedLoopsButton.setTooltip (TR ("Browse saved loops, read their notes, and load one at the current tempo.",
                                     "浏览已保存的循环，查看备注，并按当前速度载入"));
    noKeyboardFocus (savedLoopsButton);
    savedLoopsButton.onClick = [this] { openSavedLoops(); };
    addAndMakeVisible (savedLoopsButton);

    savedLoopsPanel = std::make_unique<SavedLoopsPanel>();
    savedLoopsPanel->onLoad = [this] (const juce::File& folder) { loadSavedLoop (folder); };
    savedLoopsPanel->onClose = [this]
    {
        if (savedLoopsPanel != nullptr)
            savedLoopsPanel->setVisible (false);
        grabKeyboardFocus();
    };
    addChildComponent (savedLoopsPanel.get());

    styleLabel (savedLabel, 11.5f, muted);
    savedLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (savedLabel);

    // ---- restore settings ----------------------------------------------------
    quantiseBox.setSelectedId (juce::jlimit (1, 3, settings.getIntValue ("loopQuantise", 1)),
                               juce::dontSendNotification);
    applyQuantise();

    const int fixedBars = juce::jlimit (0, 64, settings.getIntValue ("loopFixedBars", 0));
    lengthBox.setSelectedId (juce::jlimit (1, 65, fixedBars + 1), juce::dontSendNotification);
    applyFixedLength();

    countInSlider.setValue (juce::jlimit (0, 32, settings.getIntValue ("loopCountIn", 0)),
                            juce::dontSendNotification);
    applyCountIn();

    extendBarsSlider.setValue (juce::jlimit (1, 64, settings.getIntValue ("loopExtendBars", 1)),
                               juce::dontSendNotification);
    applyExtendBars();

    levelSlider.setValue (settings.getDoubleValue ("loopLevelDb", -3.0), juce::dontSendNotification);
    looper.setLevelDb ((float) levelSlider.getValue());

    updatingNotes = true;
    notesEditor.setText (settings.getValue ("loopNotes"), false);
    updatingNotes = false;

    savedLabel.setText (TR ("Not saved yet", "还没保存"), juce::dontSendNotification);

    updateControls();
    refresh();
    updateLengthHint();
    startTimerHz (30);
}

LoopPanel::~LoopPanel()
{
    stopTimer();
    saveSettings();
}

void LoopPanel::saveSettings()
{
    settings.setValue ("loopQuantise", quantiseBox.getSelectedId());
    settings.setValue ("loopFixedBars", looper.getFixedLengthBars());
    settings.setValue ("loopCountIn", looper.getCountInBeats());
    settings.setValue ("loopLevelDb", levelSlider.getValue());
    settings.setValue ("loopExtendBars", (int) std::lround (extendBarsSlider.getValue()));
    settings.setValue ("loopNotes", notesEditor.getText());
    settings.setValue ("drumBpm", drums.getBpm());
}

void LoopPanel::applyQuantise()
{
    switch (quantiseBox.getSelectedId())
    {
        case 2:  looper.setQuantise (LoopQuantise::beat); break;
        case 3:  looper.setQuantise (LoopQuantise::off);  break;
        default: looper.setQuantise (LoopQuantise::bar);  break;
    }
}

void LoopPanel::applyFixedLength()
{
    looper.setFixedLengthBars (juce::jlimit (0, 64, lengthBox.getSelectedId() - 1));
    updateLengthHint();
}

void LoopPanel::applyCountIn()
{
    looper.setCountInBeats ((int) std::lround (countInSlider.getValue()));
}

void LoopPanel::applyExtendBars()
{
    // The slider range already enforces 1..64; nothing else to store.
    updateControls();
}

void LoopPanel::updateLengthHint()
{
    const double bpm = juce::jmax (30.0, (double) drums.getBpm());
    const int bpb = juce::jmax (1, drums.getGridBeatsPerBar());
    const double barSecs = 60.0 / bpm * (double) bpb;
    const int maxBars = juce::jlimit (1, 64,
                                      (int) std::floor (Looper::getMaxSeconds() / barSecs + 1.0e-6));
    const int chosen = looper.getFixedLengthBars();

    juce::String text;
    juce::Colour colour = muted;

    if (chosen > maxBars)
    {
        text = TR ("At this tempo the 2:00 buffer holds about ", "这个速度下 2:00 的缓冲区大约能放 ")
               + juce::String (maxBars) + TR (" bars; longer takes stop at the end.",
                                               " 小节，更长会截断。");
        colour = amber;
    }
    else
    {
        text = TR ("1–64 bars. At this tempo up to ", "1–64 小节。这个速度下最多 ")
               + juce::String (maxBars) + TR (" bars fit in the 2:00 buffer.",
                                               " 小节（2:00 缓冲区）。");
    }

    if (lengthHint.getText() != text)
    {
        lengthHint.setText (text, juce::dontSendNotification);
        lengthHint.setColour (juce::Label::textColourId, colour);
    }
}

void LoopPanel::toggleLoop()   { looper.toggle(); }
void LoopPanel::stopLoop()     { looper.stop(); }

bool LoopPanel::keyPressed (const juce::KeyPress& key)
{
    if (savedLoopsPanel != nullptr && savedLoopsPanel->isVisible())
    {
        if (key == juce::KeyPress::escapeKey)
        {
            savedLoopsPanel->setVisible (false);
            grabKeyboardFocus();
            return true;
        }
        return false;   // let the list handle its keys
    }

    if (key == juce::KeyPress::spaceKey)
    {
        // Ignore Windows key auto-repeat: holding SPACE must not toggle
        // record/overdub over and over.
        if (spaceHeld && juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::spaceKey))
            return true;

        spaceHeld = true;
        toggleLoop();
        return true;
    }

    if (key == juce::KeyPress::escapeKey)
    {
        stopLoop();
        return true;
    }

    const auto ch = key.getTextCharacter();
    if (ch == 'u' || ch == 'U')
    {
        undoEdit();
        return true;
    }

    if (ch == 't' || ch == 'T')
    {
        trimSelection();
        return true;
    }

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        clearSelection();
        return true;
    }

    return false;
}

bool LoopPanel::keyStateChanged (bool)
{
    if (! juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::spaceKey))
        spaceHeld = false;
    return false;
}

void LoopPanel::focusLost (FocusChangeType)
{
    spaceHeld = false;
}

// ---- layout -----------------------------------------------------------------

void LoopPanel::resized()
{
    auto area = getLocalBounds().reduced (16);

    headerArea = area.removeFromTop (34);
    title.setBounds (headerArea.removeFromLeft (240));
    status.setBounds (headerArea);

    area.removeFromTop (6);
    hintArea = area.removeFromBottom (38);
    area.removeFromBottom (6);

    controlsArea = area.removeFromRight (372);
    area.removeFromRight (14);

    transportArea = area.removeFromTop (58);
    area.removeFromTop (8);
    editArea = area.removeFromBottom (52);
    area.removeFromBottom (6);
    timelineArea = area;

    // Transport row.
    {
        auto t = transportArea.reduced (0, 7);
        mainButton.setBounds (t.removeFromLeft (280));
        t.removeFromLeft (10);
        stopButton.setBounds (t.removeFromLeft (78));
        t.removeFromLeft (8);
        clearButton.setBounds (t.removeFromLeft (78));
        t.removeFromLeft (8);
        undoButton.setBounds (t.removeFromLeft (86));
    }

    // Selection / trim row.
    {
        auto e = editArea;
        selectionLabel.setBounds (e.removeFromLeft (juce::jmax (180, e.getWidth() - 156)));
        e.removeFromLeft (10);
        trimButton.setBounds (e.removeFromLeft (juce::jmin (136, juce::jmax (90, e.getWidth()))));
    }

    // Right-hand controls.
    {
        auto c = controlsArea.reduced (14);
        auto addControl = [&c] (juce::Label& label, juce::Component& control, int height)
        {
            label.setBounds (c.removeFromTop (17));
            control.setBounds (c.removeFromTop (height));
            c.removeFromTop (9);
        };

        addControl (quantiseLabel, quantiseBox, 27);
        addControl (tempoLabel, tempoSlider, 26);
        addControl (lengthLabel, lengthBox, 27);
        lengthHint.setBounds (c.removeFromTop (30));
        c.removeFromTop (4);
        addControl (countInLabel, countInSlider, 26);
        addControl (levelLabel, levelSlider, 26);

        c.removeFromTop (2);
        continueLabel.setBounds (c.removeFromTop (18));
        auto extendRow = c.removeFromTop (30);
        extendBarsSlider.setBounds (extendRow.removeFromLeft (156));
        extendRow.removeFromLeft (8);
        extendButton.setBounds (extendRow);
        c.removeFromTop (12);

        notesLabel.setBounds (c.removeFromTop (18));
        notesArea = c.removeFromTop (juce::jmax (80, juce::jmin (128, c.getHeight() - 96)));
        notesEditor.setBounds (notesArea);
        c.removeFromTop (8);

        auto saveRow = c.removeFromTop (30);
        saveButton.setBounds (saveRow.removeFromLeft (104));
        saveRow.removeFromLeft (6);
        savedLoopsButton.setBounds (saveRow.removeFromLeft (104));
        saveRow.removeFromLeft (6);
        openFolderButton.setBounds (saveRow);
        c.removeFromTop (6);
        savedLabel.setBounds (c.removeFromTop (20));
    }

    if (savedLoopsPanel != nullptr)
        savedLoopsPanel->setBounds (controlsArea.expanded (8).getIntersection (getLocalBounds()));
}

// ---- paint ------------------------------------------------------------------

juce::Colour LoopPanel::stateColour (Looper::State s)
{
    switch (s)
    {
        case Looper::State::recording:      return red;
        case Looper::State::armingRecord:
        case Looper::State::armingPlay:     return amber;
        case Looper::State::playing:        return accent;
        case Looper::State::armedOverdub:
        case Looper::State::overdubbing:    return blue;
        case Looper::State::extending:      return blue;
        case Looper::State::stopped:        return muted;
        case Looper::State::empty:
        default:                            return juce::Colour (0xff6f6f6f);
    }
}

void LoopPanel::paint (juce::Graphics& g)
{
    g.fillAll (bg);

    g.setColour (panel);
    g.fillRoundedRectangle (timelineArea.toFloat().expanded (8.0f), 12.0f);
    g.fillRoundedRectangle (controlsArea.toFloat().expanded (8.0f), 12.0f);

    g.setColour (panelLine);
    g.drawLine ((float) hintArea.getX(), (float) hintArea.getY() - 2.0f,
                (float) hintArea.getRight(), (float) hintArea.getY() - 2.0f, 1.0f);

    g.setColour (muted);
    g.setFont (juce::FontOptions (12.0f));
    g.drawFittedText (TR ("SPACE records / closes / overdubs.  Drag the waveform to select, then Trim.  "
                          "Count-in beats are shown in the middle before recording.  Extend records extra "
                          "bars at the end.  U undo · T trim · Delete clears the selection.",
                          "空格录音 / 结束 / 叠加。在波形上拖动选区，然后裁剪。录音前中间会显示预备拍数。"
                          "延长会在结尾录制新增小节。U 撤销 · T 裁剪 · Delete 清除选区。"),
                      hintArea, juce::Justification::topLeft, 2);

    paintTimeline (g, timelineArea.reduced (10));
}

void LoopPanel::paintTimeline (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (area.getWidth() < 240 || area.getHeight() < 120)
        return;

    const auto st = looper.getState();
    const bool hasLoop = looper.hasLoop();
    const auto colour = stateColour (st);
    const int bpb = hasLoop ? juce::jmax (1, looper.getLoopBeatsPerBar())
                            : juce::jmax (1, drums.getGridBeatsPerBar());
    const double bpm = hasLoop && looper.getLoopBpm() > 1.0
                           ? looper.getLoopBpm() : juce::jmax (30.0, (double) drums.getBpm());
    const double barSecs = 60.0 / bpm * (double) bpb;
    const double elapsed = looper.getRecordedSeconds();
    const double bucketSecs = juce::jmax (1.0e-4, looper.getWaveformBucketSeconds());
    const int labelW = 46;

    rulerArea = area.removeFromTop (22);
    liveLane = area.removeFromBottom (72);
    area.removeFromBottom (8);
    loopLane = area;

    const int fixed = looper.getFixedLengthBars();

    // How much time the loop lane shows.
    double secs = 0.0;
    int displayBars = 4;
    if (st == Looper::State::recording)
    {
        if (fixed > 0)
        {
            displayBars = fixed;
            secs = displayBars * barSecs;
        }
        else
        {
            displayBars = juce::jmax (4, (int) std::ceil (elapsed / juce::jmax (0.1, barSecs)));
            secs = displayBars * barSecs;
        }
    }
    else if (hasLoop)
    {
        secs = juce::jmax (0.05, looper.getLengthSeconds());
        displayBars = juce::jmax (1, (int) std::ceil (looper.getLoopBars() - 1.0e-6));
    }
    else
    {
        displayBars = 4;
        secs = displayBars * barSecs;
    }

    displaySecs = juce::jmax (0.05, secs);

    auto loopFull = loopLane;
    auto liveFull = liveLane;
    auto loopLabel = loopFull.removeFromLeft (labelW);
    auto liveLabel = liveFull.removeFromLeft (labelW);

    laneX = loopFull.getX();
    laneW = juce::jmax (1, loopFull.getWidth());

    g.setColour (juce::Colour (0xff0f0f0f));
    g.fillRoundedRectangle (loopFull.toFloat(), 6.0f);
    g.fillRoundedRectangle (liveFull.toFloat(), 6.0f);

    g.setColour (muted);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText (TR ("LOOP", "循环"), loopLabel, juce::Justification::centredLeft);
    g.drawText (TR ("LIVE", "实时"), liveLabel, juce::Justification::centredLeft);

    // ---- ruler: bars and beats ---------------------------------------------
    const int barStep = displayBars > 12 ? juce::jmax (1, displayBars / 12) : 1;
    const double beatSecs = barSecs / (double) bpb;
    const float pxPerBeat = (float) (beatSecs / displaySecs * (double) laneW);

    g.setFont (juce::FontOptions (10.0f));
    for (int bar = 0; bar <= displayBars; ++bar)
    {
        const double t = (double) bar * barSecs;
        if (t > displaySecs + 1.0e-6)
            break;

        const float x = secondsToX (t);
        g.setColour (juce::Colour (0xff383838));
        g.drawLine (x, (float) rulerArea.getBottom() - 7.0f, x,
                    (float) loopFull.getBottom(), 1.0f);

        if (bar < displayBars && bar % barStep == 0)
        {
            g.setColour (muted);
            g.drawText (juce::String (bar + 1), (int) x + 3, rulerArea.getY(), 24,
                        rulerArea.getHeight(), juce::Justification::centredLeft);
        }

        if (pxPerBeat >= 12.0f)
        {
            for (int beat = 1; beat < bpb; ++beat)
            {
                const float bx = secondsToX (t + (double) beat * beatSecs);
                if (bx > (float) loopFull.getRight())
                    break;
                g.setColour (juce::Colour (0xff262626));
                g.drawLine (bx, (float) loopFull.getY() + 4.0f, bx,
                            (float) loopFull.getBottom() - 4.0f, 1.0f);
            }
        }
    }

    // ---- loop waveform ------------------------------------------------------
    const int count = looper.getWaveformCount();
    const double recordedSecs = st == Looper::State::recording ? elapsed
                               : hasLoop ? looper.getLengthSeconds() : 0.0;
    float waveMax = 0.05f;
    for (int b = 0; b < count; ++b)
        waveMax = juce::jmax (waveMax, looper.getWaveformPeak (b));

    const int midY = loopFull.getCentreY();
    const int halfH = juce::jmax (2, loopFull.getHeight() / 2 - 5);

    if (count > 0 && recordedSecs > 0.0)
    {
        g.setColour (colour.withAlpha (0.9f));
        for (int x = 0; x < laneW; ++x)
        {
            const double t0 = (double) x / (double) laneW * displaySecs;
            if (t0 >= recordedSecs)
                break;

            const double t1 = (double) (x + 1) / (double) laneW * displaySecs;
            const int b0 = juce::jlimit (0, count - 1, (int) std::floor (t0 / bucketSecs));
            const int b1 = juce::jlimit (0, count - 1, (int) std::ceil (t1 / bucketSecs));

            float peak = 0.0f;
            for (int b = b0; b <= b1; ++b)
                peak = juce::jmax (peak, looper.getWaveformPeak (b));

            const float amp = juce::jmax (1.0f, peak / waveMax * (float) halfH);
            g.drawLine ((float) (laneX + x), (float) midY - amp,
                        (float) (laneX + x), (float) midY + amp, 1.0f);
        }
    }
    else
    {
        g.setColour (juce::Colour (0xff333333));
        g.drawLine ((float) laneX + 4.0f, (float) midY,
                    (float) loopFull.getRight() - 4.0f, (float) midY, 1.0f);

        g.setColour (muted);
        g.setFont (juce::FontOptions (12.0f));
        if (st == Looper::State::empty)
            g.drawText (TR ("No loop yet — press SPACE to record", "还没有循环 — 按空格录音"),
                        loopFull, juce::Justification::centred);
        else if (st == Looper::State::armingRecord)
            g.drawText (TR ("Get ready — the count-in is running", "准备好 — 预备拍进行中"),
                        loopFull, juce::Justification::centred);
    }

    // ---- extension overlay --------------------------------------------------
    if (st == Looper::State::extending)
    {
        const double es = looper.getExtendStartSeconds();
        const double ee = looper.getExtendEndSeconds();
        if (ee > es)
        {
            const float x0 = secondsToX (es);
            const float x1 = secondsToX (ee);
            juce::Rectangle<float> r (x0, (float) loopFull.getY(), x1 - x0,
                                      (float) loopFull.getHeight());
            g.setColour (blue.withAlpha (0.18f));
            g.fillRect (r);
            g.setColour (blue);
            g.drawRect (r, 1.5f);

            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (TR ("RECORDING ", "正在录制 ") + barsText ((ee - es) / barSecs),
                        r.toNearestInt(), juce::Justification::centred);
        }
    }

    // ---- selection ----------------------------------------------------------
    if (hasSelection() && hasLoop)
    {
        const float x0 = secondsToX (selStart);
        const float x1 = secondsToX (selEnd);

        g.setColour (accent.withAlpha (0.16f));
        g.fillRect (juce::Rectangle<float> (x0, (float) loopFull.getY(), x1 - x0,
                                            (float) loopFull.getHeight()));

        g.setColour (accent);
        g.drawLine (x0, (float) loopFull.getY() + 2.0f, x0,
                    (float) loopFull.getBottom() - 2.0f, 2.0f);
        g.drawLine (x1, (float) loopFull.getY() + 2.0f, x1,
                    (float) loopFull.getBottom() - 2.0f, 2.0f);

        g.fillRoundedRectangle (juce::Rectangle<float> (x0 - 3.0f, (float) midY - 10.0f, 6.0f, 20.0f), 3.0f);
        g.fillRoundedRectangle (juce::Rectangle<float> (x1 - 3.0f, (float) midY - 10.0f, 6.0f, 20.0f), 3.0f);

        g.setFont (juce::FontOptions (10.0f));
        g.setColour (accent.brighter (0.2f));
        g.drawText (timeText (selStart), (int) x0 + 3, loopFull.getBottom() - 16, 70, 14,
                    juce::Justification::centredLeft);
        g.drawText (timeText (selEnd), (int) x1 - 73, loopFull.getBottom() - 16, 70, 14,
                    juce::Justification::centredRight);
    }

    // ---- playhead -----------------------------------------------------------
    float head = -1.0f;
    if (st == Looper::State::recording)
        head = secondsToX (elapsed);
    else if (hasLoop)
        head = secondsToX (juce::jlimit (0.0, 1.0, looper.getPosition())
                           * looper.getLengthSeconds());

    if (head >= 0.0f)
    {
        g.setColour (st == Looper::State::stopped ? colour.withAlpha (0.35f) : colour);
        g.drawLine (head, (float) loopFull.getY() + 3.0f,
                    head, (float) loopFull.getBottom() - 3.0f, 1.6f);
    }

    // ---- live input lane ----------------------------------------------------
    const double windowSecs = juce::jlimit (2.0, 8.0, barSecs * 4.0);
    const int windowBuckets = juce::jmax (8, (int) std::lround (windowSecs / bucketSecs));
    const uint32_t end = looper.getLiveWritePosition();
    const uint32_t available = end;
    const uint32_t span = (uint32_t) juce::jmin (
        (uint32_t) windowBuckets,
        (uint32_t) juce::jmin<uint32_t> (available, (uint32_t) Looper::liveBuckets));

    const int liveX = liveFull.getX();
    const int liveW = liveFull.getWidth();
    const int liveMidY = liveFull.getCentreY();
    const int liveHalfH = juce::jmax (2, liveFull.getHeight() / 2 - 4);

    if (span > 0 && liveW > 0)
    {
        float liveMax = 0.02f;
        for (uint32_t k = 0; k < span; ++k)
            liveMax = juce::jmax (liveMax, looper.getLivePeakAt (end - span + k));

        const auto liveColour = st == Looper::State::recording ? red
                                : (st == Looper::State::overdubbing
                                   || st == Looper::State::armedOverdub) ? blue : accent;
        g.setColour (liveColour.withAlpha (0.85f));

        for (int x = 0; x < liveW; ++x)
        {
            const uint32_t k0 = (uint32_t) ((int64_t) x * (int64_t) span / (int64_t) liveW);
            const uint32_t k1 = (uint32_t) ((int64_t) (x + 1) * (int64_t) span / (int64_t) liveW);
            const uint32_t a0 = end - span + k0;
            const uint32_t a1 = juce::jmin (end - 1, end - span + k1);

            float peak = 0.0f;
            for (uint32_t idx = a0; idx <= a1 && idx < available; ++idx)
                peak = juce::jmax (peak, looper.getLivePeakAt (idx));

            const float amp = juce::jmax (1.0f, peak / liveMax * (float) liveHalfH);
            g.drawLine ((float) (liveX + x), (float) liveMidY - amp,
                        (float) (liveX + x), (float) liveMidY + amp, 1.0f);
        }
    }
    else
    {
        g.setColour (juce::Colour (0xff333333));
        g.drawLine ((float) liveX + 4.0f, (float) liveMidY,
                    (float) liveFull.getRight() - 4.0f, (float) liveMidY, 1.0f);
    }

    g.setColour (accent.withAlpha (0.65f));
    g.drawLine ((float) liveFull.getRight() - 1.0f, (float) liveFull.getY() + 3.0f,
                (float) liveFull.getRight() - 1.0f, (float) liveFull.getBottom() - 3.0f, 1.0f);

    // ---- count-in overlay ---------------------------------------------------
    paintCountIn (g, loopFull);
}

void LoopPanel::paintCountIn (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto st = looper.getState();
    if (st != Looper::State::armingRecord && st != Looper::State::armingPlay)
        return;

    const int remaining = juce::jmax (1, looper.getCountInRemaining());
    const int total = juce::jmax (remaining, looper.getCountInTotal());

    const int pillW = juce::jmin (380, juce::jmax (260, area.getWidth() / 2));
    auto pill = juce::Rectangle<int> (pillW, 118).withCentre (area.getCentre());

    g.setColour (juce::Colour (0xf0181818));
    g.fillRoundedRectangle (pill.toFloat(), 16.0f);
    g.setColour (amber.withAlpha (0.85f));
    g.drawRoundedRectangle (pill.toFloat(), 16.0f, 1.5f);

    auto inner = pill.reduced (16, 10);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (54.0f, juce::Font::bold));
    g.drawText (juce::String (remaining), inner.removeFromTop (64),
                juce::Justification::centred, false);

    g.setColour (amber);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (st == Looper::State::armingRecord
                    ? TR ("BEATS UNTIL RECORDING", "拍后开始录音")
                    : TR ("BEATS UNTIL PLAY", "拍后开始播放"),
                inner.removeFromTop (18), juce::Justification::centred, false);

    // One dot per beat (capped), with the beats still to wait filled in.
    const int dots = juce::jlimit (1, 16, total);
    const int filled = juce::jlimit (0, dots, remaining);
    const float gap = 10.0f;
    float x = (float) area.getCentreX() - (float) (dots - 1) * gap * 0.5f;
    const float y = (float) inner.getCentreY() + 1.0f;
    for (int i = 0; i < dots; ++i)
    {
        g.setColour (i < filled ? amber : juce::Colour (0xff3a3a3a));
        g.fillEllipse (x - 3.0f, y - 3.0f, 6.0f, 6.0f);
        x += gap;
    }
}

// ---- timer / controls --------------------------------------------------------

void LoopPanel::timerCallback()
{
    if (! isVisible())
        return;

    refresh();
}

void LoopPanel::refresh()
{
    // The drum page and the loop page share one tempo.
    if (! tempoSlider.isMouseButtonDown()
        && std::abs (tempoSlider.getValue() - drums.getBpm()) > 0.01)
        tempoSlider.setValue (drums.getBpm(), juce::dontSendNotification);

    // A new take starts at loopId; detach the old save target and selection.
    const int64_t id = looper.getLoopId();
    if (id != lastLoopId)
    {
        lastLoopId = id;
        clearSelection();
        savedFolder = {};
        savedTitle.clear();
        loadedFromDisk = false;
        savedRevision = -1;
        lastSavedNotes.clear();
        savedLabel.setText (TR ("Not saved yet", "还没保存"), juce::dontSendNotification);
        savedLabel.setColour (juce::Label::textColourId, muted);
    }

    looper.updateWaveformIfDirty();

    const double len = looper.getLengthSeconds();
    if (hasSelection())
    {
        selStart = juce::jlimit (0.0, juce::jmax (0.0, len), selStart);
        selEnd = juce::jlimit (selStart, juce::jmax (0.0, len), selEnd);
        if (! hasSelection())
            clearSelection();
    }

    const auto st = looper.getState();
    const int remaining = looper.getCountInRemaining();

    juce::String s;
    switch (st)
    {
        case Looper::State::empty:
            s = TR ("Ready", "就绪")
                + (drums.isPlaying() ? TR ("  ·  synced to drums", "  ·  同步鼓机") : juce::String())
                + TR ("  ·  SPACE to record", "  ·  按空格录音");
            break;

        case Looper::State::armingRecord:
            s = TR ("Recording starts in ", "离录音开始还有 ")
                + juce::String (juce::jmax (1, remaining))
                + TR (" beats", " 拍");
            break;

        case Looper::State::recording:
            s = TR ("Recording ", "录音中 ") + juce::String (looper.getRecordedSeconds(), 2) + " s";
            if (looper.getFixedLengthBars() > 0)
                s << "  ·  " << juce::String (looper.getFixedLengthBars()) + TR (" bars", " 小节");
            break;

        case Looper::State::playing:
            s = TR ("Playing", "循环中") + "  ·  " + barsText (looper.getLoopBars())
                + "  ·  " + TR ("layer ", "第 ") + juce::String (looper.getLayerCount())
                + TR ("", " 层");
            break;

        case Looper::State::armedOverdub:
            s = TR ("Overdub armed — starts at the loop start", "叠加已就绪 — 从循环起点开始");
            break;

        case Looper::State::overdubbing:
            s = TR ("Overdubbing — layer ", "叠加录音 — 第 ") + juce::String (looper.getLayerCount())
                + TR ("", " 层");
            break;

        case Looper::State::stopped:
            s = TR ("Stopped", "已停止")
                + (looper.hasLoop() ? "  ·  " + barsText (looper.getLoopBars()) : juce::String());
            break;

        case Looper::State::armingPlay:
            s = TR ("Playback starts in ", "离播放开始还有 ")
                + juce::String (juce::jmax (1, remaining)) + TR (" beats", " 拍");
            break;

        case Looper::State::extending:
        {
            const double addSecs = juce::jmax (0.0, looper.getExtendEndSeconds()
                                                     - looper.getExtendStartSeconds());
            const double bpm = looper.getLoopBpm() > 1.0 ? looper.getLoopBpm() : drums.getBpm();
            const int bpb = juce::jmax (1, looper.getLoopBeatsPerBar());
            const double barSecs = 60.0 / juce::jmax (30.0, bpm) * (double) bpb;
            s = TR ("Extending — recording +", "延长中 — 正在录制 +")
                + barsText (addSecs / juce::jmax (0.01, barSecs));
            break;
        }
    }

    if (s != statusText)
    {
        statusText = s;
        status.setText (s, juce::dontSendNotification);
    }

    // Saved / unsaved indicator.
    if (savedFolder.isDirectory())
    {
        const bool unsaved = looper.getRevision() != savedRevision;
        const auto label = savedTitle.isNotEmpty() ? savedTitle : savedFolder.getFileName();
        const auto text = unsaved ? TR ("Unsaved changes", "有未保存的修改")
                                  : (loadedFromDisk ? TR ("Loaded · ", "已载入 · ")
                                                    : TR ("Saved · ", "已保存 · ")) + label;
        if (savedLabel.getText() != text)
        {
            savedLabel.setText (text, juce::dontSendNotification);
            savedLabel.setColour (juce::Label::textColourId, unsaved ? amber : accent);
        }
    }

    updateSelectionLabel();
    updateLengthHint();
    updateControls();

    // Debounced note saving: write the sidecar after the typing pause.
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (notesDirty && now - notesDirtyAt > 0.9)
    {
        if (savedFolder.isDirectory())
            updateNotesFile();

        settings.setValue ("loopNotes", notesEditor.getText());
        notesDirty = false;
    }

    repaint (timelineArea);
    repaint (editArea);
}

void LoopPanel::updateControls()
{
    const auto st = looper.getState();

    auto setMain = [this] (const juce::String& text, juce::Colour colour)
    {
        mainButton.setButtonText (text);
        mainButton.setColour (juce::TextButton::buttonColourId, colour);
    };

    switch (st)
    {
        case Looper::State::empty:
            setMain (TR ("●  RECORD", "●  录音"), green);
            break;
        case Looper::State::armingRecord:
            setMain (TR ("CANCEL COUNT-IN", "取消预备拍"), amber);
            break;
        case Looper::State::recording:
            setMain (TR ("■  CLOSE LOOP", "■  结束循环"), red);
            break;
        case Looper::State::playing:
            setMain (TR ("＋  OVERDUB", "＋  叠加录音"), blue);
            break;
        case Looper::State::armedOverdub:
            setMain (TR ("CANCEL OVERDUB", "取消叠加"), blue);
            break;
        case Looper::State::overdubbing:
            setMain (TR ("■  FINISH OVERDUB", "■  结束叠加"), blue);
            break;
        case Looper::State::stopped:
            setMain (TR ("▶  PLAY LOOP", "▶  播放循环"), green);
            break;
        case Looper::State::armingPlay:
            setMain (TR ("CANCEL", "取消"), amber);
            break;
        case Looper::State::extending:
            setMain (TR ("■  CANCEL EXTEND", "■  取消延长"), amber);
            break;
    }

    const bool busy = st != Looper::State::empty && st != Looper::State::stopped;
    stopButton.setEnabled (busy);
    clearButton.setEnabled (looper.hasLoop() || st == Looper::State::recording
                            || st == Looper::State::armingRecord
                            || st == Looper::State::extending);

    const bool editable = looper.canEditNow();
    undoButton.setEnabled (editable && looper.canUndo());
    trimButton.setEnabled (editable && hasSelection());

    const int bars = juce::jlimit (1, 64, (int) std::lround (extendBarsSlider.getValue()));
    const double bpm = looper.getLoopBpm() > 1.0 ? looper.getLoopBpm() : drums.getBpm();
    const int bpb = juce::jmax (1, looper.hasLoop() ? looper.getLoopBeatsPerBar()
                                                    : drums.getGridBeatsPerBar());
    const double barSecs = 60.0 / juce::jmax (30.0, bpm) * (double) bpb;
    const bool fits = looper.getLengthSeconds() + (double) bars * barSecs
                      <= Looper::getMaxSeconds() + 1.0e-6;
    extendButton.setEnabled (editable && fits);

    saveButton.setEnabled (looper.hasLoop() && (st == Looper::State::playing
                                                || st == Looper::State::stopped));

    const bool canChangeLength = st != Looper::State::recording
                                 && st != Looper::State::extending
                                 && st != Looper::State::overdubbing;
    lengthBox.setEnabled (canChangeLength);
    quantiseBox.setEnabled (canChangeLength);

    stopButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff303030));
    clearButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff303030));
    undoButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff303030));
    trimButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff303030));
    extendButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff303030));
    saveButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2d5c4a));
    savedLoopsButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff303030));
    openFolderButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff303030));
}

// ---- actions -----------------------------------------------------------------

void LoopPanel::trimSelection()
{
    if (! hasSelection())
        return;

    if (looper.trimTo (selectionStart(), selectionEnd()))
    {
        clearSelection();
        refresh();
    }
    else
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                TR ("Cannot trim", "没法裁剪"),
                                                TR ("Select at least 20 ms inside the loop.",
                                                    "请在循环内至少选中 20 毫秒。"));
    }
}

void LoopPanel::extendLoop()
{
    const int bars = juce::jlimit (1, 64, (int) std::lround (extendBarsSlider.getValue()));
    if (! looper.extendByBars (bars))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                TR ("Cannot extend", "没法延长"),
                                                TR ("The loop is busy, or the extra bars do not fit "
                                                    "in the 2:00 buffer. Try fewer bars.",
                                                    "循环正忙，或者新增小节超出 2:00 缓冲区。少加几小节试试。"));
        return;
    }

    updateControls();
}

void LoopPanel::undoEdit()
{
    if (looper.undoEdit())
    {
        clearSelection();
        refresh();
    }
}

void LoopPanel::saveLoop()
{
    if (! looper.hasLoop())
        return;

    // The first line of the notes makes a useful title in the saved-loop list.
    juce::String title = savedTitle;
    if (title.isEmpty())
        title = notesEditor.getText().trim().upToFirstOccurrenceOf ("\n", false, false).trim();
    if (title.isEmpty())
        title = TR ("Loop", "循环");
    title = title.substring (0, 72);

    juce::String error;
    const auto folder = LoopStore::saveLoop (looper, savedFolder,
                                             notesEditor.getText(), title, error);

    if (folder == juce::File())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                TR ("Cannot save loop", "没法保存循环"), error);
        return;
    }

    savedFolder = folder;
    savedRevision = looper.getRevision();
    loadedFromDisk = false;
    lastSavedNotes = notesEditor.getText();
    notesDirty = false;

    LoopStore::LoopInfo info;
    savedTitle = LoopStore::readInfo (folder, info) ? info.title : title;

    if (savedLoopsPanel != nullptr && savedLoopsPanel->isVisible())
        savedLoopsPanel->refresh();

    savedLabel.setText (TR ("Saved · ", "已保存 · ") + savedTitle,
                        juce::dontSendNotification);
    savedLabel.setColour (juce::Label::textColourId, accent);
    settings.setValue ("loopNotes", notesEditor.getText());
    updateControls();
}

void LoopPanel::openSavedLoops()
{
    if (savedLoopsPanel == nullptr)
        return;

    savedLoopsPanel->refresh();
    savedLoopsPanel->setBounds (controlsArea.expanded (8).getIntersection (getLocalBounds()));
    savedLoopsPanel->setVisible (true);
    savedLoopsPanel->toFront (true);
    savedLoopsPanel->focusList();
}

void LoopPanel::loadSavedLoop (const juce::File& folder)
{
    const auto st = looper.getState();
    if (st == Looper::State::recording || st == Looper::State::armingRecord
        || st == Looper::State::overdubbing || st == Looper::State::armedOverdub
        || st == Looper::State::extending || st == Looper::State::armingPlay)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                                                TR ("Loop station is busy", "循环站正忙"),
                                                TR ("Stop the current take before loading a saved loop.",
                                                    "请先停止当前的录音或叠加，再载入已保存的循环。"));
        return;
    }

    LoopStore::LoopInfo info;
    if (! LoopStore::readInfo (folder, info))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                TR ("Cannot load loop", "没法载入循环"),
                                                TR ("The loop folder has no readable loop.wav.",
                                                    "这个循环文件夹里没有可读取的 loop.wav。"));
        return;
    }

    juce::AudioBuffer<float> audio;
    double sourceRate = 0.0;
    juce::String error;
    if (! LoopStore::loadAudio (info, audio, sourceRate, error))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                TR ("Cannot load loop", "没法载入循环"), error);
        return;
    }

    const double currentBpm = juce::jmax (30.0, (double) drums.getBpm());
    const int bpb = info.beatsPerBar > 0 ? info.beatsPerBar : drums.getGridBeatsPerBar();

    // A saved take keeps its bar count and is stretched to the tempo that is
    // running now, so it locks to the drum machine and starts playing.
    juce::AudioBuffer<float> fitted;
    double fittedRate = sourceRate;
    if (! LoopStore::stretchToTempo (audio, sourceRate,
                                     info.hasTempo() ? info.bpm : currentBpm,
                                     currentBpm, fitted, fittedRate, error))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                TR ("Cannot match the tempo", "没法匹配速度"), error);
        return;
    }

    // Load it stopped, then use the normal play action: with the drums running
    // that waits for the next drum bar, so the take starts on the grid.
    if (! looper.loadLoop (fitted, fittedRate, currentBpm, bpb, false))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                TR ("Cannot load loop", "没法载入循环"),
                                                TR ("The take is too long for the 2:00 loop buffer.",
                                                    "这段循环超过 2:00 的循环缓冲区。"));
        return;
    }
    looper.toggle();

    savedFolder = folder;
    savedTitle = info.title;
    loadedFromDisk = true;
    savedRevision = looper.getRevision();
    lastLoopId = looper.getLoopId();
    lastSavedNotes = info.notes;
    notesDirty = false;

    updatingNotes = true;
    notesEditor.setText (info.notes, false);
    updatingNotes = false;
    settings.setValue ("loopNotes", info.notes);

    savedLabel.setText (TR ("Loaded · ", "已载入 · ") + savedTitle,
                        juce::dontSendNotification);
    savedLabel.setColour (juce::Label::textColourId, accent);
    clearSelection();
    if (savedLoopsPanel != nullptr)
        savedLoopsPanel->setVisible (false);

    updateControls();
    refresh();
    grabKeyboardFocus();
}

void LoopPanel::updateNotesFile()
{
    if (! savedFolder.isDirectory())
        return;

    juce::String error;
    if (LoopStore::updateNotes (savedFolder, notesEditor.getText(), error))
    {
        lastSavedNotes = notesEditor.getText();
    }
    else
    {
        savedLabel.setText (TR ("Notes not saved", "备注没保存"), juce::dontSendNotification);
        savedLabel.setColour (juce::Label::textColourId, red);
    }
}

void LoopPanel::openLoopsFolder()
{
    auto folder = savedFolder.isDirectory() ? savedFolder : LoopStore::getLoopsFolder();
    folder.createDirectory();
    folder.startAsProcess();
}

void LoopPanel::clearSelection()
{
    selStart = selEnd = -1.0;
    dragMode = 0;
    dragged = false;
    updateSelectionLabel();
    updateControls();
    repaint (timelineArea);
}

// ---- selection / mouse -------------------------------------------------------

bool LoopPanel::hasSelection() const noexcept
{
    return selEnd > selStart + 0.02;
}

double LoopPanel::selectionStart() const noexcept
{
    return juce::jmin (selStart, selEnd);
}

double LoopPanel::selectionEnd() const noexcept
{
    return juce::jmax (selStart, selEnd);
}

double LoopPanel::xToSeconds (int x) const noexcept
{
    return ((double) (x - laneX) / (double) juce::jmax (1, laneW)) * displaySecs;
}

float LoopPanel::secondsToX (double seconds) const noexcept
{
    return (float) laneX + (float) laneW
           * (float) (seconds / juce::jmax (1.0e-6, displaySecs));
}

double LoopPanel::snapTime (double seconds, bool free) const noexcept
{
    if (free || looper.getQuantise() == LoopQuantise::off)
        return seconds;

    const bool hasLoop = looper.hasLoop();
    const double bpm = hasLoop && looper.getLoopBpm() > 1.0 ? looper.getLoopBpm()
                                                            : juce::jmax (30.0, (double) drums.getBpm());
    const int bpb = juce::jmax (1, hasLoop ? looper.getLoopBeatsPerBar()
                                           : drums.getGridBeatsPerBar());
    const double unit = looper.getQuantise() == LoopQuantise::bar
                            ? 60.0 / bpm * (double) bpb : 60.0 / bpm;

    return std::round (seconds / juce::jmax (1.0e-6, unit)) * unit;
}

void LoopPanel::updateSelectionLabel()
{
    juce::String s;
    if (hasSelection())
    {
        const bool hasLoop = looper.hasLoop();
        const double bpm = hasLoop && looper.getLoopBpm() > 1.0 ? looper.getLoopBpm()
                                                                : juce::jmax (30.0, (double) drums.getBpm());
        const int bpb = juce::jmax (1, hasLoop ? looper.getLoopBeatsPerBar()
                                               : drums.getGridBeatsPerBar());
        const double barSecs = 60.0 / bpm * (double) bpb;
        s = TR ("Selection ", "选区 ") + timeText (selectionStart()) + " – "
            + timeText (selectionEnd()) + "  ·  "
            + barsText ((selectionEnd() - selectionStart()) / juce::jmax (0.01, barSecs));
    }
    else
    {
        s = TR ("Drag on the waveform to select a region (hold Shift for free timing)",
                "在波形上拖动选区（按住 Shift 不吸附网格）");
    }

    if (selectionLabel.getText() != s)
        selectionLabel.setText (s, juce::dontSendNotification);
}

void LoopPanel::mouseMove (const juce::MouseEvent& e)
{
    if (! loopLane.contains (e.getPosition()))
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
        return;
    }

    if (hasSelection()
        && (std::abs ((float) e.x - secondsToX (selStart)) <= 7.0f
            || std::abs ((float) e.x - secondsToX (selEnd)) <= 7.0f))
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    else
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void LoopPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! loopLane.contains (e.getPosition()) || ! looper.canEditNow())
        return;

    mouseDownX = e.x;
    dragged = false;
    dragMode = 1;
    dragAnchor = 0.0;

    const double len = looper.getLengthSeconds();
    const double t = juce::jlimit (0.0, juce::jmax (0.0, len), xToSeconds (e.x));

    if (hasSelection())
    {
        if (std::abs ((float) e.x - secondsToX (selStart)) <= 7.0f)
        {
            dragMode = 2;
            return;
        }
        if (std::abs ((float) e.x - secondsToX (selEnd)) <= 7.0f)
        {
            dragMode = 3;
            return;
        }
    }

    dragAnchor = juce::jlimit (0.0, juce::jmax (0.0, len), snapTime (t, e.mods.isShiftDown()));
    selStart = selEnd = dragAnchor;
}

void LoopPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragMode == 0)
        return;

    if (std::abs (e.x - mouseDownX) > 2)
        dragged = true;

    const double len = juce::jmax (0.0, looper.getLengthSeconds());
    const bool free = e.mods.isShiftDown();
    double t = juce::jlimit (0.0, len, xToSeconds (e.x));
    t = juce::jlimit (0.0, len, snapTime (t, free));

    if (dragMode == 1)
    {
        selStart = juce::jmin (dragAnchor, t);
        selEnd = juce::jmax (dragAnchor, t);
    }
    else if (dragMode == 2)
    {
        selStart = juce::jlimit (0.0, juce::jmax (0.0, selEnd - 0.02), t);
    }
    else if (dragMode == 3)
    {
        selEnd = juce::jlimit (selStart + 0.02, len, t);
    }

    updateSelectionLabel();
    updateControls();
    repaint (timelineArea);
}

void LoopPanel::mouseUp (const juce::MouseEvent& e)
{
    if (dragMode == 0)
        return;

    const int mode = dragMode;
    dragMode = 0;

    if (! dragged && mode == 1)
    {
        // A plain click moves the playhead and clears the old selection.
        clearSelection();
        const double len = looper.getLengthSeconds();
        if (len > 0.0)
            looper.seek (juce::jlimit (0.0, juce::jmax (0.0, len), xToSeconds (e.x)));
    }
    else if (! hasSelection())
    {
        clearSelection();
    }

    updateSelectionLabel();
    updateControls();
    repaint (timelineArea);
}
