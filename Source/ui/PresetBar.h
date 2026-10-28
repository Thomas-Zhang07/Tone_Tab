#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include "fx/EffectChain.h"

// 音色预设：整条效果器链存成一个 xml 文件
// 位置：我的文档\LianqinHost\音色预设\*.xml
class PresetBar : public juce::Component
{
public:
    PresetBar (EffectChain& chainToUse, juce::PropertiesFile& settingsToUse);

    static juce::File getPresetFolder();
    void createDefaultPresetsIfNeeded();
    bool loadPreset (const juce::String& name);
    juce::String getCurrentPresetName() const { return currentName; }

    void resized() override;

    // 预设分类：按名字里“ · ”前面的部分；内置老预设归“基础”，用户自己存的归“我的预设”
    static juce::String categoryOf (const juce::String& presetName);
    static const juce::StringArray& categoryOrder();

private:
    void refreshList();
    void savePreset (const juce::String& name);
    void askForNameAndSave();
    void deleteCurrent();
    void migratePresetFileNames();

    EffectChain& chain;
    juce::PropertiesFile& settings;
    juce::String currentName;
    std::map<juce::String, juce::File> fileByName;
    std::map<int, juce::String> nameById;

    juce::Label label;
    juce::ComboBox presetBox;
    juce::TextButton saveButton, saveAsButton, deleteButton, folderButton;
    std::unique_ptr<juce::AlertWindow> nameDialog;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
};
