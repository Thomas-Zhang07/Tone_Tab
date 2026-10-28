#include "NamBrowser.h"
#include "Text.h"
#include "app/AppPaths.h"
#include <algorithm>

namespace
{
    class NamBrowserComponent final : public juce::Component,
                                      public juce::ListBoxModel
    {
    public:
        explicit NamBrowserComponent (std::function<void (juce::File)> pick)
            : onPick (std::move (pick))
        {
            addAndMakeVisible (search);
            search.setTextToShowWhenEmpty (TR ("Search name, amp, author...", "搜索名称、音箱、作者…"),
                                           juce::Colour (0xff777777));
            search.onTextChange = [this] { rebuild(); };

            addAndMakeVisible (filter);
            filter.addItem (TR ("All models", "全部模型"), 1);
            filter.addItem (TR ("Clean", "清音"), 2);
            filter.addItem (TR ("Crunch", "轻失真"), 3);
            filter.addItem (TR ("High gain", "高增益"), 4);
            filter.addItem (TR ("Boost / pedal", "推子 / 单块"), 5);
            filter.addItem (TR ("A2 full rig", "A2 带箱体"), 6);
            filter.addItem (TR ("Starter picks", "精选"), 7);
            filter.setSelectedId (1, juce::dontSendNotification);
            filter.onChange = [this] { rebuild(); };

            addAndMakeVisible (list);
            list.setModel (this);
            list.setRowHeight (30);
            list.setMultipleSelectionEnabled (false);
            list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff1d1d1d));

            loadButton.setButtonText (TR ("Load Model", "加载模型"));
            loadButton.setEnabled (false);
            loadButton.onClick = [this] { loadSelected(); };
            addAndMakeVisible (loadButton);

            cancelButton.setButtonText (TR ("Cancel", "取消"));
            cancelButton.onClick = [this]
            {
                if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
                    dialog->exitModalState (0);
            };
            addAndMakeVisible (cancelButton);

            status.setFont (juce::FontOptions (12.0f));
            status.setColour (juce::Label::textColourId, juce::Colour (0xff9a9a9a));
            addAndMakeVisible (status);

