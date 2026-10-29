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
    caption (bluesSoulLabel, TR ("Blues / Soul", "蓝调 / 灵魂乐"));
    caption (otherLabel, TR ("Other", "其他"));
    for (auto* l : { &rockPopLabel, &grooveLabel, &metalLabel, &bluesSoulLabel, &otherLabel })
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

    for (int i : { 0, 1, 5 })   addStyle (i);        // Rock, Pop, Punk
    for (int i : { 3, 4, 8 })   addStyle (i);        // Funk, Disco, Reggae
    for (int i : { 6, 7, 9, 10, 16 }) addStyle (i);  // Metal, Blues, Jazz, Waltz, Blues Rock
    for (int i : { 12, 13, 14, 15 }) addStyle (i);   // Slow Blues, Mayer Soul/Shuffle, Texas
    for (int i : { 2, 11, 17 })     addStyle (i);    // Ballad, Click only, Custom

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

    // ---- Custom pattern editing -----------------------------------------------
    editPartBox.addItem (TR ("Edit: Verse", "编辑：主歌"), 1);
    editPartBox.addItem (TR ("Edit: Chorus", "编辑：副歌"), 2);
    editPartBox.addItem (TR ("Edit: Fill", "编辑：加花"), 3);
    editPartBox.setSelectedId (1, juce::dontSendNotification);
    editPartBox.setTooltip (TR ("Which part of the Custom groove the step grid edits "
                                "(Verse/Chorus also switches playback; use Fill Now to hear the fill)",
                                "步进格编辑自定义节奏的哪个段落"
                                "（主歌/副歌会同时切换播放；加花用“手动加花”试听）"));
    editPartBox.onChange = [this]
    {
        if (selectedStyle == getCustomDrumStyleIndex())
        {
            const int part = editPartBox.getSelectedId();
            if (part == 1 || part == 2)
            {
                const bool chorus = part == 2;
                drums.setChorus (chorus);
                (chorus ? chorusButton : verseButton).setToggleState (true, juce::dontSendNotification);
            }
        }
        updateEditUi();
        save();
        repaint (patternArea);
    };
    addAndMakeVisible (editPartBox);

    clearPartButton.setButtonText (TR ("Clear Part", "清空"));
    clearPartButton.setTooltip (TR ("Remove every hit in the selected part",
                                    "删除所选段落的全部音符"));
    clearPartButton.onClick = [this] { clearEditPart(); };
    addAndMakeVisible (clearPartButton);

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
    loadCustomDrums();
    editPartBox.setSelectedId (juce::jlimit (1, 3, settings.getIntValue ("drumEditPart", 1)),
                               juce::dontSendNotification);
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
    updateEditUi();
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
    const int topH = juce::jlimit (244, 300, (int) (area.getHeight() * 0.45f));
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
    placeRow (6, 5);
    l.removeFromTop (2);
    bluesSoulLabel.setBounds (l.removeFromTop (16));
    placeRow (11, 4);
    l.removeFromTop (2);
    otherLabel.setBounds (l.removeFromTop (16));
    placeRow (15, 3);

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
    auto feelTop = r.removeFromTop (20);
    feelHeader.setBounds (feelTop.removeFromLeft (110));
    clearPartButton.setBounds (feelTop.removeFromRight (74).reduced (0, 1));
    feelTop.removeFromRight (4);
    editPartBox.setBounds (feelTop.removeFromRight (150).reduced (0, 1));

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

    // When the Custom style is selected the grid shows the part being edited;
    // otherwise it follows the playing section.
    const bool editingCustom = selectedStyle == getCustomDrumStyleIndex();
    const int editPart = juce::jlimit (1, 3, editPartBox.getSelectedId());
    const auto& p = editingCustom
                        ? (editPart == 2 ? st.chorus : editPart == 3 ? st.fill : st.verse)
                        : (drums.isInFill() ? st.fill
                                            : (drums.isChorus() ? st.chorus : st.verse));

    const DrumInstrument rows[] = { kick, snare, sideStick, closedHat, openHat, crash,
                                    ride, tomHigh, tomMid, tomLow };
    const char* names[] = { "Kick", "Snare", "Rim", "Hat", "Open", "Crash",
                            "Ride", "Tom H", "Tom M", "Tom L" };
    const int rowCount = 10;
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

