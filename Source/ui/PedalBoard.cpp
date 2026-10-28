#include "PedalBoard.h"
#include "fx/BuiltinEffects.h"
#include "fx/NamProcessor.h"
#include "EffectStyle.h"
#include "Text.h"

namespace
{
    constexpr int cardW = 124, cardH = 168, gap = 12;
    const juce::String dragPrefix ("pedal:");
}

// =============================================================================
class PedalBoard::EditorWindow : public juce::DocumentWindow
{
public:
    EditorWindow (const juce::String& title, juce::AudioProcessorEditor* editor, std::function<void()> onClose)
        : juce::DocumentWindow (title, juce::Colour (0xff222222), DocumentWindow::closeButton),
          closeCallback (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        toFront (true);
    }

    void closeButtonPressed() override { if (closeCallback) closeCallback(); }

private:
    std::function<void()> closeCallback;
};

// =============================================================================
class PedalBoard::PedalCard : public juce::Component,
                              public juce::DragAndDropTarget,
                              public juce::SettableTooltipClient
{
public:
    PedalCard (PedalBoard& o, EffectSlotPtr s, int i) : owner (o), slot (std::move (s)), index (i)
    {
        setRepaintsOnMouseActivity (true);
        juce::String tip = slot->getKindLabel() + "  " + slot->getDisplayName() + "\n"
                         + TR ("Click the switch: on/off    Double-click: open controls    Drag: reorder    Right-click: more", "单击开关：开 / 关    双击：打开参数    拖动：调整顺序    右键：更多");
        if (slot->typeId == "gv_yiyou" || slot->typeId == "gv_yimao")
            tip << "\n" << TR ("Left footswitch: change channel    Right footswitch: on/off", "左边脚踩：切通道    右边脚踩：开 / 关");
        setTooltip (tip);
    }

    // Gervana 有实物图时：整张图当卡面
    bool paintPhoto (juce::Graphics& g)
    {
        if (! EffectStyle::isGervana (*slot)) return false;
        const auto img = Gervana::imageFor (slot->typeId);
        if (! img.isValid()) return false;

        const bool on = ! slot->bypassed.load();
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setOpacity (on ? 1.0f : 0.4f);
        g.drawImage (img, photoRect(), juce::RectanglePlacement::stretchToFit);
        g.setOpacity (1.0f);

        // 双脚踩：左边上方标出当前通道
        if (twin() != nullptr)
        {
            const auto ca = channelArea();
            const auto name = channelName();
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
            const int tw = juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (11.5f, juce::Font::bold), name) + 12;
            const juce::Rectangle<int> pill (ca.getCentreX() - tw / 2, ca.getY() - 20, tw, 17);
            g.setColour (juce::Colours::black.withAlpha (0.7f));
            g.fillRoundedRectangle (pill.toFloat(), 8.0f);
            g.setColour (Gervana::gold());
            g.drawText (name, pill, juce::Justification::centred);
        }
        if (dropHighlight)
        {
            g.setColour (juce::Colours::white);
            g.fillRect (0.0f, 8.0f, 3.0f, r.getHeight() - 16.0f);
        }

        // 关着：压暗，中间写“关”
        if (! on)
        {
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillRoundedRectangle (juce::Rectangle<float> (getWidth() * 0.5f - 22.0f, getHeight() * 0.42f - 14.0f, 44.0f, 28.0f), 6.0f);
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            g.drawText (TR ("Off", "关"), juce::Rectangle<int> (getWidth() / 2 - 22, (int) (getHeight() * 0.42f) - 14, 44, 28), juce::Justification::centred);
        }
        // 开着：右上角一颗小红灯
        else
        {
            g.setColour (juce::Colour (0xffff3b30).withAlpha (0.35f));
            g.fillEllipse (getWidth() - 44.0f, 6.0f, 14.0f, 14.0f);
            g.setColour (juce::Colour (0xffff3b30));
            g.fillEllipse (getWidth() - 41.0f, 9.0f, 8.0f, 8.0f);
        }

        if (isMouseOver())
        {
            g.setColour (Gervana::gold().withAlpha (0.8f));
            g.drawRoundedRectangle (r, 10.0f, 1.5f);
            g.drawEllipse (footswitchArea().toFloat().reduced (2.0f), 1.5f);
            if (twin() != nullptr)
                g.drawEllipse (channelArea().toFloat().reduced (2.0f), 1.5f);
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillEllipse (closeArea().toFloat().reduced (2.0f));
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (juce::String::fromUTF8 ("\xc3\x97"), closeArea(), juce::Justification::centred);
        }
        return true;
    }

    void paint (juce::Graphics& g) override
    {
        if (paintPhoto (g))
            return;

        const bool on = ! slot->bypassed.load();
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const auto base = EffectStyle::colourFor (*slot);
        const auto shown = on ? base : base.interpolatedWith (juce::Colour (0xff2a2a2a), 0.7f);
        const auto ink = EffectStyle::textOn (shown);

        g.setColour (shown);
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (juce::Colours::white.withAlpha (EffectStyle::isLight (shown) ? 0.3f : 0.07f));
        g.fillRoundedRectangle (r.reduced (4.0f).withHeight (22.0f), 9.0f);
        if (dropHighlight)
        {
            g.setColour (juce::Colours::white);
            g.fillRect (0.0f, 8.0f, 3.0f, r.getHeight() - 16.0f);
        }
        if (isMouseOver())
        {
            g.setColour (ink.withAlpha (0.35f));
            g.drawRoundedRectangle (r, 12.0f, 1.5f);
        }

        // 类型 + 删除
        g.setColour (ink.withAlpha (0.65f));
        g.setFont (juce::FontOptions (11.5f));
        g.drawText (slot->getKindLabel(), 10, 8, getWidth() - 34, 16, juce::Justification::centredLeft);
        g.drawText (juce::String::fromUTF8 ("\xc3\x97"), closeArea(), juce::Justification::centred);

        // 指示灯
        const auto led = juce::Rectangle<float> (getWidth() * 0.5f - 5.0f, 32.0f, 10.0f, 10.0f);
        g.setColour (on ? juce::Colour (0xffff3b30) : juce::Colour (0xff4a2a2a));
        g.fillEllipse (led);
        if (on)
        {
            g.setColour (juce::Colour (0xffff3b30).withAlpha (0.3f));
            g.fillEllipse (led.expanded (3.0f));
        }

        // 名称（Gervana：金色描边 + 图片位 + 金色名字）
        if (EffectStyle::isGervana (*slot))
        {
            const auto gvInk = EffectStyle::gervanaInk (shown);
            g.setColour (Gervana::gold().withAlpha (on ? 0.95f : 0.45f));
            g.drawRoundedRectangle (r.reduced (2.5f), 10.5f, 2.0f);
            EffectStyle::drawImageSlot (g, juce::Rectangle<float> (12.0f, 46.0f, (float) getWidth() - 24.0f, 36.0f),
                                        Gervana::imageFor (slot->typeId), ink, 9.5f);
            g.setColour (gvInk.withAlpha (on ? 1.0f : 0.6f));
            g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
            g.drawFittedText (slot->getDisplayName(), juce::Rectangle<int> (6, 84, getWidth() - 12, 28),
                              juce::Justification::centred, 2, 0.75f);
        }
        else
        {
            g.setColour (ink.withAlpha (on ? 1.0f : 0.6f));
            g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            g.drawFittedText (slot->getDisplayName(), juce::Rectangle<int> (8, 50, getWidth() - 16, 48),
                              juce::Justification::centred, 3, 0.8f);
        }

        // NAM 采样率提示
        if (auto* nam = dynamic_cast<NamProcessor*> (&slot->getProcessor()); nam != nullptr && nam->hasSampleRateMismatch())
        {
            g.setColour (juce::Colour (0xffef9f27));
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (TR ("Needs ", "需要 ") + juce::String (nam->getExpectedSampleRate() / 1000.0, 1) + " kHz",
                        8, 98, getWidth() - 16, 14, juce::Justification::centred);
        }

        // 脚踩开关
        const auto fs = footswitchArea().toFloat();
        g.setColour (juce::Colour (0xffd5d5d5));
        g.fillEllipse (fs);
        g.setColour (juce::Colour (0xff8a8a8a));
        g.drawEllipse (fs.reduced (4.0f), 1.5f);
        g.setColour (juce::Colour (0xff333333));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (on ? TR ("On", "开") : TR ("Off", "关"), fs, juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragStarted = false;
        if (e.mods.isPopupMenu())
            owner.showSlotMenu (index);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() || dragStarted || e.getNumberOfClicks() > 1)
            return;
        if (closeArea().contains (e.getPosition()))
            owner.removeSlot (index);
        else if (twin() != nullptr && channelArea().expanded (6).contains (e.getPosition()))
            switchChannel();
        else if (footswitchArea().expanded (8).contains (e.getPosition()))
            owner.toggleBypass (index);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (! footswitchArea().expanded (8).contains (e.getPosition()))
            owner.openEditor (index);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragStarted && e.mods.isLeftButtonDown() && e.getDistanceFromDragStart() > 6)
        {
            dragStarted = true;
            owner.startDragging (dragPrefix + juce::String (index), this);
        }
    }

    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        return d.description.toString().startsWith (dragPrefix);
    }
    void itemDragEnter (const SourceDetails&) override { dropHighlight = true; repaint(); }
    void itemDragExit (const SourceDetails&) override  { dropHighlight = false; repaint(); }
    void itemDropped (const SourceDetails& d) override
    {
        dropHighlight = false;
        const int from = d.description.toString().fromFirstOccurrenceOf (dragPrefix, false, false).getIntValue();
        owner.moveSlot (from, index);
    }

