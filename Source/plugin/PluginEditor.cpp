#include "PluginEditor.h"
#include "Text.h"
#include "AppInfo.h"

ToneTabPluginEditor::ToneTabPluginEditor (ToneTabPluginProcessor& processor)
    : AudioProcessorEditor (&processor),
      proc (processor),
      presetBar (processor.getChain(), processor.getSettings()),
      board (processor.getChain(), [] {})
{
    setSize (1060, 620);

    title.setText (T (AppInfo::name) + "  " + TR ("Tone", "音色"), juce::dontSendNotification);
    title.setFont (juce::FontOptions (21.0f, juce::Font::bold));
    addAndMakeVisible (title);

    note.setText (TR ("Practice, tabs, drums and recording are in the standalone app.",
                      "练习谱面、鼓机和录音在独立程序里。"),
                  juce::dontSendNotification);
    note.setFont (juce::FontOptions (13.0f));
    note.setColour (juce::Label::textColourId, juce::Colour (0xff9a9a9a));
    addAndMakeVisible (note);

    addAndMakeVisible (presetBar);
    addAndMakeVisible (board);

    board.onOpenPedalEditor = [this] (EffectSlotPtr slot) { showOverlay (std::move (slot)); };
    board.onSlotGone = [this] (EffectSlot* slot)
    {
        if (overlay != nullptr && overlay->getSlot() == slot)
            closeOverlay();
    };
}

void ToneTabPluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff151515));
}

void ToneTabPluginEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    auto header = area.removeFromTop (32);
    title.setBounds (header.removeFromLeft (220));
    note.setBounds (header);
    area.removeFromTop (10);

    presetBar.setBounds (area.removeFromTop (30));
    area.removeFromTop (10);
    board.setBounds (area);

    if (overlay != nullptr)
        overlay->setBounds (getLocalBounds());
}

void ToneTabPluginEditor::showOverlay (EffectSlotPtr slot)
{
    closeOverlay();
    if (slot == nullptr)
        return;

    PedalEditorOverlay::Callbacks callbacks;
    callbacks.toggleBypass = [this, slot] { board.toggleBypassFor (slot.get()); };
    callbacks.replaceFile = [this, slot] { board.replaceFileFor (slot.get()); };
    callbacks.close = [this] { closeOverlay(); };

    overlay = std::make_unique<PedalEditorOverlay> (std::move (slot), callbacks);
    addAndMakeVisible (*overlay);
    overlay->setBounds (getLocalBounds());
    overlay->grabKeyboardFocus();
}

void ToneTabPluginEditor::closeOverlay()
{
    if (overlay != nullptr)
    {
        overlay->setVisible (false);
        overlay.reset();
    }
}
