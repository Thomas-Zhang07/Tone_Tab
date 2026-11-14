#include "LoopStore.h"
#include "Looper.h"
#include "app/AppPaths.h"
#include "Text.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace LoopStore
{
namespace
{
    const char* const jsonFileName = "loop.json";
    const char* const wavFileName  = "loop.wav";
    constexpr int jsonVersion = 2;

    juce::var makeLoopJson (const Looper& looper, double sampleRate, double lengthSeconds,
                            const juce::String& notes, const juce::String& title,
                            juce::int64 created)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("version", jsonVersion);
        o->setProperty ("kind", "loop");
        o->setProperty ("title", title.isNotEmpty() ? title : juce::String ("Loop"));
        o->setProperty ("created", created);
        o->setProperty ("sampleRate", sampleRate);
        o->setProperty ("length", lengthSeconds);
        o->setProperty ("bpm", looper.getLoopBpm());
        o->setProperty ("bars", looper.getLoopBars());
        o->setProperty ("beatsPerBar", looper.getLoopBeatsPerBar());
        o->setProperty ("layers", looper.getLayerCount());
        o->setProperty ("levelDb", (double) looper.getLevelDb());
        o->setProperty ("quantise", (int) looper.getQuantise());
        o->setProperty ("countInBeats", looper.getCountInBeats());
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

    // ---- tempo matching -----------------------------------------------------
    // WSOLA: overlap-add Hann windows; each frame searches a small neighbourhood
    // of its nominal input position for the segment that continues the previous
    // frame most smoothly. Pitch is preserved, unlike plain resampling.
    void wsolaStretch (const juce::AudioBuffer<float>& src, double speed,
                       juce::AudioBuffer<float>& dest)
    {
        const int inLen = juce::jmax (1, src.getNumSamples());
        const int srcChannels = juce::jlimit (1, 2, src.getNumChannels());
        int outLen = juce::jmax (1, (int) std::llround ((double) inLen / speed));

        dest.setSize (2, outLen, false, false, true);
        dest.clear();

        constexpr int windowSize = 2048;
        constexpr int hop = windowSize / 2;
        constexpr int search = 128;

        // Very short takes: fall back to linear resampling. The pitch drift is
        // irrelevant at this length and it avoids a frame smaller than a grain.
        if (inLen < windowSize * 2)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = dest.getWritePointer (ch);
                const int sourceCh = juce::jmin (ch, srcChannels - 1);
                for (int i = 0; i < outLen; ++i)
                {
                    const double pos = (double) i * speed;
                    const int i0 = juce::jlimit (0, inLen - 1, (int) pos);
                    const int i1 = juce::jmin (inLen - 1, i0 + 1);
                    const float frac = (float) (pos - (double) i0);
                    d[i] = src.getSample (sourceCh, i0) * (1.0f - frac)
                           + src.getSample (sourceCh, i1) * frac;
                }
            }
            return;
        }

        std::vector<float> window ((size_t) windowSize);
        for (int i = 0; i < windowSize; ++i)
            window[(size_t) i] = 0.5f - 0.5f * (float) std::cos (
                juce::MathConstants<double>::twoPi * (double) i / (double) windowSize);

        // The mono mix drives the correlation; both sides get the same offset.
        std::vector<float> mono ((size_t) inLen);
        auto* left  = src.getReadPointer (0);
        auto* right = src.getReadPointer (juce::jmin (1, srcChannels - 1));
        for (int i = 0; i < inLen; ++i)
            mono[(size_t) i] = 0.5f * (left[i] + right[i]);

        auto score = [&mono, inLen, hop] (int candidate, int continuation)
        {
            if (candidate < 0 || continuation < 0
                || candidate + hop > inLen || continuation + hop > inLen)
                return -2.0;

            double ab = 0.0, aa = 0.0, bb = 0.0;
            for (int i = 0; i < hop; i += 4)
            {
                const double a = (double) mono[(size_t) (candidate + i)];
                const double b = (double) mono[(size_t) (continuation + i)];
                ab += a * b;
                aa += a * a;
                bb += b * b;
            }
            return ab / (std::sqrt (aa * bb) + 1.0e-12);
        };

        const double analysisHop = (double) hop * speed;
        int previous = 0;

        for (int m = 0; ; ++m)
        {
            const int outPos = m * hop;
            if (outPos >= outLen)
                break;

            const int nominal = (int) std::llround ((double) m * analysisHop);
            int best = juce::jlimit (0, inLen - windowSize, nominal);

            if (m > 0)
            {
                const int continuation = previous + hop;
                double bestScore = score (best, continuation);

                for (int d = -search; d <= search; d += 8)
                {
                    const int candidate = juce::jlimit (0, inLen - windowSize, nominal + d);
                    const double s = score (candidate, continuation);
                    if (s > bestScore) { bestScore = s; best = candidate; }
                }

                for (int d = -8; d <= 8; d += 2)
                {
                    const int candidate = juce::jlimit (0, inLen - windowSize, best + d);
                    const double s = score (candidate, continuation);
                    if (s > bestScore) { bestScore = s; best = candidate; }
                }
            }

            const int count = juce::jmin (windowSize, inLen - best);
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = dest.getWritePointer (ch);
                const int sourceCh = juce::jmin (ch, srcChannels - 1);
                const auto* s = src.getReadPointer (sourceCh);
                for (int i = 0; i < count && outPos + i < outLen; ++i)
                    d[outPos + i] += s[best + i] * window[(size_t) i];
            }

            previous = best;
        }
    }
}