private:
    juce::Rectangle<int> closeArea() const      { return { getWidth() - 26, 4, 22, 22 }; }

    // 乙酉 / 乙卯 是横版双脚踩：左边切通道，右边开 / 关（位置按实物图量的）
    struct Spot { float x, y, r; };
    struct TwinSwitch { const char* id; Spot channel, bypass; };
    const TwinSwitch* twin() const
    {
        static const TwinSwitch list[] = {
            { "gv_yiyou", { 0.347f, 0.865f, 0.052f }, { 0.740f, 0.865f, 0.052f } },
            { "gv_yimao", { 0.314f, 0.845f, 0.048f }, { 0.697f, 0.845f, 0.048f } },
        };
        if (! hasPhoto()) return nullptr;
        for (auto& t : list) if (slot->typeId == t.id) return &t;
        return nullptr;
    }
    bool hasPhoto() const { return EffectStyle::isGervana (*slot) && Gervana::imageFor (slot->typeId).isValid(); }

    // 图片实际画在卡片上的位置
    juce::Rectangle<float> photoRect() const
    {
        const auto img = Gervana::imageFor (slot->typeId);
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        if (! img.isValid()) return r;
        return juce::RectanglePlacement (juce::RectanglePlacement::centred)
                   .appliedTo (juce::Rectangle<float> (0.0f, 0.0f, (float) img.getWidth(), (float) img.getHeight()), r);
    }
    juce::Rectangle<int> spotArea (Spot s) const
    {
        const auto pr = photoRect();
        const float rad = juce::jmax (16.0f, s.r * pr.getWidth());
        return juce::Rectangle<float> (pr.getX() + s.x * pr.getWidth() - rad, pr.getY() + s.y * pr.getHeight() - rad,
                                       rad * 2.0f, rad * 2.0f).toNearestInt();
    }
    juce::Rectangle<int> footswitchArea() const
    {
        if (auto* t = twin()) return spotArea (t->bypass);
        return { getWidth() / 2 - 22, getHeight() - 56, 44, 44 };
    }
    juce::Rectangle<int> channelArea() const
    {
        if (auto* t = twin()) return spotArea (t->channel);
        return {};
    }

