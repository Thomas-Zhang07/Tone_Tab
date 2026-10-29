// 离线测试：不需要声卡，直接把合成的吉他信号送进引擎，检查输出。
// 用法：LianqinTests <NAM 模型目录> <输出目录>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include "AudioEngine.h"
#include "fx/BuiltinEffects.h"
#include "fx/NamProcessor.h"
#include "drums/DrumMachine.h"
#include "fx/DefaultPresets.h"
#include "PitchDetector.h"
#include "ui/PedalEditor.h"
#include "ui/PedalBoard.h"
#include "ui/PracticePanel.h"
#include "ui/TabSearch.h"
#include "ui/StatsView.h"
#include "ui/PresetBar.h"
#include "ui/EffectStyle.h"
#include "song/Song.h"
#include "practice/PluckSynth.h"
#include "practice/Judge.h"
#include "practice/SpeedTrainer.h"
#include "practice/PracticeLog.h"
#include <cstdio>
#include <atomic>
#include <thread>
#include <juce_dsp/juce_dsp.h>

namespace juce::detail { bool dispatchNextMessageOnSystemQueue (bool returnIfNoPendingMessages); }

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 128;
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::printf ("  [%s] %s\n", ok ? "OK  " : "FAIL", what.toRawUTF8());
        if (! ok) ++failures;
    }

    // 合成吉他：每 0.5 秒拨一个音，带泛音、指数衰减
    std::vector<float> makeGuitar (double seconds, float level = 0.3f)
    {
        std::vector<float> s ((size_t) (seconds * sr), 0.0f);
        const double notes[] = { 82.41, 110.0, 146.83, 196.0, 246.94, 329.63 };
        for (size_t i = 0; i < s.size(); ++i)
        {
            const double t = (double) i / sr;
            const int n = (int) (t / 0.5);
            const double tn = t - n * 0.5;
            const double f = notes[n % 6];
            double v = 0.0;
            for (int h = 1; h <= 8; ++h)
                v += std::sin (juce::MathConstants<double>::twoPi * f * h * tn) / (h * h * 0.6 + 0.4);
            s[i] = (float) (level * v * std::exp (-tn * 4.0));
        }
        return s;
    }

    struct Stats { float peak = 0, rms = 0; bool finite = true; float maxStep = 0; };

    Stats stats (const std::vector<float>& l, const std::vector<float>& r, size_t from = 0)
    {
        Stats st;
        double sum = 0.0;
        for (size_t i = from; i < l.size(); ++i)
        {
            if (! std::isfinite (l[i]) || ! std::isfinite (r[i])) st.finite = false;
            st.peak = juce::jmax (st.peak, std::abs (l[i]), std::abs (r[i]));
            sum += (double) l[i] * l[i];
            if (i > from) st.maxStep = juce::jmax (st.maxStep, std::abs (l[i] - l[i - 1]));
        }
        st.rms = (float) std::sqrt (sum / juce::jmax ((size_t) 1, l.size() - from));
        return st;
    }

    juce::String db (float g) { return juce::String (juce::Decibels::gainToDecibels (g, -120.0f), 1) + " dB"; }

    void writeWav (const juce::File& f, const std::vector<float>& l, const std::vector<float>& r)
    {
        f.deleteFile();
        juce::WavAudioFormat wav;
        auto stream = f.createOutputStream();
        if (stream == nullptr) return;
        auto options = juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (24);
        std::unique_ptr<juce::OutputStream> os (stream.release());
        if (auto writer = wav.createWriterFor (os, options))
        {
            const float* chans[] = { l.data(), r.data() };
            writer->writeFromFloatArrays (chans, 2, (int) l.size());
        }
    }

    // 把输入送进引擎，返回立体声输出
    void run (AudioEngine& engine, const std::vector<float>& in, std::vector<float>& l, std::vector<float>& r)
    {
        l.assign (in.size(), 0.0f);
        r.assign (in.size(), 0.0f);
        engine.processOffline (in.data(), l.data(), r.data(), (int) in.size());
    }
}

