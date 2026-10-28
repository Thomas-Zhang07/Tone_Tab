#include "DrumPanel.h"
#include "Text.h"
#include <cmath>

namespace
{
    const juce::Colour muted { 0xff9a9a9a };
    const juce::Colour accent { 0xff5dcaa5 };
    const juce::Colour panel { 0xff1b1b1b };

    void caption (juce::Label& l, const juce::String& text, float size = 14.0f,
                  juce::Colour colour = muted, bool bold = false)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
        l.setColour (juce::Label::textColourId, colour);
    }
}

DrumPanel::DrumPanel (DrumMachine& d, juce::PropertiesFile& s) : drums (d), settings (s)
{
    const auto& styles = getDrumStyles();

    caption (title, TR ("Drum Machine", "鼓机"), 21.0f, juce::Colours::white, true);
    addAndMakeVisible (title);

    caption (status, {}, 13.0f, accent);
    addAndMakeVisible (status);

    playButton.setClickingTogglesState (false);
    playButton.onClick = [this] { togglePlay(); };
    playButton.setTooltip (TR ("Space bar also starts / stops", "空格键也可以开始 / 停止"));
    addAndMakeVisible (playButton);

    countInToggle.setButtonText (TR ("Count-in", "预备拍"));
    countInToggle.setTooltip (TR ("Play one bar of count-in first", "开始前先敲一小节"));
    countInToggle.onClick = [this] { drums.setCountIn (countInToggle.getToggleState()); save(); };
    addAndMakeVisible (countInToggle);

    metronomeToggle.setButtonText (TR ("Metronome", "节拍器"));
    metronomeToggle.setTooltip (TR ("Add a click on top of the drums", "在鼓声之外再叠加嘀嗒声"));
    metronomeToggle.onClick = [this] { drums.setMetronome (metronomeToggle.getToggleState()); save(); };
    addAndMakeVisible (metronomeToggle);

    // ---- Style grid -----------------------------------------------------------
    caption (styleHeader, TR ("Style", "风格"), 15.0f, juce::Colours::white, true);
    addAndMakeVisible (styleHeader);
    caption (patternHeader, TR ("Pattern", "节奏型"), 15.0f, juce::Colours::white, true);
    addAndMakeVisible (patternHeader);
    caption (rockPopLabel, TR ("Rock / Pop", "摇滚 / 流行"));
    caption (grooveLabel, TR ("Groove", "律动"));
    caption (metalLabel, TR ("Metal / Blues / Jazz", "金属 / 蓝调 / 爵士"));
    caption (otherLabel, TR ("Other", "其他"));
    for (auto* l : { &rockPopLabel, &grooveLabel, &metalLabel, &otherLabel })
        addAndMakeVisible (*l);

    auto addStyle = [this] (int index)
    {
        auto* b = styleButtons.add (new juce::TextButton (getDrumStyles()[(size_t) index].name));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (303);
        b->getProperties().set ("style", index);
        b->onClick = [this, b]
        {
            if (b->getToggleState())
                setStyle ((int) b->getProperties()["style"]);
        };
        addAndMakeVisible (b);
    };

    for (int i : { 0, 1, 5 })   addStyle (i);   // Rock, Pop, Punk
    for (int i : { 3, 4, 8 })   addStyle (i);   // Funk, Disco, Reggae
    for (int i : { 6, 7, 9, 10 }) addStyle (i); // Metal, Blues, Jazz, Waltz
    for (int i : { 2, 11 })     addStyle (i);   // Ballad, Click only

    // ---- Section controls -----------------------------------------------------
    for (auto* b : { &verseButton, &chorusButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (101);
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff1d9e75));
        addAndMakeVisible (*b);
    }
    verseButton.setButtonText (TR ("Verse", "主歌"));
    chorusButton.setButtonText (TR ("Chorus", "副歌"));
    verseButton.setTooltip (TR ("Switch at the next bar", "下一小节开始切换"));
    chorusButton.setTooltip (TR ("Switch at the next bar (chorus is busier)", "下一小节开始切换（副歌更热闹）"));
    verseButton.onClick = [this] { if (verseButton.getToggleState()) { drums.setChorus (false); save(); } };
    chorusButton.onClick = [this] { if (chorusButton.getToggleState()) { drums.setChorus (true); save(); } };

    fillNowButton.setButtonText (TR ("Fill Now", "手动加花"));
    fillNowButton.setTooltip (TR ("Add a fill at the next bar", "下一小节来一段加花"));
    fillNowButton.onClick = [this] { drums.triggerFillNow(); };
    addAndMakeVisible (fillNowButton);

    fillBox.addItem (TR ("Auto Fill: Off", "自动加花：关"), 1);
    fillBox.addItem (TR ("Fill every 2 bars", "每 2 小节加花"), 2);
    fillBox.addItem (TR ("Fill every 4 bars", "每 4 小节加花"), 4);
    fillBox.addItem (TR ("Fill every 8 bars", "每 8 小节加花"), 8);
    fillBox.setTooltip (TR ("Add a fill automatically every N bars", "每 N 小节自动加一段加花"));
    fillBox.onChange = [this] { drums.setFillEvery (fillBox.getSelectedId() == 1 ? 0 : fillBox.getSelectedId()); save(); };
    addAndMakeVisible (fillBox);

    // ---- Feel and level -------------------------------------------------------
    caption (feelHeader, TR ("Feel & Level", "手感与音量"), 15.0f, juce::Colours::white, true);
    addAndMakeVisible (feelHeader);

    caption (bpmLabel, TR ("Tempo", "速度"));
    addAndMakeVisible (bpmLabel);
    bpmSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    bpmSlider.setRange (30.0, 300.0, 1.0);
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 22);
    bpmSlider.setTextValueSuffix (" BPM");
    bpmSlider.onValueChange = [this] { drums.setBpm ((float) bpmSlider.getValue()); };
    bpmSlider.onDragEnd = [this] { save(); };
    addAndMakeVisible (bpmSlider);

    tapButton.setButtonText (TR ("Tap", "打拍"));
    tapButton.setTooltip (TR ("Tap a few times along with the song to set the tempo", "跟着歌连续点几下，自动算出速度"));
    tapButton.onClick = [this] { tapTempo(); };
    addAndMakeVisible (tapButton);

    caption (swingLabel, TR ("Swing", "摇摆"));
    addAndMakeVisible (swingLabel);
    swingSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    swingSlider.setRange (0.0, 100.0, 1.0);
    swingSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 22);
    swingSlider.setTextValueSuffix (" %");
    swingSlider.setTooltip (TR ("Swing the off-beat eighths; 100% is a triplet shuffle", "让八分音符的后半拍往后挪，100% 就是三连音 shuffle"));
    swingSlider.onValueChange = [this] { drums.setSwing ((float) swingSlider.getValue() / 100.0f); };
    swingSlider.onDragEnd = [this] { save(); };
    addAndMakeVisible (swingSlider);

    caption (humanizeLabel, TR ("Humanize", "人性化"));
    addAndMakeVisible (humanizeLabel);
    humanizeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    humanizeSlider.setRange (0.0, 100.0, 1.0);
    humanizeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 22);
    humanizeSlider.setTextValueSuffix (" %");
    humanizeSlider.setTooltip (TR ("Vary velocity and timing so the groove breathes", "轻微改变力度和位置，让节奏不那么机械"));
    humanizeSlider.onValueChange = [this] { drums.setHumanize ((float) humanizeSlider.getValue() / 100.0f); };
    humanizeSlider.onDragEnd = [this] { save(); };
    addAndMakeVisible (humanizeSlider);

    caption (volumeLabel, TR ("Drum level", "鼓音量"));
    addAndMakeVisible (volumeLabel);
    volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    volumeSlider.setRange (-40.0, 6.0, 0.5);
    volumeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 22);
    volumeSlider.setTextValueSuffix (" dB");
    volumeSlider.onValueChange = [this] { drums.setVolumeDb ((float) volumeSlider.getValue()); };
    volumeSlider.onDragEnd = [this] { save(); };
    addAndMakeVisible (volumeSlider);

    // ---- Per-piece mixer ------------------------------------------------------
    caption (mixerHeader, TR ("Mixer", "鼓件音量"), 15.0f, juce::Colours::white, true);
    addAndMakeVisible (mixerHeader);

    const char* mixNames[] = { "Kick", "Snare", "Hats", "Toms", "Cymbals" };
    const char* mixNamesZh[] = { "底鼓", "军鼓", "镲片", "通鼓", "吊镲" };
    for (int i = 0; i < 5; ++i)
    {
        mixLabels[(size_t) i] = mixLabelOwned.add (new juce::Label());
        caption (*mixLabels[(size_t) i], TR (mixNames[i], mixNamesZh[i]), 12.0f);
        addAndMakeVisible (*mixLabels[(size_t) i]);

        mixSliders[(size_t) i] = mixSliderOwned.add (new juce::Slider());
        auto* sl = mixSliders[(size_t) i];
        sl->setSliderStyle (juce::Slider::LinearHorizontal);
        sl->setRange (-24.0, 6.0, 0.5);
        sl->setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 20);
        sl->setTextValueSuffix (" dB");
        sl->onValueChange = [this] { applyMixer(); };
        sl->onDragEnd = [this] { save(); };
        addAndMakeVisible (*sl);
    }

    // ---- Restore settings -----------------------------------------------------
    selectedStyle = juce::jlimit (0, (int) styles.size() - 1, settings.getIntValue ("drumStyle", 0));
    drums.setStyle (selectedStyle);
    updateStyleButtons();

    const bool chorus = settings.getBoolValue ("drumChorus", false);
    drums.setChorus (chorus);
    (chorus ? chorusButton : verseButton).setToggleState (true, juce::dontSendNotification);

    bpmSlider.setValue (settings.getDoubleValue ("drumBpm", styles[(size_t) selectedStyle].defaultBpm));
    swingSlider.setValue (settings.getDoubleValue ("drumSwing", styles[(size_t) selectedStyle].defaultSwing * 100.0));
    humanizeSlider.setValue (settings.getDoubleValue ("drumHumanize", 20.0));
    volumeSlider.setValue (settings.getDoubleValue ("drumVolume", -8.0));

    const int fill = settings.getIntValue ("drumFillEvery", 4);
    fillBox.setSelectedId (fill == 0 ? 1 : fill, juce::dontSendNotification);
    drums.setFillEvery (fill);

    countInToggle.setToggleState (settings.getBoolValue ("drumCountIn", true), juce::dontSendNotification);
    drums.setCountIn (countInToggle.getToggleState());
    metronomeToggle.setToggleState (settings.getBoolValue ("drumMetronome", false), juce::dontSendNotification);
    drums.setMetronome (metronomeToggle.getToggleState());
    drums.setHumanize ((float) humanizeSlider.getValue() / 100.0f);

    for (int i = 0; i < 5; ++i)
        mixSliders[(size_t) i]->setValue (settings.getDoubleValue ("drumMix" + juce::String (i), 0.0));
    applyMixer();

    updatePlayButton();
    startTimerHz (30);
}

