#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PitchDetector.h"
#include "Text.h"

// ---------------------------------------------------------------------------
// 竖向电平表：-60..0 dBFS，带峰值保持和削波灯
class LevelMeter : public juce::Component
{
public:
    explicit LevelMeter (juce::String labelText) : label (std::move (labelText)) {}

    // 每帧调用一次，peak 为线性幅度
    void push (float peak, bool clipped)
    {
        const float db = juce::Decibels::gainToDecibels (peak, minDb);
        levelDb = db > levelDb ? db : juce::jmax (minDb, levelDb - 1.2f);   // 回落速度

        if (db >= holdDb) { holdDb = db; holdFrames = 45; }
        else if (--holdFrames <= 0) holdDb = juce::jmax (minDb, holdDb - 1.0f);

        if (clipped) clipFrames = 60;
        else if (clipFrames > 0) --clipFrames;

        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        auto labelArea = area.removeFromBottom (20.0f);
        auto clipArea = area.removeFromTop (14.0f);
        area.removeFromTop (4.0f);

        g.setColour (clipFrames > 0 ? juce::Colour (0xffe24b4a) : juce::Colour (0xff3a3a3a));
        g.fillRoundedRectangle (clipArea, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (clipFrames > 0 ? 1.0f : 0.5f));
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("CLIP", clipArea, juce::Justification::centred);

        g.setColour (juce::Colour (0xff1e1e1e));
        g.fillRoundedRectangle (area, 4.0f);

        const auto toY = [&] (float db) { return area.getBottom() - area.getHeight() * (db - minDb) / -minDb; };

        auto bar = area.reduced (3.0f);
        const float top = toY (levelDb);
        auto filled = bar.withTop (juce::jmin (bar.getBottom(), top));

        juce::ColourGradient grad (juce::Colour (0xffe24b4a), bar.getX(), toY (0.0f),
                                   juce::Colour (0xff1d9e75), bar.getX(), toY (minDb), false);
        grad.addColour ((double) ((0.0f - (-12.0f)) / -minDb), juce::Colour (0xffef9f27));
        g.setGradientFill (grad);
        g.fillRect (filled);

        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.fillRect (bar.getX(), toY (holdDb) - 1.0f, bar.getWidth(), 2.0f);

        g.setColour (juce::Colours::white.withAlpha (0.25f));
        for (float db : { -6.0f, -12.0f, -24.0f, -48.0f })
            g.fillRect (area.getX(), toY (db), 4.0f, 1.0f);

        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (label, labelArea, juce::Justification::centred);
    }

private:
    static constexpr float minDb = -60.0f;
    juce::String label;
    float levelDb = minDb, holdDb = minDb;
    int holdFrames = 0, clipFrames = 0;
};

// ---------------------------------------------------------------------------
// 调音器：大字音名 + 音分刻度
class TunerDisplay : public juce::Component
{
public:
    void update (const PitchDetector::Result& r)
    {
        if (r.frequency > 0.0f && r.clarity > 0.6f)
        {
            const auto n = NoteInfo::fromFrequency (r.frequency);
            // 简单平滑，指针不乱跳
            smoothedCents = (n.midiNote == lastMidi) ? smoothedCents * 0.7f + n.cents * 0.3f : n.cents;
            lastMidi = n.midiNote;
            noteName = n.name;
            freq = r.frequency;
            silentFrames = 0;
        }
        else if (++silentFrames > 20)
        {
            noteName = {};
            lastMidi = -1;
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat().reduced (8.0f);
        g.setColour (juce::Colour (0xff1e1e1e));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

        const bool has = noteName.isNotEmpty();
        const bool inTune = has && std::abs (smoothedCents) < 5.0f;
        const auto accent = inTune ? juce::Colour (0xff1d9e75) : juce::Colour (0xffef9f27);

        auto top = area.removeFromTop (area.getHeight() * 0.62f);
        g.setColour (has ? (inTune ? accent : juce::Colours::white) : juce::Colours::white.withAlpha (0.3f));
        g.setFont (juce::FontOptions (juce::jmin (64.0f, top.getHeight() * 0.8f), juce::Font::bold));
        g.drawText (has ? noteName : juce::String ("--"), top, juce::Justification::centred);

        auto scale = area.removeFromTop (area.getHeight() * 0.55f).reduced (12.0f, 4.0f);
        const float cx = scale.getCentreX();

        g.setColour (juce::Colours::white.withAlpha (0.25f));
        for (int c = -50; c <= 50; c += 10)
        {
            const float x = cx + scale.getWidth() * 0.5f * (float) c / 50.0f;
            const float h = c == 0 ? scale.getHeight() : scale.getHeight() * 0.5f;
            g.fillRect (x - 0.5f, scale.getCentreY() - h * 0.5f, 1.0f, h);
        }

        if (has)
        {
            const float x = cx + scale.getWidth() * 0.5f * juce::jlimit (-50.0f, 50.0f, smoothedCents) / 50.0f;
            g.setColour (accent);
            g.fillRoundedRectangle (x - 3.0f, scale.getY(), 6.0f, scale.getHeight(), 3.0f);
        }

        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.setFont (juce::FontOptions (13.0f));
        const auto info = has ? juce::String (freq, 1) + " Hz   "
                                  + (smoothedCents >= 0 ? "+" : "") + juce::String (juce::roundToInt (smoothedCents)) + TR (" cents", " 音分")
                              : TR ("Play an open string", "弹一根空弦试试");
        g.drawText (info, area, juce::Justification::centred);
    }

private:
    juce::String noteName;
    float freq = 0.0f, smoothedCents = 0.0f;
    int lastMidi = -1, silentFrames = 100;
};
