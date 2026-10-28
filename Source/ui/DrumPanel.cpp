#include "DrumPanel.h"
#include "Text.h"

namespace
{
    const juce::Colour muted { 0xff9a9a9a };
    const juce::Colour accent { 0xff5dcaa5 };

    void styleLabel (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::FontOptions (14.0f));
        l.setColour (juce::Label::textColourId, muted);
    }
}

DrumPanel::DrumPanel (DrumMachine& d, juce::PropertiesFile& s) : drums (d), settings (s)
{
    title.setText (TR ("Drum Machine", "鼓机"), juce::dontSendNotification);
    title.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    addAndMakeVisible (title);

    // ---- 播放 ----------------------------------------------------------------
    playButton.setClickingTogglesState (false);
    playButton.onClick = [this] { togglePlay(); };
    playButton.setTooltip (TR ("Space bar also starts / stops", "空格键也可以开始 / 停止"));
    addAndMakeVisible (playButton);

    // ---- 风格 ----------------------------------------------------------------
    const auto& styles = getDrumStyles();
    for (int i = 0; i < (int) styles.size(); ++i)
        styleBox.addItem (styles[(size_t) i].name, i + 1);
    styleBox.onChange = [this]
    {
        drums.setStyle (styleBox.getSelectedId() - 1);
        applyStyleDefaults();
        save();
    };
    addAndMakeVisible (styleBox);

    // ---- 段落 ----------------------------------------------------------------
    for (auto* b : { &verseButton, &chorusButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (101);
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff1d9e75));
        addAndMakeVisible (*b);
    }
    verseButton.setButtonText (TR ("Verse", "主歌"));
    chorusButton.setButtonText (TR ("Chorus", "副歌"));
    verseButton.onClick = [this] { if (verseButton.getToggleState()) { drums.setChorus (false); save(); } };
    chorusButton.onClick = [this] { if (chorusButton.getToggleState()) { drums.setChorus (true); save(); } };
    verseButton.setTooltip (TR ("Switch at the next bar", "下一小节开始切换"));
    chorusButton.setTooltip (TR ("Switch at the next bar (chorus is busier)", "下一小节开始切换（副歌更热闹）"));

    fillNowButton.setButtonText (TR ("Fill", "加花"));
    fillNowButton.setTooltip (TR ("Add a fill at the next bar", "下一小节来一段加花"));
    fillNowButton.onClick = [this] { drums.triggerFillNow(); };
    addAndMakeVisible (fillNowButton);

    // ---- 速度 ----------------------------------------------------------------
    styleLabel (bpmLabel, TR ("Tempo", "速度"));
    addAndMakeVisible (bpmLabel);
    bpmSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    bpmSlider.setRange (30.0, 300.0, 1.0);
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
    bpmSlider.setTextValueSuffix (" BPM");
    bpmSlider.onValueChange = [this] { drums.setBpm ((float) bpmSlider.getValue()); };
    bpmSlider.onDragEnd = [this] { save(); };
    addAndMakeVisible (bpmSlider);

    tapButton.setButtonText (TR ("Tap", "打拍"));
    tapButton.setTooltip (TR ("Tap a few times along with the song to set the tempo", "跟着歌连续点几下，自动算出速度"));
    tapButton.onClick = [this] { tapTempo(); };
    addAndMakeVisible (tapButton);

    styleLabel (swingLabel, TR ("Swing", "摇摆"));
    addAndMakeVisible (swingLabel);
    swingSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    swingSlider.setRange (0.0, 100.0, 1.0);
    swingSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
    swingSlider.setTextValueSuffix (" %");
    swingSlider.setTooltip (TR ("Swing the off-beat eighth notes; 100% is a triplet shuffle", "让八分音符的后半拍往后挪，100% 就是三连音 shuffle"));
    swingSlider.onValueChange = [this] { drums.setSwing ((float) swingSlider.getValue() / 100.0f); };
    swingSlider.onDragEnd = [this] { save(); };
    addAndMakeVisible (swingSlider);

    styleLabel (volumeLabel, TR ("Drum level", "鼓音量"));
    addAndMakeVisible (volumeLabel);
    volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    volumeSlider.setRange (-40.0, 6.0, 0.5);
    volumeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
    volumeSlider.setTextValueSuffix (" dB");
    volumeSlider.onValueChange = [this] { drums.setVolumeDb ((float) volumeSlider.getValue()); };
    volumeSlider.onDragEnd = [this] { save(); };
    addAndMakeVisible (volumeSlider);

    styleLabel (humanizeLabel, TR ("Humanize", "人性化"));
    addAndMakeVisible (humanizeLabel);
    humanizeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    humanizeSlider.setRange (0.0, 100.0, 1.0);
    humanizeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 22);
    humanizeSlider.setTextValueSuffix (" %");
    humanizeSlider.setTooltip (TR ("Vary velocity and timing so the groove breathes",
                                   "轻微改变力度和位置，让节奏不那么机械"));
    humanizeSlider.onValueChange = [this] { drums.setHumanize ((float) humanizeSlider.getValue() / 100.0f); };
    humanizeSlider.onDragEnd = [this] { save(); };
    addAndMakeVisible (humanizeSlider);

    // ---- 其他 ----------------------------------------------------------------
    fillBox.addItem (TR ("Auto Fill: Off", "自动加花：关"), 1);
    fillBox.addItem (TR ("Fill every 2 bars", "每 2 小节加花"), 2);
    fillBox.addItem (TR ("Fill every 4 bars", "每 4 小节加花"), 4);
    fillBox.addItem (TR ("Fill every 8 bars", "每 8 小节加花"), 8);
    fillBox.onChange = [this] { drums.setFillEvery (fillBox.getSelectedId() == 1 ? 0 : fillBox.getSelectedId()); save(); };
    addAndMakeVisible (fillBox);

    countInToggle.setButtonText (TR ("Count-in", "预备拍"));
    countInToggle.setTooltip (TR ("Play one bar of count-in first", "开始前先敲一小节"));
    countInToggle.onClick = [this] { drums.setCountIn (countInToggle.getToggleState()); save(); };
    addAndMakeVisible (countInToggle);

    metronomeToggle.setButtonText (TR ("Metronome", "节拍器"));
    metronomeToggle.setTooltip (TR ("Add a click on top of the drums", "在鼓声之外再叠加嘀嗒声"));
    metronomeToggle.onClick = [this] { drums.setMetronome (metronomeToggle.getToggleState()); save(); };
    addAndMakeVisible (metronomeToggle);

    // ---- 读取上次的设置 -------------------------------------------------------
    const int style = juce::jlimit (0, (int) styles.size() - 1, settings.getIntValue ("drumStyle", 0));
    drums.setStyle (style);
    styleBox.setSelectedId (style + 1, juce::dontSendNotification);

    const bool chorus = settings.getBoolValue ("drumChorus", false);
    drums.setChorus (chorus);
    (chorus ? chorusButton : verseButton).setToggleState (true, juce::dontSendNotification);

    bpmSlider.setValue (settings.getDoubleValue ("drumBpm", styles[(size_t) style].defaultBpm));
    swingSlider.setValue (settings.getDoubleValue ("drumSwing", styles[(size_t) style].defaultSwing * 100.0));
    volumeSlider.setValue (settings.getDoubleValue ("drumVolume", -8.0));
    humanizeSlider.setValue (settings.getDoubleValue ("drumHumanize", 20.0));

    const int fill = settings.getIntValue ("drumFillEvery", 4);
    fillBox.setSelectedId (fill == 0 ? 1 : fill, juce::dontSendNotification);
    drums.setFillEvery (fill);

    countInToggle.setToggleState (settings.getBoolValue ("drumCountIn", true), juce::dontSendNotification);
    drums.setCountIn (countInToggle.getToggleState());
    metronomeToggle.setToggleState (settings.getBoolValue ("drumMetronome", false), juce::dontSendNotification);
    drums.setMetronome (metronomeToggle.getToggleState());
    drums.setHumanize ((float) humanizeSlider.getValue() / 100.0f);

    updatePlayButton();
    startTimerHz (30);
}

