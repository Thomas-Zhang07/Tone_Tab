#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// Model browser for the merged NAM library (assets/nam/index.json).
class NamBrowser
{
public:
    static void show (juce::Component* parent, std::function<void (juce::File)> onPick);
};