juce::File getLoopsFolder()
{
    auto folder = AppPaths::recordingsDir().getChildFile ("loops");
    folder.createDirectory();
    return folder;
}

bool readInfo (const juce::File& folder, LoopInfo& info)
{
    info = {};
    info.folder = folder;

    if (! folder.isDirectory())
        return false;

    info.title = folder.getFileName();
    info.created = folder.getLastModificationTime().toMilliseconds();
    info.audioFile = folder.getChildFile (wavFileName);

    const auto jsonFile = folder.getChildFile (jsonFileName);
    if (jsonFile.existsAsFile())
    {
        const auto v = juce::JSON::parse (jsonFile);
        if (auto* o = v.getDynamicObject())
        {
            const auto title = o->getProperty ("title").toString();
            if (title.isNotEmpty())
                info.title = title;

            info.notes = o->getProperty ("notes").toString();

            const auto created = (juce::int64) o->getProperty ("created");
            if (created > 0)
                info.created = created;

            info.bpm          = (double) o->getProperty ("bpm");
            info.bars         = (double) o->getProperty ("bars");
            info.length       = (double) o->getProperty ("length");
            info.sampleRate   = (double) o->getProperty ("sampleRate");
            info.levelDb      = o->hasProperty ("levelDb") ? (double) o->getProperty ("levelDb") : -3.0;
            info.beatsPerBar  = o->hasProperty ("beatsPerBar")
                                    ? juce::jlimit (1, 12, (int) o->getProperty ("beatsPerBar")) : 4;
            info.layers       = juce::jmax (1, (int) o->getProperty ("layers"));

            const auto audioName = o->getProperty ("audioFile").toString();
            if (audioName.isNotEmpty())
                info.audioFile = folder.getChildFile (audioName);
        }
    }

    if (! info.audioFile.existsAsFile())
        info.audioFile = folder.getChildFile (wavFileName);

    // Older loop.json files may only carry some of these. Derive the rest so the
    // browser and the looper can still line a take up with the tempo.
    if (info.length > 0.0 && info.beatsPerBar > 0)
    {
        if (info.bpm <= 1.0 && info.bars > 0.0)
            info.bpm = info.bars * (double) info.beatsPerBar * 60.0 / info.length;
        if (info.bars <= 0.0 && info.bpm > 1.0)
            info.bars = info.length * info.bpm / 60.0 / (double) info.beatsPerBar;
    }

    return info.audioFile.existsAsFile();
}

std::vector<LoopInfo> scanLoops()
{
    std::vector<LoopInfo> loops;

    for (auto& dir : getLoopsFolder().findChildFiles (juce::File::findDirectories, false))
    {
        LoopInfo info;
        if (readInfo (dir, info))
            loops.push_back (std::move (info));
    }

    std::sort (loops.begin(), loops.end(), [] (const LoopInfo& a, const LoopInfo& b)
    {
        if (a.created != b.created)
            return a.created > b.created;
        return a.title.compareIgnoreCase (b.title) < 0;
    });

    return loops;
}

