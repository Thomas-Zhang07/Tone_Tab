#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

class Looper;

// Saving loop-station takes. Loops live in their own folder
// (<recordings>/loops/<timestamp>/loop.wav + loop.json) so they do not mix
// with the Record view's takes, while each loop still carries its text notes.
namespace LoopStore
{
    // One saved loop as listed by the loop-station browser.
    struct LoopInfo
    {
        juce::File folder;
        juce::File audioFile;
        juce::String title;
        juce::String notes;

        double bpm = 0.0;           // 0 when the take has no tempo metadata
        double bars = 0.0;          // 0 when unknown
        double length = 0.0;        // seconds
        double sampleRate = 0.0;
        double levelDb = -3.0;
        int beatsPerBar = 4;
        int layers = 1;
        juce::int64 created = 0;    // milliseconds since the epoch

        bool hasTempo() const noexcept { return bpm > 1.0; }
    };

    juce::File getLoopsFolder();

    // Every direct child folder that contains loop.wav (loop.json optional).
    // Newest first.
    std::vector<LoopInfo> scanLoops();

    // Reads one loop folder. Missing loop.json fields fall back to the folder
    // name / file times / values derived from the WAV length.
    bool readInfo (const juce::File& folder, LoopInfo& info);

    // Decodes loop.wav into a stereo buffer (mono is duplicated to both sides).
    bool loadAudio (const LoopInfo& info, juce::AudioBuffer<float>& dest,
                    double& sampleRateOut, juce::String& error);

    // Fits a take recorded at sourceBpm to targetBpm without changing its pitch.
    // Equal tempos (or missing tempo) return a copy. Used when a saved loop is
    // loaded into the loop station at the current drum-machine tempo.
    bool stretchToTempo (const juce::AudioBuffer<float>& source, double sourceSampleRate,
                         double sourceBpm, double targetBpm,
                         juce::AudioBuffer<float>& dest, double& sampleRateOut,
                         juce::String& error);

    // Writes loop.wav and loop.json. If `folder` is not an existing directory a
    // new timestamped folder is created under getLoopsFolder(). Returns the
    // folder, or an empty file with `error` set on failure.
    juce::File saveLoop (Looper& looper, const juce::File& folder,
                         const juce::String& notes, const juce::String& title,
                         juce::String& error);

    // Updates only the notes in an already saved loop.json.
    bool updateNotes (const juce::File& folder, const juce::String& notes, juce::String& error);
}
