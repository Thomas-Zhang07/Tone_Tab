#include "PedalEditor.h"
#include "fx/BuiltinEffects.h"
#include "EffectStyle.h"
#include "fx/NamProcessor.h"
#include "Text.h"

namespace
{
    // 旋钮 / 开关的外观：黑色旋钮帽 + 白色指针 + 外圈刻度弧
    class PedalLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        explicit PedalLookAndFeel (juce::Colour bodyColour)
            : ink (EffectStyle::textOn (bodyColour)),
              // 很深的外壳（哇音、音箱等）用银色旋钮，不然看不清
              darkBody (bodyColour.getPerceivedBrightness() < 0.24f)
        {
            setColour (juce::ComboBox::backgroundColourId, juce::Colours::black.withAlpha (0.35f));
            setColour (juce::ComboBox::textColourId, juce::Colours::white);
            setColour (juce::ComboBox::outlineColourId, juce::Colours::black.withAlpha (0.4f));
            setColour (juce::ComboBox::arrowColourId, juce::Colours::white);
            setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff222222));
            setColour (juce::ToggleButton::textColourId, ink);
            setColour (juce::ToggleButton::tickColourId, ink);
            setColour (juce::ToggleButton::tickDisabledColourId, ink.withAlpha (0.5f));
            setColour (juce::Slider::trackColourId, ink.withAlpha (0.85f));
            setColour (juce::Slider::backgroundColourId, juce::Colours::black.withAlpha (0.3f));
            setColour (juce::Slider::thumbColourId, juce::Colour (0xff111111));
        }

        void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                               float startAngle, float endAngle, juce::Slider&) override
        {
            const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
            const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
            const auto c = bounds.getCentre();
            const float angle = startAngle + pos * (endAngle - startAngle);

            // 外圈刻度
            juce::Path track;
            track.addCentredArc (c.x, c.y, radius, radius, 0.0f, startAngle, endAngle, true);
            g.setColour (ink.withAlpha (0.22f));
            g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            juce::Path value;
            value.addCentredArc (c.x, c.y, radius, radius, 0.0f, startAngle, angle, true);
            g.setColour (ink.withAlpha (0.9f));
            g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // 旋钮帽
            const float kr = radius - 9.0f;
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillEllipse (c.x - kr + 1.5f, c.y - kr + 3.0f, kr * 2.0f, kr * 2.0f);
            g.setColour (darkBody ? juce::Colour (0xffc9ccd1) : juce::Colour (0xff141414));
            g.fillEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f);
            g.setColour (darkBody ? juce::Colour (0xff8d9096) : juce::Colour (0xff3a3a3a));
            g.drawEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f, 1.5f);

            // 指针
            juce::Path pointer;
            pointer.addRoundedRectangle (-1.75f, -kr + 3.0f, 3.5f, kr * 0.55f, 1.5f);
            g.setColour (darkBody ? juce::Colour (0xff141414) : juce::Colours::white);
            g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
        }

        void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos, float, float,
                               juce::Slider::SliderStyle style, juce::Slider& s) override
        {
            if (style != juce::Slider::LinearVertical)
            {
                juce::LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, sliderPos, 0, 0, style, s);
                return;
            }
            // 图示均衡的推子
            const float cx = (float) x + (float) w * 0.5f;
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillRoundedRectangle (cx - 3.0f, (float) y, 6.0f, (float) h, 3.0f);
            g.setColour (ink.withAlpha (0.35f));
            g.fillRect (cx - 10.0f, (float) y + (float) h * 0.5f - 0.5f, 20.0f, 1.0f);   // 0 dB 刻度
            g.setColour (juce::Colour (0xff141414));
            g.fillRoundedRectangle (cx - 13.0f, sliderPos - 7.0f, 26.0f, 14.0f, 3.0f);
            g.setColour (juce::Colours::white);
            g.fillRect (cx - 9.0f, sliderPos - 0.75f, 18.0f, 1.5f);
        }

        juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (13.0f); }

    private:
        juce::Colour ink;
        bool darkBody = false;
    };

    juce::String formatValue (const juce::RangedAudioParameter& p)
    {
        const float v = p.convertFrom0to1 (p.getValue());
        if (dynamic_cast<const juce::AudioParameterInt*> (&p) != nullptr)
            return juce::String ((int) std::lround (v)) + (p.getLabel().isNotEmpty() ? " " + p.getLabel() : juce::String());

        const auto& range = p.getNormalisableRange();
        const float span = range.end - range.start;
        juce::String s;
        if (span <= 1.001f && range.start >= 0.0f && p.getLabel().isEmpty())
            s = juce::String (juce::roundToInt (v * 100.0f)) + "%";                 // 0..1 的参数显示成百分比
        else
        {
            const int decimals = span >= 100.0f ? 0 : (span >= 10.0f ? 1 : 2);
            s = juce::String (v, decimals);
            if (p.getLabel().startsWith (":"))
                s << p.getLabel();                       // 压缩比显示成 4.0:1
            else if (p.getLabel().isNotEmpty())
                s << " " << p.getLabel();
        }
        return s;
    }
}

