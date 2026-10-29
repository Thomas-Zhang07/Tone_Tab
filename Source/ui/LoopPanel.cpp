#include "LoopPanel.h"
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
            return {};

        if (std::abs (bars - std::round (bars)) < 0.02)
            return juce::String ((int) std::lround (bars)) + TR (" bars", " 小节");
        return juce::String (bars, 2) + TR (" bars", " 小节");
    }
}

LoopPanel::LoopPanel (Looper& l, DrumMachine& d, juce::PropertiesFile& s)
    : looper (l), drums (d), settings (s)
{
    setWantsKeyboardFocus (true);

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

    // ---- grid / length / level ---------------------------------------------
    styleLabel (tempoLabel, 13.0f, muted);
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
    tempoSlider.onValueChange = [this] { drums.setBpm ((float) tempoSlider.getValue()); };
    tempoSlider.onDragEnd = [this] { save(); grabKeyboardFocus(); };
    addAndMakeVisible (tempoSlider);

    styleLabel (quantiseLabel, 13.0f, muted);
    quantiseLabel.setText (TR ("Snap to", "对齐到"), juce::dontSendNotification);
    addAndMakeVisible (quantiseLabel);

    quantiseBox.addItem (TR ("Bar (recommended)", "小节（推荐）"), 1);
    quantiseBox.addItem (TR ("Beat", "拍"), 2);
    quantiseBox.addItem (TR ("Off — free timing", "关 — 自由时间"), 3);
    quantiseBox.setTooltip (TR ("Start and close land on the nearest bar. You do not have to hit SPACE exactly on time.",
                                "开始和结束都会自动落在最近的小节线上，空格按早晚一点都没关系"));
    noKeyboardFocus (quantiseBox);
    quantiseBox.onChange = [this] { applyQuantise(); save(); grabKeyboardFocus(); };
    addAndMakeVisible (quantiseBox);

    styleLabel (lengthLabel, 13.0f, muted);
    lengthLabel.setText (TR ("New loop length", "新循环长度"), juce::dontSendNotification);
    addAndMakeVisible (lengthLabel);

    lengthBox.addItem (TR ("Auto — press again to close", "自动 — 再按一次结束"), 1);
    lengthBox.addItem (TR ("1 bar", "1 小节"), 2);
    lengthBox.addItem (TR ("2 bars", "2 小节"), 3);
    lengthBox.addItem (TR ("4 bars", "4 小节"), 4);
    lengthBox.addItem (TR ("8 bars", "8 小节"), 5);
    lengthBox.setTooltip (TR ("With a fixed length, one press records exactly that long and starts looping by itself.",
                              "固定长度时，只需按一次空格，录完自动开始循环"));
    noKeyboardFocus (lengthBox);
    lengthBox.onChange = [this] { applyFixedLength(); save(); grabKeyboardFocus(); };
    addAndMakeVisible (lengthBox);

    styleLabel (levelLabel, 13.0f, muted);
    levelLabel.setText (TR ("Loop level", "循环音量"), juce::dontSendNotification);
    addAndMakeVisible (levelLabel);

    levelSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    levelSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 22);
    levelSlider.setRange (-40.0, 6.0, 0.5);
    levelSlider.setTextValueSuffix (" dB");
    levelSlider.setDoubleClickReturnValue (true, -3.0);
    noKeyboardFocus (levelSlider);
    levelSlider.onValueChange = [this] { looper.setLevelDb ((float) levelSlider.getValue()); };
    levelSlider.onDragEnd = [this] { save(); grabKeyboardFocus(); };
    addAndMakeVisible (levelSlider);

    // ---- restore settings ---------------------------------------------------
    quantiseBox.setSelectedId (juce::jlimit (1, 3, settings.getIntValue ("loopQuantise", 1)),
                               juce::dontSendNotification);
    applyQuantise();

    const int fixedBars = settings.getIntValue ("loopFixedBars", 0);
    lengthBox.setSelectedId (fixedBars == 1 ? 2 : fixedBars == 2 ? 3
                             : fixedBars == 4 ? 4 : fixedBars == 8 ? 5 : 1,
                             juce::dontSendNotification);
    applyFixedLength();

    levelSlider.setValue (settings.getDoubleValue ("loopLevelDb", -3.0), juce::dontSendNotification);
    looper.setLevelDb ((float) levelSlider.getValue());

    hintText = TR ("SPACE: record, close the loop, then overdub. Start and close snap to the bar, "
                   "so a late or early press still makes a tight loop.  ESC stops.  Max 30 s.",
                   "空格：录音 → 结束循环 → 叠加录音。开始和结束都会自动对齐小节，"
                   "按早按晚都能得到干净的循环。Esc 停止。最长 30 秒。");

    updateControls();
    refresh();
    startTimerHz (30);
}