public:
    // 卡片宽度：横版的前级按图片比例加宽，高度和别的一样
    int preferredWidth (int cardHeight, int normalWidth) const
    {
        if (! hasPhoto()) return normalWidth;
        const auto img = Gervana::imageFor (slot->typeId);
        if (img.getWidth() <= img.getHeight()) return normalWidth;
        return juce::roundToInt ((cardHeight - 4) * (float) img.getWidth() / (float) img.getHeight()) + 4;
    }
private:
    // 左边脚踩：切通道（乙酉 清音 / 失真，乙卯 普通 → Top Boost → 并联）
    void switchChannel()
    {
        if (auto* fx = dynamic_cast<juce::AudioProcessor*> (&slot->getProcessor()))
            for (auto* p : fx->getParameters())
                if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p); c != nullptr && c->getParameterID() == "channel")
                {
                    const int next = (c->getIndex() + 1) % c->choices.size();
                    c->beginChangeGesture();
                    c->setValueNotifyingHost (c->convertTo0to1 ((float) next));
                    c->endChangeGesture();
                    repaint();
                }
    }
    juce::String channelName() const
    {
        for (auto* p : slot->getProcessor().getParameters())
            if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p); c != nullptr && c->getParameterID() == "channel")
                return c->getCurrentChoiceName();
        return {};
    }

    PedalBoard& owner;
    EffectSlotPtr slot;
    const int index;
    bool dragStarted = false, dropHighlight = false;
};

