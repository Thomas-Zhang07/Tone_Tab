#include "TakeTimeline.h"
#include "Text.h"
#include <cmath>
#include <memory>

namespace
{
    const juce::Colour background { 0xff1b1b1b };
    const juce::Colour panelLine  { 0xff303030 };
    const juce::Colour muted      { 0xff9a9a9a };
    const juce::Colour accent     { 0xff5dcaa5 };
    const juce::Colour red        { 0xffc0392b };

    juce::String timeLabel (double s)
    {
        if (s >= 60.0)
        {
            const int whole = (int) std::round (s);
            return juce::String (whole / 60) + ":" + juce::String (whole % 60).paddedLeft ('0', 2);
        }

        if (std::abs (s - std::round (s)) < 0.01)
            return juce::String ((int) std::round (s)) + "s";
        return juce::String (s, 1) + "s";
    }
}

TakeTimeline::TakeTimeline()
    : juce::Thread ("Tone_Tab take waveform")
{
    setWantsKeyboardFocus (false);
}

TakeTimeline::~TakeTimeline()
{
    stopThread (2000);
}

void TakeTimeline::setFile (const juce::File& wav, double fallbackLengthSeconds)
{
    stopThread (2000);
    ++loadGeneration;

    file = wav;
    peaks.clear();
    message.clear();
    loading = false;
    recording = false;
    liveActive = false;
    livePeakAt = nullptr;
    punchRegions.clear();
    punchActive = false;
    playSeconds = 0.0;
    recordSeconds = 0.0;
    regionStart = regionEnd = -1.0;
    lengthSeconds = juce::jmax (0.0, fallbackLengthSeconds);

    if (wav.existsAsFile())
    {
        loading = true;
        startThread();
    }
    else
    {
        message = TR ("No take selected", "没有选择录音");
    }

    repaint();
}

void TakeTimeline::clearFile()
{
    stopThread (2000);
    ++loadGeneration;
    file = {};
    peaks.clear();
    message = TR ("Select a take to see its waveform", "选择一次录音查看波形");
    loading = false;
    recording = false;
    liveActive = false;
    livePeakAt = nullptr;
    punchRegions.clear();
    punchActive = false;
    playSeconds = 0.0;
    recordSeconds = 0.0;
    lengthSeconds = 0.0;
    regionStart = regionEnd = -1.0;
    repaint();
}

void TakeTimeline::applyLoad (std::vector<float>&& newPeaks, double length,
                              const juce::String& error, int generation)
{
    if (generation != loadGeneration.load())
        return;

    loading = false;
    if (error.isNotEmpty())
    {
        message = error;
        peaks.clear();
    }
    else
    {
        message.clear();
        peaks = std::move (newPeaks);
        lengthSeconds = juce::jmax (0.0, length);
    }

    repaint();
}

void TakeTimeline::run()
{
    const int generation = loadGeneration.load();
    const auto source = file;

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (source));

    auto postError = [this, generation] (const juce::String& error)
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<TakeTimeline> (this),
                                          error, generation]
        {
            if (safe != nullptr)
                safe->applyLoad ({}, 0.0, error, generation);
        });
    };

    if (reader == nullptr)
    {
        postError (TR ("Cannot read this recording", "读不了这个录音"));
        return;
    }

    const auto total = reader->lengthInSamples;
    if (total <= 0)
    {
        postError (TR ("This recording is empty", "这个录音是空的"));
        return;
    }

    constexpr int buckets = 1600;
    std::vector<float> local ((size_t) buckets, 0.0f);
    std::vector<float> buffer (16384, 0.0f);
    float maxPeak = 0.0f;

    for (int64_t pos = 0; pos < total; pos += (int64_t) buffer.size())
    {
        if (threadShouldExit())
            return;

        const int n = (int) juce::jmin<int64_t> ((int64_t) buffer.size(), total - pos);
        float* channels[] = { buffer.data() };
        if (! reader->read (channels, 1, pos, n))
            break;

        for (int i = 0; i < n; ++i)
        {
            const float v = std::abs (buffer[(size_t) i]);
            maxPeak = juce::jmax (maxPeak, v);

            const int b = juce::jlimit (0, buckets - 1,
                                        (int) ((double) (pos + i) * (double) buckets / (double) total));
            local[(size_t) b] = juce::jmax (local[(size_t) b], v);
        }
    }

    if (maxPeak > 1.0e-7f)
        for (auto& v : local)
            v /= maxPeak;

    const double length = (double) total / reader->sampleRate;
    auto result = std::make_shared<std::vector<float>> (std::move (local));

    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<TakeTimeline> (this),
                                      result, length, generation]
    {
        if (safe != nullptr)
            safe->applyLoad (std::move (*result), length, {}, generation);
    });
}