LoopPanel::~LoopPanel()
{
    stopTimer();
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
    static const int barsForId[] = { 0, 1, 2, 4, 8 };   // id 1 = auto
    const int id = juce::jlimit (1, 5, lengthBox.getSelectedId());
    looper.setFixedLengthBars (barsForId[id - 1]);
}

void LoopPanel::save()
{
    settings.setValue ("loopQuantise", quantiseBox.getSelectedId());
    settings.setValue ("loopFixedBars", looper.getFixedLengthBars());
    settings.setValue ("loopLevelDb", levelSlider.getValue());
    settings.setValue ("drumBpm", drums.getBpm());
}

void LoopPanel::toggleLoop()
{
    looper.toggle();
}

void LoopPanel::stopLoop()
{
    looper.stop();
}

bool LoopPanel::keyPressed (const juce::KeyPress& key)
{
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

    auto header = area.removeFromTop (40);
    title.setBounds (header.removeFromLeft (230));
    status.setBounds (header);

    area.removeFromTop (6);
    hintArea = area.removeFromBottom (46);
    area.removeFromBottom (8);

    controlsArea = area.removeFromRight (340);
    area.removeFromRight (16);
    loopDisplay = area;

    {
        auto display = loopDisplay.reduced (8);
        timelineArea = display.removeFromBottom (176);
        display.removeFromBottom (6);
        ringArea = display;
    }

    auto c = controlsArea.reduced (16);

    auto addControl = [&c] (juce::Label& label, juce::Component& control, int height)
    {
        label.setBounds (c.removeFromTop (18));
        control.setBounds (c.removeFromTop (height));
        c.removeFromTop (12);
    };

    auto buttons = c.removeFromBottom (104);
    mainButton.setBounds (buttons.removeFromTop (54));
    buttons.removeFromTop (8);
    const int half = (buttons.getWidth() - 8) / 2;
    stopButton.setBounds (buttons.removeFromLeft (half));
    buttons.removeFromLeft (8);
    clearButton.setBounds (buttons.removeFromLeft (half));

    addControl (tempoLabel, tempoSlider, 26);
    addControl (quantiseLabel, quantiseBox, 28);
    addControl (lengthLabel, lengthBox, 28);
    addControl (levelLabel, levelSlider, 26);

    c.removeFromBottom (6);
    tipsArea = c;
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
        case Looper::State::stopped:        return muted;
        case Looper::State::empty:
        default:                            return juce::Colour (0xff6f6f6f);
    }
}

void LoopPanel::paint (juce::Graphics& g)
{
    g.fillAll (bg);

    g.setColour (panel);
    g.fillRoundedRectangle (loopDisplay.toFloat().expanded (8.0f), 12.0f);
    g.fillRoundedRectangle (controlsArea.toFloat().expanded (8.0f), 12.0f);

    g.setColour (panelLine);
    g.drawLine ((float) hintArea.getX(), (float) hintArea.getY() - 2.0f,
                (float) hintArea.getRight(), (float) hintArea.getY() - 2.0f, 1.0f);

    g.setColour (muted);
    g.setFont (juce::FontOptions (12.5f));
    g.drawFittedText (hintText, hintArea, juce::Justification::topLeft, 2);

    // Tips: the timing guarantees, spelled out where they matter.
    if (tipsArea.getHeight() > 60)
    {
        auto t = tipsArea;
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (TR ("TIMING", "时间对齐"), t.removeFromTop (18), juce::Justification::topLeft);

        g.setColour (muted);
        g.setFont (juce::FontOptions (12.0f));
        const juce::StringArray tips {
            TR ("Close early → it keeps recording to the bar.",
                "按早就继续录到小节线。"),
            TR ("Close late → it trims back to the bar.",
                "按晚就自动修剪回小节线。"),
            TR ("Drums running → the loop starts on the next drum bar.",
                "鼓机在放 → 循环从下一小节开始。"),
            TR ("SPACE while playing adds another layer.",
                "循环中按空格可以继续叠加。")
        };

        for (int i = 0; i < tips.size() && t.getHeight() > 16; ++i)
        {
            auto row = t.removeFromTop (22);
            g.setColour (i == 0 ? accent : muted);
            g.fillEllipse ((float) row.getX(), (float) row.getCentreY() - 1.5f, 3.0f, 3.0f);
            g.setColour (muted);
            g.drawFittedText (tips[i], row.withTrimmedLeft (10), juce::Justification::centredLeft, 1);
        }
    }

    paintLoopDisplay (g, ringArea);
    paintTimeline (g, timelineArea);
}