DrumPanel::~DrumPanel()
{
    stopTimer();
}

void DrumPanel::applyStyleDefaults()
{
    const auto& st = getDrumStyles()[(size_t) juce::jmax (0, styleBox.getSelectedId() - 1)];
    bpmSlider.setValue (st.defaultBpm);
    swingSlider.setValue (st.defaultSwing * 100.0);
}

void DrumPanel::save()
{
    settings.setValue ("drumStyle", styleBox.getSelectedId() - 1);
    settings.setValue ("drumChorus", chorusButton.getToggleState());
    settings.setValue ("drumBpm", bpmSlider.getValue());
    settings.setValue ("drumSwing", swingSlider.getValue());
    settings.setValue ("drumVolume", volumeSlider.getValue());
    settings.setValue ("drumHumanize", humanizeSlider.getValue());
    settings.setValue ("drumFillEvery", drums.getFillEvery());
    settings.setValue ("drumCountIn", countInToggle.getToggleState());
    settings.setValue ("drumMetronome", metronomeToggle.getToggleState());
}

void DrumPanel::togglePlay()
{
    drums.setPlaying (! drums.isPlaying());
    updatePlayButton();
}

void DrumPanel::updatePlayButton()
{
    const bool p = drums.isPlaying();
    playButton.setButtonText (p ? TR ("Stop", "停止") : TR ("Start", "开始"));
    playButton.setColour (juce::TextButton::buttonColourId, p ? juce::Colour (0xffa32d2d) : juce::Colour (0xff1d9e75));
}