DrumPanel::~DrumPanel()
{
    stopTimer();
}

void DrumPanel::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto header = area.removeFromTop (38);
    title.setBounds (header.removeFromLeft (200));
    metronomeToggle.setBounds (header.removeFromRight (110));
    countInToggle.setBounds (header.removeFromRight (100));
    playButton.setBounds (header.removeFromRight (120).reduced (0, 2));
    status.setBounds (header);

    area.removeFromTop (6);

    // Top row: style grid on the left, feel controls on the right.
    const int topH = juce::jlimit (210, 270, (int) (area.getHeight() * 0.45f));
    auto top = area.removeFromTop (topH);
    const int leftW = juce::jlimit (320, 480, (int) (top.getWidth() * 0.55f));
    auto left = top.removeFromLeft (leftW);
    top.removeFromLeft (12);
    auto right = top;

    // Pattern gets the full width; section and mixer rows sit below it.
    area.removeFromTop (8);
    auto bottom = area.removeFromBottom (104);
    patternArea = area;
    patternHeader.setBounds (patternArea.removeFromTop (22));

    // Left: style groups.
    auto l = left;
    styleHeader.setBounds (l.removeFromTop (24));

    auto placeRow = [this, &l] (int first, int count)
    {
        auto row = l.removeFromTop (26);
        const int w = (row.getWidth() - (count - 1) * 4) / count;
        for (int i = 0; i < count; ++i)
        {
            if (auto* b = styleButtons[first + i])
                b->setBounds (row.removeFromLeft (w).reduced (2, 1));
            row.removeFromLeft (4);
        }
    };

    rockPopLabel.setBounds (l.removeFromTop (16));
    placeRow (0, 3);
    l.removeFromTop (2);
    grooveLabel.setBounds (l.removeFromTop (16));
    placeRow (3, 3);
    l.removeFromTop (2);
    metalLabel.setBounds (l.removeFromTop (16));
    placeRow (6, 4);
    l.removeFromTop (2);
    otherLabel.setBounds (l.removeFromTop (16));
    placeRow (10, 2);

    // Bottom: section + fill, then the per-piece mixer.
    auto sectionRow = bottom.removeFromTop (30);
    verseButton.setBounds (sectionRow.removeFromLeft (100).reduced (0, 1));
    sectionRow.removeFromLeft (6);
    chorusButton.setBounds (sectionRow.removeFromLeft (100).reduced (0, 1));
    sectionRow.removeFromLeft (8);
    fillNowButton.setBounds (sectionRow.removeFromLeft (110).reduced (0, 1));
    sectionRow.removeFromLeft (8);
    fillBox.setBounds (sectionRow);

    bottom.removeFromTop (2);
    mixerHeader.setBounds (bottom.removeFromTop (18));
    bottom.removeFromTop (2);

    const int mixW = bottom.getWidth() / 5;
    for (int i = 0; i < 5; ++i)
    {
        auto cell = bottom.removeFromLeft (i == 4 ? bottom.getWidth() : mixW);
        cell.removeFromRight (6);
        mixLabels[(size_t) i]->setBounds (cell.removeFromTop (14));
        mixSliders[(size_t) i]->setBounds (cell.removeFromTop (24).reduced (0, 1));
    }

    // Right: feel.
    auto r = right;
    feelHeader.setBounds (r.removeFromTop (20));

    auto addSlider = [&r] (juce::Label& label, juce::Slider& slider)
    {
        label.setBounds (r.removeFromTop (14));
        slider.setBounds (r.removeFromTop (22));
        r.removeFromTop (2);
    };

    bpmLabel.setBounds (r.removeFromTop (14));
    auto bpmRow = r.removeFromTop (24);
    tapButton.setBounds (bpmRow.removeFromRight (54).reduced (0, 1));
    bpmSlider.setBounds (bpmRow);
    r.removeFromTop (2);

    addSlider (swingLabel, swingSlider);
    addSlider (humanizeLabel, humanizeSlider);
    addSlider (volumeLabel, volumeSlider);
}

void DrumPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff151515));
    paintPattern (g, patternArea);
}

void DrumPanel::paintPattern (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (area.getWidth() < 80 || area.getHeight() < 80)
        return;

    const auto& styles = getDrumStyles();
    const auto& st = styles[(size_t) juce::jlimit (0, (int) styles.size() - 1, selectedStyle)];
    const int steps = juce::jlimit (1, 16, st.beatsPerBar * st.stepsPerBeat);

    const auto& p = drums.isInFill() ? st.fill
                  : (drums.isChorus() ? st.chorus : st.verse);

    const DrumInstrument rows[] = { kick, snare, closedHat, openHat, crash, ride, tomHigh, tomMid, tomLow };
    const char* names[] = { "Kick", "Snare", "Hat", "Open", "Crash", "Ride", "Tom H", "Tom M", "Tom L" };
    const int rowCount = 9;
    const int headerH = 22;
    const int labelW = 56;

    g.setColour (panel);
    g.fillRect (area);

    const int gridW = juce::jmax (1, area.getWidth() - labelW);
    const float cellW = (float) gridW / (float) steps;
    const float rowH = (float) (area.getHeight() - headerH) / (float) rowCount;

    g.setFont (juce::FontOptions (12.0f));
    for (int b = 0; b < st.beatsPerBar; ++b)
    {
        const float x = (float) area.getX() + labelW + (float) b * st.stepsPerBeat * cellW;
        g.setColour (muted);
        g.drawText (juce::String (b + 1), (int) x, area.getY(), (int) (st.stepsPerBeat * cellW), headerH,
                    juce::Justification::centred);
    }

    const int current = drums.getCurrentStep();
    for (int row = 0; row < rowCount; ++row)
    {
        const float y = (float) (area.getY() + headerH) + row * rowH;
        g.setColour (row == 0 ? juce::Colours::white : muted);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (names[row], area.getX(), (int) y, labelW - 8, (int) rowH,
                    juce::Justification::centredRight);

        for (int step = 0; step < steps; ++step)
        {
            const float x = (float) area.getX() + labelW + step * cellW;
            auto cell = juce::Rectangle<float> (x, y, cellW - 1.0f, rowH - 1.0f).reduced (1.0f);

            g.setColour ((step / st.stepsPerBeat) % 2 == 0 ? juce::Colour (0xff202020)
                                                           : juce::Colour (0xff242424));
            g.fillRect (cell);

            const float v = p.velocity[(size_t) rows[row]][(size_t) step];
            if (v > 0.0f)
            {
                g.setColour (accent.withAlpha (0.30f + 0.70f * v));
                g.fillRoundedRectangle (cell, 2.0f);
            }

            if (step == current && drums.isPlaying())
            {
                g.setColour (juce::Colours::white.withAlpha (0.20f));
                g.fillRect (cell);
            }
        }
    }

    // Beat separators and playhead.
    for (int b = 1; b < st.beatsPerBar; ++b)
    {
        const float x = (float) area.getX() + labelW + (float) b * st.stepsPerBeat * cellW;
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawVerticalLine ((int) x, (float) (area.getY() + headerH), (float) area.getBottom());
    }

    if (current >= 0 && current < steps && drums.isPlaying())
    {
        const float x = (float) area.getX() + labelW + current * cellW;
        g.setColour (accent.withAlpha (0.85f));
        g.fillRect (juce::Rectangle<float> (x, (float) (area.getY() + headerH), 2.0f,
                                            (float) area.getHeight() - headerH));
    }

    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawRect (area);
}