            loadModels();
            rebuild();
            setSize (760, 560);
        }

        int getNumRows() override { return (int) filtered.size(); }

        void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
        {
            if (! juce::isPositiveAndBelow (row, (int) filtered.size()))
                return;

            const auto& m = models[(size_t) filtered[(size_t) row]];
            if (selected)
                g.fillAll (juce::Colour (0xff2d5c4a));
            else if ((row & 1) != 0)
                g.fillAll (juce::Colour (0xff222222));

            auto r = juce::Rectangle<int> (0, 0, width, height).reduced (8, 0);
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (14.0f));
            g.drawText (m.name, r.removeFromLeft (width - 250), juce::Justification::centredLeft, true);

            g.setColour (juce::Colour (0xff9a9a9a));
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (m.collection + (m.tags.isNotEmpty() ? "  ·  " + m.tags : juce::String()),
                        r, juce::Justification::centredRight, true);
        }

        void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { loadSelected(); }

        void selectedRowsChanged (int) override
        {
            loadButton.setEnabled (list.getSelectedRow() >= 0);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff151515));
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (12);

            auto top = area.removeFromTop (28);
            search.setBounds (top.removeFromLeft (300));
            top.removeFromLeft (8);
            filter.setBounds (top.removeFromLeft (190));

            area.removeFromTop (8);
            auto bottom = area.removeFromBottom (34);
            loadButton.setBounds (bottom.removeFromRight (110).reduced (0, 2));
            bottom.removeFromRight (8);
            cancelButton.setBounds (bottom.removeFromRight (90).reduced (0, 2));
            status.setBounds (bottom);
            area.removeFromBottom (8);

            list.setBounds (area);
        }

    private:
        struct Model
        {
            juce::File file;
            juce::String name, collection, tags, search;
        };

        void loadModels()
        {
            const auto indexFile = AppPaths::modelsDir().getChildFile ("index.json");
            const auto index = juce::JSON::parse (indexFile);
            const auto* entries = index.getArray();
            if (entries == nullptr)
                return;

            for (auto& item : *entries)
            {
                const auto relative = item["file"].toString();
                const auto file = AppPaths::modelsDir().getChildFile (relative);
                if (! file.existsAsFile())
                    continue;

                auto meta = item["metadata"];
                auto name = meta["name"].toString();
                if (name.isEmpty())
                    name = file.getFileNameWithoutExtension();

                auto collection = item["collection"].toString();
                if (collection == "tone3000_a2")
                    collection = "A2";
                else if (collection == "community")
                    collection = "Community";
                else
                    collection = collection.substring (0, 1).toUpperCase() + collection.substring (1);

                juce::StringArray tagList;
                if (const auto* tags = item["tags"].getArray())
                    for (auto& t : *tags)
                        tagList.add (t.toString());

                if (const auto* aliases = item["aliases"].getArray())
                {
                    for (auto& a : *aliases)
                    {
                        if (name.isEmpty()) name = a["name"].toString();
                        if (const auto cat = a["category"].toString(); cat.isNotEmpty())
                            tagList.addIfNotAlreadyThere (cat);
                    }
                }

                tagList.removeDuplicates (false);
                Model m;
                m.file = file;
                m.name = name;
                m.collection = collection;
                m.tags = tagList.joinIntoString (", ");
                m.search = (name + " " + m.tags + " " + collection + " " + relative).toLowerCase();
                models.push_back (std::move (m));
            }

            std::sort (models.begin(), models.end(), [] (const Model& a, const Model& b)
                       { return a.name.compareIgnoreCase (b.name) < 0; });
        }

        void rebuild()
        {
            filtered.clear();
            const auto text = search.getText().trim().toLowerCase();
            const int f = filter.getSelectedId();

            for (size_t i = 0; i < models.size(); ++i)
            {
                const auto& m = models[i];
                if (text.isNotEmpty() && ! m.search.contains (text))
                    continue;

                auto hasTag = [&m] (const char* tag) { return m.tags.containsIgnoreCase (tag); };
                const bool keep = f == 1
                               || (f == 2 && hasTag ("clean"))
                               || (f == 3 && hasTag ("crunch"))
                               || (f == 4 && hasTag ("high_gain"))
                               || (f == 5 && hasTag ("boost"))
                               || (f == 6 && (hasTag ("a2") || hasTag ("full_rig")))
                               || (f == 7 && hasTag ("starter"));
                if (keep)
                    filtered.push_back (i);
            }

            list.updateContent();
            list.repaint();
            status.setText (juce::String (filtered.size()) + " / " + juce::String (models.size()) + " models",
                            juce::dontSendNotification);
        }

        void loadSelected()
        {
            const int row = list.getSelectedRow();
            if (! juce::isPositiveAndBelow (row, (int) filtered.size()))
                return;

            const auto file = models[(size_t) filtered[(size_t) row]].file;
            if (onPick)
                onPick (file);

            if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
                dialog->exitModalState (0);
        }

        std::function<void (juce::File)> onPick;
        std::vector<Model> models;
        std::vector<size_t> filtered;

        juce::TextEditor search;
        juce::ComboBox filter;
        juce::ListBox list;
        juce::TextButton loadButton, cancelButton;
        juce::Label status;
    };
}

void NamBrowser::show (juce::Component* parent, std::function<void (juce::File)> onPick)
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new NamBrowserComponent (std::move (onPick)));
    options.dialogTitle = TR ("NAM Model Browser", "NAM 模型浏览器");
    options.dialogBackgroundColour = juce::Colour (0xff151515);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.componentToCentreAround = parent;
    options.launchAsync();
}
