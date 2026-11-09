#include "LoopStore.h"
#include "Looper.h"
#include "app/AppPaths.h"
#include "Text.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace LoopStore
{
namespace
{
    const char* const jsonFileName = "loop.json";
    const char* const wavFileName  = "loop.wav";

    juce::var makeLoopJson (const Looper& looper, double sampleRate, double lengthSeconds,
                            const juce::String& notes, const juce::String& title)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("version", 1);
        o->setProperty ("kind", "loop");
        o->setProperty ("title", title.isNotEmpty() ? title : juce::String ("Loop"));
        o->setProperty ("created", juce::Time::getCurrentTime().toMilliseconds());
        o->setProperty ("sampleRate", sampleRate);
        o->setProperty ("length", lengthSeconds);
        o->setProperty ("bpm", looper.getLoopBpm());
        o->setProperty ("bars", looper.getLoopBars());
        o->setProperty ("beatsPerBar", looper.getLoopBeatsPerBar());
        o->setProperty ("layers", looper.getLayerCount());
        o->setProperty ("notes", notes);
        o->setProperty ("audioFile", wavFileName);
        return juce::var (o);
    }

    bool writeJson (const juce::File& file, const juce::var& v, juce::String& error)
    {
        if (! file.replaceWithText (juce::JSON::toString (v, false)))
        {
            error = TR ("Could not write ", "没法写入 ") + file.getFullPathName();
            return false;
        }
        return true;
    }
}

juce::File getLoopsFolder()
{
    auto folder = AppPaths::recordingsDir().getChildFile ("loops");
    folder.createDirectory();
    return folder;
}

juce::File saveLoop (Looper& looper, const juce::File& folder,
                     const juce::String& notes, const juce::String& title,
                     juce::String& error)
{
    error.clear();

    juce::AudioBuffer<float> audio;
    double sampleRate = 0.0;
    if (! looper.copyLoopTo (audio, sampleRate) || audio.getNumSamples() <= 0)
    {
        error = TR ("Record a loop before saving", "先录一个循环再保存");
        return {};
    }

    auto dir = folder;
    if (! dir.isDirectory())
    {
        const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");
        dir = getLoopsFolder().getChildFile ("Loop " + stamp).getNonexistentSibling();
        if (! dir.createDirectory())
        {
            error = TR ("Could not create the loop folder: ", "没法创建循环文件夹：")
                    + dir.getFullPathName();
            return {};
        }
    }

    const auto wavFile = dir.getChildFile (wavFileName);
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (wavFile.createOutputStream());
        if (stream == nullptr)
        {
            error = TR ("Could not create ", "没法创建 ") + wavFile.getFullPathName();
            return {};
        }

        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), sampleRate, 2u, 24, {}, 0));
        if (writer == nullptr)
        {
            error = TR ("Could not open the WAV writer", "没法打开 WAV 写入器");
            return {};
        }

        stream.release();   // owned by the writer
        if (! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()))
        {
            error = TR ("Could not write the loop audio", "没法写入循环音频");
            return {};
        }
    }

    const double lengthSeconds = (double) audio.getNumSamples() / juce::jmax (1.0, sampleRate);
    const auto json = makeLoopJson (looper, sampleRate, lengthSeconds, notes, title);
    if (! writeJson (dir.getChildFile (jsonFileName), json, error))
        return {};

    return dir;
}

bool updateNotes (const juce::File& folder, const juce::String& notes, juce::String& error)
{
    error.clear();

    if (! folder.isDirectory())
    {
        error = TR ("Save the loop first", "先保存循环");
        return false;
    }

    const auto file = folder.getChildFile (jsonFileName);
    auto v = file.existsAsFile() ? juce::JSON::parse (file) : juce::var();
    auto* o = v.getDynamicObject();
    if (o == nullptr)
    {
        auto* fresh = new juce::DynamicObject();
        fresh->setProperty ("version", 1);
        fresh->setProperty ("kind", "loop");
        fresh->setProperty ("created", juce::Time::getCurrentTime().toMilliseconds());
        fresh->setProperty ("audioFile", wavFileName);
        v = juce::var (fresh);
        o = fresh;
    }

    o->setProperty ("notes", notes);
    return writeJson (file, v, error);
}
}