void DrumPanel::timerCallback()
{
    updatePlayButton();
    repaint (patternArea);

    if (drums.isPlaying())
    {
        auto section = drums.isInCountIn() ? TR ("Count-in", "预备")
                       : drums.isInFill() ? TR ("Fill", "加花")
                       : drums.isChorus() ? TR ("Chorus", "副歌") : TR ("Verse", "主歌");
        status.setText (TR ("Playing", "播放中") + "  ·  " + section + "  ·  "
                            + juce::String (juce::roundToInt (drums.getBpm())) + " BPM",
                        juce::dontSendNotification);
    }
    else
    {
        status.setText (TR ("Stopped", "已停止"), juce::dontSendNotification);
    }
}

void DrumPanel::setStyle (int index)
{
    selectedStyle = juce::jlimit (0, (int) getDrumStyles().size() - 1, index);
    drums.setStyle (selectedStyle);
    applyStyleDefaults();
    updateStyleButtons();
    save();
    repaint (patternArea);
}

void DrumPanel::applyStyleDefaults()
{
    if (! juce::isPositiveAndBelow (selectedStyle, (int) getDrumStyles().size()))
        return;

    const auto& st = getDrumStyles()[(size_t) selectedStyle];
    bpmSlider.setValue (st.defaultBpm);
    swingSlider.setValue (st.defaultSwing * 100.0);
}