// =============================================================================
class PedalEditorOverlay::Pedal : public juce::Component, private juce::Timer
{
public:
    Pedal (EffectSlot& s, const Callbacks& cb) : slot (s), callbacks (cb)
    {
        body = EffectStyle::colourFor (slot);
        ink = EffectStyle::textOn (body);
        lnf = std::make_unique<PedalLookAndFeel> (body);
        setLookAndFeel (lnf.get());

        auto& proc = slot.getProcessor();
        const bool isGeq = slot.kind == EffectSlot::Kind::builtin && slot.typeId == "geq";

        for (auto* p : proc.getParameters())
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
            if (rp == nullptr)
                continue;

            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (rp))
            {
                auto c = std::make_unique<Control>();
                c->param = rp;
                c->combo = std::make_unique<juce::ComboBox>();
                c->combo->addItemList (choice->choices, 1);
                c->comboAttachment = std::make_unique<juce::ComboBoxParameterAttachment> (*choice, *c->combo);
                addAndMakeVisible (*c->combo);
                addLabel (*c, rp->getName (32), 13.0f);
                choices.push_back (std::move (c));
            }
            else if (auto* b = dynamic_cast<juce::AudioParameterBool*> (rp))
            {
                auto c = std::make_unique<Control>();
                c->param = rp;
                c->toggle = std::make_unique<juce::ToggleButton> (rp->getName (32));
                c->buttonAttachment = std::make_unique<juce::ButtonParameterAttachment> (*b, *c->toggle);
                addAndMakeVisible (*c->toggle);
                toggles.push_back (std::move (c));
            }
            else
            {
                auto c = std::make_unique<Control>();
                c->param = rp;
                const bool fader = isGeq && rp->getParameterID().startsWith ("b");
                c->slider = std::make_unique<juce::Slider> (fader ? juce::Slider::LinearVertical
                                                                  : juce::Slider::RotaryHorizontalVerticalDrag,
                                                            juce::Slider::NoTextBox);
                c->slider->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                                juce::MathConstants<float>::pi * 2.75f, true);
                c->slider->setMouseDragSensitivity (220);
                c->sliderAttachment = std::make_unique<juce::SliderParameterAttachment> (*rp, *c->slider);
                c->slider->setDoubleClickReturnValue (true, rp->convertFrom0to1 (rp->getDefaultValue()));
                c->slider->setPopupMenuEnabled (false);
                c->slider->setTooltip (TR ("Drag to adjust; double-click to reset", "拖动调节，双击恢复默认"));
                addAndMakeVisible (*c->slider);
                addLabel (*c, rp->getName (32), 13.0f);
                c->value = std::make_unique<juce::Label>();
                c->value->setFont (juce::FontOptions (12.0f));
                c->value->setJustificationType (juce::Justification::centred);
                c->value->setColour (juce::Label::textColourId, ink.withAlpha (0.8f));
                c->value->setInterceptsMouseClicks (false, false);
                addAndMakeVisible (*c->value);
                (fader ? faders : knobs).push_back (std::move (c));
            }
        }

        // NAM / IR：显示文件名和“更换”按钮
        if (slot.kind == EffectSlot::Kind::nam || slot.kind == EffectSlot::Kind::ir)
        {
            fileLabel.setText (juce::File (slot.typeId).getFileName(), juce::dontSendNotification);
            fileLabel.setFont (juce::FontOptions (12.5f));
            fileLabel.setColour (juce::Label::textColourId, ink.withAlpha (0.85f));
            fileLabel.setMinimumHorizontalScale (0.7f);
            addAndMakeVisible (fileLabel);
            replaceButton.setButtonText (slot.kind == EffectSlot::Kind::nam ? TR ("Change Model…", "更换模型…") : TR ("Change IR…", "更换 IR…"));
            replaceButton.onClick = [this] { if (callbacks.replaceFile) callbacks.replaceFile(); };
            addAndMakeVisible (replaceButton);
        }

        closeButton.setButtonText (juce::String::fromUTF8 ("\xc3\x97"));
        closeButton.setColour (juce::TextButton::buttonColourId, juce::Colours::black.withAlpha (0.25f));
        closeButton.setColour (juce::TextButton::textColourOffId, ink);
        closeButton.onClick = [this] { if (callbacks.close) callbacks.close(); };
        closeButton.setTooltip (TR ("Close (Esc)", "关闭（Esc）"));
        addAndMakeVisible (closeButton);

        computeSize();
        refreshValues();
        startTimerHz (15);
    }

    ~Pedal() override
    {
        stopTimer();
        // 先拆掉附着在参数上的控件，再换回默认外观
        choices.clear(); toggles.clear(); knobs.clear(); faders.clear();
        setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        // 外壳 + 阴影
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (r.translated (0.0f, 6.0f).reduced (2.0f), 18.0f);
        g.setColour (body);
        g.fillRoundedRectangle (r.reduced (2.0f).withTrimmedBottom (6.0f), 18.0f);
        g.setColour (juce::Colours::white.withAlpha (EffectStyle::isLight (body) ? 0.35f : 0.08f));
        g.fillRoundedRectangle (r.reduced (6.0f).withHeight (36.0f), 14.0f);

        // 四颗螺丝
        const auto inner = r.reduced (16.0f).withTrimmedBottom (6.0f);
        for (auto p : { inner.getTopLeft(), inner.getTopRight(), inner.getBottomLeft(), inner.getBottomRight() })
        {
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillEllipse (p.x - 4.0f, p.y - 4.0f, 8.0f, 8.0f);
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            g.drawLine (p.x - 2.5f, p.y, p.x + 2.5f, p.y, 1.0f);
        }

        // 标题
        const bool gv = EffectStyle::isGervana (slot);
        const auto gvInk = EffectStyle::gervanaInk (body);
        g.setColour (gv ? gvInk : ink.withAlpha (0.7f));
        g.setFont (juce::FontOptions (13.0f, gv ? juce::Font::bold : juce::Font::plain));
        juce::String kind = slot.getKindLabel();
        if (gv)
            if (auto* info = Gervana::infoFor (slot.typeId))
                kind << "  " << info->model << juce::String::fromUTF8 ("  \xc2\xb7  ") << juce::String::fromUTF8 (info->tagline);
        g.drawText (kind, 28, 22, getWidth() - 96, 18, juce::Justification::centredLeft);
        g.setColour (gv ? gvInk : ink);
        g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
        g.drawFittedText (slot.getDisplayName(), juce::Rectangle<int> (28, 40, getWidth() - 96, 34),
                          juce::Justification::centredLeft, 1, 0.6f);

        // Gervana：金色内框 + 实物图片位
        if (gv)
        {
            g.setColour (Gervana::gold().withAlpha (0.85f));
            g.drawRoundedRectangle (r.reduced (9.0f).withTrimmedBottom (6.0f), 13.0f, 2.0f);
            EffectStyle::drawImageSlot (g, imageArea.toFloat(), Gervana::imageFor (slot.typeId), ink, 13.0f);
        }

        // NAM 采样率提示
        if (auto* nam = dynamic_cast<NamProcessor*> (&slot.getProcessor()); nam != nullptr && nam->hasSampleRateMismatch())
        {
            g.setColour (juce::Colour (0xffffb347));
            g.setFont (juce::FontOptions (12.5f));
            g.drawText (TR ("Model needs ", "模型需要 ") + juce::String (nam->getExpectedSampleRate() / 1000.0, 1) + TR (" kHz sample rate", " kHz 采样率"),
                        28, 74, getWidth() - 56, 16, juce::Justification::centredLeft);
        }

        // 指示灯 + 脚踩开关
        const bool on = ! slot.bypassed.load();
        const auto led = ledArea.toFloat();
        g.setColour (on ? juce::Colour (0xffff3b30) : juce::Colour (0xff3a1d1d));
        g.fillEllipse (led);
        if (on)
        {
            g.setColour (juce::Colour (0xffff3b30).withAlpha (0.35f));
            g.fillEllipse (led.expanded (4.0f));
        }

        const auto fs = footswitchArea.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillEllipse (fs.translated (0.0f, 3.0f));
        g.setColour (juce::Colour (0xffd9d9d9));
        g.fillEllipse (fs);
        g.setColour (juce::Colour (0xff9a9a9a));
        g.drawEllipse (fs.reduced (6.0f), 2.0f);
        g.setColour (juce::Colour (0xff333333));
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (on ? TR ("On", "开") : TR ("Off", "关"), fs, juce::Justification::centred);

        g.setColour (ink.withAlpha (0.6f));
        g.setFont (juce::FontOptions (11.5f));
        g.drawText (TR ("Footswitch: On / Off", "踩下开关：开 / 关"), footswitchArea.getRight() + 12, footswitchArea.getCentreY() - 8, 160, 16,
                    juce::Justification::centredLeft);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (footswitchArea.contains (e.getPosition()) && callbacks.toggleBypass)
        {
            callbacks.toggleBypass();
            repaint();
        }
    }

    void resized() override
    {
        closeButton.setBounds (getWidth() - 56, 22, 32, 32);

        int y = topHeight;
        if (EffectStyle::isGervana (slot))
        {
            imageArea = { margin, y - 6, getWidth() - 2 * margin, imageH };
            y += imageH + 4;
        }

        // 选择器：每行两个
        const int innerW = getWidth() - 2 * margin;
        for (size_t i = 0; i < choices.size(); i += 2)
        {
            const int colW = choices.size() - i > 1 ? (innerW - 16) / 2 : innerW;
            for (size_t j = i; j < juce::jmin (i + 2, choices.size()); ++j)
            {
                const int x = margin + (int) (j - i) * (colW + 16);
                choices[j]->label->setBounds (x, y, 90, 28);
                choices[j]->combo->setBounds (x + 90, y, colW - 90, 28);
            }
            y += 38;
        }

        // 开关
        if (! toggles.empty())
        {
            int x = margin;
            for (auto& t : toggles)
            {
                const int w = 40 + juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (14.0f), t->toggle->getButtonText());
                if (x + w > getWidth() - margin) { x = margin; y += 32; }
                t->toggle->setBounds (x, y, w, 28);
                x += w + 18;
            }
            y += 38;
        }

        // 推子（图示均衡）
        if (! faders.empty())
        {
            const int w = innerW / (int) faders.size();
            for (size_t i = 0; i < faders.size(); ++i)
            {
                const int x = margin + (int) i * w;
                faders[i]->value->setBounds (x, y, w, 16);
                faders[i]->slider->setBounds (x, y + 18, w, faderH);
                faders[i]->label->setBounds (x - 4, y + 20 + faderH, w + 8, 18);
            }
            y += faderH + 46;
        }

        // 旋钮网格
        const int gridW = cols * cellW;
        const int x0 = (getWidth() - gridW) / 2;
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            const int col = (int) i % cols, row = (int) i / cols;
            const int x = x0 + col * cellW, ky = y + row * cellH;
            knobs[i]->label->setBounds (x, ky, cellW, 18);
            knobs[i]->slider->setBounds (x + (cellW - knobSize) / 2, ky + 20, knobSize, knobSize);
            knobs[i]->value->setBounds (x, ky + 22 + knobSize, cellW, 16);
        }
        if (! knobs.empty())
            y += rows * cellH + 4;

        if (fileLabel.isVisible())
        {
            replaceButton.setBounds (getWidth() - margin - 100, y, 100, 28);
            fileLabel.setBounds (margin, y, getWidth() - 2 * margin - 110, 28);
            y += 40;
        }

        ledArea = { getWidth() / 2 - 6, y + 4, 12, 12 };
        footswitchArea = { getWidth() / 2 - 36, y + 26, 72, 72 };
    }