// =============================================================================
class PedalBoard::AddCard : public juce::Component, public juce::DragAndDropTarget
{
public:
    explicit AddCard (PedalBoard& o) : owner (o) { setRepaintsOnMouseActivity (true); }

    std::function<int()> getCount;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colours::white.withAlpha (isMouseOver() || dropHighlight ? 0.5f : 0.25f));
        const float dashes[] = { 6.0f, 4.0f };
        juce::Path p;
        p.addRoundedRectangle (r, 12.0f);
        juce::Path dashed;
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, p, dashes, 2);
        g.fillPath (dashed);
        g.setFont (juce::FontOptions (30.0f));
        g.drawText ("+", r.withTrimmedBottom (40.0f), juce::Justification::centred);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (TR ("Add Effect", "添加效果"), r.withTrimmedTop (60.0f), juce::Justification::centred);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (getLocalBounds().contains (e.getPosition()))
            owner.showAddMenu (this);
    }

    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.toString().startsWith (dragPrefix); }
    void itemDragEnter (const SourceDetails&) override { dropHighlight = true; repaint(); }
    void itemDragExit (const SourceDetails&) override  { dropHighlight = false; repaint(); }
    void itemDropped (const SourceDetails& d) override
    {
        dropHighlight = false;
        const int from = d.description.toString().fromFirstOccurrenceOf (dragPrefix, false, false).getIntValue();
        owner.moveSlot (from, getCount ? getCount() - 1 : from);
    }

private:
    PedalBoard& owner;
    bool dropHighlight = false;
};

// =============================================================================
PedalBoard::PedalBoard (EffectChain& c, std::function<void()> openPluginManager)
    : chain (c), onOpenPluginManager (std::move (openPluginManager))
{
    addCard = std::make_unique<AddCard> (*this);
    addCard->getCount = [this] { return (int) chain.getSlots().size(); };

    viewport.setViewedComponent (&row, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);

    chain.addChangeListener (this);
    chain.onSlotRemoved = [this] (EffectSlot* s) { closeEditor (s); };

    lastNamFolder = lastIrFolder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    if (const auto lib = EffectChain::getToneLibrary(); lib.isDirectory())
    {
        lastNamFolder = lib.getChildFile ("NAM").isDirectory() ? lib.getChildFile ("NAM") : lib;
        lastIrFolder  = lib.getChildFile ("IR").isDirectory()  ? lib.getChildFile ("IR")  : lib;
    }
    rebuild();
}

PedalBoard::~PedalBoard()
{
    chain.removeChangeListener (this);
    chain.onSlotRemoved = nullptr;
    closeAllEditors();
}

void PedalBoard::closeAllEditors()
{
    editors.clear();
}

void PedalBoard::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rebuild();
}

void PedalBoard::rebuild()
{
    cards.clear();
    const auto& slots = chain.getSlots();
    for (int i = 0; i < (int) slots.size(); ++i)
        row.addAndMakeVisible (cards.add (new PedalCard (*this, slots[(size_t) i], i)));
    row.addAndMakeVisible (*addCard);
    resized();
}

void PedalBoard::paint (juce::Graphics& g)
{
    // 信号走向的连线
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.fillRect (0, getHeight() / 2 - 1, getWidth(), 2);
}

void PedalBoard::resized()
{
    viewport.setBounds (getLocalBounds());
    const int h = juce::jmax (cardH, viewport.getMaximumVisibleHeight());
    int x = 0;
    const int y = juce::jmax (0, (h - cardH) / 2);
    for (auto* c : cards)
    {
        const int w = c->preferredWidth (cardH, cardW);
        c->setBounds (x, y, w, cardH);
        x += w + gap;
    }
    addCard->setBounds (x, y, cardW, cardH);
    row.setSize (x + cardW + 4, h);
}

// ---- 操作 ----------------------------------------------------------------------
void PedalBoard::toggleBypass (int index)
{
    const auto& slots = chain.getSlots();
    if (juce::isPositiveAndBelow (index, (int) slots.size()))
        chain.setBypassed (index, ! slots[(size_t) index]->bypassed.load());
}

void PedalBoard::removeSlot (int index)  { chain.remove (index); }
void PedalBoard::moveSlot (int from, int to) { chain.move (from, to); }

