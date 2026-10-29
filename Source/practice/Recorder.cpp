#include "Recorder.h"
#include "TabPlayer.h"
#include "app/AppPaths.h"
#include "Text.h"
#include <cmath>

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

    juce::Array<juce::var> clips;
    for (const auto& p : punches)
    {
        auto* c = new juce::DynamicObject();
        c->setProperty ("path", p.relativePath);
        c->setProperty ("start", p.startSeconds);
        c->setProperty ("end", p.endSeconds);
        c->setProperty ("sampleRate", p.sampleRate);
        clips.add (juce::var (c));
    }
    o->setProperty ("punches", clips);

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

    i.punches.clear();
    if (auto* clips = v["punches"].getArray())
    {
        for (const auto& pv : *clips)
        {
            PunchClip p;
            p.relativePath = pv["path"].toString();
            p.startSeconds = (double) pv["start"];
            p.endSeconds = (double) pv["end"];
            p.sampleRate = (double) pv["sampleRate"];
            p.file = juce::File (p.relativePath);
            i.punches.push_back (p);
        }
    }

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

    for (auto& p : info.punches)
        if (p.relativePath.isNotEmpty() && ! juce::File::isAbsolutePath (p.relativePath))
            p.file = folder.getChildFile (p.relativePath);

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

    if (punchScratch.size() < 16384)
        punchScratch.assign (16384, 0.0f);

    liveBucketSamples.store (juce::jmax (64, (int) std::lround (0.010 * sr)));
    if (liveWave == nullptr)
        liveWave = std::make_unique<LiveWaveArray>();
    for (auto& p : *liveWave)
        p.store (0.0f);
    liveWriteIndex = 0;
    liveFill = 0;
    livePeak = 0.0f;
    liveWaveWrite.store (0);
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
std::shared_ptr<juce::AudioBuffer<float>> Recorder::loadEffectiveDry (
    const juce::File& takeFile, juce::String& error, Info* infoOut, double* rateOut)
{
    auto info = Info::loadFor (takeFile);

    // A mix.wav plays its sibling dry.wav through the current chain (re-amping).
    auto actual = takeFile;
    if (takeFile.getFileName().equalsIgnoreCase ("mix.wav") && info.dryFile.existsAsFile())
        actual = info.dryFile;

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (actual));
    if (reader == nullptr)
    {
        error = TR ("Cannot read this recording: ", "读不了这个录音：") + actual.getFileName();
        return nullptr;
    }
    if (reader->lengthInSamples > (juce::int64) (reader->sampleRate * 60 * 30))
    {
        error = TR ("Recording too long (over 30 minutes)", "录音太长了（超过 30 分钟）");
        return nullptr;
    }

    auto data = std::make_shared<juce::AudioBuffer<float>> (1, (int) reader->lengthInSamples);
    reader->read (data.get(), 0, (int) reader->lengthInSamples, 0, true, false);

    const double baseRate = reader->sampleRate;
    auto* dst = data->getWritePointer (0);

    // Non-destructive punch layers: apply in order; later clips win.
    for (const auto& p : info.punches)
    {
        if (! p.file.existsAsFile())
            continue;

        std::unique_ptr<juce::AudioFormatReader> clipReader (fm.createReaderFor (p.file));
        if (clipReader == nullptr || clipReader->lengthInSamples <= 0)
            continue;

        const auto srcLen = (int) clipReader->lengthInSamples;
        std::vector<float> src ((size_t) srcLen, 0.0f);
        float* clipChannels[] = { src.data() };
        clipReader->read (clipChannels, 1, 0, srcLen);

        const int64_t start = (int64_t) std::llround (p.startSeconds * baseRate);
        if (start < 0 || start >= data->getNumSamples())
            continue;

        int64_t outLen = juce::jmin<int64_t> ((int64_t) data->getNumSamples() - start,
                                              (int64_t) std::llround ((double) srcLen
                                                                      * baseRate / clipReader->sampleRate));
        if (p.endSeconds > p.startSeconds + 0.001)
            outLen = juce::jmin (outLen, (int64_t) std::llround ((p.endSeconds - p.startSeconds) * baseRate));

        for (int64_t i = 0; i < outLen; ++i)
        {
            const double sp = (double) i * clipReader->sampleRate / baseRate;
            const auto i0 = (int) sp;
            const float frac = (float) (sp - (double) i0);
            const float a = src[(size_t) juce::jlimit (0, srcLen - 1, i0)];
            const float b = src[(size_t) juce::jlimit (0, srcLen - 1, i0 + 1)];
            dst[start + i] = a + (b - a) * frac;
        }
    }

    if (infoOut != nullptr)
        *infoOut = info;
    if (rateOut != nullptr)
        *rateOut = baseRate;

    return data;
}