bool loadAudio (const LoopInfo& info, juce::AudioBuffer<float>& dest,
                double& sampleRateOut, juce::String& error)
{
    error.clear();
    dest.setSize (0, 0);
    sampleRateOut = 0.0;

    auto file = info.audioFile;
    if (! file.existsAsFile())
        file = info.folder.getChildFile (wavFileName);

    if (! file.existsAsFile())
    {
        error = TR ("The loop audio file is missing: ", "缺少循环音频文件：") + file.getFullPathName();
        return false;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
    {
        error = TR ("Could not read the loop audio: ", "没法读取循环音频：") + file.getFileName();
        return false;
    }

    const int samples = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) 0x7fffffff);
    const int channels = juce::jlimit (1, 2, (int) reader->numChannels);

    juce::AudioBuffer<float> tmp (channels, samples);
    tmp.clear();
    if (! reader->read (&tmp, 0, samples, 0, true, true))
    {
        error = TR ("Could not decode the loop audio: ", "没法解码循环音频：") + file.getFileName();
        return false;
    }

    dest.setSize (2, samples, false, false, true);
    dest.copyFrom (0, 0, tmp, 0, 0, samples);
    dest.copyFrom (1, 0, tmp, channels > 1 ? 1 : 0, 0, samples);

    sampleRateOut = reader->sampleRate;
    return true;
}

bool stretchToTempo (const juce::AudioBuffer<float>& source, double sourceSampleRate,
                     double sourceBpm, double targetBpm,
                     juce::AudioBuffer<float>& dest, double& sampleRateOut,
                     juce::String& error)
{
    error.clear();
    sampleRateOut = sourceSampleRate;

    if (source.getNumSamples() <= 0)
    {
        error = TR ("The loop has no audio", "循环里没有音频");
        return false;
    }

    if (! (sourceBpm > 1.0) || ! (targetBpm > 1.0)
        || std::abs (sourceBpm - targetBpm) < 0.01)
    {
        dest.makeCopyOf (source);
        return true;
    }

    const double speed = targetBpm / sourceBpm;
    if (speed <= 0.05 || speed >= 20.0)
    {
        error = TR ("The tempo difference is too large to match",
                    "速度差太大，没法匹配");
        return false;
    }

    const double outSeconds = ((double) source.getNumSamples() / juce::jmax (1.0, sourceSampleRate)) / speed;
    if (outSeconds > Looper::getMaxSeconds() + 0.01)
    {
        error = TR ("Matching this loop to the current tempo would exceed the 2:00 buffer.",
                    "按当前速度匹配后，循环会超过 2:00 缓冲区。");
        return false;
    }

    wsolaStretch (source, speed, dest);
    return true;
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

    // Re-saving a loop keeps its original creation time and title.
    juce::int64 created = juce::Time::getCurrentTime().toMilliseconds();
    juce::String finalTitle = title;
    const auto existingJson = dir.getChildFile (jsonFileName);
    if (existingJson.existsAsFile())
    {
        const auto old = juce::JSON::parse (existingJson);
        if (auto* o = old.getDynamicObject())
        {
            const auto oldCreated = (juce::int64) o->getProperty ("created");
            if (oldCreated > 0)
                created = oldCreated;
            if (finalTitle.isEmpty())
                finalTitle = o->getProperty ("title").toString();
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
    const auto json = makeLoopJson (looper, sampleRate, lengthSeconds, notes, finalTitle, created);
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
        fresh->setProperty ("version", jsonVersion);
        fresh->setProperty ("kind", "loop");
        fresh->setProperty ("created", juce::Time::getCurrentTime().toMilliseconds());
        fresh->setProperty ("audioFile", wavFileName);
        v = juce::var (fresh);
        o = fresh;
    }

    o->setProperty ("notes", notes);
    o->setProperty ("notesUpdated", juce::Time::getCurrentTime().toMilliseconds());
    return writeJson (file, v, error);
}
}