void DrumPanel::save()
{
    settings.setValue ("drumStyle", selectedStyle);
    settings.setValue ("drumChorus", drums.isChorus());
    settings.setValue ("drumBpm", bpmSlider.getValue());
    settings.setValue ("drumSwing", swingSlider.getValue());
    settings.setValue ("drumHumanize", humanizeSlider.getValue());
    settings.setValue ("drumVolume", volumeSlider.getValue());
    settings.setValue ("drumFillEvery", drums.getFillEvery());
    settings.setValue ("drumCountIn", countInToggle.getToggleState());
    settings.setValue ("drumMetronome", metronomeToggle.getToggleState());
    for (int i = 0; i < 5; ++i)
        settings.setValue ("drumMix" + juce::String (i), mixSliders[(size_t) i]->getValue());
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
    playButton.setColour (juce::TextButton::buttonColourId,
                          p ? juce::Colour (0xffa32d2d) : juce::Colour (0xff1d9e75));
}

void DrumPanel::updateStyleButtons()
{
    for (auto* b : styleButtons)
        b->setToggleState ((int) b->getProperties()["style"] == selectedStyle,
                           juce::dontSendNotification);
}

void DrumPanel::applyMixer()
{
    auto gain = [this] (int i)
    {
        return juce::Decibels::decibelsToGain ((float) mixSliders[(size_t) i]->getValue());
    };

    drums.setPieceGain (kick, gain (0));
    drums.setPieceGain (snare, gain (1));
    drums.setPieceGain (sideStick, gain (1));
    drums.setPieceGain (closedHat, gain (2));
    drums.setPieceGain (openHat, gain (2));
    drums.setPieceGain (tomHigh, gain (3));
    drums.setPieceGain (tomMid, gain (3));
    drums.setPieceGain (tomLow, gain (3));
    drums.setPieceGain (crash, gain (4));
    drums.setPieceGain (ride, gain (4));
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
