#include "Recorder.h"
#include "TabPlayer.h"
#include "app/AppPaths.h"
#include "Text.h"

// ---- Take metadata -----------------------------------------------------------
juce::var Recorder::Info::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("version", version);
    o->setProperty ("song", songFile.getFullPathName());
    o->setProperty ("title", songTitle);
    o->setProperty ("track", track);
    o->setProperty ("speed", speed);
    o->setProperty ("loopOn", loopOn);
    o->setProperty ("loopA", loopA);
    o->setProperty ("loopB", loopB);
    o->setProperty ("countIn", countIn);
    o->setProperty ("metronome", metronome);
    o->setProperty ("waitMode", waitMode);
    o->setProperty ("synced", synced);
    o->setProperty ("syncSample", (juce::int64) syncSample);
    o->setProperty ("syncBeat", syncBeat);
    o->setProperty ("sampleRate", sampleRate);
    o->setProperty ("length", lengthSeconds);
    o->setProperty ("chainLatencySamples", chainLatencySamples);
    o->setProperty ("mixFile", mixFile.getFileName());
    o->setProperty ("dryFile", dryFile.getFileName());
    o->setProperty ("created", created.toMilliseconds());
    return juce::var (o);
}

Recorder::Info Recorder::Info::fromVar (const juce::var& v)
{
    Info i;
    if (! v.isObject())
        return i;
    i.version = (int) v.getProperty ("version", 2);
    i.songFile = juce::File (v["song"].toString());
    i.songTitle = v["title"].toString();
    i.track = (int) v["track"];
    i.speed = (float) (double) v["speed"];
    i.loopOn = (bool) v["loopOn"];
    i.loopA = (double) v["loopA"];
    i.loopB = (double) v["loopB"];
    i.countIn = (bool) v["countIn"];
    i.metronome = (bool) v["metronome"];
    i.waitMode = (bool) v["waitMode"];
    i.synced = (bool) v["synced"];
    i.syncSample = (int64_t) (juce::int64) v["syncSample"];
    i.syncBeat = (double) v["syncBeat"];
    i.sampleRate = (double) v["sampleRate"];
    i.lengthSeconds = (double) v["length"];
    i.chainLatencySamples = (int) v.getProperty ("chainLatencySamples", 0);
    i.mixFile = juce::File (v.getProperty ("mixFile", "").toString());
    i.dryFile = juce::File (v.getProperty ("dryFile", "").toString());
    i.created = juce::Time ((juce::int64) v["created"]);
    if (i.speed <= 0.0f) i.speed = 1.0f;
    return i;
}

Recorder::Info Recorder::Info::loadFor (const juce::File& wav)
{
    const auto folder = wav.getParentDirectory();
    const auto takeJson = folder.getChildFile ("take.json");
    const auto legacyJson = wav.withFileExtension ("json");

    juce::var v;
    if (takeJson.existsAsFile())
        v = juce::JSON::parse (takeJson);
    else
        v = juce::JSON::parse (legacyJson);

    auto info = fromVar (v);
    if (info.created.toMilliseconds() == 0)
        info.created = wav.getLastModificationTime();

    if (! info.mixFile.existsAsFile())
        info.mixFile = wav;

    const auto dry = folder.getChildFile ("dry.wav");
    if (! info.dryFile.existsAsFile())
        info.dryFile = dry.existsAsFile() ? dry : wav;

    return info;
}

// =============================================================================
Recorder::Recorder (TabPlayer& p) : player (p) {}

Recorder::~Recorder()
{
    stopRecording();
    stopPlayback();
    mixThread.stopThread (2000);
    dryThread.stopThread (2000);
}

juce::File Recorder::getFolder()
{
    return AppPaths::recordingsDir();
}

void Recorder::setSampleRate (double sr)
{
    deviceRate.store (sr);
    if (sr <= 0.0)
        return;

    const auto size = (size_t) juce::jmax (1024.0, sr) + 4;
    if (dryDelay.size() != size)
    {
        dryDelay.assign (size, 0.0f);
        dryScratch.assign (8192, 0.0f);
    }
}

void Recorder::setChainLatencySamples (int samples) noexcept
{
    chainLatency.store (juce::jmax (0, samples));
}

