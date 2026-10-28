#include "Song.h"
#include <algorithm>
#include <map>

// =============================================================================
double Song::tempoAt (double beat) const
{
    if (tempos.empty())
        return 120.0;
    auto it = std::upper_bound (tempos.begin(), tempos.end(), beat,
                                [] (double b, const TempoPoint& t) { return b < t.beat; });
    return it == tempos.begin() ? tempos.front().bpm : std::prev (it)->bpm;
}

double Song::beatToSeconds (double beat) const
{
    if (tempos.empty())
        return beat * 0.5;
    auto it = std::upper_bound (tempos.begin(), tempos.end(), beat,
                                [] (double b, const TempoPoint& t) { return b < t.beat; });
    const size_t i = it == tempos.begin() ? 0 : (size_t) (std::distance (tempos.begin(), it) - 1);
    return tempoStartSeconds[i] + (beat - tempos[i].beat) * 60.0 / tempos[i].bpm;
}

double Song::secondsToBeat (double seconds) const
{
    if (tempos.empty())
        return seconds * 2.0;
    auto it = std::upper_bound (tempoStartSeconds.begin(), tempoStartSeconds.end(), seconds);
    const size_t i = it == tempoStartSeconds.begin() ? 0 : (size_t) (std::distance (tempoStartSeconds.begin(), it) - 1);
    return tempos[i].beat + (seconds - tempoStartSeconds[i]) * tempos[i].bpm / 60.0;
}

int Song::barIndexAt (double beat) const
{
    if (bars.empty())
        return -1;
    auto it = std::upper_bound (bars.begin(), bars.end(), beat,
                                [] (double b, const SongBar& bar) { return b < bar.start; });
    return it == bars.begin() ? 0 : (int) std::distance (bars.begin(), it) - 1;
}

int Song::defaultTrack() const
{
    int fallback = -1;
    for (int i = 0; i < (int) tracks.size(); ++i)
    {
        const auto& t = tracks[(size_t) i];
        if (t.percussion || t.notes.empty())
            continue;
        if (fallback < 0)
            fallback = i;
        if (! t.isBass() && t.stringCount() >= 6)
            return i;
    }
    return fallback < 0 ? 0 : fallback;
}

// =============================================================================
namespace
{
    // 按演奏顺序展开反复记号和多房子（1. 2. 结尾）
    std::vector<int> unrollRepeats (const std::vector<RawMasterBar>& mbs)
    {
        std::vector<int> order;
        const int n = (int) mbs.size();
        std::map<int, int> jumpsDone;
        int repeatStart = 0, pass = 1, i = 0;
        bool arrivedByJump = false;

        while (i < n && (int) order.size() < 20000)
        {
            const auto& mb = mbs[(size_t) i];

            if (mb.repeatStart && ! arrivedByJump)
            {
                repeatStart = i;
                pass = 1;
            }
            arrivedByJump = false;

            // 多房子：这一遍不该弹的结尾跳过
            if (mb.alternateEndings != 0 && (mb.alternateEndings & (1 << juce::jlimit (0, 30, pass - 1))) == 0)
            {
                ++i;
                continue;
            }

            order.push_back (i);

            if (mb.repeatCount > 1)
            {
                int& done = jumpsDone[i];
                if (done < mb.repeatCount - 1)
                {
                    ++done;
                    ++pass;
                    i = repeatStart;
                    arrivedByJump = true;
                    continue;
                }
                done = 0;
            }
            ++i;
        }
        return order;
    }
}