void LoopPanel::paintTimeline (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (area.getWidth() < 160 || area.getHeight() < 80)
        return;

    const auto st = looper.getState();
    const auto colour = stateColour (st);
    const bool hasLoop = looper.hasLoop();
    const int bpb = juce::jmax (1, drums.getGridBeatsPerBar());
    const double fallbackBpm = juce::jlimit (30.0, 300.0, (double) drums.getBpm());
    const double loopBpm = looper.getLoopBpm() > 1.0 ? looper.getLoopBpm() : fallbackBpm;
    const double bpm = hasLoop && st != Looper::State::recording
                           && st != Looper::State::armingRecord
                       ? loopBpm : fallbackBpm;
    const double barSecs = 60.0 / bpm * (double) bpb;
    const double elapsed = looper.getRecordedSeconds();
    const double bucketSecs = juce::jmax (1.0e-4, looper.getWaveformBucketSeconds());

    const int labelW = 46;
    auto a = area;
    auto ruler = a.removeFromTop (18);
    auto liveLane = a.removeFromBottom (46);
    a.removeFromBottom (6);
    auto loopLane = a;

    auto loopFull = loopLane;
    auto liveFull = liveLane;
    auto loopLabelArea = loopLane.removeFromLeft (labelW);
    auto liveLabelArea = liveLane.removeFromLeft (labelW);

    // How much time the loop lane shows.
    double displaySecs = 0.0;
    int displayBars = 4;
    const int fixed = looper.getFixedLengthBars();

    if (st == Looper::State::recording)
    {
        if (fixed > 0)
        {
            displayBars = fixed;
            displaySecs = displayBars * barSecs;
        }
        else
        {
            displayBars = juce::jmax (2, (int) std::ceil (elapsed / juce::jmax (0.1, barSecs)));
            displaySecs = displayBars * barSecs;
        }
    }
    else if (hasLoop)
    {
        displaySecs = juce::jmax (0.05, looper.getLengthSeconds());
        displayBars = juce::jmax (1, (int) std::ceil (looper.getLoopBars() - 1.0e-6));
    }
    else
    {
        displayBars = 4;
        displaySecs = displayBars * barSecs;
    }

    displaySecs = juce::jmax (0.05, displaySecs);

    g.setColour (juce::Colour (0xff0f0f0f));
    g.fillRoundedRectangle (loopFull.toFloat(), 5.0f);
    g.fillRoundedRectangle (liveFull.toFloat(), 5.0f);

    g.setColour (muted);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText (TR ("LOOP", "循环"), loopLabelArea, juce::Justification::centredLeft);
    g.drawText (TR ("LIVE", "实时"), liveLabelArea, juce::Justification::centredLeft);

    // Bar ruler.
    const int laneX = loopLane.getX();
    const int laneW = loopLane.getWidth();
    const int barStep = displayBars > 10 ? 2 : 1;
    g.setFont (juce::FontOptions (10.0f));
    for (int bar = 0; bar <= displayBars; ++bar)
    {
        const float x = (float) laneX + (float) laneW
                        * (float) ((double) bar * barSecs / displaySecs);
        if (x > (float) loopFull.getRight() + 0.5f)
            break;

        g.setColour (juce::Colour (0xff383838));
        g.drawLine (x, (float) ruler.getBottom() - 7.0f, x, (float) loopFull.getBottom(), 1.0f);

        if (bar < displayBars && bar % barStep == 0)
        {
            g.setColour (muted);
            g.drawText (juce::String (bar + 1), (int) x + 3, ruler.getY(), 24,
                        ruler.getHeight(), juce::Justification::centredLeft);
        }
    }

    // Loop waveform.
    const int count = looper.getWaveformCount();
    const double recordedSecs = st == Looper::State::recording ? elapsed
                               : hasLoop ? looper.getLengthSeconds() : 0.0;
    float waveMax = 0.05f;
    for (int b = 0; b < count; ++b)
        waveMax = juce::jmax (waveMax, looper.getWaveformPeak (b));

    const int midY = loopLane.getCentreY();
    const int halfH = juce::jmax (2, loopLane.getHeight() / 2 - 4);

    if (count > 0 && recordedSecs > 0.0)
    {
        g.setColour (colour.withAlpha (0.9f));
        for (int x = 0; x < laneW; ++x)
        {
            const double t0 = (double) x / (double) laneW * displaySecs;
            if (t0 >= recordedSecs)
                break;

            const double t1 = (double) (x + 1) / (double) laneW * displaySecs;
            int b0 = juce::jlimit (0, count - 1, (int) std::floor (t0 / bucketSecs));
            int b1 = juce::jlimit (0, count - 1, (int) std::ceil (t1 / bucketSecs));

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

        if (st == Looper::State::empty)
        {
            g.setColour (muted);
            g.setFont (juce::FontOptions (11.5f));
            g.drawText (TR ("No loop yet — press SPACE", "还没有循环 — 按空格录音"),
                        loopLane, juce::Justification::centred);
        }
        else if (st == Looper::State::armingRecord)
        {
            g.setColour (amber);
            g.setFont (juce::FontOptions (11.5f));
            g.drawText (TR ("Waiting for the bar…", "等待下一小节…"),
                        loopLane, juce::Justification::centred);
        }
    }

    // Playhead / recording head.
    if (loopLane.getWidth() > 0)
    {
        float head = -1.0f;
        if (st == Looper::State::recording)
            head = (float) laneX + (float) laneW
                   * (float) juce::jlimit (0.0, 1.0, elapsed / displaySecs);
        else if (hasLoop)
            head = (float) laneX + (float) laneW
                   * (float) juce::jlimit (0.0, 1.0, looper.getPosition());

        if (head >= 0.0f)
        {
            g.setColour (st == Looper::State::stopped ? colour.withAlpha (0.35f) : colour);
            g.drawLine (head, (float) loopLane.getY() + 3.0f,
                        head, (float) loopLane.getBottom() - 3.0f, 1.6f);
        }
    }

    // ---- live input lane ----------------------------------------------------
    const double windowSecs = juce::jlimit (2.0, 8.0, barSecs * 4.0);
    const int windowBuckets = juce::jmax (8, (int) std::lround (windowSecs / bucketSecs));
    const uint32_t end = looper.getLiveWritePosition();
    const uint32_t available = end;
    const uint32_t span = (uint32_t) juce::jmin ((uint32_t) windowBuckets,
                                                 (uint32_t) juce::jmin<uint32_t> (
                                                     available, (uint32_t) Looper::liveBuckets));

    const int liveX = liveLane.getX();
    const int liveW = liveLane.getWidth();
    const int liveMidY = liveLane.getCentreY();
    const int liveHalfH = juce::jmax (2, liveLane.getHeight() / 2 - 4);

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

    // "Now" edge of the live lane.
    g.setColour (accent.withAlpha (0.65f));
    g.drawLine ((float) liveFull.getRight() - 1.0f, (float) liveFull.getY() + 3.0f,
                (float) liveFull.getRight() - 1.0f, (float) liveFull.getBottom() - 3.0f, 1.0f);
}

void LoopPanel::paintLoopDisplay (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (area.getWidth() < 120 || area.getHeight() < 120)
        return;

    const auto st = looper.getState();
    const auto colour = stateColour (st);
    const bool hasLoop = looper.hasLoop();
    const int bpb = juce::jmax (1, drums.getGridBeatsPerBar());
    const auto areaF = area.toFloat();
    const float size = juce::jmin (areaF.getWidth(), areaF.getHeight()) - 26.0f;
    const auto circle = juce::Rectangle<float> (size, size).withCentre (areaF.getCentre());
    const auto centre = circle.getCentre();
    const float radius = size * 0.5f;

    g.setColour (juce::Colour (0xff121212));
    g.fillEllipse (circle);

    // Bar ticks.
    const double bars = hasLoop && looper.getLoopBars() > 0.0
                            ? juce::jmax (1.0, std::round (looper.getLoopBars()))
                            : 4.0;
    const int divisions = juce::jlimit (4, 32, (int) std::lround (bars * bpb));
    for (int i = 0; i < divisions; ++i)
    {
        const float a = -juce::MathConstants<float>::halfPi
                        + juce::MathConstants<float>::twoPi * (float) i / (float) divisions;
        const float inner = radius - (i % bpb == 0 ? 23.0f : 18.0f);
        const float outer = radius - 12.0f;
        g.setColour (i % bpb == 0 ? juce::Colour (0xff5a5a5a) : juce::Colour (0xff3d3d3d));
        g.drawLine (centre.x + std::cos (a) * inner, centre.y + std::sin (a) * inner,
                    centre.x + std::cos (a) * outer, centre.y + std::sin (a) * outer,
                    i % bpb == 0 ? 1.6f : 1.0f);
    }

    // Track.
    {
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius - 7.0f, radius - 7.0f, 0.0f,
                             0.0f, juce::MathConstants<float>::twoPi, true);
        g.setColour (juce::Colour (0xff2e2e2e));
        g.strokePath (track, juce::PathStrokeType (10.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    double progress = -1.0;
    if (st == Looper::State::playing || st == Looper::State::armedOverdub
        || st == Looper::State::overdubbing)
        progress = looper.getPosition();
    else if (st == Looper::State::recording)
        progress = std::fmod (looper.getRecordedSeconds() * 2.0, 1.0);   // moving activity arc
    else if (st == Looper::State::armingRecord || st == Looper::State::armingPlay)
        progress = std::fmod (juce::Time::getMillisecondCounterHiRes() * 0.0015, 1.0);
    else if (hasLoop)
        progress = 0.0;

    if (progress >= 0.0)
    {
        // Path arcs measure clockwise from 12 o'clock; the playhead dot below
        // uses normal maths angles, so the arc starts at 0, not -pi/2.
        const float end = juce::MathConstants<float>::twoPi * (float) progress;
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, radius - 7.0f, radius - 7.0f, 0.0f,
                           0.0f, end, true);
        g.setColour (colour);
        g.strokePath (arc, juce::PathStrokeType (10.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

        const float a = -juce::MathConstants<float>::halfPi + juce::MathConstants<float>::twoPi * (float) progress;
        const float dotX = centre.x + std::cos (a) * (radius - 7.0f);
        const float dotY = centre.y + std::sin (a) * (radius - 7.0f);
        g.setColour (colour.brighter (0.35f));
        g.fillEllipse (dotX - 7.0f, dotY - 7.0f, 14.0f, 14.0f);
    }

    // Layer rings.
    const int layers = looper.getLayerCount();
    for (int i = 0; i < juce::jmin (layers, 5); ++i)
    {
        g.setColour (colour.withAlpha (0.10f + 0.05f * (float) i));
        g.drawEllipse (circle.reduced (30.0f + 7.0f * (float) i), 1.0f);
    }

    // Centre text.
    juce::String big, line1, line2;
    switch (st)
    {
        case Looper::State::empty:
            big = TR ("READY", "就绪");
            line1 = TR ("Press SPACE to record", "按空格开始录音");
            line2 = juce::String (juce::roundToInt (drums.getBpm())) + " BPM";
            break;

        case Looper::State::armingRecord:
        case Looper::State::armingPlay:
            big = TR ("WAITING", "等待");
            line1 = TR ("Starts on the next bar", "下一小节开始");
            line2 = juce::String (juce::roundToInt (drums.getBpm())) + " BPM";
            break;

        case Looper::State::recording:
            big = TR ("RECORDING", "录音中");
            line1 = juce::String (looper.getRecordedSeconds(), 2) + " s";
            line2 = looper.getFixedLengthBars() > 0
                        ? (juce::String (looper.getFixedLengthBars()) + TR (" bar take", " 小节"))
                        : TR ("SPACE closes at the bar", "按空格在小节线结束");
            break;

        case Looper::State::playing:
            big = TR ("PLAYING", "循环中");
            line1 = barsText (looper.getLoopBars());
            line2 = juce::String (looper.getLengthSeconds(), 2) + " s  ·  "
                    + juce::String (juce::roundToInt (looper.getLoopBpm())) + " BPM";
            break;

        case Looper::State::armedOverdub:
            big = TR ("OVERDUB", "叠加");
            line1 = TR ("Starts at the loop start", "从循环起点开始");
            line2 = TR ("SPACE cancels", "按空格取消");
            break;

        case Looper::State::overdubbing:
            big = TR ("OVERDUBBING", "叠加录音");
            line1 = TR ("Layer ", "第 ") + juce::String (layers) + TR ("", " 层");
            line2 = TR ("SPACE finishes", "按空格结束");
            break;

        case Looper::State::stopped:
            big = TR ("STOPPED", "已停止");
            line1 = barsText (looper.getLoopBars());
            line2 = TR ("SPACE plays", "按空格播放");
            break;
    }

    auto textArea = circle.reduced (radius * 0.42f).toNearestInt();

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (juce::jlimit (18.0f, 30.0f, radius * 0.16f), juce::Font::bold));
    g.drawText (big, textArea.removeFromTop (textArea.getHeight() / 3),
                juce::Justification::centredBottom, false);

    g.setColour (juce::Colours::white.withAlpha (0.88f));
    g.setFont (juce::FontOptions (juce::jlimit (13.0f, 18.0f, radius * 0.10f)));
    g.drawText (line1, textArea.removeFromTop (textArea.getHeight() / 2),
                juce::Justification::centred, false);

    g.setColour (muted);
    g.setFont (juce::FontOptions (juce::jlimit (11.0f, 14.0f, radius * 0.075f)));
    g.drawText (line2, textArea, juce::Justification::centredTop, false);

    // Sync badge.
    if (drums.isPlaying())
    {
        auto badge = juce::Rectangle<int> (110, 22)
                         .withCentre ({ (int) centre.x, (int) (centre.y + radius * 0.72f) });
        g.setColour (accent.withAlpha (0.12f));
        g.fillRoundedRectangle (badge.toFloat(), 11.0f);
        g.setColour (accent);
        g.setFont (juce::FontOptions (11.5f));
        g.drawText (TR ("SYNC · DRUMS", "同步 · 鼓机"), badge, juce::Justification::centred);
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

    const auto st = looper.getState();
    if (st != lastState)
    {
        lastState = st;
        updateControls();
    }

    juce::String s;
    const bool synced = drums.isPlaying();

    switch (st)
    {
        case Looper::State::empty:
            s = TR ("Ready", "就绪") + (synced ? TR ("  ·  synced to drums", "  ·  同步鼓机") : juce::String());
            break;
        case Looper::State::armingRecord:
            s = TR ("Waiting for the next bar to record…", "等待下一小节开始录音…");
            break;
        case Looper::State::recording:
            s = TR ("Recording ", "录音中 ") + juce::String (looper.getRecordedSeconds(), 2) + " s";
            break;
        case Looper::State::playing:
            s = TR ("Playing", "循环中") + "  ·  " + barsText (looper.getLoopBars())
                + "  ·  " + TR ("layer ", "第 ") + juce::String (looper.getLayerCount())
                + TR ("", " 层")
                + (synced ? TR ("  ·  synced", "  ·  同步") : juce::String());
            break;
        case Looper::State::armedOverdub:
            s = TR ("Overdub armed — starts at the loop start", "叠加已就绪 — 从循环起点开始");
            break;
        case Looper::State::overdubbing:
            s = TR ("Overdubbing — layer ", "叠加录音 — 第 ") + juce::String (looper.getLayerCount())
                + TR ("", " 层");
            break;
        case Looper::State::stopped:
            s = TR ("Stopped", "已停止") + (looper.hasLoop() ? "  ·  " + barsText (looper.getLoopBars())
                                                              : juce::String());
            break;
        case Looper::State::armingPlay:
            s = TR ("Waiting for the next bar to play…", "等待下一小节开始播放…");
            break;
    }

    if (s != statusText)
    {
        statusText = s;
        status.setText (s, juce::dontSendNotification);
    }

    repaint (loopDisplay);
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
            setMain (TR ("CANCEL", "取消"), amber);
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
    }

    const bool busy = st != Looper::State::empty && st != Looper::State::stopped;
    stopButton.setEnabled (busy);
    clearButton.setEnabled (looper.hasLoop() || st == Looper::State::recording
                            || st == Looper::State::armingRecord);

    stopButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff303030));
    clearButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff303030));
}
