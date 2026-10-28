#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "fx/EffectChain.h"

// 双击单块后弹出的“放大踏板”：盖在主窗口上，中间是一块大踏板，
// 旋钮 / 开关 / 选择器根据效果器的参数自动生成。点空白处或按 Esc 关闭。
class PedalEditorOverlay : public juce::Component
{
public:
    struct Callbacks
    {
        std::function<void()> toggleBypass;
        std::function<void()> replaceFile;     // 仅 NAM / IR
        std::function<void()> close;
    };

    PedalEditorOverlay (EffectSlotPtr slot, Callbacks callbacks);
    ~PedalEditorOverlay() override;

    EffectSlot* getSlot() const { return slot.get(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class Pedal;

    EffectSlotPtr slot;
    Callbacks callbacks;
    std::unique_ptr<Pedal> pedal;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalEditorOverlay)
};