void PedalBoard::openEditor (int index)
{
    const auto& slots = chain.getSlots();
    if (! juce::isPositiveAndBelow (index, (int) slots.size()))
        return;

    auto* slot = slots[(size_t) index].get();
    if (slot->kind != EffectSlot::Kind::vst3 && onOpenPedalEditor)
    {
        onOpenPedalEditor (slots[(size_t) index]);
        return;
    }

    if (auto it = editors.find (slot); it != editors.end())
    {
        it->second->toFront (true);
        return;
    }

    auto& proc = slot->getProcessor();
    juce::AudioProcessorEditor* editor = proc.hasEditor() ? proc.createEditorAndMakeActive() : nullptr;
    if (editor == nullptr)
        editor = new juce::GenericAudioProcessorEditor (proc);

    if (dynamic_cast<juce::GenericAudioProcessorEditor*> (editor) != nullptr)
        editor->setSize (juce::jmax (420, editor->getWidth()), editor->getHeight());

    editors[slot] = std::make_unique<EditorWindow> (slot->getDisplayName(), editor,
                                                    [this, slot] { closeEditor (slot); });
}

int PedalBoard::indexOf (const EffectSlot* slot) const
{
    const auto& slots = chain.getSlots();
    for (int i = 0; i < (int) slots.size(); ++i)
        if (slots[(size_t) i].get() == slot)
            return i;
    return -1;
}

void PedalBoard::replaceFileFor (const EffectSlot* slot)
{
    const int i = indexOf (slot);
    if (i >= 0 && (slot->kind == EffectSlot::Kind::nam || slot->kind == EffectSlot::Kind::ir))
        chooseFile (slot->kind == EffectSlot::Kind::nam, i, true);
}

void PedalBoard::closeEditor (EffectSlot* slot)
{
    if (onSlotGone)
        onSlotGone (slot);

    if (auto it = editors.find (slot); it != editors.end())
    {
        // 窗口要在下一轮消息里删除，避免在它自己的回调里析构
        // 编辑器本身要立刻删掉（效果器此时还活着）
        auto* raw = it->second.release();
        editors.erase (it);
        raw->setVisible (false);
        raw->clearContentComponent();
        juce::MessageManager::callAsync ([raw] { delete raw; });
    }
}

void PedalBoard::showError (const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, TR ("Error", "出错了"), message);
}

void PedalBoard::chooseFile (bool nam, int index, bool replaceExisting)
{
    chooser = std::make_unique<juce::FileChooser> (nam ? TR ("Choose NAM Model", "选择 NAM 模型") : TR ("Choose Cab IR", "选择箱体 IR"),
                                                   nam ? lastNamFolder : lastIrFolder,
                                                   nam ? "*.nam" : "*.wav;*.aif;*.aiff");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, nam, index, replaceExisting] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (! file.existsAsFile())
            return;

        (nam ? lastNamFolder : lastIrFolder) = file.getParentDirectory();

        juce::String error;
        auto slot = nam ? chain.createNam (file, error) : chain.createIr (file, error);
        if (slot == nullptr)
        {
            showError (error);
            return;
        }

        if (replaceExisting)
            chain.replace (index, slot);
        else
            chain.insert (slot, index);
    });
}

void PedalBoard::duplicate (int index)
{
    const auto& slots = chain.getSlots();
    if (! juce::isPositiveAndBelow (index, (int) slots.size()))
        return;

    const auto& src = slots[(size_t) index];
    EffectSlotPtr copy;
    juce::String error;

    switch (src->kind)
    {
        case EffectSlot::Kind::builtin: copy = chain.createBuiltin (src->typeId); break;
        case EffectSlot::Kind::nam:     copy = chain.createNam (EffectChain::resolvePortablePath (src->typeId), error); break;
        case EffectSlot::Kind::ir:      copy = chain.createIr (EffectChain::resolvePortablePath (src->typeId), error); break;
        case EffectSlot::Kind::vst3:    copy = chain.createPlugin (src->pluginDescription, error); break;
    }

    if (copy == nullptr)
    {
        showError (error);
        return;
    }

    juce::MemoryBlock state;
    src->getProcessor().getStateInformation (state);
    copy->getProcessor().setStateInformation (state.getData(), (int) state.getSize());
    chain.insert (copy, index + 1);
}