bool Recorder::startPlayback (const juce::File& wav, juce::String& error, Info* infoOut,
                              double startSeconds)
{
    stopPlayback();

    Info info;
    double rate = 0.0;
    auto data = loadEffectiveDry (wav, error, &info, &rate);
    if (data == nullptr)
        return false;

    if (infoOut != nullptr) *infoOut = info;

    playbackRate = rate;
    playbackLength = data->getNumSamples() / rate;
    // Starting from a clicked position should not fire the tab-sync marker.
    playbackSync.store (info.synced && ! info.waitMode && startSeconds <= 0.0);
    playbackSyncSample.store (info.syncSample);
    playbackSyncBeat.store (info.syncBeat);
    playbackPos.store (juce::jlimit<int64_t> (0, data->getNumSamples(),
                                              (int64_t) (juce::jlimit (0.0, playbackLength, startSeconds)
                                                         * rate)));
    pendingSeek.store (-1);

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
    pendingSeek.store (-1);
}

void Recorder::setPlaybackPosition (double seconds) noexcept
{
    if (! playbackOn.load())
        return;

    const auto len = (int64_t) (playbackLength * playbackRate);
    pendingSeek.store (juce::jlimit<int64_t> (0, len, (int64_t) (juce::jmax (0.0, seconds) * playbackRate)));
    // A seek should not trigger the tab-sync marker later.
    playbackSync.store (false);
}

// ---- Non-destructive punch (message thread) ----------------------------------
bool Recorder::startPunchRecording (const juce::File& takeFile, double startSeconds,
                                    double endSeconds, juce::String& error)
{
    if (punchWriter != nullptr)
        finishPunchRecording();

    stopRecording();
    stopPlayback();

    const double device = deviceRate.load();
    if (device <= 0.0)
    {
        error = TR ("Audio device is not open", "声卡没有打开");
        return false;
    }

    const double start = juce::jmax (0.0, startSeconds);
    const double preRoll = 2.0;
    const double preRollStart = juce::jmax (0.0, start - preRoll);

    Info info;
    if (! startPlayback (takeFile, error, &info, preRollStart))
        return false;

    if (endSeconds <= start + 0.005)
    {
        stopPlayback();
        error = TR ("Select a region longer than 5 ms", "选区要大于 5 毫秒");
        return false;
    }

    if (std::abs (playbackRate - device) > 1.0)
    {
        stopPlayback();
        error = TR ("This take was recorded at ", "这个录音的采样率是 ")
                + juce::String (playbackRate, 0)
                + TR (" Hz; set the audio device to the same rate to punch.",
                     " Hz，请把声卡采样率设成相同值再补录。");
        return false;
    }

    const double end = juce::jmin (endSeconds, playbackLength);
    if (end <= start + 0.005)
    {
        stopPlayback();
        error = TR ("The selection is past the end of the take", "选区超出了录音结尾");
        return false;
    }

    const auto folder = takeFile.getParentDirectory();
    auto clipsDir = folder.getChildFile ("clips");
    if (! clipsDir.createDirectory())
    {
        stopPlayback();
        error = TR ("Cannot create the clips folder: ", "没法创建片段文件夹：") + clipsDir.getFullPathName();
        return false;
    }

    punchFile = clipsDir.getChildFile ("punch-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".wav")
                    .getNonexistentSibling();

    std::unique_ptr<juce::OutputStream> stream (punchFile.createOutputStream());
    if (stream == nullptr)
    {
        stopPlayback();
        error = TR ("Cannot create the punch file: ", "没法创建补录文件：") + punchFile.getFullPathName();
        return false;
    }

    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        format.createWriterFor (stream.get(), device, 1, 24, {}, 0));
    if (writer == nullptr)
    {
        stopPlayback();
        punchFile.deleteFile();
        error = TR ("Cannot write the punch file", "没法写入补录文件");
        return false;
    }
    stream.release();

    dryThread.startThread();
    punchWriter = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer.release(), dryThread, 1 << 16);

    punchInfo = info;
    punchStartSec = start;
    punchEndSec = end;
    punchClipRate = device;
    punchSamplesWritten.store (0);
    punchFinished.store (false);
    punchStartSample.store ((int64_t) std::llround (start * playbackRate));
    punchEndSample.store ((int64_t) std::llround (end * playbackRate));
    pendingSeek.store (-1);
    playbackSync.store (false);

    activePunch.store (punchWriter.get());
    punchRecording.store (true);
    return true;
}