private:
    struct Control
    {
        juce::RangedAudioParameter* param = nullptr;
        std::unique_ptr<juce::Label> label, value;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<juce::ToggleButton> toggle;
        std::unique_ptr<juce::SliderParameterAttachment> sliderAttachment;
        std::unique_ptr<juce::ComboBoxParameterAttachment> comboAttachment;
        std::unique_ptr<juce::ButtonParameterAttachment> buttonAttachment;

        ~Control()
        {
            // 附着对象要先于控件销毁
            sliderAttachment.reset(); comboAttachment.reset(); buttonAttachment.reset();
        }
    };

    void addLabel (Control& c, const juce::String& text, float size)
    {
        c.label = std::make_unique<juce::Label>();
        c.label->setText (text, juce::dontSendNotification);
        c.label->setFont (juce::FontOptions (size, juce::Font::bold));
        c.label->setJustificationType (juce::Justification::centred);
        c.label->setColour (juce::Label::textColourId, ink);
        c.label->setInterceptsMouseClicks (false, false);
        addAndMakeVisible (*c.label);
    }

    void computeSize()
    {
        const int n = (int) knobs.size();
        cols = n <= 4 ? juce::jmax (1, n) : (n <= 6 ? 3 : (n <= 8 ? 4 : 5));
        rows = n == 0 ? 0 : (n + cols - 1) / cols;

        const int width = juce::jmax (EffectStyle::isGervana (slot) ? 560 : 420, cols * cellW + 2 * margin, faders.empty() ? 0 : (int) faders.size() * 58 + 2 * margin);

        int h = topHeight;
        if (EffectStyle::isGervana (slot)) h += imageH + 4;
        h += (int) ((choices.size() + 1) / 2) * 38;
        if (! toggles.empty()) h += 38 + ((int) toggles.size() > 3 ? 32 : 0);
        if (! faders.empty()) h += faderH + 46;
        h += rows * cellH + (rows > 0 ? 4 : 0);
        if (slot.kind == EffectSlot::Kind::nam || slot.kind == EffectSlot::Kind::ir) h += 40;
        h += 26 + 72 + 34;   // 指示灯 + 脚踩开关 + 底边

        setSize (width, h);
    }

    void refreshValues()
    {
        for (auto* list : { &knobs, &faders })
            for (auto& c : *list)
                c->value->setText (formatValue (*c->param), juce::dontSendNotification);
    }

    void timerCallback() override
    {
        refreshValues();
        repaint (ledArea.expanded (8));
        repaint (footswitchArea.expanded (4));
    }

    juce::Rectangle<int> imageArea;
    static constexpr int imageH = 260;
    static constexpr int margin = 32, topHeight = 96, cellW = 104, cellH = 122, knobSize = 72, faderH = 150;

    EffectSlot& slot;
    Callbacks callbacks;
    juce::Colour body, ink;
    std::unique_ptr<PedalLookAndFeel> lnf;
    std::vector<std::unique_ptr<Control>> choices, toggles, knobs, faders;
    juce::Label fileLabel;
    juce::TextButton replaceButton, closeButton;
    juce::Rectangle<int> ledArea, footswitchArea;
    int cols = 1, rows = 0;
};