void PedalBoard::showSlotMenu (int index)
{
    const auto& slots = chain.getSlots();
    if (! juce::isPositiveAndBelow (index, (int) slots.size()))
        return;

    const auto kind = slots[(size_t) index]->kind;
    const bool on = ! slots[(size_t) index]->bypassed.load();

    juce::PopupMenu m;
    m.addItem (1, TR ("Open Controls…", "打开参数…"));
    m.addItem (2, on ? TR ("Bypass", "关闭（旁通）") : TR ("Enable", "打开"));
    if (kind == EffectSlot::Kind::nam) m.addItem (3, TR ("Change Model…", "更换模型…"));
    if (kind == EffectSlot::Kind::ir)  m.addItem (4, TR ("Change IR…", "更换 IR…"));
    m.addItem (5, TR ("Duplicate", "复制一块"));
    m.addSeparator();
    m.addItem (6, TR ("Move Left", "左移"), index > 0);
    m.addItem (7, TR ("Move Right", "右移"), index < (int) slots.size() - 1);
    m.addSeparator();
    m.addItem (8, TR ("Delete", "删除"));

    m.showMenuAsync (juce::PopupMenu::Options(), [this, index] (int r)
    {
        switch (r)
        {
            case 1: openEditor (index); break;
            case 2: toggleBypass (index); break;
            case 3: chooseFile (true, index, true); break;
            case 4: chooseFile (false, index, true); break;
            case 5: duplicate (index); break;
            case 6: moveSlot (index, index - 1); break;
            case 7: moveSlot (index, index + 1); break;
            case 8: removeSlot (index); break;
            default: break;
        }
    });
}

void PedalBoard::showAddMenu (juce::Component* target)
{
    const auto& builtins = getBuiltinEffectList();
    const int insertIndex = (int) chain.getSlots().size();

    juce::PopupMenu m;
    juce::StringArray categories;
    for (auto& b : builtins)
        categories.addIfNotAlreadyThere (b.category);

    // Gervana 系列放最前面，单独一组，金色字
    m.addSectionHeader ("Gervana");
    for (int i = 0; i < (int) builtins.size(); ++i)
        if (builtins[(size_t) i].category == Gervana::category)
        {
            juce::PopupMenu::Item item (builtins[(size_t) i].name);
            item.itemID = 1000 + i;
            item.colour = Gervana::gold();
            m.addItem (item);
        }
    m.addSeparator();

    for (auto& cat : categories)
    {
        if (cat == Gervana::category) continue;
        juce::PopupMenu sub;
        for (int i = 0; i < (int) builtins.size(); ++i)
            if (builtins[(size_t) i].category == cat)
                sub.addItem (1000 + i, builtins[(size_t) i].name);
        m.addSubMenu (cat, sub);
    }

    m.addSeparator();
    m.addItem (1, TR ("Load NAM Amp Model…", "加载 NAM 箱头模型…"));
    m.addItem (2, TR ("Load Cab IR…", "加载箱体 IR…"));
    m.addSeparator();

    auto types = chain.getKnownPlugins().getTypes();
    std::sort (types.begin(), types.end(), [] (const auto& a, const auto& b)
               { return (a.manufacturerName + a.name).compareIgnoreCase (b.manufacturerName + b.name) < 0; });

    juce::PopupMenu vst;
    if (types.isEmpty())
        vst.addItem (-1, TR ("(no plug-ins scanned yet)", "（还没有扫描到插件）"), false);
    for (int i = 0; i < types.size(); ++i)
        vst.addItem (5000 + i, types[i].name + "  -  " + types[i].manufacturerName);
    m.addSubMenu (TR ("VST3 Plug-ins", "VST3 插件"), vst);
    m.addItem (3, TR ("Manage / Scan VST3 Plug-ins…", "管理 / 扫描 VST3 插件…"));

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
                     [this, insertIndex, types] (int r)
    {
        if (r >= 5000 && r - 5000 < types.size())
        {
            juce::String error;
            if (auto slot = chain.createPlugin (types[r - 5000], error))
                chain.insert (slot, insertIndex);
            else
                showError (error);
        }
        else if (r >= 1000)
        {
            const auto& list = getBuiltinEffectList();
            if (r - 1000 < (int) list.size())
                chain.insert (chain.createBuiltin (list[(size_t) (r - 1000)].id), insertIndex);
        }
        else if (r == 1) chooseFile (true, insertIndex, false);
        else if (r == 2) chooseFile (false, insertIndex, false);
        else if (r == 3 && onOpenPluginManager) onOpenPluginManager();
    });
}