// 把每个效果器的放大踏板渲染成 PNG，用来检查界面
static int renderPedalSnapshots (const juce::File& outDir)
{
    outDir.createDirectory();
    AudioEngine engine;
    engine.prepareForOffline (sr, block);
    EffectChain chain (engine);
    for (auto& info : getBuiltinEffectList())
    {
        auto slot = chain.createBuiltin (info.id);
        PedalEditorOverlay overlay (slot, {});
        overlay.setSize (1240, 800);
        auto img = overlay.createComponentSnapshot (overlay.getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        auto f = outDir.getChildFile ("pedal_" + info.id + ".png");
        f.deleteFile();
        if (auto os = f.createOutputStream())
            png.writeImageToStream (img, *os);
        std::printf ("  %s\n", f.getFileName().toRawUTF8());
    }
    return 0;
}

// 练习页截图：载入谱子，把谱面定在几个位置
static int renderHighwaySnapshots (const juce::File& songFile, const juce::File& outDir)
{
    outDir.createDirectory();
    AudioEngine engine;
    engine.prepareForOffline (sr, block);
    juce::PropertiesFile::Options o;
    o.applicationName = "LianqinTests";
    juce::PropertiesFile props (outDir.getChildFile ("test.settings"), o);
    PracticeLog plog (outDir.getChildFile ("log.json"));
    PracticePanel panel (engine.getTabPlayer(), engine.getDrums(), engine.getJudge(), engine.getRecorder(), plog, props);
    panel.setSize (884, 630);
    if (! panel.loadFile (songFile)) { std::printf ("  载入失败\n"); return 1; }
    auto song = engine.getTabPlayer().getSong();
    const double len = song->lengthBeats;
    int i = 0;
    for (double frac : { 0.0, 0.12, 0.3, 0.55 })
    {
        // 找到 frac 附近第一个音之前一点，让判定线上有音
        const auto& notes = song->tracks[(size_t) engine.getTabPlayer().getTrack()].notes;
        double beat = frac * len;
        for (auto& n : notes) if (n.start >= beat) { beat = juce::jmax (0.0, n.start - 0.02); break; }
        panel.getHighway().setFrozenPosition (beat);
        auto img = panel.createComponentSnapshot (panel.getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        auto f = outDir.getChildFile ("highway_" + juce::String (i++) + ".png");
        f.deleteFile();
        if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
        std::printf ("  %s  (拍 %.2f)\n", f.getFileName().toRawUTF8(), beat);
    }
    std::vector<TrackMixer::Strip> strips (song->tracks.size());
    if (strips.size() > 1) { strips[1].mute = true; strips[0].solo = true; strips[0].db = -6.0f; }
    TrackMixer mixer (*song, strips, nullptr);
    auto img = mixer.createComponentSnapshot (mixer.getLocalBounds(), true, 1.0f);
    juce::PNGImageFormat png;
    auto f = outDir.getChildFile ("mixer.png");
    f.deleteFile();
    if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
    return 0;
}

// 读取一个目录里所有吉他谱，打印概要；同名不同格式（.gp5/.gpx/.gp）互相比对
static int testSongs (const juce::File& dir)
{
    int failed = 0, total = 0;
    std::map<juce::String, std::vector<std::pair<juce::String, juce::String>>> fingerprints;

    for (auto& f : dir.findChildFiles (juce::File::findFiles, true, "*.gp3;*.gp4;*.gp5;*.gpx;*.gp"))
    {
        ++total;
        juce::String error;
        auto song = loadSongFile (f, error);
        if (song == nullptr)
        {
            ++failed;
            std::printf ("  [FAIL] %s: %s\n", f.getFileName().toRawUTF8(), error.toRawUTF8());
            continue;
        }

        int notes = 0;
        for (auto& t : song->tracks) notes += (int) t.notes.size();
        const int dt = song->defaultTrack();

        // 指纹：第一条轨前 24 个音的（拍位置, 弦, 品）
        juce::String fp;
        if (juce::isPositiveAndBelow (dt, (int) song->tracks.size()))
        {
            const auto& tr = song->tracks[(size_t) dt];
            for (size_t i = 0; i < juce::jmin ((size_t) 24, tr.notes.size()); ++i)
                fp << juce::String (tr.notes[i].start, 3) << ":" << tr.notes[i].string << "/" << tr.notes[i].fret << " ";
            fp << "| bars=" << (int) song->bars.size() << " notes=" << (int) tr.notes.size();
        }
        fingerprints[f.getFileNameWithoutExtension()].push_back ({ f.getFileExtension(), fp });

        std::printf ("  [OK  ] %-42s %-15s %3d 小节 %2d 轨 %5d 音  %5.1f 秒  速度 %.0f\n",
                     f.getFileName().toRawUTF8(), song->sourceFormat.toRawUTF8(), (int) song->bars.size(),
                     (int) song->tracks.size(), notes, song->lengthSeconds(), song->tempos.front().bpm);
    }

    std::printf ("\n== 同一首曲子不同格式的比对\n");
    int compared = 0, mismatched = 0;
    for (auto& [name, list] : fingerprints)
    {
        if (list.size() < 2) continue;
        ++compared;
        bool same = true;
        for (auto& e : list) same = same && e.second == list.front().second;
        if (! same)
        {
            ++mismatched;
            std::printf ("  [DIFF] %s\n", name.toRawUTF8());
            for (auto& e : list) std::printf ("         %-5s %s\n", e.first.toRawUTF8(), e.second.substring (0, 160).toRawUTF8());
        }
    }
    std::printf ("\n读取 %d 个文件，失败 %d；%d 组多格式比对，不一致 %d\n", total, failed, compared, mismatched);
    return failed == 0 ? 0 : 1;
}

// ---- 弹奏判定测试用：自己编的练习曲 + 合成的“演奏” ------------------------------
namespace judgetest
{
    RawNote note (int str, int fret, bool hammer = false)
    {
        RawNote n; n.string = str; n.fret = fret; n.hammerPull = hammer; return n;
    }

    // 8 小节，100 BPM：四分音符单音、八分音符、强力和弦、击弦
    std::shared_ptr<const Song> makeExerciseSong()
    {
        RawScore raw;
        raw.title = "Judge test";
        raw.initialTempo = 100.0;
        RawTrack t;
        t.name = "Guitar";
        t.tuning = { 64, 59, 55, 50, 45, 40 };
        auto addBar = [&] (std::vector<RawBeat> beats)
        {
            RawMasterBar mb;
            raw.masterBars.push_back (mb);
            RawBar bar;
            bar.voices.push_back ({ std::move (beats) });
            t.bars.push_back (std::move (bar));
        };
        auto beat = [] (double d, std::vector<RawNote> ns) { RawBeat b; b.duration = d; b.notes = std::move (ns); return b; };

        // 1–4：四分音符，各根弦各个把位
        const int q[4][4][2] = { { {5,3},{4,2},{3,0},{2,2} }, { {1,1},{0,0},{0,3},{1,3} },
                                 { {2,4},{3,2},{4,3},{5,5} }, { {3,5},{2,7},{1,8},{0,7} } };
        for (auto& bar : q)
        {
            std::vector<RawBeat> beats;
            for (auto& nf : bar) beats.push_back (beat (1.0, { note (nf[0], nf[1]) }));
            addBar (beats);
        }
        // 5–6：八分音符音阶
        const int e[16][2] = { {5,5},{5,7},{4,5},{4,7},{3,4},{3,5},{3,7},{2,5}, {2,7},{1,5},{1,6},{1,8},{0,5},{0,7},{0,8},{0,10} };
        for (int b = 0; b < 2; ++b)
        {
            std::vector<RawBeat> beats;
            for (int i = 0; i < 8; ++i) beats.push_back (beat (0.5, { note (e[b * 8 + i][0], e[b * 8 + i][1]) }));
            addBar (beats);
        }
        // 7：强力和弦（二分音符）
        addBar ({ beat (2.0, { note (5, 0), note (4, 2), note (3, 2) }), beat (2.0, { note (4, 0), note (3, 2), note (2, 2) }) });
        // 8：拨一个音 + 击弦
        {
            std::vector<RawBeat> beats;
            for (int i = 0; i < 4; ++i)
            {
                beats.push_back (beat (0.5, { note (2, 5) }));
                beats.push_back (beat (0.5, { note (2, 7, true) }));
            }
            addBar (beats);
        }
        raw.tracks.push_back (t);
        return std::shared_ptr<const Song> (buildSong (raw).release());
    }

    struct Plan { int kind = 0; float offsetMs = 0.0f; };   // 0 正确，1 弹错（高半音），2 漏弹

    // 按计划合成演奏（单声道），lat = 整体再晚多少毫秒
    std::vector<float> perform (const Song& song, const JudgeSession& js, const std::vector<Plan>& plan, double extraLatencyMs)
    {
        const auto& notes = song.tracks[0].notes;
        const double total = song.lengthSeconds() + 1.5;
        std::vector<float> out ((size_t) (total * sr), 0.0f);
        PluckSynth synth;
        synth.prepare (sr);
        struct Hit { int64_t at; PluckSynth::NoteOn on; };
        std::vector<Hit> hits;
        for (size_t e = 0; e < js.events.size(); ++e)
        {
            const auto& ev = js.events[e];
            if (plan[e].kind == 2) continue;
            const double t = song.beatToSeconds (ev.beat) + (plan[e].offsetMs + extraLatencyMs) / 1000.0;
            for (int k = ev.firstNote; k < ev.firstNote + ev.numNotes; ++k)
            {
                const auto& n = notes[(size_t) k];
                PluckSynth::NoteOn on;
                on.midi = n.midi + (plan[e].kind == 1 ? 1 : 0);
                on.velocity = n.hammerPull ? 0.55f : 0.85f;
                on.hammer = n.hammerPull;
                on.lengthSeconds = song.beatToSeconds (n.start + n.length) - song.beatToSeconds (n.start);
                hits.push_back ({ (int64_t) (t * sr), on });
            }
        }
        std::sort (hits.begin(), hits.end(), [] (const Hit& a, const Hit& b) { return a.at < b.at; });
        std::vector<float> l (block), r (block);
        size_t h = 0;
        juce::Random rnd (7);
        for (int64_t pos = 0; pos < (int64_t) out.size(); pos += block)
        {
            const int n = (int) juce::jmin ((int64_t) block, (int64_t) out.size() - pos);
            while (h < hits.size() && hits[h].at < pos + n)
            {
                auto on = hits[h].on;
                on.delaySamples = (int) juce::jmax ((int64_t) 0, hits[h].at - pos);
                synth.noteOn (on);
                ++h;
            }
            std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
            synth.render (l.data(), r.data(), n);
            for (int i = 0; i < n; ++i)
                out[(size_t) (pos + i)] = 0.5f * (l[(size_t) i] + r[(size_t) i]) + 0.0005f * (rnd.nextFloat() - 0.5f)
                                          + 0.0015f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 50.0 * (double) (pos + i) / sr);
        }
        return out;
    }

    // 把演奏喂进引擎 + 判定（同步处理，不开线程）
    void runEngine (AudioEngine& engine, const std::vector<float>& in)
    {
        std::vector<float> l (in.size()), r (in.size());
        for (size_t pos = 0; pos < in.size(); pos += block)
        {
            const int n = (int) juce::jmin ((size_t) block, in.size() - pos);
            engine.processOffline (in.data() + pos, l.data() + pos, r.data() + pos, n);
            engine.getJudge().processPending();
        }
    }

    void setupEngine (AudioEngine& engine, std::shared_ptr<const Song> song, std::shared_ptr<JudgeSession> js, float latencyMs)
    {
        engine.prepareForOffline (sr, block);
        engine.setOutputGainDb (-60.0f);
        auto& tp = engine.getTabPlayer();
        tp.setSong (song);
        tp.setTrack (0);
        tp.setCountIn (false);
        tp.setDemoOn (false);
        tp.setBackingVolumeDb (-60.0f);
        engine.getJudge().setSession (js);
        engine.getJudge().setLatencyMs (latencyMs);
        engine.getJudge().setEnabled (true);
        engine.getJudge().processPending();
        tp.setPlaying (true);
    }
}

// 看每个 NAM 模型有没有带箱体：白噪声进去，比较 6–10 kHz 和 1–3 kHz 的能量
static int probeNamModels (const juce::File& dir)
{
    for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.nam"))
    {
        NamProcessor p;
        juce::String err;
        if (! p.loadModel (f, err)) { std::printf ("  [FAIL] %s %s\n", f.getFileName().toRawUTF8(), err.toRawUTF8()); continue; }
        p.prepareToPlay (48000.0, 512);
        juce::Random rnd (1);
        juce::dsp::FFT fft (12);
        std::vector<float> acc (2048, 0.0f), buf (8192);
        juce::AudioBuffer<float> b (2, 4096);
        double inSum = 0.0, outSum = 0.0;
        for (int k = 0; k < 24; ++k)
        {
            for (int i = 0; i < 4096; ++i) { const float x = 0.05f * (rnd.nextFloat() * 2.0f - 1.0f); b.setSample (0, i, x); b.setSample (1, i, x); inSum += x * x; }
            for (int pos = 0; pos < 4096; pos += 512)
            {
                juce::AudioBuffer<float> sub (b.getArrayOfWritePointers(), 2, pos, 512);
                juce::MidiBuffer m;
                p.processBlock (sub, m);
            }
            if (k < 4) continue;
            for (int i = 0; i < 4096; ++i) { buf[(size_t) i] = b.getSample (0, i) * (0.5f - 0.5f * std::cos (6.2831853f * i / 4096.0f)); outSum += b.getSample (0, i) * b.getSample (0, i); }
            std::fill (buf.begin() + 4096, buf.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (buf.data());
            for (int i = 0; i < 2048; ++i) acc[(size_t) i] += buf[(size_t) i] * buf[(size_t) i];
        }
        auto band = [&] (double lo, double hi) { double s = 0; for (int i = (int) (lo / 11.71875); i < (int) (hi / 11.71875); ++i) s += acc[(size_t) i]; return s / (hi - lo); };
        const double hiRatio = 10.0 * std::log10 (band (6000, 10000) / band (1000, 3000));
        const double lowRatio = 10.0 * std::log10 (band (60, 200) / band (1000, 3000));
        std::printf ("  %-48s 高频 %6.1f dB  低频 %6.1f dB  增益 %5.1f dB\n", f.getFileName().toRawUTF8(), hiRatio, lowRatio,
                     10.0 * std::log10 (outSum / (inSum * 20.0 / 24.0)));
    }
    return 0;
}

// 判定界面截图：合成一段有对有错的演奏，渲染谱面和成绩卡
static int renderJudgeSnapshots (const juce::File& outDir)
{
    using namespace judgetest;
    outDir.createDirectory();
    auto song = makeExerciseSong();
    auto js = std::make_shared<JudgeSession> (song, 0);
    std::vector<Plan> plan (js->events.size());
    juce::Random rnd (3);
    for (size_t e = 0; e < plan.size(); ++e)
    {
        plan[e].kind = (e % 9 == 4) ? 1 : (e % 11 == 6 ? 2 : 0);
        plan[e].offsetMs = (rnd.nextFloat() - 0.5f) * 90.0f;
    }
    const auto audio = perform (*song, *js, plan, 0.0);
    AudioEngine engine;
    setupEngine (engine, song, js, 0.0f);
    runEngine (engine, audio);

    juce::PNGImageFormat png;
    auto save = [&] (juce::Component& c, const juce::String& name)
    {
        auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
        auto f = outDir.getChildFile (name);
        f.deleteFile();
        if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
        std::printf ("  %s\n", f.getFileName().toRawUTF8());
    };

    juce::PropertiesFile::Options o;
    o.applicationName = "LianqinTests";
    juce::PropertiesFile props (outDir.getChildFile ("test.settings"), o);
    PracticeLog plog (outDir.getChildFile ("log.json"));
    PracticePanel panel (engine.getTabPlayer(), engine.getDrums(), engine.getJudge(), engine.getRecorder(), plog, props);
    panel.setSize (884, 630);
    auto& hw = panel.getHighway();
    hw.setSong (song, 0);
    hw.setJudgeSession (js, false);
    hw.setFrozenPosition (17.0);
    save (panel, "judge_highway.png");

    JudgeSummaryCard card;
    card.setSize (460, 240);
    card.show (*js);
    save (card, "judge_summary.png");

    // 练习记录页（假数据，只为看排版）
    {
        const auto f = outDir.getChildFile ("stats_demo.json");
        f.deleteFile();
        PracticeLog pl (f);
        juce::Random rr (5);
        const char* names[] = { "Canon Rock", "Exercise in A minor", "Blues shuffle in E", "Pentatonic run" };
        for (int k = 0; k < 4; ++k)
        {
            const juce::String path = juce::String ("C:/tabs/") + names[k] + ".gp5";
            pl.addTime (path, names[k], 600.0 + 900.0 * rr.nextFloat());
            float acc = 0.55f + 0.1f * (float) k;
            for (int i = 0; i < 18 + 6 * k; ++i)
            {
                acc = juce::jlimit (0.3f, 1.0f, acc + (rr.nextFloat() - 0.35f) * 0.08f);
                pl.addPass (path, names[k], acc, juce::jmin (1.0f, 0.6f + 0.025f * (float) i));
            }
        }
        StatsView sv (pl);
        sv.setSize (884, 690);
        sv.refresh();
        save (sv, "stats_view.png");
        f.deleteFile();
    }
    return 0;
}

static int probePresets (const juce::File& dir)
{
    for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.xml"))
    {
        auto xml = juce::parseXML (f);
        if (xml == nullptr)
        {
            std::printf ("  [FAIL] %s (parse)\n", f.getFileName().toRawUTF8());
            continue;
        }

        AudioEngine engine;
        engine.prepareForOffline (sr, block);
        engine.setOutputGainDb (0.0f);
        EffectChain chain (engine);
        juce::StringArray problems;
        chain.loadFromXml (*xml, problems);

        std::vector<float> silence ((size_t) (4.0 * sr), 0.0f), l, r;
        run (engine, silence, l, r);
        const auto noise = stats (l, r, (size_t) (0.5 * sr));

        // A quiet, realistic input: -66 dBFS hiss plus a little 50 Hz hum.
        std::vector<float> hiss ((size_t) (4.0 * sr), 0.0f);
        juce::Random rnd (1234);
        for (size_t i = 0; i < hiss.size(); ++i)
            hiss[i] = 0.0005f * (rnd.nextFloat() * 2.0f - 1.0f)
                    + 0.0008f * (float) std::sin (juce::MathConstants<double>::twoPi * 50.0 * (double) i / sr);
        std::vector<float> hl, hr;
        run (engine, hiss, hl, hr);
        const auto hissOut = stats (hl, hr, (size_t) (0.5 * sr));

        const auto guitar = makeGuitar (3.0);
        std::vector<float> gl, gr;
        run (engine, guitar, gl, gr);
        const auto tone = stats (gl, gr, (size_t) (0.2 * sr));

        std::printf ("  %-34s silence %7s | hiss RMS %7s peak %7s | guitar RMS %7s peak %7s%s\n",
                     f.getFileNameWithoutExtension().toRawUTF8(),
                     db (noise.rms).toRawUTF8(),
                     db (hissOut.rms).toRawUTF8(), db (hissOut.peak).toRawUTF8(),
                     db (tone.rms).toRawUTF8(), db (tone.peak).toRawUTF8(),
                     problems.isEmpty() ? "" : ("  problems: " + problems.joinIntoString (", ")).toRawUTF8());
    }
    return 0;
}

int runOfflineTests (int argc, char* argv[])
{
#ifndef TONE_TESTS_EMBEDDED
    juce::ScopedJuceInitialiser_GUI init;
#endif
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    std::setvbuf (stderr, nullptr, _IONBF, 0);
    if (argc > 2 && juce::String (argv[1]) == "--songs")
        return testSongs (juce::File (argv[2]));
    if (argc > 3 && juce::String (argv[1]) == "--highway")
        return renderHighwaySnapshots (juce::File (argv[2]), juce::File (argv[3]));
    if (argc > 2 && juce::String (argv[1]) == "--nam-probe")
        return probeNamModels (juce::File (argv[2]));
    if (argc > 2 && juce::String (argv[1]) == "--preset-probe")
        return probePresets (juce::File (argv[2]));
    if (argc > 2 && juce::String (argv[1]) == "--judge-snap")
        return renderJudgeSnapshots (juce::File (argv[2]));
    if (argc > 2 && juce::String (argv[1]) == "--gervana-ui")
    {
        // 效果器板上的 Gervana 卡片（看配色和图片预留位）
        AudioEngine engine;
        engine.prepareForOffline (sr, block);
        EffectChain chain (engine);
        auto xml = juce::parseXML (T (DefaultPresets::listGervana[10][1]));
        juce::StringArray problems;
        chain.loadFromXml (*xml, problems);
        chain.insert (chain.createBuiltin ("gv_wuyin"), 1);
        chain.insert (chain.createBuiltin ("gv_yimao"));
        chain.insert (chain.createBuiltin ("gv_yiyou"), 2);
        chain.setBypassed (3, true);
        PedalBoard board (chain, nullptr);
        board.setSize (1900, 200);
        for (int i = 0; i < 200; ++i) if (! juce::detail::dispatchNextMessageOnSystemQueue (true)) break;
        auto img = board.createComponentSnapshot (board.getLocalBounds(), true, 1.5f);
        juce::PNGImageFormat png;
        auto f = juce::File (juce::String (argv[2])).getChildFile ("gervana_board.png");
        f.deleteFile();
        if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
        return 0;
    }
    if (argc > 2 && juce::String (argv[1]) == "--snapshots")
        return renderPedalSnapshots (juce::File (argv[2]));
    const juce::File modelDir (argc > 1 ? argv[1] : ".");
    const juce::File outDir (argc > 2 ? argv[2] : ".");
    outDir.createDirectory();

    const auto guitar = makeGuitar (3.0);
    std::vector<float> l, r;

    // ---------------------------------------------------------------------
    std::printf ("\n== 1. 每个内置效果器单独测试（3 秒，48 kHz，128 采样块）\n");
    for (auto& info : getBuiltinEffectList())
    {
        AudioEngine engine;
        engine.prepareForOffline (sr, block);
        engine.setOutputGainDb (0.0f);
        EffectChain chain (engine);
        chain.insert (chain.createBuiltin (info.id));
        run (engine, guitar, l, r);
        const auto st = stats (l, r, (size_t) (0.05 * sr));
        check (st.finite && st.peak > 0.01f && st.peak < 4.0f,
               info.id + " (" + info.name + ")  峰值 " + db (st.peak) + "  RMS " + db (st.rms));
        chain.clear();
        engine.processOffline (guitar.data(), l.data(), r.data(), block * 4);
        chain.collectGarbage();
    }

    // ---------------------------------------------------------------------
    std::printf ("\n== 2. 默认音色预设整条链路\n");
    const auto presetDir = outDir.getChildFile ("presets");
    presetDir.createDirectory();
    for (auto& p : DefaultPresets::list)
        presetDir.getChildFile (T (p[0]) + ".xml").replaceWithText (T (p[1]));
    for (auto& f : presetDir.findChildFiles (juce::File::findFiles, false, "*.xml"))
    {
        AudioEngine engine;
        engine.prepareForOffline (sr, block);
        engine.setOutputGainDb (0.0f);
        EffectChain chain (engine);
        juce::StringArray problems;
        chain.loadFromXml (*juce::parseXML (f), problems);
        run (engine, guitar, l, r);
        const auto st = stats (l, r, (size_t) (0.05 * sr));
        check (problems.isEmpty() && st.finite && st.peak > 0.05f && st.peak < 1.5f,
               f.getFileNameWithoutExtension() + "  " + juce::String ((int) chain.getSlots().size()) + T (" 块  峰值 ")
                   + db (st.peak) + "  RMS " + db (st.rms));
        writeWav (outDir.getChildFile ("preset_" + f.getFileNameWithoutExtension() + ".wav"), l, r);
    }

    // ---------------------------------------------------------------------
    std::printf ("\n== 3. 换链路 / 开关时不爆音\n");
    {
        // 持续正弦，中途换链路和旁通，看相邻样本的最大跳变
        std::vector<float> sine ((size_t) (2.0 * sr));
        for (size_t i = 0; i < sine.size(); ++i)
            sine[i] = 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * (double) i / sr);

        AudioEngine engine;
        engine.prepareForOffline (sr, block);
        engine.setOutputGainDb (0.0f);
        EffectChain chain (engine);
        chain.insert (chain.createBuiltin ("boost"));

        std::vector<float> outL (sine.size()), outR (sine.size());
        const int quarter = (int) sine.size() / 4;
        engine.processOffline (sine.data(), outL.data(), outR.data(), quarter);
        chain.insert (chain.createBuiltin ("eq"));                // 换链路
        engine.processOffline (sine.data() + quarter, outL.data() + quarter, outR.data() + quarter, quarter);
        chain.setBypassed (0, true);                              // 旁通
        engine.processOffline (sine.data() + 2 * quarter, outL.data() + 2 * quarter, outR.data() + 2 * quarter, quarter);
        chain.remove (0);                                         // 删除
        engine.processOffline (sine.data() + 3 * quarter, outL.data() + 3 * quarter, outR.data() + 3 * quarter, quarter);
        chain.collectGarbage();

        float maxStep = 0.0f;
        for (size_t i = 1; i < outL.size(); ++i)
            maxStep = juce::jmax (maxStep, std::abs (outL[i] - outL[i - 1]));
        // 0.3 振幅 220 Hz 正弦本身的最大斜率约 0.0086；+6 dB 后约 0.017
        check (maxStep < 0.04f, T ("最大相邻样本跳变 ") + juce::String (maxStep, 4) + T ("（无爆音阈值 0.04）"));
    }

    // ---------------------------------------------------------------------
    std::printf ("\n== 4. NAM 箱头模型\n");
    for (auto& f : modelDir.findChildFiles (juce::File::findFiles, false, "*.nam"))
    {
        AudioEngine engine;
        engine.prepareForOffline (sr, block);
        engine.setOutputGainDb (0.0f);
        EffectChain chain (engine);
        juce::String error;
        auto slot = chain.createNam (f, error);
        if (slot == nullptr)
        {
            std::printf ("  [skip] %s: %s\n", f.getFileName().toRawUTF8(), error.toRawUTF8());
            continue;
        }
        chain.insert (slot);
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        run (engine, guitar, l, r);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        const auto st = stats (l, r, (size_t) (0.05 * sr));
        auto* nam = dynamic_cast<NamProcessor*> (&slot->getProcessor());
        check (st.finite && st.peak > 1.0e-4f,
               f.getFileName() + "  峰值 " + db (st.peak) + T ("  耗时 ") + juce::String (ms / 30.0, 2)
                   + T ("% 实时  模型采样率 ") + juce::String (nam->getExpectedSampleRate(), 0));
    }

    // ---------------------------------------------------------------------
    std::printf ("\n== 4b. NAM 采样率转换\n");
    {
        auto models = modelDir.findChildFiles (juce::File::findFiles, true, "*.nam");
        if (! models.isEmpty())
        {
            for (double testRate : { 44100.0, 96000.0 })
            {
                AudioEngine engine;
                engine.prepareForOffline (testRate, block);
                engine.setOutputGainDb (0.0f);
                EffectChain chain (engine);
                juce::String error;
                auto slot = chain.createNam (models[0], error);
                if (slot == nullptr)
                    continue;
                chain.insert (slot);

                const int n = (int) (0.5 * testRate);
                std::vector<float> in ((size_t) n), l ((size_t) n), r ((size_t) n);
                for (int i = 0; i < n; ++i)
                    in[(size_t) i] = 0.2f * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * i / testRate);

                engine.processOffline (in.data(), l.data(), r.data(), n);
                chain.collectGarbage();

                const auto st = stats (l, r, (size_t) (0.05 * testRate));
                auto* nam = dynamic_cast<NamProcessor*> (&slot->getProcessor());
                check (st.finite && st.peak > 1.0e-4f && nam != nullptr
                           && ! nam->hasSampleRateMismatch() && nam->effectLatencySamples() > 0,
                       T ("NAM @ ") + juce::String (testRate / 1000.0, 1) + T (" kHz  峰值 ")
                           + db (st.peak) + T ("  延迟 ") + juce::String (nam != nullptr ? nam->effectLatencySamples() : 0)
                           + T (" 采样"));
            }
        }
    }

    // ---------------------------------------------------------------------
    std::printf ("\n== 5. 箱体 IR\n");
    {
        // 合成一个 IR：低通的衰减噪声
        const auto irFile = outDir.getChildFile ("test_ir.wav");
        std::vector<float> ir ((size_t) (0.05 * sr));
        juce::Random rnd (1);
        float lp = 0.0f;
        for (size_t i = 0; i < ir.size(); ++i)
        {
            lp += 0.2f * ((rnd.nextFloat() * 2.0f - 1.0f) - lp);
            ir[i] = lp * std::exp (-(float) i / (float) (0.008 * sr));
        }
        ir[0] = 1.0f;
        writeWav (irFile, ir, ir);

        AudioEngine engine;
        engine.prepareForOffline (sr, block);
        engine.setOutputGainDb (0.0f);
        EffectChain chain (engine);
        juce::String error;
        auto slot = chain.createIr (irFile, error);
        check (slot != nullptr, T ("加载 IR ") + error);
        if (slot != nullptr)
        {
            chain.insert (slot);
            juce::Thread::sleep (500);   // 卷积在后台线程加载
            run (engine, guitar, l, r);
            const auto st = stats (l, r, (size_t) (0.05 * sr));
            check (st.finite && st.peak > 0.01f && st.peak < 4.0f, T ("IR 卷积  峰值 ") + db (st.peak));
        }
    }

    // ---------------------------------------------------------------------
    std::printf ("\n== 6. 鼓机\n");
    {
        // 6a. 节拍器在 120 BPM 下的节拍间隔必须精确
        AudioEngine engine;
        engine.prepareForOffline (sr, block);
        engine.setMonitoring (false);
        auto& drums = engine.getDrums();
        const auto& styles = getDrumStyles();
        int clickOnly = 0;
        for (int i = 0; i < (int) styles.size(); ++i)
            if (styles[(size_t) i].clickOnly) clickOnly = i;

        drums.setStyle (clickOnly);
        drums.setBpm (120.0f);
        drums.setCountIn (false);
        drums.setVolumeDb (0.0f);
        drums.setPlaying (true);

        std::vector<float> silence ((size_t) (4.0 * sr), 0.0f);
        run (engine, silence, l, r);

        std::vector<int> onsets;
        for (size_t i = 1; i < l.size(); ++i)
            if (std::abs (l[i]) > 0.02f && (onsets.empty() || (int) i - onsets.back() > (int) (0.1 * sr)))
                onsets.push_back ((int) i);

        int worst = 0;
        for (size_t i = 1; i < onsets.size(); ++i)
            worst = juce::jmax (worst, std::abs (onsets[i] - onsets[i - 1] - 24000));
        check (onsets.size() >= 7 && worst <= 2,
               T ("120 BPM 节拍器：") + juce::String ((int) onsets.size()) + T (" 拍，间隔误差最大 ") + juce::String (worst) + T (" 个采样"));

        // 6b. 每种风格渲染 8 小节
        for (int i = 0; i < (int) styles.size(); ++i)
        {
            AudioEngine e2;
            e2.prepareForOffline (sr, block);
            e2.setMonitoring (false);
            auto& d = e2.getDrums();
            d.setStyle (i);
            d.setBpm (styles[(size_t) i].defaultBpm);
            d.setSwing (styles[(size_t) i].defaultSwing);
            d.setFillEvery (4);
            d.setCountIn (true);
            d.setVolumeDb (-6.0f);
            d.setPlaying (true);
            const double secs = 60.0 / styles[(size_t) i].defaultBpm * styles[(size_t) i].beatsPerBar * 9.0;
            std::vector<float> sil ((size_t) (secs * sr), 0.0f);
            run (e2, sil, l, r);
            const auto st = stats (l, r);
            check (st.finite && st.peak > 0.05f && st.peak < 1.0f,
                   styles[(size_t) i].name + T ("  峰值 ") + db (st.peak) + "  RMS " + db (st.rms));
            writeWav (outDir.getChildFile ("drums_" + juce::String (i) + ".wav"), l, r);
        }

        // 6c. The editable Custom style must not use alternating B bars, so the
        // pattern shown in the grid is what plays on every bar.
        {
            const auto& custom = getCustomDrumStyle();
            bool bHits = false;
            for (auto& instrument : custom.verseB.velocity)
                for (float v : instrument) bHits = bHits || v > 0.0f;
            for (auto& instrument : custom.chorusB.velocity)
                for (float v : instrument) bHits = bHits || v > 0.0f;
            check (! bHits, T ("自定义节奏没有 A/B 变体（格子与实际播放一致）"));
        }
    }

    // ---------------------------------------------------------------------
    std::printf ("\n== 7. 吉他 + 鼓 + 默认音色 混在一起\n");
    {
        AudioEngine engine;
        engine.prepareForOffline (sr, block);
        engine.setOutputGainDb (-6.0f);
        EffectChain chain (engine);
        if (auto xml = juce::parseXML (presetDir.getChildFile (T ("过载.xml"))))
        {
            juce::StringArray p;
            chain.loadFromXml (*xml, p);
        }
        auto& d = engine.getDrums();
        d.setStyle (0); d.setBpm (110); d.setCountIn (false); d.setVolumeDb (-8.0f); d.setPlaying (true);
        const auto g = makeGuitar (6.0);
        run (engine, g, l, r);
        const auto st = stats (l, r);
        check (st.finite && st.peak < 1.0f, T ("混合输出 峰值 ") + db (st.peak) + "  RMS " + db (st.rms));
        writeWav (outDir.getChildFile ("mix.wav"), l, r);
    }

    // ---------------------------------------------------------------------
    std::printf ("\n== 8. 周边效果器：音高、降噪、去嗡声\n");
    {
        auto sineAt = [] (double f, double secs, float amp)
        {
            std::vector<float> v ((size_t) (secs * sr));
            for (size_t i = 0; i < v.size(); ++i)
                v[i] = amp * (float) std::sin (juce::MathConstants<double>::twoPi * f * (double) i / sr);
            return v;
        };

        // 跑一块效果器，返回输出（左声道）
        auto runFx = [] (const juce::String& id, std::initializer_list<std::pair<const char*, float>> params,
                         const std::vector<float>& in)
        {
            AudioEngine engine;
            engine.prepareForOffline (sr, block);
            engine.setOutputGainDb (0.0f);
            EffectChain chain (engine);
            auto slot = chain.createBuiltin (id);
            if (auto* fx = dynamic_cast<BuiltinEffect*> (&slot->getProcessor()))
                for (auto& p : params)
                    fx->setParam (p.first, p.second);
            chain.insert (slot);
            std::vector<float> outL (in.size()), outR (in.size());
            engine.processOffline (in.data(), outL.data(), outR.data(), (int) in.size());
            return outL;
        };

        auto pitchOf = [] (const std::vector<float>& v)
        {
            PitchDetector pd;
            const int len = 4096;
            return pd.detect (v.data() + v.size() - (size_t) len, len, sr, 40.0f, 2000.0f).frequency;
        };

        const auto a3 = sineAt (220.0, 1.5, 0.3f);
        struct Case { const char* name; juce::String id; std::initializer_list<std::pair<const char*, float>> p; float expect; };
        const Case cases[] = {
            { "八度 复音 低八度", "octave", { { "mode", 0 }, { "dry", 0 }, { "down", 1 }, { "up", 0 } }, 110.0f },
            { "八度 复音 高八度", "octave", { { "mode", 0 }, { "dry", 0 }, { "down", 0 }, { "up", 1 } }, 440.0f },
            { "八度 单音模拟 低八度", "octave", { { "mode", 1 }, { "dry", 0 }, { "down", 1 }, { "up", 0 } }, 110.0f },
            { "移调 +7 半音", "pitch", { { "semis", 7 }, { "mix", 1 } }, 329.6f },
            { "移调 -5 半音", "pitch", { { "semis", -5 }, { "mix", 1 } }, 164.8f },
        };
        for (auto& c : cases)
        {
            const float f = pitchOf (runFx (c.id, c.p, a3));
            const float cents = f > 0 ? 1200.0f * std::log2 (f / c.expect) : 9999.0f;
            check (std::abs (cents) < 15.0f, T (c.name) + T ("：输入 220 Hz，输出 ") + juce::String (f, 1)
                                                 + T (" Hz（期望 ") + juce::String (c.expect, 1) + "）");
        }

        // 降噪：-70 dBFS 的底噪应该被压低约 40 dB；正常弹奏音量不受影响
        std::vector<float> noise ((size_t) (1.0 * sr));
        juce::Random rnd (7);
        for (auto& x : noise) x = 0.000316f * (rnd.nextFloat() * 2.0f - 1.0f) * 1.7f;
        const auto nOut = runFx ("denoise", { { "threshold", -58 }, { "amount", 40 } }, noise);
        auto rms = [] (const std::vector<float>& v, size_t from)
        {
            double s2 = 0; for (size_t i = from; i < v.size(); ++i) s2 += (double) v[i] * v[i];
            return (float) std::sqrt (s2 / (double) (v.size() - from));
        };
        const float reduction = juce::Decibels::gainToDecibels (rms (nOut, (size_t) (0.5 * sr)) / rms (noise, (size_t) (0.5 * sr)));
        check (reduction < -35.0f, T ("降噪：底噪被压低 ") + juce::String (reduction, 1) + " dB");

        const auto loud = sineAt (220.0, 1.0, 0.1f);
        const auto lOut = runFx ("denoise", { { "threshold", -58 }, { "amount", 40 } }, loud);
        const float pass = juce::Decibels::gainToDecibels (rms (lOut, (size_t) (0.5 * sr)) / rms (loud, (size_t) (0.5 * sr)));
        check (std::abs (pass) < 0.5f, T ("降噪：正常音量的信号变化 ") + juce::String (pass, 2) + " dB");

        const auto hum = sineAt (50.0, 1.5, 0.05f);
        const auto hOut = runFx ("denoise", { { "threshold", -85 }, { "amount", 0 }, { "hum", 1 } }, hum);
        const float humCut = juce::Decibels::gainToDecibels (rms (hOut, (size_t) (1.0 * sr)) / rms (hum, (size_t) (1.0 * sr)));
        check (humCut < -20.0f, T ("去嗡声：50 Hz 被压低 ") + juce::String (humCut, 1) + " dB");
    }

    // ---------------------------------------------------------------------
    std::printf ("\n== 9. 谱子播放\n");
    {
        // 9a. 拨弦合成器音准
        for (int midi : { 40, 45, 57, 64, 76 })
        {
            PluckSynth ps;
            ps.prepare (sr);
            PluckSynth::NoteOn on;
            on.midi = midi; on.lengthSeconds = 1.0;
            on.bass = midi < 43;
            ps.noteOn (on);
            std::vector<float> bl ((size_t) (0.6 * sr), 0.0f), br (bl.size(), 0.0f);
            ps.render (bl.data(), br.data(), (int) bl.size());
            PitchDetector pd;
            const auto res = pd.detect (bl.data() + (size_t) (0.2 * sr), 4096, sr, 40.0f, 2000.0f);
            const double expect = 440.0 * std::pow (2.0, (midi - 69) / 12.0);
            const double cents = res.frequency > 0 ? 1200.0 * std::log2 (res.frequency / expect) : 9999.0;
            check (std::abs (cents) < 10.0, T ("拨弦音 MIDI ") + juce::String (midi) + T ("：") + juce::String (res.frequency, 1)
                                                + " Hz（" + juce::String (cents, 1) + T (" 音分）"));
        }

        // 9b. 走带：读一首测试谱，播放 3 秒，位置要和速度对得上
        const auto file = juce::File (argc > 3 ? argv[3] : "");
        juce::String error;
        auto loaded = file.existsAsFile() ? loadSongFile (file, error) : nullptr;
        if (loaded == nullptr)
            std::printf ("  [skip] 没有给测试谱（第三个参数）\n");
        else
        {
            std::shared_ptr<const Song> song (std::move (loaded));
            AudioEngine engine;
            engine.prepareForOffline (sr, block);
            engine.setMonitoring (false);
            auto& tp = engine.getTabPlayer();
            tp.setSong (song);
            tp.setTrack (song->defaultTrack());
            tp.setCountIn (false);
            tp.setDemoVolumeDb (0.0f);
            tp.setSpeed (0.75f);
            tp.setPlaying (true);

            std::vector<float> sil ((size_t) (3.0 * sr), 0.0f);
            run (engine, sil, l, r);
            const double expectBeats = song->secondsToBeat (3.0 * 0.75);
            const double got = tp.getPosition();
            const auto st = stats (l, r);
            check (std::abs (got - juce::jmin (expectBeats, song->lengthBeats)) < 0.02,
                   T ("75% 速度播放 3 秒：位置 ") + juce::String (got, 3) + T (" 拍（期望 ") + juce::String (expectBeats, 3) + "）");
            check (st.finite && st.peak > 0.01f && st.peak < 1.5f, T ("示范音有声音，峰值 ") + db (st.peak));

            // 9c. A-B 循环
            tp.seek (0.0);
            tp.setSpeed (1.0f);
            tp.setLoop (0.0, 2.0, true);
            run (engine, sil, l, r);
            check (tp.getPosition() >= 0.0 && tp.getPosition() <= 2.0 + 1.0e-6,
                   T ("A-B 循环 0~2 拍，3 秒后位置 ") + juce::String (tp.getPosition(), 3));
            writeWav (outDir.getChildFile ("tab_demo.wav"), l, r);
        }
    }

    // ---------------------------------------------------------------------
    std::printf ("\n== 10. 在线找谱：下载文件夹监视\n");
    {
        auto dir = juce::File::createTempFile ("dl");
        dir.createDirectory();
        dir.getChildFile ("old.gp5").replaceWithText ("old");
        DownloadWatcher w;
        w.start (dir);
        check (w.isActive() && ! w.poll().exists(), juce::String::fromUTF8 ("开始监视时已有的谱子不算新下载"));
        dir.getChildFile ("readme.txt").replaceWithText ("x");
        dir.getChildFile ("new.gp.crdownload").replaceWithText ("partial");
        check (! w.poll().exists() && ! w.poll().exists(), juce::String::fromUTF8 ("非谱子文件、未下载完的临时文件被忽略"));
        dir.getChildFile ("new.gp").replaceWithText ("0123456789");
        const auto first = w.poll();
        const auto second = w.poll();
        check (! first.exists() && second.getFileName() == "new.gp", juce::String::fromUTF8 ("新谱子大小稳定后被发现：") + second.getFileName());
        check (! w.poll().exists(), juce::String::fromUTF8 ("同一个文件只报告一次"));
        dir.deleteRecursively();

        TabSearchBox box ("Canon Rock", juce::File ("C:/Users/me/Downloads"));
        juce::String opened;
        box.onSearch = [&opened] (const juce::String& u) { opened = u; };
        auto img = box.createComponentSnapshot (box.getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        auto f = outDir.getChildFile ("tabsearch.png");
        f.deleteFile();
        if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
        for (auto& site : TabSearchBox::getSites())
            check (site.urlTemplate.startsWith ("https://") && site.urlTemplate.contains ("{q}"), juce::String::fromUTF8 ("搜索网址：") + site.name);
    }

    // ---------------------------------------------------------------------
    std::printf ("\n== 11. 弹奏判定（合成演奏：正确 / 弹错 / 漏弹 / 时间偏差）\n");
    for (float latency : { 0.0f, 25.0f })
    {
        using namespace judgetest;
        auto song = makeExerciseSong();
        auto js = std::make_shared<JudgeSession> (song, 0);
        std::vector<Plan> plan (js->events.size());
        juce::Random rnd (42);
        for (size_t e = 0; e < plan.size(); ++e)
        {
            plan[e].kind = (e % 10 == 3) ? 1 : (e % 10 == 7 ? 2 : 0);
            if (js->events[e].kind == JudgeEvent::legato) plan[e].kind = (e % 10 == 7) ? 2 : 0;   // 击弦只测对 / 漏
            plan[e].offsetMs = js->events[e].kind == JudgeEvent::legato ? 0.0f : (rnd.nextFloat() - 0.5f) * 60.0f;
        }
        const auto audio = perform (*song, *js, plan, latency);

        AudioEngine engine;
        setupEngine (engine, song, js, latency);
        runEngine (engine, audio);

        int okRight = 0, nRight = 0, okWrong = 0, nWrong = 0, okSkip = 0, nSkip = 0, okLegato = 0, nLegato = 0;
        double errSum = 0.0, errSq = 0.0; int errN = 0;
        for (size_t e = 0; e < plan.size(); ++e)
        {
            const auto v = js->verdictOf ((int) e);
            const bool hit = v == Verdict::perfect || v == Verdict::good;
            const auto& ev = js->events[e];
            if (ev.kind == JudgeEvent::legato && plan[e].kind == 0) { ++nLegato; okLegato += hit; continue; }
            switch (plan[e].kind)
            {
                case 0:
                    ++nRight; okRight += hit;
                    if (hit) { const double d = js->offsetOf ((int) e) - plan[e].offsetMs; errSum += d; errSq += d * d; ++errN; }
                    else std::printf ("     漏判：第 %d 个（拍 %.2f，%d 个音，判成 %d）\n", (int) e, ev.beat, ev.numPitches, (int) v);
                    break;
                case 1: ++nWrong; okWrong += (v == Verdict::wrongNote); if (v != Verdict::wrongNote) std::printf ("     错音没抓到：第 %d 个（判成 %d）\n", (int) e, (int) v); break;
                default: ++nSkip; okSkip += (v == Verdict::miss); break;
            }
        }
        const double bias = errN > 0 ? errSum / errN : 0.0;
        const double sd = errN > 1 ? std::sqrt (juce::jmax (0.0, errSq / errN - bias * bias)) : 0.0;
        const auto label = juce::String::fromUTF8 ("延迟 ") + juce::String (latency, 0) + " ms";
        check (okRight >= nRight * 95 / 100, label + juce::String::fromUTF8 ("：正确的音判中 ") + juce::String (okRight) + "/" + juce::String (nRight));
        check (okWrong >= nWrong * 8 / 10, label + juce::String::fromUTF8 ("：弹错的音判为错音 ") + juce::String (okWrong) + "/" + juce::String (nWrong));
        check (okSkip == nSkip, label + juce::String::fromUTF8 ("：漏弹判为 Miss ") + juce::String (okSkip) + "/" + juce::String (nSkip));
        check (okLegato >= nLegato * 8 / 10, label + juce::String::fromUTF8 ("：击弦（不拨弦）判中 ") + juce::String (okLegato) + "/" + juce::String (nLegato));
        check (std::abs (bias) <= 10.0 && sd <= 10.0,
               label + juce::String::fromUTF8 ("：时间误差 平均 ") + juce::String (bias, 1) + " ms，" + juce::String::fromUTF8 ("标准差 ") + juce::String (sd, 1) + " ms");
        const auto st = js->computeStats();
        std::printf ("     统计：Perfect %d  Good %d  错音 %d  Miss %d  命中率 %.0f%%  最大连击 %d  多余起音 %d\n",
                     st.perfect, st.good, st.wrong, st.miss, st.accuracy() * 100.0f, js->bestCombo.load(), js->extraOnsets.load());
    }

    std::printf ("\n== 12. 等待模式\n");
    {
        using namespace judgetest;
        auto song = makeExerciseSong();
        auto js = std::make_shared<JudgeSession> (song, 0);
        AudioEngine engine;
        setupEngine (engine, song, js, 0.0f);
        auto& judge = engine.getJudge();
        judge.setWaitMode (true);
        judge.processPending();
        auto& tp = engine.getTabPlayer();

        PluckSynth synth;
        synth.prepare (sr);
        const auto& notes = song->tracks[0].notes;
        std::vector<float> in (block), l (block), r (block), sl (block), sr2 (block);
        int played = 0, wrongOnce = 0, wrongFlashes = 0;
        uint32_t lastSeq = 0;
        double stuckSince = -1.0, maxBeatWhileStuck = 0.0;
        bool overran = false;
        const int maxBlocks = (int) (120.0 * sr / block);
        int blocksRun = 0;
        for (; blocksRun < maxBlocks && tp.getPosition() < song->lengthBeats - 1.0e-6; ++blocksRun)
        {
            const double now = blocksRun * (double) block / sr;
            // 找当前卡住的事件：第一个还没判过的
            int gate = -1;
            for (size_t e = 0; e < js->events.size(); ++e)
                if (js->verdictOf ((int) e) == Verdict::pending) { gate = (int) e; break; }
            if (gate >= 0)
            {
                const auto& ev = js->events[(size_t) gate];
                if (tp.getPosition() > ev.beat + 1.0e-6) overran = true;
                if (tp.getPosition() >= ev.beat - 1.0e-6)
                {
                    if (stuckSince < 0.0) stuckSince = now;
                    if (now - stuckSince > 0.3 && played == gate)
                    {
                        // 第 3 个事件先弹错一次，0.3 秒后再弹对
                        const bool wrong = gate == 2 && wrongOnce == 0;
                        if (wrong) wrongOnce = 1;
                        for (int k = ev.firstNote; k < ev.firstNote + ev.numNotes; ++k)
                        {
                            PluckSynth::NoteOn on;
                            on.midi = notes[(size_t) k].midi + (wrong ? 2 : 0);
                            on.hammer = notes[(size_t) k].hammerPull;
                            on.velocity = on.hammer ? 0.6f : 0.85f;
                            on.lengthSeconds = 1.0;
                            synth.noteOn (on);
                        }
                        if (wrong) stuckSince = now; else { ++played; stuckSince = -1.0; }
                    }
                }
                maxBeatWhileStuck = juce::jmax (maxBeatWhileStuck, tp.getPosition());
            }
            std::fill (sl.begin(), sl.end(), 0.0f); std::fill (sr2.begin(), sr2.end(), 0.0f);
            synth.render (sl.data(), sr2.data(), block);
            for (int i = 0; i < block; ++i) in[(size_t) i] = 0.5f * (sl[(size_t) i] + sr2[(size_t) i]);
            engine.processOffline (in.data(), l.data(), r.data(), block);
            judge.processPending();
            if (js->lastSeq.load() != lastSeq)
            {
                lastSeq = js->lastSeq.load();
                if (js->lastVerdict.load() == (uint8_t) Verdict::wrongNote) ++wrongFlashes;
            }
        }
        const auto st = js->computeStats();
        check (! overran, juce::String::fromUTF8 ("没弹对之前谱子不会往前走"));
        check (st.perfect == (int) js->events.size(),
               juce::String::fromUTF8 ("全部音符最终判中 ") + juce::String (st.perfect) + "/" + juce::String ((int) js->events.size()));
        check (wrongFlashes >= 1 && st.wrong == 0, juce::String::fromUTF8 ("弹错时提示“错音”，但可以重弹（提示 ") + juce::String (wrongFlashes) + juce::String::fromUTF8 (" 次）"));
        check (tp.getPosition() >= song->lengthBeats - 1.0e-6,
               juce::String::fromUTF8 ("弹完整首（用时 ") + juce::String (blocksRun * (double) block / sr, 1) + juce::String::fromUTF8 (" 秒）"));
    }

    std::printf ("\n== 13. 判定：A-B 循环每遍重新计分\n");
    {
        using namespace judgetest;
        auto song = makeExerciseSong();
        auto js = std::make_shared<JudgeSession> (song, 0);
        std::vector<Plan> plan (js->events.size());
        const auto audio = perform (*song, *js, plan, 0.0);      // 全对
        AudioEngine engine;
        setupEngine (engine, song, js, 0.0f);
        engine.getTabPlayer().setLoop (0.0, 8.0, true);         // 第 1–2 小节
        // 循环 3 遍需要的音频：前 8 拍的演奏重复 3 遍
        const size_t loopLen = (size_t) (song->beatToSeconds (8.0) * sr);
        std::vector<float> in;
        for (int k = 0; k < 3; ++k) in.insert (in.end(), audio.begin(), audio.begin() + (long) loopLen);
        runEngine (engine, in);
        check (js->passes.load() >= 2 && js->lastPassAccuracy.load() > 0.95f,
               juce::String::fromUTF8 ("循环了 ") + juce::String (js->passes.load()) + juce::String::fromUTF8 (" 遍，上一遍命中率 ")
                   + juce::String (js->lastPassAccuracy.load() * 100.0f, 0) + "%");
    }

    std::printf ("\n== 13b. 循环：上一遍弹错，这一遍弹对，标记要跟着刷新\n");
    for (float latency : { 0.0f, 30.0f })
    {
        using namespace judgetest;
        auto song = makeExerciseSong();
        auto js = std::make_shared<JudgeSession> (song, 0);
        std::vector<Plan> wrong (js->events.size()), right (js->events.size());
        for (auto& p : wrong) p.kind = 1;
        const auto bad = perform (*song, *js, wrong, latency);
        const auto good = perform (*song, *js, right, latency);
        AudioEngine engine;
        setupEngine (engine, song, js, latency);
        engine.getTabPlayer().setLoop (0.0, 8.0, true);
        const size_t loopLen = (size_t) (song->beatToSeconds (8.0) * sr);
        std::vector<float> in (bad.begin(), bad.begin() + (long) loopLen);          // 第 1 遍全错
        for (int k = 0; k < 2; ++k)                                                  // 第 2、3 遍全对
            in.insert (in.end(), good.begin(), good.begin() + (long) loopLen);
        runEngine (engine, in);
        int wrongNow = 0, judgedNow = 0;
        for (size_t e = 0; e < js->events.size(); ++e)
            if (js->events[e].beat < 8.0 && js->verdictOf ((int) e) != Verdict::pending)
            {
                ++judgedNow;
                wrongNow += js->verdictOf ((int) e) != Verdict::perfect && js->verdictOf ((int) e) != Verdict::good;
            }
        const auto label = juce::String::fromUTF8 ("延迟 ") + juce::String (juce::roundToInt (latency)) + " ms";
        check (js->passes.load() == 2 && js->lastPassAccuracy.load() >= 0.85f,
               label + juce::String::fromUTF8 ("：第 2 遍（弹对）命中率 ") + juce::String (juce::roundToInt (js->lastPassAccuracy.load() * 100.0f)) + "%");
        check (judgedNow >= 6 && wrongNow <= 1,
               label + juce::String::fromUTF8 ("：第 3 遍谱面上的错误标记 ") + juce::String (wrongNow) + "/" + juce::String (judgedNow));
    }

    if (argc > 3)
    {
        std::printf ("\n== 14. 判定：真实谱子（整首，每 10 个音弹错 1 个）\n");
        using namespace judgetest;
        juce::String err;
        auto loaded = loadSongFile (juce::File (argv[3]), err);
        if (loaded != nullptr)
        {
            std::shared_ptr<const Song> song (loaded.release());
            const int t = song->defaultTrack();
            // 只取这一轨，组成新歌
            auto single = std::make_shared<Song> (*song);
            single->tracks = { song->tracks[(size_t) t] };
            std::shared_ptr<const Song> s1 = single;
            auto js = std::make_shared<JudgeSession> (s1, 0);
            std::vector<Plan> plan (js->events.size());
            for (size_t e = 0; e < plan.size(); ++e)
                if (e % 10 == 5 && js->events[e].kind == JudgeEvent::picked)
                    plan[e].kind = 1;       // 每 10 个弹错 1 个（高半音）
            const double limitBeat = s1->lengthBeats + 1.0;
            auto audio = perform (*s1, *js, plan, 0.0);
            AudioEngine engine;
            setupEngine (engine, s1, js, 0.0f);
            runEngine (engine, audio);
            const auto st = js->computeStats (0.0, limitBeat - 1.0);
            if (std::getenv ("JUDGE_DEBUG") != nullptr)
                for (size_t e = 0; e < js->events.size(); ++e)
                    if (js->verdictOf ((int) e) == Verdict::wrongNote)
                    {
                        const auto& ev = js->events[e];
                        const double gap = e + 1 < js->events.size() ? (s1->beatToSeconds (js->events[e + 1].beat) - s1->beatToSeconds (ev.beat)) * 1000.0 : 999.0;
                        const double prevGap = e > 0 ? (s1->beatToSeconds (ev.beat) - s1->beatToSeconds (js->events[e - 1].beat)) * 1000.0 : 999.0;
                        std::printf ("     wrong e=%d kind=%d n=%d midi=%d,%d,%d prevGap=%.0f gap=%.0f\n", (int) e, (int) ev.kind, ev.numPitches,
                                     ev.midi[0], ev.numPitches > 1 ? ev.midi[1] : 0, ev.numPitches > 2 ? ev.midi[2] : 0, prevGap, gap);
                    }
            int legato = 0, legatoHit = 0;
            for (size_t e = 0; e < js->events.size(); ++e)
                if (js->events[e].beat < limitBeat - 1.0 && js->events[e].kind == JudgeEvent::legato)
                {
                    ++legato;
                    const auto v = js->verdictOf ((int) e);
                    legatoHit += (v == Verdict::perfect || v == Verdict::good);
                }
            int nWrong = 0, caught = 0, rightN = 0, rightHit = 0;
            for (size_t e = 0; e < plan.size(); ++e)
            {
                const auto v = js->verdictOf ((int) e);
                if (plan[e].kind == 1) { ++nWrong; caught += v == Verdict::wrongNote; }
                else { ++rightN; rightHit += (v == Verdict::perfect || v == Verdict::good); }
            }
            check (rightHit >= rightN * 95 / 100, juce::String::fromUTF8 ("弹对的音判中 ") + juce::String (rightHit) + "/" + juce::String (rightN));
            check (caught >= nWrong * 85 / 100, juce::String::fromUTF8 ("弹错（高半音）的抓到 ") + juce::String (caught) + "/" + juce::String (nWrong));
            check (st.accuracy() >= 0.80f, juce::String::fromUTF8 ("总命中率 ") + juce::String (st.accuracy() * 100.0f, 0) + "%  (Perfect "
                   + juce::String (st.perfect) + " Good " + juce::String (st.good) + juce::String::fromUTF8 (" 错音 ") + juce::String (st.wrong)
                   + " Miss " + juce::String (st.miss) + juce::String::fromUTF8 ("，其中击勾弦 ") + juce::String (legatoHit) + "/" + juce::String (legato) + ")");
        }
    }

    std::printf ("\n== 16. 箱头 / 过载 / 失真的各个型号（同一段吉他，看音量和是否正常）\n");
    {
        struct Case { const char* id; const char* param; int count; const char* extra; };
        const Case cases[] = { { "amp", "voicing", 11, "gain=5" }, { "overdrive", "model", 6, "drive=18" }, { "distortion", "model", 3, "gain=34" } };
        for (auto& c : cases)
        {
            float minDb = 100.0f, maxDb = -100.0f;
            for (int m = 0; m < c.count; ++m)
            {
                AudioEngine engine;
                engine.prepareForOffline (sr, block);
                engine.setOutputGainDb (0.0f);
                EffectChain chain (engine);
                auto slot = chain.createBuiltin (c.id);
                auto* fx = dynamic_cast<BuiltinEffect*> (&slot->getProcessor());
                fx->setParam (c.param, (float) m);
                chain.insert (slot);
                run (engine, guitar, l, r);
                const auto st = stats (l, r, (size_t) (0.05 * sr));
                const float d = juce::Decibels::gainToDecibels (st.rms, -120.0f);
                minDb = juce::jmin (minDb, d); maxDb = juce::jmax (maxDb, d);
                check (st.finite && st.peak > 0.005f && st.peak < 4.0f,
                       juce::String (c.id) + " " + fx->getName() + "  RMS " + juce::String (d, 1) + " dB  " + juce::String::fromUTF8 ("峰值 ") + db (st.peak));
                chain.clear();
                engine.processOffline (guitar.data(), l.data(), r.data(), block * 4);
                chain.collectGarbage();
            }
            check (maxDb - minDb < 14.0f, juce::String (c.id) + juce::String::fromUTF8 (" 各型号音量差 ") + juce::String (maxDb - minDb, 1) + " dB");
        }
    }

    std::printf ("\n== 17. 经典音色预设（第 2 版新增）\n");
    {
        const bool haveLibrary = EffectChain::getToneLibrary().getChildFile ("NAM").isDirectory();
        for (auto& p : DefaultPresets::listV2)
        {
            const auto name = juce::String::fromUTF8 (p[0]);
            auto xml = juce::parseXML (juce::String::fromUTF8 (p[1]));
            if (xml == nullptr) { check (false, name + " XML"); continue; }
            if (name.startsWith (juce::String::fromUTF8 ("真实采样")) && ! haveLibrary) continue;
            AudioEngine engine;
            engine.prepareForOffline (sr, block);
            engine.setOutputGainDb (0.0f);
            EffectChain chain (engine);
            juce::StringArray problems;
            chain.loadFromXml (*xml, problems);
            for (int k = 0; k < 6; ++k) { engine.processOffline (guitar.data(), l.data(), r.data(), block); chain.collectGarbage(); }
            run (engine, guitar, l, r);
            const auto st = stats (l, r, (size_t) (0.05 * sr));
            const float d = juce::Decibels::gainToDecibels (st.rms, -120.0f);
            check (problems.isEmpty() && st.finite && d > -36.0f && d < -6.0f && st.peak < 1.0f,
                   name + "  RMS " + juce::String (d, 1) + " dB  " + juce::String::fromUTF8 ("峰值 ") + db (st.peak)
                       + (problems.isEmpty() ? juce::String() : "  " + problems.joinIntoString ("; ")));
            chain.clear();
            engine.processOffline (guitar.data(), l.data(), r.data(), block * 4);
            chain.collectGarbage();
        }
        if (! haveLibrary) std::printf ("     （测试程序旁边没有音色库，跳过“真实采样”预设）\n");
    }

    std::printf ("\n== 17b. 先锋 / 后摇 / 迷幻 / 后朋克 / 怪异 / 贝斯预设（第 3 版新增）\n");
    {
        juce::StringArray names;
        auto checkList = [&] (auto& list)
        {
        for (auto& p : list)
        {
            const auto name = juce::String::fromUTF8 (p[0]);
            check (! names.contains (name) && ! name.containsAnyOf ("/\\:*?\"<>|"), name + juce::String::fromUTF8 (" 文件名可用"));
            names.add (name);
            auto xml = juce::parseXML (juce::String::fromUTF8 (p[1]));
            if (xml == nullptr) { check (false, name + " XML"); continue; }

            // 每个参数名都要是这个效果器真有的（写错了会被悄悄忽略）
            juce::StringArray bad;
            for (auto* slot : xml->getChildWithTagNameIterator ("Slot"))
            {
                auto fx = createBuiltinEffect (slot->getStringAttribute ("id"));
                if (fx == nullptr) { bad.add ("?" + slot->getStringAttribute ("id")); continue; }
                juce::StringArray ids;
                for (auto* prm : fx->getParameters())
                    if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm)) ids.add (r->getParameterID());
                if (auto* ps = slot->getChildByName ("Params"))
                    for (int a = 0; a < ps->getNumAttributes(); ++a)
                        if (! ids.contains (ps->getAttributeName (a)))
                            bad.add (slot->getStringAttribute ("id") + "." + ps->getAttributeName (a));
            }

            AudioEngine engine;
            engine.prepareForOffline (sr, block);
            engine.setOutputGainDb (0.0f);
            EffectChain chain (engine);
            juce::StringArray problems;
            chain.loadFromXml (*xml, problems);
            for (int k = 0; k < 6; ++k) { engine.processOffline (guitar.data(), l.data(), r.data(), block); chain.collectGarbage(); }
            run (engine, guitar, l, r);
            const auto st = stats (l, r, (size_t) (0.05 * sr));
            const float d = juce::Decibels::gainToDecibels (st.rms, -120.0f);
            check (problems.isEmpty() && bad.isEmpty() && st.finite && d > -36.0f && d < -6.0f && st.peak < 1.0f,
                   name + "  RMS " + juce::String (d, 1) + " dB  " + juce::String::fromUTF8 ("峰值 ") + db (st.peak)
                       + (bad.isEmpty() ? juce::String() : juce::String::fromUTF8 ("  参数名不对：") + bad.joinIntoString (", "))
                       + (problems.isEmpty() ? juce::String() : "  " + problems.joinIntoString ("; ")));
            chain.clear();
            engine.processOffline (guitar.data(), l.data(), r.data(), block * 4);
            chain.collectGarbage();
        }
        };
        checkList (DefaultPresets::listV3);
        std::printf ("  -- Gervana 预设\n");
        checkList (DefaultPresets::listGervana);

        // 吉他变贝斯：弹 A 弦空弦（110 Hz），出来的基音应该在 55 Hz
        for (int idx : { 18, 19 })
        {
            const auto name = juce::String::fromUTF8 (DefaultPresets::listV3[idx][0]);
            auto xml = juce::parseXML (juce::String::fromUTF8 (DefaultPresets::listV3[idx][1]));
            AudioEngine engine;
            engine.prepareForOffline (sr, block);
            engine.setOutputGainDb (0.0f);
            EffectChain chain (engine);
            juce::StringArray problems;
            chain.loadFromXml (*xml, problems);
            const int n = (int) (1.5 * sr);
            std::vector<float> in ((size_t) n), ol ((size_t) n), orr ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double t = i / sr;
                float v = 0.0f;
                for (int h = 1; h <= 8; ++h) v += (float) (std::sin (2.0 * juce::MathConstants<double>::pi * 110.0 * h * t) / h);
                in[(size_t) i] = 0.12f * v * (float) std::exp (-t * 1.2);
            }
            for (int k = 0; k < 6; ++k) { std::vector<float> z ((size_t) block), a ((size_t) block), b ((size_t) block); engine.processOffline (z.data(), a.data(), b.data(), block); chain.collectGarbage(); }
            for (int pos = 0; pos + block <= n; pos += block)
                engine.processOffline (in.data() + pos, ol.data() + pos, orr.data() + pos, block);
            // 0.4–1.2 秒这段：55 Hz（低八度基音）要是 30–250 Hz 里最强的，且比 110 Hz 强
            const int s0 = (int) (0.4 * sr), s1 = (int) (1.2 * sr);
            auto energyAt = [&] (double f)
            {
                double re = 0.0, im = 0.0;
                for (int i = s0; i < s1; ++i)
                {
                    const double ph = 2.0 * juce::MathConstants<double>::pi * f * i / sr;
                    re += ol[(size_t) i] * std::cos (ph); im += ol[(size_t) i] * std::sin (ph);
                }
                return re * re + im * im;
            };
            double bestF = 0.0, bestE = 0.0;
            for (double f = 30.0; f <= 250.0; f += 1.0)
                if (const double e = energyAt (f); e > bestE) { bestE = e; bestF = f; }
            const double rel = 10.0 * std::log10 (energyAt (55.0) / juce::jmax (1e-12, energyAt (110.0)));
            check (std::abs (bestF - 55.0) < 3.0, name + juce::String::fromUTF8 ("：弹 110 Hz，最强在 ") + juce::String (bestF, 0)
                       + juce::String::fromUTF8 (" Hz，55 Hz 比 110 Hz ") + juce::String (rel, 1) + " dB");
        }
    }

    std::printf ("\n== 17c. Gervana 效果器：每个拨档组合都要正常出声、音量别差太多\n");
    {
        for (auto& info : getBuiltinEffectList())
        {
            if (! Gervana::isGervana (info.id)) continue;
            auto probe = createBuiltinEffect (info.id);
            std::vector<juce::AudioParameterChoice*> chs;
            std::vector<juce::AudioParameterBool*> bools;
            for (auto* prm : probe->getParameters())
            {
                if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (prm)) chs.push_back (c);
                else if (auto* bp = dynamic_cast<juce::AudioParameterBool*> (prm)) bools.push_back (bp);
            }
            // 所有拨档（选择 + 开关）的组合
            int combos = 1;
            for (auto* c : chs) combos *= c->choices.size();
            combos <<= (int) bools.size();
            float minDb = 100.0f, maxDb = -100.0f;
            bool allOk = true;
            juce::String worst;
            for (int k = 0; k < combos; ++k)
            {
                AudioEngine engine;
                engine.prepareForOffline (sr, block);
                engine.setOutputGainDb (0.0f);
                EffectChain chain (engine);
                auto slot = chain.createBuiltin (info.id);
                auto* fx = dynamic_cast<BuiltinEffect*> (&slot->getProcessor());
                int rest = k;
                juce::String desc;
                for (auto* c : chs)
                {
                    const int v = rest % c->choices.size(); rest /= c->choices.size();
                    fx->setParam (c->getParameterID(), (float) v);
                    desc << c->choices[v] << "/";
                }
                for (auto* bp : bools)
                {
                    const int v = rest % 2; rest /= 2;
                    fx->setParam (bp->getParameterID(), (float) v);
                    desc << bp->getName (16) << (v ? "+" : "-") << "/";
                }
                chain.insert (slot);
                run (engine, guitar, l, r);
                const auto st = stats (l, r, (size_t) (0.05 * sr));
                const float d = juce::Decibels::gainToDecibels (st.rms, -120.0f);
                if (! (st.finite && st.peak < 1.5f && d > -40.0f)) { allOk = false; worst = desc + " RMS " + juce::String (d, 1); }
                if (d < minDb) minDb = d;
                if (d > maxDb) maxDb = d;
                chain.clear();
                engine.processOffline (guitar.data(), l.data(), r.data(), block * 4);
                chain.collectGarbage();
            }
            check (allOk && maxDb - minDb < 16.0f,
                   info.name + "  " + juce::String (combos) + juce::String::fromUTF8 (" 种组合，RMS ")
                       + juce::String (minDb, 1) + " ~ " + juce::String (maxDb, 1) + " dB" + (allOk ? juce::String() : "  " + worst));
        }

        // 预设分类：内置预设都要落在固定分类里，Gervana 排第一
        juce::StringArray stray;
        auto cat = [&] (auto& list) { for (auto& p : list) if (PresetBar::categoryOf (T (p[0])) == "user") stray.add (T (p[0])); };
        cat (DefaultPresets::list); cat (DefaultPresets::listV2); cat (DefaultPresets::listV3); cat (DefaultPresets::listGervana);
        check (stray.isEmpty() && PresetBar::categoryOrder()[0] == "fender"
                   && PresetBar::categoryOf (T ("真实采样 · Fender Twin 清音")) == "fender"
                   && PresetBar::categoryOf (T ("我自己的音色")) == "user",
               juce::String::fromUTF8 ("预设分类（Fender 在最前）") + (stray.isEmpty() ? juce::String() : "  " + stray.joinIntoString (", ")));
    }

    std::printf ("\n== 18. 录音和回放（回放时谱子要对上，判定结果应该一样）\n");
    {
        using namespace judgetest;
        auto song = makeExerciseSong();
        auto js = std::make_shared<JudgeSession> (song, 0);
        std::vector<Plan> plan (js->events.size());
        juce::Random rnd (11);
        for (size_t e = 0; e < plan.size(); ++e)
        {
            plan[e].kind = (e % 10 == 3) ? 1 : (e % 10 == 7 ? 2 : 0);
            if (js->events[e].kind == JudgeEvent::legato) plan[e].kind = (e % 10 == 7) ? 2 : 0;
            plan[e].offsetMs = js->events[e].kind == JudgeEvent::legato ? 0.0f : (rnd.nextFloat() - 0.5f) * 60.0f;
        }
        const auto perf = perform (*song, *js, plan, 0.0);

        // 第一次：边弹边录。先录 0.5 秒空白，再开始放谱子
        AudioEngine engine;
        setupEngine (engine, song, js, 0.0f);
        auto& tp = engine.getTabPlayer();
        tp.setPlaying (false);
        auto& rec = engine.getRecorder();
        Recorder::Info info;
        info.songTitle = "Judge test";
        juce::String err;
        const bool started = rec.startRecording (info, err);
        const int lead = 188 * block;
        std::vector<float> silence ((size_t) lead, 0.0f), l ((size_t) lead), r ((size_t) lead);
        engine.processOffline (silence.data(), l.data(), r.data(), lead);
        engine.getJudge().processPending();
        tp.setPlaying (true);
        runEngine (engine, perf);
        const auto wav = rec.stopRecording();
        const auto saved = Recorder::Info::loadFor (wav);
        std::vector<int> firstRun (js->events.size());
        for (size_t e = 0; e < js->events.size(); ++e) firstRun[e] = (int) js->verdictOf ((int) e);

        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (saved.dryFile));
        float maxErr = 0.0f;
        if (reader != nullptr)
        {
            juce::AudioBuffer<float> back (1, (int) reader->lengthInSamples);
            reader->read (&back, 0, (int) reader->lengthInSamples, 0, true, false);
            for (int i = 0; i < juce::jmin ((int) perf.size(), back.getNumSamples() - lead); ++i)
                maxErr = juce::jmax (maxErr, std::abs (back.getSample (0, lead + i) - juce::jlimit (-1.0f, 1.0f, perf[(size_t) i])));   // 声卡输入本来就在 ±1 以内
        }
        check (started && saved.mixFile.existsAsFile() && saved.dryFile.existsAsFile(),
               juce::String::fromUTF8 ("录音两个文件都在：") + saved.mixFile.getFileName() + " + " + saved.dryFile.getFileName());
        check (reader != nullptr && reader->lengthInSamples == (juce::int64) (lead + perf.size()),
               juce::String::fromUTF8 ("录成 WAV：") + saved.dryFile.getFileName() + "  " + juce::String (reader != nullptr ? reader->lengthInSamples : 0)
                   + juce::String::fromUTF8 (" 个采样") + (err.isNotEmpty() ? "  " + err : juce::String()));
        float peak = 0.0f;
        for (auto x : perf) peak = juce::jmax (peak, std::abs (x));
        check (maxErr < 1.0e-5f, juce::String::fromUTF8 ("录下的就是输入的干信号（最大误差 ") + juce::String (maxErr, 7) + juce::String::fromUTF8 ("，合成演奏峰值 ") + juce::String (peak, 2) + ")");

        float mixPeak = 0.0f;
        bool mixOk = false;
        if (auto mixReader = std::unique_ptr<juce::AudioFormatReader> (fm.createReaderFor (saved.mixFile)))
        {
            mixOk = mixReader->numChannels == 2
                 && mixReader->lengthInSamples == (juce::int64) (lead + perf.size());
            if (mixOk)
            {
                juce::AudioBuffer<float> mix (2, (int) mixReader->lengthInSamples);
                mixReader->read (&mix, 0, (int) mixReader->lengthInSamples, 0, true, true);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < mix.getNumSamples(); ++i)
                        mixPeak = juce::jmax (mixPeak, std::abs (mix.getSample (ch, i)));
            }
        }
        check (mixOk && mixPeak > 0.001f, juce::String::fromUTF8 ("混音是立体声、长度对齐、有声音：峰值 ") + juce::String (mixPeak, 2));
        check (saved.synced && saved.syncSample == lead && std::abs (saved.syncBeat) < 1.0e-9,
               juce::String::fromUTF8 ("记下了谱子开始的位置：第 ") + juce::String (saved.syncSample) + juce::String::fromUTF8 (" 个采样，第 ") + juce::String (saved.syncBeat, 2) + juce::String::fromUTF8 (" 拍"));

        // 第二次：回放（输入全是静音，录音代替吉他），谱子自己在同一个采样点开始
        auto js2 = std::make_shared<JudgeSession> (song, 0);
        AudioEngine engine2;
        setupEngine (engine2, song, js2, 0.0f);
        engine2.getTabPlayer().setPlaying (false);
        engine2.getTabPlayer().seek (saved.syncBeat);
        const bool replayStarted = engine2.getRecorder().startPlayback (wav, err);
        std::vector<float> quiet ((size_t) (lead + perf.size()), 0.0f);
        runEngine (engine2, quiet);
        int same = 0;
        for (size_t e = 0; e < js2->events.size(); ++e) same += (int) js2->verdictOf ((int) e) == firstRun[e];
        check (replayStarted && same >= (int) js2->events.size() - 1,
               juce::String::fromUTF8 ("回放判定和现场一致：") + juce::String (same) + "/" + juce::String ((int) js2->events.size()));
        check (! engine2.getRecorder().isPlayingBack(), juce::String::fromUTF8 ("回放到结尾自动停"));
        saved.mixFile.getParentDirectory().deleteRecursively();
    }

    std::printf ("\n== 20. Latency, pitch spurs, smoothing, XML stress\n");
    {
        // Impulse latency: the output peak must land at the reported latency.
        // Fuzz is excluded: its asymmetric clipper + IIR filters ring, so the
        // response peak is not a meaningful latency measurement.
        for (const char* id : { "overdrive", "distortion" })
        {
            AudioEngine e;
            e.prepareForOffline (sr, block);
            e.setInputGainDb (0.0f);
            e.setOutputGainDb (0.0f);
            e.setMonitoring (true);
            EffectChain c (e);
            auto slot = c.createBuiltin (id);
            c.insert (slot);

            std::vector<float> in ((size_t) block * 8, 0.0f), l, r;
            in[0] = 0.5f;
            l.assign (in.size(), 0.0f);
            r.assign (in.size(), 0.0f);
            e.processOffline (in.data(), l.data(), r.data(), (int) in.size());

            int peakIndex = 0;
            for (int i = 1; i < (int) l.size(); ++i)
                if (std::abs (l[(size_t) i]) > std::abs (l[(size_t) peakIndex]))
                    peakIndex = i;

            const int reported = slot->getLatencySamples();
            check (std::abs (peakIndex - reported) <= 1,
                   T (id) + T (" impulse latency: peak ") + juce::String (peakIndex)
                       + T (", reported ") + juce::String (reported));
        }

        // Pitch shift: spurious energy outside the shifted fundamental.
        {
            AudioEngine e;
            e.prepareForOffline (sr, block);
            e.setInputGainDb (0.0f);
            e.setOutputGainDb (0.0f);
            e.setMonitoring (true);
            EffectChain c (e);
            auto slot = c.createBuiltin ("pitch");
            c.insert (slot);
            if (auto* fx = dynamic_cast<BuiltinEffect*> (&slot->getProcessor()))
            {
                fx->setParam ("mix", 1.0f);
                fx->setParam ("semis", 7.0f);
                fx->setParam ("cents", 0.0f);
            }

            const int n = (int) sr;
            std::vector<float> in ((size_t) n), l ((size_t) n), r ((size_t) n);
            for (int i = 0; i < n; ++i)
                in[(size_t) i] = 0.2f * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * i / sr);

            e.processOffline (in.data(), l.data(), r.data(), n);

            const int N = 4096, start = (int) (0.4 * sr);
            std::vector<float> buf ((size_t) N * 2, 0.0f);
            for (int i = 0; i < N; ++i)
                buf[(size_t) i] = l[(size_t) (start + i)] * (0.5f - 0.5f * (float) std::cos (juce::MathConstants<double>::twoPi * i / (N - 1)));

            juce::dsp::FFT fft (12);
            fft.performFrequencyOnlyForwardTransform (buf.data());

            const int fundamental = (int) std::round (1498.0 * N / sr);
            double fundEnergy = 0.0, spurEnergy = 0.0;
            for (int k = 1; k < N / 2; ++k)
            {
                const double m = (double) buf[(size_t) k] * (double) buf[(size_t) k];
                if (std::abs (k - fundamental) <= 3) fundEnergy += m;
                else spurEnergy += m;
            }
            const double spurDb = 10.0 * std::log10 ((spurEnergy + 1.0e-30) / (fundEnergy + 1.0e-30));
            check (spurDb < -45.0, T ("pitch +7 semitones spurs: ") + juce::String (spurDb, 1) + " dB");
        }

        // Parameter steps must not produce zipper clicks.
        {
            auto zipper = [] (const char* id, const char* param, float from, float to)
            {
                AudioEngine e;
                e.prepareForOffline (sr, block);
                e.setInputGainDb (0.0f);
                e.setOutputGainDb (0.0f);
                EffectChain c (e);
                auto slot = c.createBuiltin (id);
                c.insert (slot);
                if (auto* fx = dynamic_cast<BuiltinEffect*> (&slot->getProcessor()))
                    fx->setParam (param, from);

                const int n = (int) (0.6 * sr);
                std::vector<float> in ((size_t) n), l1 ((size_t) n), r1 ((size_t) n),
                                   l2 ((size_t) n), r2 ((size_t) n);
                for (int i = 0; i < n; ++i)
                    in[(size_t) i] = 0.2f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * i / sr);

                e.processOffline (in.data(), l1.data(), r1.data(), n);
                if (auto* fx = dynamic_cast<BuiltinEffect*> (&slot->getProcessor()))
                    fx->setParam (param, to);
                e.processOffline (in.data(), l2.data(), r2.data(), n);

                return stats (l2, r2, 0).maxStep;
            };

            const float mixStep = zipper ("delay", "mix", 0.1f, 0.9f);
            const float eqStep = zipper ("eq", "midFreq", 400.0f, 2000.0f);
            check (mixStep < 0.02f && eqStep < 0.02f,
                   T ("parameter steps: delay mix ") + juce::String (mixStep, 4)
                       + T (", EQ freq ") + juce::String (eqStep, 4));
        }

        // Removing the reverb must not cut its tail immediately.
        {
            AudioEngine e;
            e.prepareForOffline (sr, block);
            e.setInputGainDb (0.0f);
            e.setOutputGainDb (0.0f);
            EffectChain c (e);
            auto slot = c.createBuiltin ("reverb");
            c.insert (slot);
            if (auto* fx = dynamic_cast<BuiltinEffect*> (&slot->getProcessor()))
            {
                fx->setParam ("size", 0.9f);
                fx->setParam ("mix", 0.6f);
            }

            auto guitar = makeGuitar (0.6);
            std::vector<float> l (guitar.size()), r (guitar.size());
            e.processOffline (guitar.data(), l.data(), r.data(), (int) guitar.size());

            c.remove (0);
            std::vector<float> silence ((size_t) block, 0.0f), tailL ((size_t) block), tailR ((size_t) block);
            e.processOffline (silence.data(), tailL.data(), tailR.data(), block);
            const float tailPeak = stats (tailL, tailR, 0).peak;
            check (tailPeak > 1.0e-4f, T ("reverb tail survives a chain edit: ") + db (tailPeak));
        }

        // XML export while the audio thread is running.
        {
            AudioEngine e;
            e.prepareForOffline (sr, block);
            EffectChain c (e);
            c.insert (c.createBuiltin ("gate"));
            c.insert (c.createBuiltin ("reverb"));
            c.insert (c.createBuiltin ("eq"));

            std::atomic<bool> running { true };
            std::thread audio ([&]
            {
                std::vector<float> in ((size_t) block, 0.0f), l ((size_t) block), r ((size_t) block);
                while (running.load())
                    e.processOffline (in.data(), l.data(), r.data(), block);
            });

            int ok = 0;
            for (int i = 0; i < 200; ++i)
                if (auto xml = c.toXml(); xml != nullptr && xml->hasTagName ("Chain"))
                    ++ok;

            running.store (false);
            audio.join();
            check (ok == 200, T ("XML export during audio processing: ") + juce::String (ok) + "/200");
        }

        // VST3 smoke: load the built plug-in, prepare, process, save state.
        {
            const auto vst3 = juce::File::getCurrentWorkingDirectory()
                                  .getChildFile ("build/Tone_TabPlugin_artefacts/Release/VST3/Tone_Tab.vst3");
            if (! vst3.exists())
            {
                std::printf ("  [skip] VST3 not built\n");
            }
            else
            {
                juce::AudioPluginFormatManager fm;
                juce::addHeadlessDefaultFormatsToManager (fm);
                juce::OwnedArray<juce::PluginDescription> descriptions;

                for (int i = 0; i < fm.getNumFormats(); ++i)
                    fm.getFormat (i)->findAllTypesForFile (descriptions, vst3.getFullPathName());

                if (descriptions.isEmpty())
                {
                    std::printf ("  [skip] VST3 could not be scanned (Smart App Control?)\n");
                }
                else
                {
                    juce::String error;
                    auto instance = fm.createPluginInstance (*descriptions[0], sr, block, error);
                    if (instance == nullptr)
                    {
                        check (false, T ("VST3 load failed: ") + error);
                    }
                    else
                    {
                        instance->enableAllBuses();
                        instance->prepareToPlay (sr, block);

                        juce::AudioBuffer<float> buffer (2, block);
                        juce::MidiBuffer midi;
                        bool finite = true;
                        for (int b = 0; b < 200; ++b)
                        {
                            buffer.clear();
                            instance->processBlock (buffer, midi);
                            for (int ch = 0; ch < 2; ++ch)
                                for (int i = 0; i < block; ++i)
                                    if (! std::isfinite (buffer.getSample (ch, i)))
                                        finite = false;
                        }

                        juce::MemoryBlock state;
                        instance->getStateInformation (state);
                        check (finite && state.getSize() > 0,
                               T ("VST3 smoke: load, process, state ") + juce::String (state.getSize()) + T (" bytes"));
                        instance->releaseResources();
                    }
                }
            }
        }
    }

    std::printf ("\n== 19. 速度阶梯 / 练习记录\n");
    {
        SpeedTrainer t;
        t.enabled = true; t.start = 0.6f; t.step = 0.1f; t.target = 0.8f; t.passesPerStep = 2; t.needAccuracy = 0.9f;
        t.restart();
        const bool a = ! t.onPass (0.95f);            // 第 1 遍过
        const bool b = t.onPass (0.92f);              // 第 2 遍过 -> 70%
        const bool c = ! t.onPass (0.80f);            // 没达标，重新数
        const bool d = ! t.onPass (0.95f) && t.onPass (0.95f);   // 再过两遍 -> 80%
        const bool e = t.finished() && ! t.onPass (1.0f);
        check (a && b && c && d && e && std::abs (t.current - 0.8f) < 1.0e-4f,
               juce::String::fromUTF8 ("命中率够才提速、不够重新数、到目标停：") + juce::String (juce::roundToInt (t.current * 100.0f)) + "%");
        SpeedTrainer t2; t2.enabled = true; t2.start = 0.5f; t2.step = 0.25f; t2.target = 1.0f; t2.passesPerStep = 1; t2.restart();
        t2.onPass (-1.0f); t2.onPass (-1.0f);
        check (std::abs (t2.current - 1.0f) < 1.0e-4f, juce::String::fromUTF8 ("没开判定时只数遍数"));

        const auto f = juce::File::createTempFile ("json");
        {
            PracticeLog pl (f);
            pl.addTime ("C:/tabs/a.gp5", "Song A", 90.0);
            pl.addTime ("C:/tabs/a.gp5", "Song A", 30.0);
            pl.addTime ("C:/tabs/b.gp5", "Song B", 60.0);
            pl.addPass ("C:/tabs/a.gp5", "Song A", 0.8f, 1.0f);
            pl.addPass ("C:/tabs/a.gp5", "Song A", 0.95f, 0.8f);   // 慢速的不算“原速最好”
            pl.save();
        }
        PracticeLog back (f);
        const auto& songs = back.getSongs();
        const bool ok = songs.size() == 2 && std::abs (songs[0].seconds - 120.0) < 1.0 && songs[0].sessions == 1
                        && songs[0].passes.size() == 2 && std::abs (songs[0].best - 0.8f) < 1.0e-3f
                        && std::abs (back.secondsOnDay (juce::Time::getCurrentTime()) - 180.0) < 1.0 && back.streakDays() == 1;
        check (ok, juce::String::fromUTF8 ("练习记录存盘再读回：今天 ") + juce::String (back.secondsOnDay (juce::Time::getCurrentTime()), 0)
                   + juce::String::fromUTF8 (" 秒，连续 ") + juce::String (back.streakDays()) + juce::String::fromUTF8 (" 天"));
        f.deleteFile();
    }

    if (argc > 3)
    {
        std::printf ("\n== 15. 全部音轨合奏 + 混音\n");
        juce::String err;
        auto loaded = loadSongFile (juce::File (argv[3]), err);
        if (loaded != nullptr)
        {
            std::shared_ptr<const Song> song (loaded.release());
            auto rms = [&] (int track, bool demo, std::function<void (TabPlayer&)> mixFn)
            {
                AudioEngine engine;
                engine.prepareForOffline (sr, block);
                engine.setOutputGainDb (-60.0f);
                auto& tp = engine.getTabPlayer();
                tp.setSong (song);
                tp.setTrack (track);
                tp.setCountIn (false);
                tp.setDemoOn (demo);
                tp.setBackingVolumeDb (0.0f);
                if (mixFn) mixFn (tp);
                tp.seek (song->bars[(size_t) juce::jmin ((int) song->bars.size() - 1, 20)].start);
                tp.setPlaying (true);
                std::vector<float> in ((size_t) (sr * 4.0), 0.0f), l (in.size()), r (in.size());
                engine.processOffline (in.data(), l.data(), r.data(), (int) in.size());
                double sum = 0.0;
                for (size_t i = (size_t) (sr * 0.5); i < l.size(); ++i) sum += (double) l[i] * l[i] + (double) r[i] * r[i];
                return juce::Decibels::gainToDecibels ((float) std::sqrt (sum / (2.0 * (l.size() - sr * 0.5))), -120.0f);
            };
            const float all = rms (-1, false, nullptr);
            const float allMuted = rms (-1, false, [&] (TabPlayer& tp) { for (size_t t = 0; t < song->tracks.size(); ++t) tp.setTrackGain ((int) t, 0.0f); });
            const int dt = song->defaultTrack();
            const float oneSolo = rms (-1, false, [&] (TabPlayer& tp) { for (size_t t = 0; t < song->tracks.size(); ++t) tp.setTrackGain ((int) t, (int) t == dt ? 1.0f : 0.0f); });
            check (all > -50.0f, juce::String::fromUTF8 ("合奏模式（示范音关着）所有音轨都在响：") + juce::String (all, 1) + " dB");
            check (allMuted < -90.0f, juce::String::fromUTF8 ("全部静音后没有声音：") + juce::String (allMuted, 1) + " dB");
            check (oneSolo > -60.0f && oneSolo < all, juce::String::fromUTF8 ("只留一条音轨时声音变小：") + juce::String (oneSolo, 1) + " dB");
            check (JudgeSession (song, -1).events.empty(), juce::String::fromUTF8 ("合奏模式不判定"));
        }
    }

    std::printf ("\n%s：%d 项失败\n", failures == 0 ? "全部通过" : "有失败", failures);
    return failures == 0 ? 0 : 1;
}

#ifndef TONE_TESTS_EMBEDDED
int main (int argc, char* argv[])
{
    return runOfflineTests (argc, argv);
}
#endif