bool DrumPanel::cellAt (juce::Point<int> position, int& row, int& step) const
{
    if (! patternArea.contains (position))
        return false;

    const auto& styles = getDrumStyles();
    const auto& st = styles[(size_t) juce::jlimit (0, (int) styles.size() - 1, selectedStyle)];
    const int steps = juce::jlimit (1, 16, st.beatsPerBar * st.stepsPerBeat);

    constexpr int headerH = 22, labelW = 56, rowCount = 10;
    const int x = position.x - patternArea.getX();
    const int y = position.y - patternArea.getY();
    if (x < labelW || y < headerH)
        return false;

    const float cellW = (float) juce::jmax (1, patternArea.getWidth() - labelW) / (float) steps;
    const float rowH = (float) (patternArea.getHeight() - headerH) / (float) rowCount;
    if (cellW <= 0.0f || rowH <= 0.0f)
        return false;

    step = (int) ((float) (x - labelW) / cellW);
    row = (int) ((float) (y - headerH) / rowH);
    return step >= 0 && step < steps && row >= 0 && row < rowCount;
}

DrumPattern* DrumPanel::editPattern()
{
    auto& custom = getCustomDrumStyle();
    switch (juce::jlimit (1, 3, editPartBox.getSelectedId()))
    {
        case 2:  return &custom.chorus;
        case 3:  return &custom.fill;
        default: return &custom.verse;
    }
}

void DrumPanel::copySelectedStyleToCustom()
{
    const int customIndex = getCustomDrumStyleIndex();
    if (selectedStyle == customIndex)
        return;

    const DrumStyle source = getDrumStyles()[(size_t) selectedStyle];
    auto& custom = getCustomDrumStyle();
    custom = source;
    custom.name = TR ("Custom", "自定义");
    custom.clickOnly = false;
    // One editable pattern per section: clear the alternating B bars so the
    // grid matches what plays on every bar.
    custom.verseB = DrumPattern {};
    custom.chorusB = DrumPattern {};

    selectedStyle = customIndex;
    drums.setStyle (customIndex);
    bpmSlider.setValue (custom.defaultBpm);
    swingSlider.setValue (custom.defaultSwing * 100.0);
    updateStyleButtons();
    updateEditUi();
}

void DrumPanel::mouseDown (const juce::MouseEvent& e)
{
    int row = 0, step = 0;
    if (! cellAt (e.getPosition(), row, step))
        return;

    // Editing a built-in pattern makes a Custom copy first, so the original
    // style is never destroyed. Match the part that was on screen.
    if (selectedStyle != getCustomDrumStyleIndex())
    {
        const int shownPart = drums.isInFill() ? 3 : (drums.isChorus() ? 2 : 1);
        editPartBox.setSelectedId (shownPart, juce::dontSendNotification);
        copySelectedStyleToCustom();
    }

    auto* p = editPattern();
    if (p == nullptr || step >= p->steps)
        return;

    static const DrumInstrument rows[] = { kick, snare, sideStick, closedHat, openHat,
                                           crash, ride, tomHigh, tomMid, tomLow };
    auto& v = p->velocity[(size_t) rows[row]][(size_t) step];

    // Left click toggles a hit on/off; Shift sets an accent, Alt a ghost note;
    // right click clears the cell. Every click changes the cell immediately.
    if (e.mods.isRightButtonDown())
        v = 0.0f;
    else if (e.mods.isShiftDown())
        v = v > 0.9f ? 0.7f : 1.0f;
    else if (e.mods.isAltDown())
        v = (v > 0.1f && v < 0.5f) ? 0.7f : 0.28f;
    else
        v = v > 0.0f ? 0.0f : 0.7f;

    save();
    repaint (patternArea);
}

void DrumPanel::clearEditPart()
{
    if (selectedStyle != getCustomDrumStyleIndex())
        copySelectedStyleToCustom();

    if (auto* p = editPattern())
        for (auto& instrument : p->velocity)
            for (auto& v : instrument)
                v = 0.0f;

    save();
    repaint (patternArea);
}

void DrumPanel::updateEditUi()
{
    const bool custom = selectedStyle == getCustomDrumStyleIndex();
    patternHeader.setText (custom ? TR ("Pattern — click on/off · Shift accent · Alt ghost",
                                       "节奏型 — 左键开关 · Shift 重音 · Alt 鬼音")
                                  : TR ("Pattern — click to copy into Custom", "节奏型 — 点击会复制到自定义"),
                           juce::dontSendNotification);
    clearPartButton.setEnabled (custom);
}