void Recorder::stopPunchRecording()
{
    punchRecording.store (false);
    playbackOn.store (false);
    // Let the message thread close the writer and commit the clip even though
    // the audio thread did not reach the natural end of the selection.
    punchFinished.store (true);
}

double Recorder::getPunchRecordedSeconds() const
{
    return punchClipRate > 0.0 ? (double) punchSamplesWritten.load() / punchClipRate : 0.0;
}

juce::File Recorder::finishPunchRecording()
{
    punchRecording.store (false);
    playbackOn.store (false);
    activePunch.store (nullptr);

    while (punchWriteUsers.load() > 0)
        juce::Thread::yield();

    punchWriter.reset();
    punchFinished.store (false);

    const auto written = punchSamplesWritten.exchange (0);
    const auto clipFile = punchFile;

    if (written <= 0)
    {
        clipFile.deleteFile();
        return {};
    }

    auto folder = punchFile.getParentDirectory().getParentDirectory();
    if (punchInfo.mixFile.existsAsFile())
        folder = punchInfo.mixFile.getParentDirectory();

    PunchClip clip;
    clip.file = punchFile;
    clip.relativePath = punchFile.getRelativePathFrom (folder);
    clip.startSeconds = punchStartSec;
    clip.endSeconds = punchStartSec + (double) written / punchClipRate;
    clip.sampleRate = punchClipRate;
    punchInfo.punches.push_back (clip);

    folder.getChildFile ("take.json")
          .replaceWithText (juce::JSON::toString (punchInfo.toVar()));

    return punchFile;
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
void Recorder::updateLiveEnvelope (const float* mono, int n) noexcept
{
    if (liveWave == nullptr || mono == nullptr || n <= 0)
        return;

    const int bucket = juce::jmax (1, liveBucketSamples.load (std::memory_order_relaxed));

    for (int i = 0; i < n; ++i)
    {
        livePeak = juce::jmax (livePeak, std::abs (mono[i]));

        if (++liveFill >= bucket)
        {
            (*liveWave)[(size_t) (liveWriteIndex & (uint32_t) (liveWaveBuckets - 1))]
                .store (livePeak, std::memory_order_relaxed);
            ++liveWriteIndex;
            liveWaveWrite.store (liveWriteIndex, std::memory_order_relaxed);
            livePeak = 0.0f;
            liveFill = 0;
        }
    }
}

void Recorder::handlePunchBlock (float* mono, juce::AudioBuffer<float>& base, int n) noexcept
{
    if (n <= 0 || n > (int) punchScratch.size())
        return;

    const int64_t pos = playbackPos.load();
    const int64_t start = punchStartSample.load();
    const int64_t end = punchEndSample.load();
    const int64_t len = base.getNumSamples();
    const auto* src = base.getReadPointer (0);

    int written = 0;

    for (int i = 0; i < n; ++i)
    {
        const int64_t t = pos + (int64_t) i;
        const float live = mono[i];

        if (t >= start && t < end)
        {
            // Punch window: monitor the live guitar, not the old part.
            mono[i] = live;
            punchScratch[(size_t) written++] = live;
        }
        else if (t >= 0 && t < len)
        {
            mono[i] = src[(int) t];
        }
        else
        {
            mono[i] = 0.0f;
        }
    }

    auto* writer = activePunch.load (std::memory_order_acquire);
    if (writer != nullptr && written > 0)
    {
        punchWriteUsers.fetch_add (1, std::memory_order_acq_rel);
        if (activePunch.load (std::memory_order_acquire) == writer)
        {
            const float* channels[] = { punchScratch.data() };
            writer->write (channels, written);
            punchSamplesWritten.fetch_add (written, std::memory_order_relaxed);
        }
        punchWriteUsers.fetch_sub (1, std::memory_order_acq_rel);
    }

    playbackPos.store (pos + n);

    if (pos + n >= end)
    {
        punchRecording.store (false, std::memory_order_release);
        punchFinished.store (true, std::memory_order_release);
        playbackOn.store (false, std::memory_order_release);
    }
}

bool Recorder::replaceInput (float* mono, int n) noexcept
{
    updateLiveEnvelope (mono, n);

    auto* buf = playbackOn.load() ? playbackPtr.load() : nullptr;
    playbackInUse.store (buf);
    if (buf == nullptr)
        return false;

    if (punchRecording.load (std::memory_order_acquire))
    {
        handlePunchBlock (mono, *buf, n);
        return true;
    }

    int64_t pos = playbackPos.load();
    if (const auto seek = pendingSeek.exchange (-1); seek >= 0)
        pos = seek;
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
