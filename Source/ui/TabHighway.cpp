#include "TabHighway.h"
#include "Text.h"
#include <algorithm>

namespace
{
    // 弦的颜色（从最细的弦开始），和常见音游的配色一致
    const juce::Colour stringColours[] = {
        juce::Colour (0xffe8433f),   // 1 弦 e  红
        juce::Colour (0xfff2c12e),   // 2 弦 B  黄
        juce::Colour (0xff3d8ff0),   // 3 弦 G  蓝
        juce::Colour (0xfff07f2a),   // 4 弦 D  橙
        juce::Colour (0xff37b24d),   // 5 弦 A  绿
        juce::Colour (0xffa65be8),   // 6 弦 E  紫
        juce::Colour (0xff9aa0a6),   // 7 弦
        juce::Colour (0xff7d8590),   // 8 弦
    };

    juce::Colour colourForString (int s) { return stringColours[juce::jlimit (0, 7, s)]; }

    juce::String noteName (int midi)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return names[((midi % 12) + 12) % 12];
    }

    juce::String bendText (float semis)
    {
        if (semis <= 0.6f) return juce::String::fromUTF8 ("\xc2\xbd");      // ½
        if (semis <= 1.2f) return TR ("½ step", "½音");
        if (semis <= 2.2f) return TR ("Whole step", "全音");
        return juce::String (semis / 2.0f, 1) + TR (" whole steps", "音");
    }

    const juce::Colour bg       { 0xff121417 };

    juce::Colour verdictColour (Verdict v)
    {
        switch (v)
        {
            case Verdict::perfect:   return juce::Colour (0xffffd34d);
            case Verdict::good:      return juce::Colour (0xff5dd879);
            case Verdict::wrongNote: return juce::Colour (0xffff8a3d);
            case Verdict::miss:      return juce::Colour (0xffff4d4d);
            case Verdict::pending:   break;
        }
        return juce::Colours::transparentBlack;
    }

    juce::Colour accuracyColour (float a)
    {
        // 红 -> 黄 -> 绿
        return a < 0.5f ? juce::Colour (0xffff4d4d).interpolatedWith (juce::Colour (0xffffc94d), a * 2.0f)
                        : juce::Colour (0xffffc94d).interpolatedWith (juce::Colour (0xff4ccf6b), (a - 0.5f) * 2.0f);
    }

    double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }
    const juce::Colour laneLine { 0xff2a2f36 };
}

TabHighway::TabHighway (TabPlayer& p) : player (p)
{
    vblank = std::make_unique<juce::VBlankAttachment> (this, [this] { if (isShowing()) repaint(); });
}

TabHighway::~TabHighway() = default;

void TabHighway::setSong (std::shared_ptr<const Song> s, int trackIndex)
{
    song = std::move (s);
    track = trackIndex;
    analyseTrack();
    repaint();
}

void TabHighway::setTrack (int trackIndex)
{
    track = trackIndex;
    analyseTrack();
    repaint();
}

void TabHighway::setJudgeSession (std::shared_ptr<JudgeSession> s, bool wait)
{
    judge = std::move (s);
    waitMode = wait;
    seenSeq = judge != nullptr ? judge->lastSeq.load() : 0;
    flash = {};
    barAccuracy.clear();
    repaint();
}

void TabHighway::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! laneArea.contains (e.getPosition()))
        return;
    pxPerSec = juce::jlimit (100.0f, 600.0f, pxPerSec * (1.0f + w.deltaY * 0.5f));
    if (onZoomChanged) onZoomChanged (pxPerSec);
    repaint();
}

void TabHighway::analyseTrack()
{
    hopoLabel.clear(); nextOnString.clear(); startSec.clear(); endSec.clear();
    if (song == nullptr || ! juce::isPositiveAndBelow (track, (int) song->tracks.size()))
        return;

    const auto& notes = song->tracks[(size_t) track].notes;
    const size_t n = notes.size();
    hopoLabel.assign (n, 0);
    nextOnString.assign (n, -1);
    startSec.resize (n);
    endSec.resize (n);

    std::vector<int> lastOn (16, -1);
    for (size_t i = 0; i < n; ++i)
    {
        const auto& note = notes[i];
        startSec[i] = song->beatToSeconds (note.start);
        endSec[i] = song->beatToSeconds (note.start + note.length);
        const int s = juce::jlimit (0, 15, note.string);
        if (lastOn[(size_t) s] >= 0)
        {
            nextOnString[(size_t) lastOn[(size_t) s]] = (int) i;
            if (note.hammerPull)
                hopoLabel[i] = note.fret >= notes[(size_t) lastOn[(size_t) s]].fret ? 'H' : 'P';
        }
        lastOn[(size_t) s] = (int) i;
    }
}

