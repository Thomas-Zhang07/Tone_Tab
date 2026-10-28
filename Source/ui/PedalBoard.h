#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include "fx/EffectChain.h"

// 效果器板：一排单块，可以开关、拖动排序、双击打开参数、右键更多操作
class PedalBoard : public juce::Component,
                   public juce::DragAndDropContainer,
                   private juce::ChangeListener
{
public:
    PedalBoard (EffectChain& chainToUse, std::function<void()> openPluginManager);
    ~PedalBoard() override;

    void closeAllEditors();
    void resized() override;
    void paint (juce::Graphics&) override;

    // 卡片调用
    void toggleBypass (int index);
    void openEditor (int index);
    void removeSlot (int index);
    void moveSlot (int from, int to);
    void showSlotMenu (int index);
    void showAddMenu (juce::Component* target);

    // 放大踏板（由主窗口显示）用到的操作
    std::function<void (EffectSlotPtr)> onOpenPedalEditor;   // 内置 / NAM / IR 双击时调用
    std::function<void (EffectSlot*)> onSlotGone;            // 某块被删除或替换
    int indexOf (const EffectSlot* slot) const;
    void toggleBypassFor (const EffectSlot* slot)      { toggleBypass (indexOf (slot)); }
    void replaceFileFor (const EffectSlot* slot);

private:
    class PedalCard;
    class AddCard;
    class EditorWindow;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rebuild();
    void closeEditor (EffectSlot* slot);
    void chooseFile (bool nam, int index, bool replaceExisting);
    void duplicate (int index);
    void showError (const juce::String& message);

    EffectChain& chain;
    std::function<void()> onOpenPluginManager;

    juce::Viewport viewport;
    juce::Component row;
    juce::OwnedArray<PedalCard> cards;
    std::unique_ptr<AddCard> addCard;
    std::map<EffectSlot*, std::unique_ptr<EditorWindow>> editors;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::File lastNamFolder, lastIrFolder;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalBoard)
};