// =============================================================================
PedalEditorOverlay::PedalEditorOverlay (EffectSlotPtr s, Callbacks cb)
    : slot (std::move (s)), callbacks (std::move (cb))
{
    pedal = std::make_unique<Pedal> (*slot, callbacks);
    addAndMakeVisible (*pedal);
    setWantsKeyboardFocus (true);
}

PedalEditorOverlay::~PedalEditorOverlay()
{
    pedal = nullptr;
}

void PedalEditorOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.62f));
}

void PedalEditorOverlay::resized()
{
    // 放不下时等比缩小
    const float scale = juce::jmin (1.0f, (float) (getWidth() - 40) / (float) pedal->getWidth(),
                                    (float) (getHeight() - 40) / (float) pedal->getHeight());
    pedal->setTransform (juce::AffineTransform::scale (scale));
    const int w = (int) (pedal->getWidth() * scale), h = (int) (pedal->getHeight() * scale);
    pedal->setTopLeftPosition ((int) ((float) ((getWidth() - w) / 2) / scale), (int) ((float) ((getHeight() - h) / 2) / scale));
}

void PedalEditorOverlay::mouseDown (const juce::MouseEvent&)
{
    // 点在踏板外面：关闭
    if (callbacks.close)
        callbacks.close();
}

bool PedalEditorOverlay::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (callbacks.close)
            callbacks.close();
        return true;
    }
    return false;
}