double TabHighway::currentBeat() const
{
    return frozenBeat >= 0.0 ? frozenBeat : player.getPosition();
}

void TabHighway::resized()
{
    auto r = getLocalBounds();
    timelineArea = r.removeFromBottom (40).reduced (12, 8);
    laneArea = r.reduced (0, 8);
    hitX = 150.0f;
}

float TabHighway::yForString (int s) const
{
    const int count = (song != nullptr && juce::isPositiveAndBelow (track, (int) song->tracks.size()))
                        ? juce::jmax (1, song->tracks[(size_t) track].stringCount()) : 6;
    const float top = (float) laneArea.getY() + 34.0f, bottom = (float) laneArea.getBottom() - 16.0f;
    const float gap = (bottom - top) / (float) juce::jmax (1, count - 1);
    return top + gap * (float) s;
}

// =============================================================================
void TabHighway::paint (juce::Graphics& g)
{
    g.fillAll (bg);

    if (song == nullptr || ! juce::isPositiveAndBelow (track, (int) song->tracks.size()))
    {
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.setFont (juce::FontOptions (18.0f));
        g.drawText (TR ("Click “Open Tab…” to load a guitar tab", "点“打开谱子…”载入一个吉他谱"), getLocalBounds().withTrimmedBottom (20), juce::Justification::centred);
        g.setFont (juce::FontOptions (13.0f));
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawText (TR ("Supports Guitar Pro 3 / 4 / 5 / 6 / 7 / 8 (.gp3 .gp4 .gp5 .gpx .gp)", "支持 Guitar Pro 3 / 4 / 5 / 6 / 7 / 8（.gp3 .gp4 .gp5 .gpx .gp）"),
                    getLocalBounds().withTrimmedTop (60), juce::Justification::centred);
        return;
    }

    const double nowBeat = currentBeat();
    const double nowSec = song->beatToSeconds (nowBeat);
    paintLanes (g, nowSec, nowBeat);
    paintTimeline (g, nowBeat);
}

