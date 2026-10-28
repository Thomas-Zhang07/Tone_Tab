#include "PresetBar.h"
#include "NamBrowser.h"
#include "Text.h"
#include "app/AppPaths.h"
#include "fx/DefaultPresets.h"
#include "fx/BuiltinEffects.h"
#include <map>

namespace
{
    // ASCII file name for a preset. The human-readable name lives in the XML
    // (displayName), so translated names never become paths.
    juce::String slugifyPresetName (const juce::String& name)
    {
        juce::String out;
        bool needsHash = false;

        for (auto c : name)
        {
            if (c < 128 && (juce::CharacterFunctions::isLetterOrDigit (c) || c == '-' || c == '_'))
                out << juce::String::charToString (c);
            else if (c == ' ')
                out << "_";
            else
                needsHash = true;
        }

        out = out.trimCharactersAtStart ("_").trimCharactersAtEnd ("_");
        if (out.isEmpty())
            out = "preset";
        if (out.length() > 50)
            out = out.substring (0, 50);

        if (needsHash)
        {
            uint32_t h = 2166136261u;
            for (auto c : name)
            {
                h ^= (uint32_t) c;
                h *= 16777619u;
            }
            out << "_" << juce::String::toHexString ((int) (h & 0xffffff)).toLowerCase();
        }
        return out.toLowerCase();
    }

    juce::String presetDisplayName (const juce::File& file)
    {
        if (auto xml = juce::parseXML (file))
        {
            auto n = xml->getStringAttribute ("displayName");
            if (n.isEmpty()) n = xml->getStringAttribute ("name");
            if (n.isNotEmpty()) return n;
        }
        return file.getFileNameWithoutExtension();
    }
}

PresetBar::PresetBar (EffectChain& c, juce::PropertiesFile& s) : chain (c), settings (s)
{
    label.setText (TR ("Tone Preset", "音色预设"), juce::dontSendNotification);
    label.setFont (juce::FontOptions (14.0f));
    label.setColour (juce::Label::textColourId, juce::Colour (0xff9a9a9a));
    addAndMakeVisible (label);

    presetBox.setTextWhenNothingSelected (TR ("(unsaved tone)", "（未保存的音色）"));
    presetBox.onChange = [this]
    {
        if (presetBox.getSelectedId() == 9000)
        {
            presetBox.setSelectedId (currentPresetId, juce::dontSendNotification);
            NamBrowser::show (this, [this] (juce::File file)
            {
                juce::String error;
                auto slot = chain.createNam (file, error);
                if (slot == nullptr)
                {
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                            TR ("Cannot load model", "没法加载模型"), error);
                    return;
                }

                chain.insert (slot);
                currentName = {};
                settings.setValue ("currentPreset", currentName);
                refreshList();
                presetBox.setText (TR ("(unsaved tone)", "（未保存的音色）"),
                                   juce::dontSendNotification);
            });
            return;
        }

        const auto it = nameById.find (presetBox.getSelectedId());
        const auto name = it != nameById.end() ? it->second : presetBox.getText();
        if (name.isNotEmpty() && name != currentName)
            loadPreset (name);
    };
    addAndMakeVisible (presetBox);

    saveButton.setButtonText (TR ("Save", "保存"));
    saveButton.setTooltip (TR ("Overwrite current preset", "覆盖保存当前预设"));
    saveButton.onClick = [this]
    {
        if (currentName.isEmpty()) askForNameAndSave();
        else savePreset (currentName);
    };
    addAndMakeVisible (saveButton);

    saveAsButton.setButtonText (TR ("Save As…", "另存为…"));
    saveAsButton.onClick = [this] { askForNameAndSave(); };
    addAndMakeVisible (saveAsButton);

    deleteButton.setButtonText (TR ("Delete", "删除"));
    deleteButton.onClick = [this] { deleteCurrent(); };
    addAndMakeVisible (deleteButton);

    folderButton.setButtonText (TR ("Open Folder", "打开文件夹"));
    folderButton.onClick = [] { getPresetFolder().startAsProcess(); };
    addAndMakeVisible (folderButton);

    currentName = settings.getValue ("currentPreset");
    createDefaultPresetsIfNeeded();
    refreshList();
}