void DrumPanel::tapTempo()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (! tapTimes.isEmpty() && now - tapTimes.getLast() > 2000.0)
        tapTimes.clear();

    tapTimes.add (now);
    while (tapTimes.size() > 5)
        tapTimes.remove (0);

    if (tapTimes.size() >= 2)
    {
        const double avg = (tapTimes.getLast() - tapTimes.getFirst()) / (tapTimes.size() - 1);
        bpmSlider.setValue (juce::roundToInt (60000.0 / avg));
        save();
    }
}

void DrumPanel::timerCallback()
{
    repaint (beatArea);
    if (playButton.getButtonText() != (drums.isPlaying() ? TR ("Stop", "停止") : TR ("Start", "开始")))
        updatePlayButton();
}

void DrumPanel::paint (juce::Graphics& g)
{
    // 拍子指示灯
    const auto& st = getDrumStyles()[(size_t) juce::jlimit (0, (int) getDrumStyles().size() - 1, drums.getStyle())];
    const int beats = st.beatsPerBar;
    const int current = drums.isPlaying() ? drums.getCurrentBeat() : -1;
    const float size = juce::jmin (26.0f, (float) beatArea.getHeight());
    float x = (float) beatArea.getX();

    for (int b = 0; b < beats; ++b)
    {
        const auto r = juce::Rectangle<float> (x, (float) beatArea.getCentreY() - size * 0.5f, size, size);
        juce::Colour c = juce::Colour (0xff333333);
        if (b == current)
            c = drums.isInCountIn() ? juce::Colour (0xffef9f27) : (b == 0 ? juce::Colour (0xffe24b4a) : accent);
        g.setColour (c);
        g.fillEllipse (r);
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (juce::String (b + 1), r, juce::Justification::centred);
        x += size + 8.0f;
    }

    g.setFont (juce::FontOptions (13.0f));
    g.setColour (muted);
    juce::String status;
    if (drums.isPlaying())
        status = drums.isInCountIn() ? TR ("Count-in…", "预备…") : (drums.isInFill() ? TR ("Fill", "加花") : (drums.isChorus() ? TR ("Chorus", "副歌") : TR ("Verse", "主歌")));
    g.drawText (status, juce::Rectangle<int> ((int) x + 8, beatArea.getY(), 80, beatArea.getHeight()), juce::Justification::centredLeft);
}

void DrumPanel::resized()
{
    auto r = getLocalBounds();

    auto top = r.removeFromTop (34);
    title.setBounds (top.removeFromLeft (60));
    playButton.setBounds (top.removeFromLeft (90).reduced (2));
    top.removeFromLeft (12);
    beatArea = top.removeFromLeft (260);
    metronomeToggle.setBounds (top.removeFromRight (90));
    countInToggle.setBounds (top.removeFromRight (90));

    r.removeFromTop (12);

    auto row1 = r.removeFromTop (30);
    styleBox.setBounds (row1.removeFromLeft (180).reduced (0, 1));
    row1.removeFromLeft (12);
    verseButton.setBounds (row1.removeFromLeft (64).reduced (0, 1));
    chorusButton.setBounds (row1.removeFromLeft (64).reduced (0, 1));
    row1.removeFromLeft (8);
    fillNowButton.setBounds (row1.removeFromLeft (64).reduced (0, 1));
    row1.removeFromLeft (12);
    fillBox.setBounds (row1.removeFromLeft (150).reduced (0, 1));

    r.removeFromTop (10);
    auto row2 = r.removeFromTop (30);
    bpmLabel.setBounds (row2.removeFromLeft (60));
    tapButton.setBounds (row2.removeFromRight (64).reduced (0, 1));
    row2.removeFromRight (8);
    bpmSlider.setBounds (row2);

    r.removeFromTop (6);
    auto row3 = r.removeFromTop (30);
    const int third = row3.getWidth() / 3;
    auto swingArea = row3.removeFromLeft (third);
    swingLabel.setBounds (swingArea.removeFromLeft (60));
    swingSlider.setBounds (swingArea.withTrimmedRight (14));
    auto volumeArea = row3.removeFromLeft (third);
    volumeLabel.setBounds (volumeArea.removeFromLeft (60));
    volumeSlider.setBounds (volumeArea.withTrimmedRight (14));
    humanizeLabel.setBounds (row3.removeFromLeft (72));
    humanizeSlider.setBounds (row3);
}