void TabHighway::paintLanes (juce::Graphics& g, double nowSec, double nowBeat)
{
    const auto& tr = song->tracks[(size_t) track];
    const int strings = juce::jmax (1, tr.stringCount());
    const float left = (float) laneArea.getX(), right = (float) laneArea.getRight();
    const double secLeft = nowSec - (hitX - left) / pxPerSec;
    const double secRight = nowSec + (right - hitX) / pxPerSec;
    const double beatLeft = song->secondsToBeat (juce::jmax (0.0, secLeft));
    const double beatRight = song->secondsToBeat (secRight);

    // ---- 小节线、拍线、段落 ---------------------------------------------------
    const float laneTop = yForString (0) - 18.0f, laneBottom = yForString (strings - 1) + 18.0f;
    juce::String lastSection;
    const int firstBar = juce::jmax (0, song->barIndexAt (beatLeft) - 1);
    for (int b = firstBar; b < (int) song->bars.size(); ++b)
    {
        const auto& bar = song->bars[(size_t) b];
        if (bar.start > beatRight) break;

        for (double beat = bar.start + 1.0; beat < bar.start + bar.length - 1.0e-6; beat += 1.0)
        {
            const float x = xForSeconds (song->beatToSeconds (beat), nowSec);
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRect (x, laneTop, 1.0f, laneBottom - laneTop);
        }

        const float x = xForSeconds (song->beatToSeconds (bar.start), nowSec);
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.fillRect (x - 1.0f, laneTop, 2.0f, laneBottom - laneTop);
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.setFont (juce::FontOptions (11.5f));
        g.drawText (juce::String (bar.sourceIndex + 1), (int) x + 4, (int) laneTop - 16, 40, 14, juce::Justification::centredLeft);

        if (bar.section.isNotEmpty())
        {
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            const int w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), bar.section) + 16;
            const auto chip = juce::Rectangle<float> (x + 4.0f, (float) laneArea.getY() + 2.0f, (float) w, 20.0f);
            g.setColour (juce::Colour (0xff2f6fdb).withAlpha (0.85f));
            g.fillRoundedRectangle (chip, 6.0f);
            g.setColour (juce::Colours::white);
            g.drawText (bar.section, chip, juce::Justification::centred);
        }
    }

    // ---- 弦（轨道） ---------------------------------------------------------
    for (int s = 0; s < strings; ++s)
    {
        const float y = yForString (s);
        g.setColour (colourForString (s).withAlpha (0.35f));
        g.fillRect (left + 64.0f, y - 1.0f, right - left - 64.0f, 2.0f + 0.25f * (float) s);
    }

    // 判定线
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.fillRect (hitX - 14.0f, laneTop, 28.0f, laneBottom - laneTop);
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.fillRect (hitX - 1.5f, laneTop, 3.0f, laneBottom - laneTop);

    // ---- 音符 ---------------------------------------------------------------
    const auto& notes = tr.notes;
    const auto firstIt = std::lower_bound (notes.begin(), notes.end(), beatLeft - 16.0,
                                           [] (const SongNote& n, double b) { return n.start < b; });
    const float gemH = juce::jlimit (16.0f, 30.0f, (yForString (1) - yForString (0)) * 0.62f);

    // 和弦连接条：同一时刻的多个音
    for (auto it = firstIt; it != notes.end() && it->start <= beatRight; )
    {
        auto end = it;
        int minS = it->string, maxS = it->string;
        while (end != notes.end() && std::abs (end->start - it->start) < 1.0e-6)
        {
            minS = juce::jmin (minS, end->string);
            maxS = juce::jmax (maxS, end->string);
            ++end;
        }
        if (maxS > minS)
        {
            const float x = xForSeconds (song->beatToSeconds (it->start), nowSec);
            g.setColour (juce::Colours::white.withAlpha (x < hitX ? 0.06f : 0.16f));
            g.fillRoundedRectangle (x - 5.0f, yForString (minS), 10.0f, yForString (maxS) - yForString (minS), 5.0f);
        }
        it = end;
    }

    // 拖尾、滑音线（画在音符下面）
    for (auto it = firstIt; it != notes.end() && it->start <= beatRight; ++it)
    {
        const size_t i = (size_t) std::distance (notes.begin(), it);
        const auto& n = *it;
        const float x0 = xForSeconds (startSec[i], nowSec);
        const float x1 = xForSeconds (endSec[i], nowSec);
        const float y = yForString (n.string);
        const auto c = colourForString (n.string);
        if (x1 < left) continue;

        if (n.slideOut && nextOnString[i] >= 0)
        {
            const auto& nx = notes[(size_t) nextOnString[i]];
            const float xn = xForSeconds (startSec[(size_t) nextOnString[i]], nowSec);
            const float dy = nx.fret > n.fret ? -8.0f : 8.0f;
            g.setColour (c.withAlpha (0.8f));
            g.drawLine (x0 + 14.0f, y - dy * 0.5f, xn - 14.0f, y + dy * 0.5f, 3.0f);
        }
        else if (x1 - x0 > 34.0f && ! n.dead)
        {
            const float tailH = juce::jmax (8.0f, gemH * 0.42f);
            juce::Path tail;
            if (n.vibrato)
            {
                tail.startNewSubPath (x0, y);
                for (float x = x0; x < x1; x += 4.0f)
                    tail.lineTo (x, y + 3.0f * std::sin ((x - x0) * 0.35f));
                g.setColour (c.withAlpha (0.55f));
                g.strokePath (tail, juce::PathStrokeType (4.0f));
            }
            else if (n.bend > 0.0f)
            {
                const float rise = juce::jmin (gemH * 1.1f, 10.0f * n.bend);
                tail.startNewSubPath (x0, y);
                tail.quadraticTo (x0 + (x1 - x0) * 0.4f, y, x0 + (x1 - x0) * 0.5f, y - rise);
                tail.lineTo (x1, y - rise);
                g.setColour (c.withAlpha (0.6f));
                g.strokePath (tail, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            else
            {
                g.setColour (c.withAlpha (n.palmMute ? 0.3f : 0.55f));
                g.fillRoundedRectangle (x0, y - tailH * 0.5f, x1 - x0, tailH, tailH * 0.5f);
            }
        }
    }

    // 音符块
    for (auto it = firstIt; it != notes.end() && it->start <= beatRight; ++it)
    {
        const size_t i = (size_t) std::distance (notes.begin(), it);
        const auto& n = *it;
        const float x = xForSeconds (startSec[i], nowSec);
        if (x < left - 30.0f) continue;
        const float y = yForString (n.string);
        const auto c = colourForString (n.string);

        const juce::String label = n.dead ? juce::String::fromUTF8 ("\xc3\x97") : juce::String (n.fret);
        const float w = juce::jmax (gemH * 1.15f, 12.0f + 9.0f * (float) label.length());
        auto gem = juce::Rectangle<float> (x - w * 0.5f, y - gemH * 0.5f, w, gemH);

        // 已经过了判定线的音变暗；正在判定线上的音发亮
        const double dt = (startSec[i] - nowSec);
        const bool past = dt < -0.06;
        const bool atLine = std::abs (dt) <= 0.06;
        float alpha = past ? juce::jmax (0.15f, 0.6f - (float) (-dt) * 1.5f) : 1.0f;

        if (atLine)
        {
            g.setColour (c.withAlpha (0.35f));
            g.fillRoundedRectangle (gem.expanded (7.0f), 10.0f);
        }

        if (n.harmonic)
        {
            juce::Path d;
            d.addPolygon (gem.getCentre(), 4, gemH * 0.72f, juce::MathConstants<float>::pi * 0.25f);
            g.setColour (c.withAlpha (alpha));
            g.fillPath (d);
        }
        else
        {
            g.setColour (c.withAlpha (alpha));
            g.fillRoundedRectangle (gem, 7.0f);
            g.setColour (juce::Colours::white.withAlpha (0.25f * alpha));
            g.fillRoundedRectangle (gem.withHeight (gemH * 0.42f).reduced (2.0f, 1.5f), 5.0f);
        }
        if (n.accent && ! past)
        {
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.drawRoundedRectangle (gem.expanded (2.0f), 8.0f, 1.5f);
        }

        g.setColour (juce::Colours::white.withAlpha (juce::jmax (0.35f, alpha)));
        g.setFont (juce::FontOptions (juce::jmin (18.0f, gemH * 0.72f), juce::Font::bold));
        g.drawText (label, gem, juce::Justification::centred);

        // 判定结果：金色 = Perfect，绿 = Good，橙 = 弹错，红叉 = 漏弹
        if (judge != nullptr && judgeVisible && i < judge->noteToEvent.size() && judge->noteToEvent[i] >= 0)
        {
            const auto v = judge->verdictOf (judge->noteToEvent[i]);
            if (v != Verdict::pending)
            {
                const auto vc = verdictColour (v);
                g.setColour (vc);
                g.drawRoundedRectangle (gem.expanded (2.5f), 8.0f, 2.5f);
                if (v == Verdict::miss || v == Verdict::wrongNote)
                {
                    g.setColour (vc.withAlpha (0.85f));
                    g.drawLine (gem.getX() + 3.0f, gem.getY() + 3.0f, gem.getRight() - 3.0f, gem.getBottom() - 3.0f, 2.5f);
                    g.drawLine (gem.getRight() - 3.0f, gem.getY() + 3.0f, gem.getX() + 3.0f, gem.getBottom() - 3.0f, 2.5f);
                }
            }
        }

        // 技巧标记（画在音符上方）
        juce::String tag;
        if (hopoLabel.size() > i && hopoLabel[i] != 0) tag << (char) hopoLabel[i] << " ";
        if (n.palmMute) tag << "PM ";
        if (n.bend > 0.0f) tag << juce::String::fromUTF8 ("\xe2\x86\x91") << bendText (n.bend) << " ";
        if (n.slideIn) tag << "/";
        // 音名（紧贴音符上方），技巧标记再往上一行
        float tagY = gem.getY() - 15.0f;
        if (showNoteNames && ! n.dead)
        {
            const int midi = n.midi;
            const juce::String name = noteName (midi) + juce::String (midi / 12 - 1);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.setColour (c.brighter (0.5f).withAlpha (past ? 0.35f : 0.95f));
            g.drawText (name, juce::Rectangle<float> (x - 30.0f, gem.getY() - 15.0f, 60.0f, 14.0f), juce::Justification::centred);
            tagY -= 13.0f;
        }
        if (tag.isNotEmpty())
        {
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
            g.setColour (juce::Colours::white.withAlpha (past ? 0.3f : 0.85f));
            g.drawText (tag.trim(), (int) (x - 40), (int) tagY, 80, 14, juce::Justification::centred);
        }
    }

    // ---- 左边：弦名 ---------------------------------------------------------
    g.setColour (bg.withAlpha (0.92f));
    g.fillRect (left, laneTop - 22.0f, 62.0f, laneBottom - laneTop + 40.0f);
    for (int s = 0; s < strings; ++s)
    {
        const float y = yForString (s);
        const auto c = colourForString (s);
        g.setColour (c);
        g.fillEllipse (left + 12.0f, y - 11.0f, 22.0f, 22.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (juce::String (s + 1), juce::Rectangle<float> (left + 12.0f, y - 11.0f, 22.0f, 22.0f), juce::Justification::centred);
        if (s < (int) tr.tuning.size())
        {
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.setFont (juce::FontOptions (12.5f));
            g.drawText (noteName (tr.tuning[(size_t) s]), (int) left + 38, (int) y - 8, 24, 16, juce::Justification::centredLeft);
        }
    }

    if (judge != nullptr && judgeVisible)
        paintJudgeHud (g, nowBeat);

    // 预备拍提示
    if (player.isCountingIn() && frozenBeat < 0.0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.setFont (juce::FontOptions (40.0f, juce::Font::bold));
        g.drawText (TR ("Count-in…", "预备…"), laneArea, juce::Justification::centred);
    }
    juce::ignoreUnused (nowBeat);
}

void TabHighway::paintTimeline (juce::Graphics& g, double nowBeat)
{
    const auto r = timelineArea.toFloat();
    g.setColour (juce::Colour (0xff23272e));
    g.fillRoundedRectangle (r, 6.0f);

    const double len = juce::jmax (1.0, song->lengthBeats);
    auto xFor = [&] (double beat) { return r.getX() + (float) (beat / len) * r.getWidth(); };

    // 段落
    g.setFont (juce::FontOptions (11.0f));
    std::vector<float> sectionX;
    for (auto& bar : song->bars)
        if (bar.section.isNotEmpty())
            sectionX.push_back (xFor (bar.start));
    size_t si = 0;
    for (auto& bar : song->bars)
        if (bar.section.isNotEmpty())
        {
            const float x = sectionX[si];
            const float nextX = si + 1 < sectionX.size() ? sectionX[si + 1] : r.getRight();
            ++si;
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.fillRect (x, r.getY(), 1.0f, r.getHeight());
            const int room = (int) (nextX - x) - 6;
            if (room < 24) continue;     // 段落太密就只画分隔线
            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.drawFittedText (bar.section, (int) x + 3, (int) r.getY(), room, (int) r.getHeight(),
                              juce::Justification::centredLeft, 1, 1.0f);
        }

    // 每个小节的命中率（进度条上沿一条色带）
    if (judge != nullptr && judgeVisible)
    {
        if (++framesSinceBarUpdate > 10 || barAccuracy.size() != song->bars.size())
        {
            barAccuracy = judge->perBarAccuracy();
            framesSinceBarUpdate = 0;
        }
        for (size_t i = 0; i < barAccuracy.size() && i < song->bars.size(); ++i)
            if (barAccuracy[i] >= 0.0f)
            {
                const auto& bar = song->bars[i];
                g.setColour (accuracyColour (barAccuracy[i]));
                g.fillRect (juce::Rectangle<float> (xFor (bar.start), r.getY(), juce::jmax (1.0f, xFor (bar.start + bar.length) - xFor (bar.start)), 5.0f));
            }
    }

    // A-B 循环
    double a = player.getLoopA(), b = player.getLoopB();
    bool showLoop = player.isLooping();
    if (draggingLoop) { a = juce::jmin (dragStartBeat, dragEndBeat); b = juce::jmax (dragStartBeat, dragEndBeat); showLoop = true; }
    if (showLoop)
    {
        g.setColour (juce::Colour (0xff37b24d).withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (xFor (a), r.getY(), xFor (b) - xFor (a), r.getHeight()));
        g.setColour (juce::Colour (0xff37b24d));
        g.fillRect (xFor (a), r.getY(), 2.0f, r.getHeight());
        g.fillRect (xFor (b) - 2.0f, r.getY(), 2.0f, r.getHeight());
    }

    // 已播放部分 + 播放头
    g.setColour (juce::Colour (0xff3d8ff0).withAlpha (0.35f));
    g.fillRoundedRectangle (r.withWidth (xFor (nowBeat) - r.getX()), 6.0f);
    g.setColour (juce::Colours::white);
    g.fillRect (xFor (nowBeat) - 1.5f, r.getY() - 3.0f, 3.0f, r.getHeight() + 6.0f);
}

double TabHighway::beatAtTimelineX (float x) const
{
    if (song == nullptr) return 0.0;
    const auto r = timelineArea.toFloat();
    return juce::jlimit (0.0, song->lengthBeats, (double) ((x - r.getX()) / r.getWidth()) * song->lengthBeats);
}

void TabHighway::mouseDown (const juce::MouseEvent& e)
{
    if (song == nullptr || ! timelineArea.expanded (0, 6).contains (e.getPosition()))
        return;
    dragStartBeat = dragEndBeat = beatAtTimelineX ((float) e.x);
    draggingLoop = false;
}

void TabHighway::mouseDrag (const juce::MouseEvent& e)
{
    if (song == nullptr || ! timelineArea.expanded (0, 6).contains (e.getMouseDownPosition()))
        return;
    if (e.getDistanceFromDragStart() > 6)
    {
        draggingLoop = true;
        dragEndBeat = beatAtTimelineX ((float) e.x);
        repaint();
    }
}

void TabHighway::mouseUp (const juce::MouseEvent& e)
{
    if (song == nullptr || ! timelineArea.expanded (0, 6).contains (e.getMouseDownPosition()))
        return;

    if (draggingLoop)
    {
        draggingLoop = false;
        // 循环段对齐到小节
        const double a = juce::jmin (dragStartBeat, dragEndBeat), b = juce::jmax (dragStartBeat, dragEndBeat);
        const int ia = song->barIndexAt (a), ib = song->barIndexAt (b);
        if (ia >= 0 && ib >= 0 && onLoopDragged)
        {
            const auto& barB = song->bars[(size_t) ib];
            onLoopDragged (song->bars[(size_t) ia].start, barB.start + barB.length);
        }
    }
    else
    {
        // 单击：跳到所在小节开头
        const int bi = song->barIndexAt (beatAtTimelineX ((float) e.x));
        if (bi >= 0)
            player.seek (song->bars[(size_t) bi].start);
    }
    repaint();
}

// ---- 判定：飘字、计分板、等待提示 ---------------------------------------------------
void TabHighway::paintJudgeHud (juce::Graphics& g, double nowBeat)
{
    const auto& tr = song->tracks[(size_t) track];

    // 新的判定 -> 飘字
    if (const auto seq = judge->lastSeq.load(); seq != seenSeq)
    {
        seenSeq = seq;
        const auto v = (Verdict) judge->lastVerdict.load();
        const int e = judge->lastEvent.load();
        const float off = judge->lastOffsetMs.load();
        flash.time = nowSeconds();
        flash.colour = verdictColour (v);
        flash.detail = {};
        switch (v)
        {
            case Verdict::perfect:   flash.text = "PERFECT"; break;
            case Verdict::good:      flash.text = "GOOD"; break;
            case Verdict::wrongNote: flash.text = TR ("Wrong note", "错音"); break;
            case Verdict::miss:      flash.text = "MISS"; break;
            case Verdict::pending:   flash.text = {}; break;
        }
        if ((v == Verdict::perfect || v == Verdict::good) && ! waitMode && std::abs (off) >= 8.0f)
            flash.detail = (off < 0 ? TR ("Early ", "早 ") : TR ("Late ", "晚 ")) + juce::String (juce::roundToInt (std::abs (off))) + " ms";
        flash.string = 0;
        if (juce::isPositiveAndBelow (e, (int) judge->events.size()))
            flash.string = tr.notes[(size_t) judge->events[(size_t) e].firstNote].string;
    }

    const double age = nowSeconds() - flash.time;
    if (flash.text.isNotEmpty() && age < 0.8)
    {
        const float a = (float) juce::jlimit (0.0, 1.0, 1.0 - (age - 0.35) / 0.45);
        const float rise = (float) age * 30.0f;
        const float y = juce::jlimit ((float) laneArea.getY() + 30.0f, (float) laneArea.getBottom() - 30.0f, yForString (flash.string) - 30.0f - rise);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.setColour (juce::Colours::black.withAlpha (0.5f * a));
        g.drawText (flash.text, juce::Rectangle<float> (hitX - 79.0f, y - 13.0f, 160.0f, 26.0f), juce::Justification::centred);
        g.setColour (flash.colour.withAlpha (a));
        g.drawText (flash.text, juce::Rectangle<float> (hitX - 80.0f, y - 14.0f, 160.0f, 26.0f), juce::Justification::centred);
        if (flash.detail.isNotEmpty())
        {
            g.setFont (juce::FontOptions (12.5f));
            g.setColour (juce::Colours::white.withAlpha (0.8f * a));
            g.drawText (flash.detail, juce::Rectangle<float> (hitX - 80.0f, y + 10.0f, 160.0f, 16.0f), juce::Justification::centred);
        }
    }

    // 计分板（右上角）
    const auto st = judge->computeStats();
    auto box = juce::Rectangle<float> ((float) laneArea.getRight() - 236.0f, (float) laneArea.getY() + 4.0f, 228.0f, 50.0f);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (box, 8.0f);
    auto inner = box.reduced (10.0f, 5.0f);
    auto top = inner.removeFromTop (22.0f);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.setColour (st.judged() > 0 ? accuracyColour (st.accuracy()) : juce::Colours::white.withAlpha (0.6f));
    g.drawText (st.judged() > 0 ? juce::String (juce::roundToInt (st.accuracy() * 100.0f)) + "%" : "--", top.removeFromLeft (64.0f), juce::Justification::centredLeft);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    const int combo = judge->combo.load();
    g.drawText (waitMode ? TR ("Wait Mode", "等待模式") : (combo >= 2 ? juce::String (combo) + TR (" combo", " 连击") : juce::String()),
                top, juce::Justification::centredRight);
    g.setFont (juce::FontOptions (12.0f));
    g.setColour (juce::Colours::white.withAlpha (0.75f));
    juce::String line;
    line << "P " << st.perfect << "   G " << st.good << TR ("   W ", "   错 ") << st.wrong << "   M " << st.miss;
    const int passes = judge->passes.load();
    if (passes > 0 && judge->lastPassAccuracy.load() >= 0.0f)
        line << TR ("   Last ", "   上遍 ") << juce::String (juce::roundToInt (judge->lastPassAccuracy.load() * 100.0f)) << "%";
    g.drawText (line, inner, juce::Justification::centredLeft);

    // 等待模式：提示该弹哪个音
    if (waitMode && player.isPlaying() && ! player.isCountingIn())
    {
        const auto& ev = judge->events;
        auto it = std::lower_bound (ev.begin(), ev.end(), nowBeat - 1.0e-6, [] (const JudgeEvent& e, double x) { return e.beat < x; });
        for (; it != ev.end(); ++it)
            if (judge->verdictOf ((int) (it - ev.begin())) == Verdict::pending)
                break;
        if (it != ev.end() && it->beat - nowBeat < 0.02)
        {
            const float pulse = 0.5f + 0.5f * (float) std::sin (nowSeconds() * 6.0);
            g.setColour (juce::Colour (0xff6fa8ff).withAlpha (0.25f + 0.35f * pulse));
            g.fillRect (hitX - 4.0f, (float) laneArea.getY() + 24.0f, 8.0f, (float) laneArea.getHeight() - 30.0f);

            juce::StringArray names;
            for (int k = it->firstNote; k < it->firstNote + it->numNotes; ++k)
            {
                const auto& n = tr.notes[(size_t) k];
                names.add (juce::String (n.string + 1) + TR (" string, fret ", " 弦 ") + (n.dead ? juce::String::fromUTF8 ("\xc3\x97") : juce::String (n.fret) + TR ("", " 品")));
            }
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.setColour (juce::Colour (0xff9cc4ff));
            g.drawText (TR ("Waiting for: ", "等你弹：") + names.joinIntoString ("  "), juce::Rectangle<float> (hitX + 16.0f, (float) laneArea.getBottom() - 22.0f, 420.0f, 18.0f),
                        juce::Justification::centredLeft);
        }
    }
}
