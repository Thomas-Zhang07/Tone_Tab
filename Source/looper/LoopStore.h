#pragma once

#include <juce_core/juce_core.h>

class Looper;

// Saving loop-station takes. Loops live in their own folder
// (<recordings>/loops/<timestamp>/loop.wav + loop.json) so they do not mix
// with the Record view's takes, while each loop still carries its text notes.
namespace LoopStore
{
    juce::File getLoopsFolder();

    // Writes loop.wav and loop.json. If `folder` is not an existing directory a
    // new timestamped folder is created under getLoopsFolder(). Returns the
    // folder, or an empty file with `error` set on failure.
    juce::File saveLoop (Looper& looper, const juce::File& folder,
                         const juce::String& notes, const juce::String& title,
                         juce::String& error);

    // Updates only the notes in an already saved loop.json.
    bool updateNotes (const juce::File& folder, const juce::String& notes, juce::String& error);
}