juce::File PresetBar::getPresetFolder()
{
    return AppPaths::presetsDir();
}

void PresetBar::migratePresetFileNames()
{
    auto folder = getPresetFolder();
    folder.createDirectory();

    for (auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.xml"))
    {
        const auto stem = f.getFileNameWithoutExtension();
        bool nonAscii = false;
        for (auto c : stem)
            if (c >= 128) { nonAscii = true; break; }

        if (! nonAscii)
            continue;

        auto doc = juce::parseXML (f);
        if (doc == nullptr)
            continue;

        const auto display = doc->getStringAttribute ("displayName").isNotEmpty()
                                 ? doc->getStringAttribute ("displayName")
                                 : stem;
        if (doc->getStringAttribute ("displayName").isEmpty())
            doc->setAttribute ("displayName", display);

        auto target = folder.getChildFile (slugifyPresetName (display) + ".xml");
        if (target == f)
            continue;

        if (target.existsAsFile())
        {
            // The ASCII copy already exists. Remove the old file only when it is
            // clearly the same preset, not a different user preset.
            auto targetDoc = juce::parseXML (target);
            if (targetDoc != nullptr && targetDoc->getStringAttribute ("displayName") == display)
                f.deleteFile();
            continue;
        }

        if (doc->writeTo (target))
            f.deleteFile();
    }
}

void PresetBar::createDefaultPresetsIfNeeded()
{
    auto folder = getPresetFolder();
    folder.createDirectory();
    migratePresetFileNames();
    const bool firstRun = folder.findChildFiles (juce::File::findFiles, false, "*.xml").isEmpty();
    const int seen = settings.getIntValue ("defaultPresetsVersion", firstRun ? 0 : 1);

    // 只补新版本加的预设，不覆盖、也不把用户删掉的旧预设再生成回来
    auto write = [&folder] (const char* name, const char* xml)
    {
        const juce::String display = T (name);
        auto doc = juce::parseXML (T (xml));
        if (doc == nullptr)
            return;

        // A NAM preset is only generated when its model file exists.
        for (auto* e : doc->getChildWithTagNameIterator ("Slot"))
            if (e->getStringAttribute ("kind") == "nam"
                && ! EffectChain::resolvePortablePath (e->getStringAttribute ("path")).existsAsFile())
                return;

        if (doc->getStringAttribute ("displayName").isEmpty())
            doc->setAttribute ("displayName", display);

        const auto f = folder.getChildFile (slugifyPresetName (display) + ".xml");
        if (f.existsAsFile())
            return;

        doc->writeTo (f);
    };

    if (seen < 1)
        for (auto& p : DefaultPresets::list)
            write (p[0], p[1]);
    if (seen < 2)
        for (auto& p : DefaultPresets::listV2)
            write (p[0], p[1]);
    if (seen < 3)
        for (auto& p : DefaultPresets::listV3)
            write (p[0], p[1]);
    if (seen < 4)
        for (auto& p : DefaultPresets::listGervana)
            write (p[0], p[1]);

    settings.setValue ("defaultPresetsVersion", DefaultPresets::version);

    // 压缩包里带的预设（程序旁边的“音色预设”文件夹）：补进来，不覆盖同名的。
    // 只在压缩包换了版本时补一次，用户自己删掉的不会每次都冒出来
    const auto bundled = AppPaths::defaultPresetsDir();
    if (bundled.isDirectory() && bundled != folder)
    {
        auto files = bundled.findChildFiles (juce::File::findFiles, false, "*.xml");
        juce::int64 stamp = 0;
        for (auto& f : files) stamp += f.getSize() + f.getLastModificationTime().toMilliseconds() / 1000;
        if (settings.getValue ("bundledPresetsStamp") != juce::String (stamp))
        {
            for (auto& f : files)
                if (! folder.getChildFile (f.getFileName()).existsAsFile())
                    f.copyFileTo (folder.getChildFile (f.getFileName()));
            settings.setValue ("bundledPresetsStamp", juce::String (stamp));
        }
    }
}

namespace
{
    juce::String categoryLabel (const juce::String& key)
    {
        if (key == "Gervana")     return "Gervana";
        if (key == "basic")       return TR ("Basic", "基础");
        if (key == "classic")     return TR ("Classic", "经典");
        if (key == "fender")      return TR ("Fender NAM", "Fender NAM");
        if (key == "nam")         return TR ("Captures", "真实采样");
        if (key == "pioneer")     return TR ("Pioneer", "先锋");
        if (key == "postrock")    return TR ("Post-rock", "后摇");
        if (key == "psychedelic") return TR ("Psychedelic", "迷幻");
        if (key == "postpunk")    return TR ("Post-punk", "后朋克");
        if (key == "weird")       return TR ("Experimental", "怪异");
        if (key == "bass")        return TR ("Bass", "贝斯");
        if (key == "user")        return TR ("My Presets", "我的预设");
        return key;
    }

    juce::String categoryKeyForPrefix (const juce::String& prefix)
    {
        if (prefix == T ("基础"))     return "basic";
        if (prefix == T ("经典"))     return "classic";
        if (prefix == T ("真实采样")) return "nam";
        if (prefix == T ("先锋"))     return "pioneer";
        if (prefix == T ("后摇"))     return "postrock";
        if (prefix == T ("迷幻"))     return "psychedelic";
        if (prefix == T ("后朋克"))   return "postpunk";
        if (prefix == T ("怪异"))     return "weird";
        if (prefix == T ("贝斯"))     return "bass";
        if (prefix == T ("我的预设")) return "user";

        // Future ASCII preset names use the key directly.
        static const juce::StringArray keys { "basic", "classic", "nam", "pioneer", "postrock",
                                              "psychedelic", "postpunk", "weird", "bass", "user" };
        if (keys.contains (prefix.toLowerCase()))
            return prefix.toLowerCase();
        return {};
    }
}

const juce::StringArray& PresetBar::categoryOrder()
{
    static const juce::StringArray order { "fender", "nam", "basic", "classic", "pioneer", "postrock",
                                           "psychedelic", "postpunk", "weird", "bass", "Gervana", "user" };
    return order;
}

juce::String PresetBar::categoryOf (const juce::String& name)
{
    if (Gervana::isGervanaPreset (name))
        return "Gervana";

    for (auto& p : DefaultPresets::list)
        if (name == T (p[0]))
            return "basic";

    const auto sep = juce::String::fromUTF8 (" \xc2\xb7 ");
    if (name.contains (sep))
    {
        const auto prefix = name.upToFirstOccurrenceOf (sep, false, false).trim();
        if (prefix == T ("真实采样") && name.containsIgnoreCase ("Fender"))
            return "fender";
        if (const auto key = categoryKeyForPrefix (prefix); key.isNotEmpty())
            return key;
    }
    return "user";
}

void PresetBar::refreshList()
{
    presetBox.clear (juce::dontSendNotification);
    auto files = getPresetFolder().findChildFiles (juce::File::findFiles, false, "*.xml");
    files.sort();

    // Group by category; the file name is an ASCII slug, the display name lives in
    // the XML and is localised here.
    std::map<juce::String, juce::StringArray> groups;
    fileByName.clear();
    nameById.clear();
    for (auto& f : files)
    {
        const auto display = presetDisplayName (f);
        fileByName[display] = f;
        groups[categoryOf (display)].add (display);
    }

    auto* root = presetBox.getRootMenu();
    int id = 1, selectedId = 0;
    currentPresetId = 0;

    root->addItem (9000, TR ("Browse NAM Models…", "浏览 NAM 模型…"));
    root->addSeparator();
    for (auto& cat : categoryOrder())
    {
        auto it = groups.find (cat);
        if (it == groups.end() || it->second.isEmpty())
            continue;

        const bool gv = cat == "Gervana";
        juce::PopupMenu sub;
        for (auto& name : it->second)
        {
            juce::PopupMenu::Item item (L (name.toRawUTF8()));
            item.itemID = id;
            nameById[id] = name;
            if (gv) item.colour = Gervana::gold();
            sub.addItem (item);
            if (name == currentName)
            {
                selectedId = id;
                currentPresetId = id;
            }
            ++id;
        }
        root->addSubMenu (categoryLabel (cat) + "  (" + juce::String (it->second.size()) + ")", sub);
    }
    if (selectedId > 0)
        presetBox.setSelectedId (selectedId, juce::dontSendNotification);

    // 当前是 Gervana 预设：下拉框里的字也换成金色
    presetBox.setColour (juce::ComboBox::textColourId,
                         Gervana::isGervanaPreset (currentName) ? Gervana::gold()
                                                                : getLookAndFeel().findColour (juce::Label::textColourId));
}

bool PresetBar::loadPreset (const juce::String& name)
{
    juce::File file;
    if (const auto it = fileByName.find (name); it != fileByName.end())
        file = it->second;
    else
    {
        file = getPresetFolder().getChildFile (slugifyPresetName (name) + ".xml");
        if (! file.existsAsFile())
            file = getPresetFolder().getChildFile (name + ".xml");
    }

    auto xml = juce::parseXML (file);
    if (xml == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, TR ("Failed to load preset", "读取预设失败"), file.getFullPathName());
        return false;
    }

    juce::StringArray problems;
    chain.loadFromXml (*xml, problems);
    currentName = name;
    settings.setValue ("currentPreset", currentName);
    refreshList();

    if (! problems.isEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                TR ("Some effects failed to load", "部分效果器没有加载"),
                                                problems.joinIntoString ("\n"));
    return true;
}