void DrumPanel::loadCustomDrums()
{
    const auto text = settings.getValue ("customDrums");
    if (text.isEmpty())
        return;

    auto xml = juce::parseXML (text);
    if (xml == nullptr)
        return;

    auto& s = getCustomDrumStyle();
    s.beatsPerBar = juce::jlimit (1, 12, xml->getIntAttribute ("beatsPerBar", s.beatsPerBar));
    s.stepsPerBeat = juce::jlimit (1, 4, xml->getIntAttribute ("stepsPerBeat", s.stepsPerBeat));
    s.defaultBpm = (float) juce::jlimit (30.0, 300.0, xml->getDoubleAttribute ("bpm", (double) s.defaultBpm));
    s.defaultSwing = (float) juce::jlimit (0.0, 1.0, xml->getDoubleAttribute ("swing", (double) s.defaultSwing));
    s.clickOnly = false;
    s.verseB = DrumPattern {};
    s.chorusB = DrumPattern {};

    auto loadPart = [&xml] (const char* tag, DrumPattern& p)
    {
        auto* part = xml->getChildByName (tag);
        if (part == nullptr)
            return;

        p = DrumPattern {};
        p.steps = juce::jlimit (1, 16, part->getIntAttribute ("steps", 16));
        for (auto* inst : part->getChildWithTagNameIterator ("Inst"))
        {
            const int index = inst->getIntAttribute ("index", -1);
            if (! juce::isPositiveAndBelow (index, (int) clickHigh))
                continue;

            const auto values = juce::StringArray::fromTokens (inst->getStringAttribute ("v"), ",", "");
            for (int i = 0; i < values.size() && i < 16; ++i)
                p.velocity[(size_t) index][(size_t) i] = juce::jlimit (0.0f, 1.0f, values[i].getFloatValue());
        }
    };

    loadPart ("verse", s.verse);
    loadPart ("chorus", s.chorus);
    loadPart ("fill", s.fill);
}

void DrumPanel::saveCustomDrums()
{
    const auto& s = getCustomDrumStyle();
    juce::XmlElement xml ("CustomDrums");
    xml.setAttribute ("beatsPerBar", s.beatsPerBar);
    xml.setAttribute ("stepsPerBeat", s.stepsPerBeat);
    xml.setAttribute ("bpm", (double) s.defaultBpm);
    xml.setAttribute ("swing", (double) s.defaultSwing);

    auto addPart = [&xml] (const char* tag, const DrumPattern& p)
    {
        auto* part = xml.createNewChildElement (tag);
        part->setAttribute ("steps", p.steps);

        const int n = juce::jlimit (0, 16, p.steps);
        for (int inst = 0; inst < (int) clickHigh; ++inst)
        {
            juce::StringArray values;
            bool any = false;
            for (int i = 0; i < n; ++i)
            {
                const float v = p.velocity[(size_t) inst][(size_t) i];
                any = any || v > 0.0f;
                values.add (juce::String (v, 3));
            }

            if (! any)
                continue;

            auto* e = part->createNewChildElement ("Inst");
            e->setAttribute ("index", inst);
            e->setAttribute ("v", values.joinIntoString (","));
        }
    };

    addPart ("verse", s.verse);
    addPart ("chorus", s.chorus);
    addPart ("fill", s.fill);

    settings.setValue ("customDrums", xml.toString (juce::XmlElement::TextFormat().singleLine()));
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

    // Selecting Custom shows one part at a time; make playback follow it so
    // edits are audible immediately.
    if (selectedStyle == getCustomDrumStyleIndex())
    {
        const int part = juce::jlimit (1, 3, editPartBox.getSelectedId());
        if (part == 1 || part == 2)
        {
            const bool chorus = part == 2;
            drums.setChorus (chorus);
            (chorus ? chorusButton : verseButton).setToggleState (true, juce::dontSendNotification);
        }
    }

    updateEditUi();
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
    settings.setValue ("drumEditPart", editPartBox.getSelectedId());
    for (int i = 0; i < 5; ++i)
        settings.setValue ("drumMix" + juce::String (i), mixSliders[(size_t) i]->getValue());
    saveCustomDrums();
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