// ---- Recording (message thread) ---------------------------------------------
bool Recorder::startRecording (const Info& info, juce::String& error)
{
    stopRecording();

    const double sampleRate = deviceRate.load();
    if (sampleRate <= 0.0)
    {
        error = TR ("Audio device is not open", "声卡没有打开");
        return false;
    }

    auto folder = getFolder();
    folder.createDirectory();

    juce::String name = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");
    if (info.songTitle.isNotEmpty())
        name << " " << juce::File::createLegalFileName (info.songTitle).substring (0, 40);

    takeFolder = folder.getChildFile (name).getNonexistentSibling();
    if (! takeFolder.createDirectory())
    {
        error = TR ("Cannot create the take folder: ", "没法创建录音文件夹：") + takeFolder.getFullPathName();
        return false;
    }

    mixFile = takeFolder.getChildFile ("mix.wav");
    dryFile = takeFolder.getChildFile ("dry.wav");

    auto createWriter = [sampleRate] (const juce::File& f, int channels,
                                      juce::TimeSliceThread& thread)
        -> std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter>
    {
        std::unique_ptr<juce::OutputStream> stream (f.createOutputStream());
        if (stream == nullptr)
            return nullptr;

        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer (
            format.createWriterFor (stream.get(), sampleRate, (unsigned int) channels, 24, {}, 0));
        if (writer == nullptr)
            return nullptr;

        stream.release();   // owned by the writer
        thread.startThread();
        return std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer.release(), thread, 1 << 17);
    };

    mixWriter = createWriter (mixFile, 2, mixThread);
    dryWriter = createWriter (dryFile, 1, dryThread);

    if (mixWriter == nullptr || dryWriter == nullptr)
    {
        error = TR ("Failed to create WAV files in ", "没法在这里创建 WAV 文件：") + takeFolder.getFullPathName();
        mixWriter.reset();
        dryWriter.reset();
        takeFolder.deleteRecursively();
        return false;
    }

    pendingInfo = info;
    pendingInfo.version = 2;
    pendingInfo.sampleRate = sampleRate;
    pendingInfo.created = juce::Time::getCurrentTime();
    pendingInfo.mixFile = mixFile;
    pendingInfo.dryFile = dryFile;
    pendingInfo.chainLatencySamples = chainLatency.load();

    recordRate = sampleRate;
    samplesRecorded.store (0);
    syncCaptured.store (false);
    syncSample.store (0);
    syncBeat.store (0.0);
    lastTabPlaying = false;
    dryDelayPos = 0;
    std::fill (dryDelay.begin(), dryDelay.end(), 0.0f);

    activeMix.store (mixWriter.get());
    activeDry.store (dryWriter.get());
    recording.store (true);
    return true;
}

juce::File Recorder::stopRecording()
{
    if (! recording.exchange (false))
        return {};

    activeMix.store (nullptr);
    activeDry.store (nullptr);

    // Wait for any in-flight audio-thread write before destroying the writers.
    while (captureUsers.load() > 0)
        juce::Thread::yield();

    mixWriter.reset();
    dryWriter.reset();

    pendingInfo.synced = syncCaptured.load();
    pendingInfo.syncSample = syncSample.load();
    pendingInfo.syncBeat = syncBeat.load();
    pendingInfo.lengthSeconds = (double) samplesRecorded.load() / recordRate;
    pendingInfo.chainLatencySamples = chainLatency.load();

    if (takeFolder.isDirectory())
        takeFolder.getChildFile ("take.json")
                  .replaceWithText (juce::JSON::toString (pendingInfo.toVar()));

    return mixFile;
}

double Recorder::getRecordedSeconds() const
{
    return recording.load() ? (double) samplesRecorded.load() / recordRate : 0.0;
}

// ---- Playback (message thread) ----------------------------------------------
bool Recorder::startPlayback (const juce::File& wav, juce::String& error, Info* infoOut)
{
    stopPlayback();

    // A mix.wav plays its sibling dry.wav through the current chain (re-amping).
    auto actual = wav;
    if (wav.getFileName().equalsIgnoreCase ("mix.wav"))
        if (const auto dry = wav.getSiblingFile ("dry.wav"); dry.existsAsFile())
            actual = dry;

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (actual));
    if (reader == nullptr)
    {
        error = TR ("Cannot read this recording: ", "读不了这个录音：") + actual.getFileName();
        return false;
    }
    if (reader->lengthInSamples > (juce::int64) (reader->sampleRate * 60 * 30))
    {
        error = TR ("Recording too long (over 30 minutes)", "录音太长了（超过 30 分钟）");
        return false;
    }

    auto data = std::make_shared<juce::AudioBuffer<float>> (1, (int) reader->lengthInSamples);
    reader->read (data.get(), 0, (int) reader->lengthInSamples, 0, true, false);

    const auto info = Info::loadFor (wav);
    if (infoOut != nullptr) *infoOut = info;

    playbackRate = reader->sampleRate;
    playbackLength = (double) reader->lengthInSamples / reader->sampleRate;
    playbackSync.store (info.synced && ! info.waitMode);
    playbackSyncSample.store (info.syncSample);
    playbackSyncBeat.store (info.syncBeat);
    playbackPos.store (0);

    if (playbackData != nullptr)
        retired.push_back (playbackData);
    playbackData = std::move (data);
    playbackPtr.store (playbackData.get());
    playbackOn.store (true);
    return true;
}