std::unique_ptr<Song> buildSong (const RawScore& raw)
{
    auto song = std::make_unique<Song>();
    song->title = raw.title;
    song->artist = raw.artist;
    song->sourceFormat = raw.format;

    const auto order = unrollRepeats (raw.masterBars);

    // ---- 小节和速度 --------------------------------------------------------
    double t = 0.0;
    song->tempos.push_back ({ 0.0, raw.initialTempo > 0 ? raw.initialTempo : 120.0 });
    juce::String lastSection;

    for (int src : order)
    {
        const auto& mb = raw.masterBars[(size_t) src];
        SongBar bar;
        bar.start = t;
        bar.numerator = juce::jmax (1, mb.numerator);
        bar.denominator = juce::jmax (1, mb.denominator);
        bar.length = 4.0 * bar.numerator / bar.denominator;
        bar.sourceIndex = src;
        bar.section = mb.section;
        song->bars.push_back (bar);

        for (auto& tp : mb.tempos)
            if (tp.bpm > 1.0)
                song->tempos.push_back ({ t + juce::jlimit (0.0, 1.0, tp.ratio) * bar.length, tp.bpm });

        t += bar.length;
    }
    song->lengthBeats = t;

    std::stable_sort (song->tempos.begin(), song->tempos.end(),
                      [] (const TempoPoint& a, const TempoPoint& b) { return a.beat < b.beat; });
    // 同一位置的多个速度只保留最后一个，相邻相同速度合并
    std::vector<TempoPoint> cleaned;
    for (auto& tp : song->tempos)
    {
        if (! cleaned.empty() && std::abs (cleaned.back().beat - tp.beat) < 1.0e-9)
            cleaned.back() = tp;
        else if (cleaned.empty() || std::abs (cleaned.back().bpm - tp.bpm) > 1.0e-6)
            cleaned.push_back (tp);
    }
    song->tempos = cleaned;
    song->tempoStartSeconds.resize (song->tempos.size());
    double secs = 0.0;
    for (size_t k = 0; k < song->tempos.size(); ++k)
    {
        if (k > 0)
            secs += (song->tempos[k].beat - song->tempos[k - 1].beat) * 60.0 / song->tempos[k - 1].bpm;
        song->tempoStartSeconds[k] = secs;
    }

    // ---- 音符 --------------------------------------------------------------
    for (const auto& rt : raw.tracks)
    {
        SongTrack track;
        track.name = rt.name.trim();
        track.percussion = rt.percussion;
        track.tuning = rt.tuning;
        track.capo = rt.capo;
        track.program = rt.program;

        std::map<int, size_t> lastOnString;   // 弦 -> 最近一个音在 notes 里的下标（处理连音线）

        for (size_t k = 0; k < order.size(); ++k)
        {
            const int src = order[k];
            if (src >= (int) rt.bars.size())
                continue;

            const auto& bar = rt.bars[(size_t) src];
            const double barStart = song->bars[k].start;

            for (const auto& voice : bar.voices)
            {
                double cursor = barStart;
                for (const auto& beat : voice.beats)
                {
                    if (beat.grace)
                        continue;

                    for (const auto& rn : beat.notes)
                    {
                        if (rn.tieDestination && ! track.percussion)
                        {
                            if (auto it = lastOnString.find (rn.string); it != lastOnString.end())
                            {
                                auto& prev = track.notes[it->second];
                                prev.length = juce::jmax (prev.length, cursor + beat.duration - prev.start);
                                prev.vibrato = prev.vibrato || rn.vibrato;
                                prev.bend = juce::jmax (prev.bend, rn.bend);
                                continue;
                            }
                        }

                        SongNote n;
                        n.start = cursor;
                        n.length = beat.duration;
                        n.string = rn.string;
                        n.fret = rn.fret;
                        if (track.percussion)
                            n.midi = rn.fret;
                        else if (juce::isPositiveAndBelow (rn.string, (int) track.tuning.size()))
                            n.midi = track.tuning[(size_t) rn.string] + rn.fret + track.capo;
                        else
                            continue;   // 弦号超出范围：坏数据，跳过
                        n.bend = rn.bend;
                        n.hammerPull = rn.hammerPull;
                        n.slideOut = rn.slideOut;
                        n.slideIn = rn.slideIn;
                        n.palmMute = rn.palmMute;
                        n.dead = rn.dead;
                        n.harmonic = rn.harmonic;
                        n.vibrato = rn.vibrato;
                        n.letRing = rn.letRing;
                        n.accent = rn.accent;
                        track.notes.push_back (n);
                        lastOnString[rn.string] = track.notes.size() - 1;
                    }
                    cursor += beat.duration;
                }
            }
        }

        // 击弦 / 勾弦：GP 标在“起点”音上，这里改成标在被弹出来的下一个音上，方便判定和显示
        std::vector<size_t> idx (track.notes.size());
        for (size_t q = 0; q < idx.size(); ++q) idx[q] = q;
        std::stable_sort (idx.begin(), idx.end(), [&] (size_t a, size_t b)
        {
            const auto& na = track.notes[a];
            const auto& nb = track.notes[b];
            if (std::abs (na.start - nb.start) > 1.0e-9) return na.start < nb.start;
            return na.string < nb.string;
        });
        std::vector<SongNote> sorted;
        sorted.reserve (idx.size());
        for (auto q : idx) sorted.push_back (track.notes[q]);
        track.notes = std::move (sorted);

        std::vector<bool> hopoOrigin (track.notes.size());
        for (size_t q = 0; q < track.notes.size(); ++q)
        {
            hopoOrigin[q] = track.notes[q].hammerPull;
            track.notes[q].hammerPull = false;
        }
        for (size_t q = 0; q < track.notes.size(); ++q)
        {
            if (! hopoOrigin[q])
                continue;
            for (size_t r = q + 1; r < track.notes.size(); ++r)
            {
                if (track.notes[r].start > track.notes[q].start + track.notes[q].length + 1.0e-6)
                    break;
                if (track.notes[r].string == track.notes[q].string && track.notes[r].start > track.notes[q].start)
                {
                    track.notes[r].hammerPull = true;
                    break;
                }
            }
        }

        song->tracks.push_back (std::move (track));
    }

    return song;
}