void PresetBar::savePreset (const juce::String& name)
{
    auto xml = chain.toXml();
    xml->setAttribute ("displayName", name);
    if (xml->writeTo (getPresetFolder().getChildFile (slugifyPresetName (name) + ".xml")))
    {
        currentName = name;
        settings.setValue ("currentPreset", currentName);
        refreshList();
    }
}

void PresetBar::askForNameAndSave()
{
    nameDialog = std::make_unique<juce::AlertWindow> (TR ("Save Tone Preset", "保存音色预设"), TR ("Name this tone:", "给这个音色起个名字："), juce::MessageBoxIconType::NoIcon);
    nameDialog->addTextEditor ("name", currentName.isNotEmpty() ? currentName + T (" 副本") : T ("我的音色"));
    nameDialog->addButton (TR ("Save", "保存"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    nameDialog->addButton (TR ("Cancel", "取消"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    nameDialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (result == 1 && nameDialog != nullptr)
        {
            const auto name = juce::File::createLegalFileName (nameDialog->getTextEditorContents ("name").trim());
            if (name.isNotEmpty())
                savePreset (name);
        }
        nameDialog.reset();
    }), false);
}

void PresetBar::deleteCurrent()
{
    if (currentName.isEmpty())
        return;

    const auto name = currentName;
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, TR ("Delete Preset", "删除预设"),
                                        TR ("Delete \"", "确定删除“") + name + TR ("\"?", "”吗？"), TR ("Delete", "删除"), TR ("Cancel", "取消"), this,
                                        juce::ModalCallbackFunction::create ([this, name] (int r)
    {
        if (r == 1)
        {
            juce::File file;
            if (const auto it = fileByName.find (name); it != fileByName.end())
                file = it->second;
            else
                file = getPresetFolder().getChildFile (slugifyPresetName (name) + ".xml");
            if (file.existsAsFile())
                file.deleteFile();
            currentName = {};
            settings.setValue ("currentPreset", currentName);
            refreshList();
        }
    }));
}

void PresetBar::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromLeft (76));
    folderButton.setBounds (r.removeFromRight (96).reduced (2));
    deleteButton.setBounds (r.removeFromRight (64).reduced (2));
    saveAsButton.setBounds (r.removeFromRight (84).reduced (2));
    saveButton.setBounds (r.removeFromRight (64).reduced (2));
    r.removeFromRight (8);
    presetBox.setBounds (r.reduced (0, 2));
}