// ---- layout / painting -------------------------------------------------------

void TakeTimeline::resized()
{
    auto area = getLocalBounds().reduced (10);
    headerArea = area.removeFromTop (18);
    rulerArea = area.removeFromTop (18);
    plotArea = area;
}

void TakeTimeline::paint (juce::Graphics& g)
{
    g.setColour (background);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
    g.setColour (panelLine);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 8.0f, 1.0f);

    const juce::String name = recording
                                  ? TR ("New recording", "新录音")
                                  : file.existsAsFile()
                                        ? file.getParentDirectory().getFileName() + " / " + file.getFileName()
                                        : TR ("Take waveform", "录音波形");
    g.setColour (juce::Colours::white.withAlpha (0.8f));
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (name, headerArea.withTrimmedRight (140), juce::Justification::centredLeft, true);

    if (loading)
    {
        g.setColour (muted);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (TR ("Reading waveform…", "正在读取波形…"), plotArea, juce::Justification::centred);
        return;
    }

    if (plotArea.getWidth() < 20 || plotArea.getHeight() < 20)
        return;

    const bool liveView = recording && liveActive && livePeakAt != nullptr;
    constexpr double liveWindow = 15.0;
    const double displayLength = liveView ? liveWindow
                                   : recording ? juce::jmax (30.0, recordSeconds)
                                   : juce::jmax (0.05, lengthSeconds);

    // Header right: state / selection / time.
    juce::String rightText;
    juce::Colour rightColour = muted;
    if (recording)
    {
        rightText = TR ("REC  ", "录音  ") + juce::String (recordSeconds, 1) + " s";
        rightColour = red;
    }
    else if (punchActive)
    {
        rightText = TR ("PUNCH  ", "补录  ") + juce::String (juce::jmax (0.0, punchEnd - punchStart), 2) + " s";
        rightColour = red;
    }
    else if (hasSelection())
    {
        rightText = TR ("selection ", "选区 ") + juce::String (regionEnd - regionStart, 2) + " s";
        rightColour = accent;
    }
    else if (playSeconds > 0.0)
    {
        rightText = timeLabel (playSeconds);
    }

    if (rightText.isNotEmpty())
    {
        g.setColour (rightColour);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (rightText, headerArea, juce::Justification::centredRight, true);
    }

    // Ruler.
    g.setFont (juce::FontOptions (10.0f));
    if (liveView)
    {
        for (double t = 0.0; t <= liveWindow + 1.0e-6; t += 5.0)
        {
            const float x = secondsToX (t, liveWindow);
            g.setColour (juce::Colour (0xff383838));
            g.drawLine (x, (float) rulerArea.getBottom() - 5.0f, x, (float) plotArea.getBottom(), 1.0f);
            g.setColour (muted);
            const auto label = t >= liveWindow - 0.01
                                   ? TR ("now", "现在")
                                   : "-" + juce::String ((int) std::round (liveWindow - t)) + "s";
            if (t >= liveWindow - 0.01)
                g.drawText (label, juce::Rectangle<int> ((int) x - 42, rulerArea.getY(), 44,
                                                         rulerArea.getHeight()),
                            juce::Justification::centredRight);
            else
                g.drawText (label, (int) x + 3, rulerArea.getY(), 44, rulerArea.getHeight(),
                            juce::Justification::centredLeft);
        }
    }
    else
    {
        double step = 1.0;
        for (const double c : { 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0 })
        {
            step = c;
            if (displayLength / c <= 9.0)
                break;
        }

        for (double t = 0.0; t <= displayLength + 1.0e-6; t += step)
        {
            const float x = secondsToX (t, displayLength);
            g.setColour (juce::Colour (0xff383838));
            g.drawLine (x, (float) rulerArea.getBottom() - 5.0f, x, (float) plotArea.getBottom(), 1.0f);
            g.setColour (muted);
            g.drawText (timeLabel (t), (int) x + 3, rulerArea.getY(), 44, rulerArea.getHeight(),
                        juce::Justification::centredLeft);
        }
    }

    // Selection and punch-clip fills.
    if (hasSelection() && ! recording)
    {
        const float x1 = secondsToX (regionStart, displayLength);
        const float x2 = secondsToX (regionEnd, displayLength);
        g.setColour (accent.withAlpha (0.16f));
        g.fillRect (juce::Rectangle<float> (x1, (float) plotArea.getY(),
                                            juce::jmax (1.0f, x2 - x1), (float) plotArea.getHeight()));
    }

    if (! recording)
    {
        for (const auto& r : punchRegions)
        {
            if (r.second <= r.first)
                continue;

            const float x1 = secondsToX (r.first, displayLength);
            const float x2 = secondsToX (r.second, displayLength);
            g.setColour (juce::Colour (0xff3d7dd8).withAlpha (0.16f));
            g.fillRect (juce::Rectangle<float> (x1, (float) plotArea.getY(),
                                                juce::jmax (1.0f, x2 - x1), (float) plotArea.getHeight()));
        }

        if (punchActive)
        {
            const float x1 = secondsToX (punchStart, displayLength);
            const float x2 = secondsToX (punchEnd, displayLength);
            g.setColour (red.withAlpha (0.18f));
            g.fillRect (juce::Rectangle<float> (x1, (float) plotArea.getY(),
                                                juce::jmax (1.0f, x2 - x1), (float) plotArea.getHeight()));
        }
    }

    const int midY = plotArea.getCentreY();
    const int halfH = juce::jmax (2, plotArea.getHeight() / 2 - 4);

    if (liveView)
    {
        g.setColour (juce::Colour (0xff343434));
        g.drawLine ((float) plotArea.getX(), (float) midY, (float) plotArea.getRight(), (float) midY, 1.0f);

        const int windowBuckets = juce::jmax (1, (int) std::lround (liveWindow / liveBucketSeconds));
        const int recordedBuckets = juce::jmax (0, (int) std::lround (recordSeconds / liveBucketSeconds));
        const uint32_t span = (uint32_t) juce::jmin (windowBuckets,
                                                     juce::jmin (recordedBuckets, (int) liveWrite));

        if (span > 0)
        {
            float maxPeak = 0.02f;
            for (uint32_t k = 0; k < span; ++k)
                maxPeak = juce::jmax (maxPeak, livePeakAt (liveWrite - span + k));

            const double visible = (double) span * liveBucketSeconds;
            const float regionX = (float) plotArea.getRight()
                                  - (float) plotArea.getWidth()
                                      * (float) juce::jlimit (0.0, 1.0, visible / liveWindow);
            const float regionW = (float) plotArea.getRight() - regionX;

            g.setColour (red.withAlpha (0.85f));
            for (int x = 0; x < (int) regionW; ++x)
            {
                const uint32_t k = (uint32_t) ((int64_t) x * (int64_t) span
                                               / (int64_t) juce::jmax (1, (int) regionW));
                const float peak = livePeakAt (liveWrite - span + k);
                const float amp = juce::jmax (1.0f, peak / maxPeak * (float) halfH);
                const float px = regionX + (float) x;
                g.drawLine (px, (float) midY - amp, px, (float) midY + amp, 1.0f);
            }
        }

        g.setColour (red);
        g.drawLine ((float) plotArea.getRight() - 1.0f, (float) plotArea.getY(),
                    (float) plotArea.getRight() - 1.0f, (float) plotArea.getBottom(), 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.72f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (TR ("LIVE INPUT", "实时输入"), plotArea.withTrimmedTop (2),
                    juce::Justification::centredTop);
        return;
    }

    if (recording)
    {
        // Fallback until the first live bucket arrives.
        const float head = secondsToX (juce::jmin (recordSeconds, displayLength), displayLength);
        g.setColour (red.withAlpha (0.10f));
        g.fillRect (juce::Rectangle<float> ((float) plotArea.getX(), (float) plotArea.getY(),
                                            juce::jmax (0.0f, head - (float) plotArea.getX()),
                                            (float) plotArea.getHeight()));
        g.setColour (red);
        g.drawLine (head, (float) plotArea.getY(), head, (float) plotArea.getBottom(), 2.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (TR ("RECORDING  ", "录音中  ") + juce::String (recordSeconds, 1) + " s",
                    plotArea, juce::Justification::centred);
        return;
    }

    if (! message.isEmpty())
    {
        g.setColour (muted);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (message, plotArea, juce::Justification::centred);
    }

    // Centre line.
    g.setColour (juce::Colour (0xff343434));
    g.drawLine ((float) plotArea.getX(), (float) midY, (float) plotArea.getRight(), (float) midY, 1.0f);

    if (! peaks.empty() && lengthSeconds > 0.0)
    {
        g.setColour (accent.withAlpha (0.85f));
        const int n = (int) peaks.size();
        const int w = plotArea.getWidth();

        for (int x = 0; x < w; ++x)
        {
            const double t0 = (double) x / (double) w * displayLength;
            const double t1 = (double) (x + 1) / (double) w * displayLength;
            int b0 = juce::jlimit (0, n - 1, (int) std::floor (t0 / displayLength * n));
            int b1 = juce::jlimit (0, n - 1, (int) std::ceil (t1 / displayLength * n));

            float peak = 0.0f;
            for (int b = b0; b <= b1; ++b)
                peak = juce::jmax (peak, peaks[(size_t) b]);

            const float amp = juce::jmax (1.0f, peak * (float) halfH);
            g.drawLine ((float) (plotArea.getX() + x), (float) midY - amp,
                        (float) (plotArea.getX() + x), (float) midY + amp, 1.0f);
        }
    }

    // Punch-region borders and labels, drawn over the waveform.
    if (! recording && (! punchRegions.empty() || punchActive))
    {
        auto drawRegion = [&] (double a, double b, juce::Colour colour)
        {
            if (b <= a)
                return;

            const float x1 = secondsToX (a, displayLength);
            const float x2 = secondsToX (b, displayLength);
            g.setColour (colour.withAlpha (0.75f));
            g.drawLine (x1, (float) plotArea.getY() + 2.0f, x2, (float) plotArea.getY() + 2.0f, 1.5f);
            g.drawLine (x1, (float) plotArea.getBottom() - 2.0f, x2, (float) plotArea.getBottom() - 2.0f, 1.5f);

            if (x2 - x1 > 34.0f)
            {
                const juce::Rectangle<int> badge ((int) x1 + 4, plotArea.getY() + 3, 48, 14);
                g.setColour (background.withAlpha (0.78f));
                g.fillRoundedRectangle (badge.toFloat(), 3.0f);
                g.setColour (colour);
                g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
                g.drawText (TR ("PUNCH", "补录"), badge, juce::Justification::centred);
            }
        };

        for (const auto& r : punchRegions)
            drawRegion (r.first, r.second, juce::Colour (0xff3d7dd8));

        if (punchActive)
            drawRegion (punchStart, punchEnd, red);
    }

    // Replay head.
    if (playSeconds > 0.0)
    {
        const float x = secondsToX (playSeconds, displayLength);
        g.setColour (juce::Colour (0xffef9f27));
        g.drawLine (x, (float) plotArea.getY(), x, (float) plotArea.getBottom(), 2.0f);
    }
}

// ---- interaction -------------------------------------------------------------

double TakeTimeline::xToSeconds (float x) const noexcept
{
    if (lengthSeconds <= 0.0 || plotArea.getWidth() <= 0)
        return 0.0;
    const double f = ((double) x - (double) plotArea.getX()) / (double) plotArea.getWidth();
    return juce::jlimit (0.0, lengthSeconds, f * lengthSeconds);
}

float TakeTimeline::secondsToX (double seconds, double displayLength) const noexcept
{
    if (displayLength <= 0.0)
        return (float) plotArea.getX();
    return (float) plotArea.getX() + (float) plotArea.getWidth()
                                     * (float) juce::jlimit (0.0, 1.0, seconds / displayLength);
}

void TakeTimeline::setSelection (double startSeconds, double endSeconds)
{
    if (lengthSeconds <= 0.0)
        return;

    regionStart = juce::jlimit (0.0, lengthSeconds, juce::jmin (startSeconds, endSeconds));
    regionEnd = juce::jlimit (0.0, lengthSeconds, juce::jmax (startSeconds, endSeconds));
    if (regionEnd - regionStart < 0.01)
        regionStart = regionEnd = -1.0;

    repaint();
    if (onSelectionChanged != nullptr)
        onSelectionChanged (regionStart, regionEnd);
}

void TakeTimeline::clearSelection()
{
    if (regionStart >= 0.0 || regionEnd >= 0.0)
    {
        regionStart = regionEnd = -1.0;
        repaint();
    }
}

void TakeTimeline::setPunchRegions (const std::vector<std::pair<double, double>>& regions)
{
    punchRegions = regions;
    repaint();
}

void TakeTimeline::setPunchState (bool active, double startSeconds, double endSeconds)
{
    if (punchActive == active && punchStart == startSeconds && punchEnd == endSeconds)
        return;

    punchActive = active;
    punchStart = startSeconds;
    punchEnd = endSeconds;
    repaint();
}

void TakeTimeline::setRecordingLive (bool active, double recordedSeconds, uint32_t writePos,
                                     double bucketSeconds, PeakProvider peakAt)
{
    const bool changed = recording != active || liveActive != active
                         || (active && (std::abs (recordSeconds - recordedSeconds) > 0.01
                                        || writePos != liveWrite));

    recording = active;
    liveActive = active;
    recordSeconds = juce::jmax (0.0, recordedSeconds);
    liveWrite = writePos;
    liveBucketSeconds = juce::jmax (1.0e-4, bucketSeconds);
    livePeakAt = std::move (peakAt);

    if (changed)
        repaint();
}

void TakeTimeline::mouseDown (const juce::MouseEvent& e)
{
    if (recording || lengthSeconds <= 0.0)
        return;

    dragStartX = e.position.x;
    dragging = false;

    if (e.mods.isPopupMenu())
        clearSelection();
}

void TakeTimeline::mouseDrag (const juce::MouseEvent& e)
{
    if (recording || lengthSeconds <= 0.0 || ! e.mods.isLeftButtonDown())
        return;

    if (! dragging && std::abs (e.position.x - dragStartX) < 4.0f)
        return;

    dragging = true;
    const float a = juce::jmin (dragStartX, e.position.x);
    const float b = juce::jmax (dragStartX, e.position.x);
    regionStart = xToSeconds (a);
    regionEnd = xToSeconds (b);
    repaint();
}

void TakeTimeline::mouseUp (const juce::MouseEvent& e)
{
    if (recording || lengthSeconds <= 0.0)
        return;

    if (dragging)
    {
        dragging = false;
        if (regionEnd - regionStart < 0.02)
        {
            clearSelection();
            return;
        }

        if (onSelectionChanged != nullptr)
            onSelectionChanged (regionStart, regionEnd);
        return;
    }

    // A plain click seeks; it also clears an old selection.
    clearSelection();
    if (onSeek != nullptr)
        onSeek (xToSeconds (e.position.x));
}

void TakeTimeline::setPlayPosition (double seconds, bool isRecording, double recordedSeconds)
{
    if (! isRecording && std::abs (seconds - playSeconds) < 0.005)
    {
        if (! recording)
            return;
    }

    playSeconds = juce::jmax (0.0, seconds);
    recording = isRecording;
    recordSeconds = juce::jmax (0.0, recordedSeconds);
    repaint();
}