void Recorder::stopPlayback()
{
    playbackOn.store (false);
    playbackPtr.store (nullptr);
}

double Recorder::getPlaybackSeconds() const
{
    return (double) playbackPos.load() / playbackRate;
}

void Recorder::releaseRetired()
{
    auto* inUse = playbackInUse.load();
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [inUse] (const std::shared_ptr<juce::AudioBuffer<float>>& b) { return b.get() != inUse; }),
                   retired.end());
    if (! playbackOn.load() && playbackData != nullptr && inUse != playbackData.get())
        playbackData = nullptr;
}

// ---- Audio thread ------------------------------------------------------------
bool Recorder::replaceInput (float* mono, int n) noexcept
{
    auto* buf = playbackOn.load() ? playbackPtr.load() : nullptr;
    playbackInUse.store (buf);
    if (buf == nullptr)
        return false;

    const int64_t pos = playbackPos.load();
    const int64_t len = buf->getNumSamples();
    const int avail = (int) juce::jlimit ((int64_t) 0, (int64_t) n, len - pos);
    if (avail > 0)
        juce::FloatVectorOperations::copy (mono, buf->getReadPointer (0, (int) pos), avail);
    if (avail < n)
        juce::FloatVectorOperations::clear (mono + avail, n - avail);

    if (playbackSync.load() && pos <= playbackSyncSample.load() && playbackSyncSample.load() < pos + n)
    {
        playbackSync.store (false);
        player.seek (playbackSyncBeat.load());
        player.startFromAudioThread();
    }

    playbackPos.store (pos + n);
    if (pos + n >= len)
    {
        playbackOn.store (false);
        player.setPlayingFlag (false);
    }
    return true;
}

void Recorder::capture (const float* mono, int n, bool tabPlaying, double tabBeatBeforeRender) noexcept
{
    if (! recording.load (std::memory_order_relaxed))
    {
        lastTabPlaying = tabPlaying;
        return;
    }

    if (tabPlaying && ! lastTabPlaying && ! syncCaptured.load())
    {
        syncSample.store (samplesRecorded.load());
        syncBeat.store (tabBeatBeforeRender);
        syncCaptured.store (true);
    }
    lastTabPlaying = tabPlaying;

    auto* writer = activeDry.load (std::memory_order_acquire);
    if (writer == nullptr)
    {
        samplesRecorded.fetch_add (n);
        return;
    }

    captureUsers.fetch_add (1, std::memory_order_acq_rel);
    if (activeDry.load (std::memory_order_acquire) == writer)
    {
        const float* source = mono;
        const int delay = juce::jlimit (0, juce::jmax (0, (int) dryDelay.size() - 1), chainLatency.load());

        if (delay > 0 && (int) dryScratch.size() >= n && (int) dryDelay.size() > delay)
        {
            const int size = (int) dryDelay.size();
            for (int i = 0; i < n; ++i)
            {
                dryDelay[(size_t) dryDelayPos] = mono[i];
                const int read = ((int) dryDelayPos - delay + size) % size;
                dryScratch[(size_t) i] = dryDelay[(size_t) read];
                dryDelayPos = (uint32_t) ((dryDelayPos + 1) % (uint32_t) size);
            }
            source = dryScratch.data();
        }

        const float* channels[] = { source };
        writer->write (channels, n);
        samplesRecorded.fetch_add (n);
    }
    captureUsers.fetch_sub (1, std::memory_order_acq_rel);
}

void Recorder::captureMix (const float* left, const float* right, int n) noexcept
{
    if (! recording.load (std::memory_order_relaxed))
        return;

    auto* writer = activeMix.load (std::memory_order_acquire);
    if (writer == nullptr)
        return;

    captureUsers.fetch_add (1, std::memory_order_acq_rel);
    if (activeMix.load (std::memory_order_acquire) == writer)
    {
        const float* channels[] = { left, right };
        writer->write (channels, n);
    }
    captureUsers.fetch_sub (1, std::memory_order_acq_rel);
}
