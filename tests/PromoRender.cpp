// 宣传视频用：离线把软件界面渲染成逐帧图片（30 fps）。只给测试程序用，不进正式程序。
#include <juce_audio_formats/juce_audio_formats.h>
#include "AudioEngine.h"
#include "Widgets.h"
#include "fx/BuiltinEffects.h"
#include "fx/DefaultPresets.h"
#include "drums/DrumMachine.h"
#include "ui/PedalBoard.h"
#include "ui/PedalEditor.h"
#include "ui/PracticePanel.h"
#include "ui/TabHighway.h"
#include "ui/TabSearch.h"
#include "ui/StatsView.h"
#include "ui/DrumPanel.h"
#include "song/Song.h"
#include "practice/PluckSynth.h"
#include "practice/Judge.h"
#include "practice/PracticeLog.h"
#include <cstdio>

namespace promo
{
    constexpr double sr = 48000.0;
    constexpr int block = 160;            // 1600 = 1/30 秒 = 10 块
    constexpr int fps = 30;
    constexpr int samplesPerFrame = 1600;

    double clockNow = 0.0;

    void writeJpg (juce::Component& c, const juce::File& f, float scale, juce::Rectangle<int> area = {})
    {
        if (area.isEmpty()) area = c.getLocalBounds();
        auto img = c.createComponentSnapshot (area, true, scale);
        juce::JPEGImageFormat jpg;
        jpg.setQuality (0.92f);
        f.deleteFile();
        if (auto os = f.createOutputStream())
            jpg.writeImageToStream (img, *os);
    }

    void writePng (juce::Component& c, const juce::File& f, float scale)
    {
        auto img = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
        juce::PNGImageFormat png;
        f.deleteFile();
        if (auto os = f.createOutputStream())
            png.writeImageToStream (img, *os);
    }

    juce::File frameFile (const juce::File& dir, int i) { return dir.getChildFile (juce::String (i).paddedLeft ('0', 4) + ".jpg"); }

    juce::File sceneDir (const juce::File& out, const char* name)
    {
        auto d = out.getChildFile (name);
        d.deleteRecursively();
        d.createDirectory();
        return d;
    }

    // ---- 合成“演奏” ---------------------------------------------------------
    struct Hit { int64_t at; PluckSynth::NoteOn on; };

    std::vector<Hit> planHits (const Song& song, const JudgeSession& js, int track, double fromBeat, double toBeat, int wrongEvery, int skipEvery)
    {
        std::vector<Hit> hits;
        juce::Random rnd (9);
        const auto& notes = song.tracks[(size_t) track].notes;
        const double t0 = song.beatToSeconds (fromBeat);
        for (size_t e = 0; e < js.events.size(); ++e)
        {
            const auto& ev = js.events[e];
            if (ev.beat < fromBeat || ev.beat > toBeat) continue;
            if (skipEvery > 0 && (int) e % skipEvery == skipEvery - 1) continue;
            const bool wrong = wrongEvery > 0 && (int) e % wrongEvery == wrongEvery / 2 && ev.kind == JudgeEvent::picked;
            const double off = ev.kind == JudgeEvent::legato ? 0.0 : (rnd.nextFloat() - 0.5f) * 0.05;
            const double t = song.beatToSeconds (ev.beat) - t0 + off;
            for (int k = ev.firstNote; k < ev.firstNote + ev.numNotes; ++k)
            {
                const auto& n = notes[(size_t) k];
                PluckSynth::NoteOn on;
                on.midi = n.midi + (wrong ? 1 : 0);
                on.velocity = n.hammerPull ? 0.55f : 0.85f;
                on.hammer = n.hammerPull;
                on.lengthSeconds = juce::jmax (0.1, song.beatToSeconds (n.start + n.length) - song.beatToSeconds (n.start));
                hits.push_back ({ (int64_t) (juce::jmax (0.0, t) * sr), on });
            }
        }
        std::sort (hits.begin(), hits.end(), [] (const Hit& a, const Hit& b) { return a.at < b.at; });
        return hits;
    }

    // 练习曲（和测试里的一样，换成 110 BPM）
    std::shared_ptr<const Song> exerciseSong()
    {
        RawScore raw;
        raw.title = "Riff Practice";
        raw.initialTempo = 110.0;
        RawTrack t;
        t.name = "Guitar";
        t.tuning = { 64, 59, 55, 50, 45, 40 };
        auto note = [] (int s, int f, bool h = false) { RawNote n; n.string = s; n.fret = f; n.hammerPull = h; return n; };
        auto beat = [] (double d, std::vector<RawNote> ns) { RawBeat b; b.duration = d; b.notes = std::move (ns); return b; };
        auto addBar = [&] (std::vector<RawBeat> beats)
        {
            raw.masterBars.push_back ({});
            RawBar bar; bar.voices.push_back ({ std::move (beats) });
            t.bars.push_back (std::move (bar));
        };
        for (int rep = 0; rep < 3; ++rep)
        {
            addBar ({ beat (1.0, { note (5, 0), note (4, 2), note (3, 2) }), beat (0.5, { note (5, 0) }), beat (0.5, { note (5, 0) }),
                      beat (1.0, { note (5, 3), note (4, 5), note (3, 5) }), beat (0.5, { note (5, 5) }), beat (0.5, { note (4, 5) }) });
            const int e[8][2] = { {4,5},{4,7},{3,4},{3,5},{3,7},{2,5},{2,7},{1,5} };
            std::vector<RawBeat> beats;
            for (int i = 0; i < 8; ++i) beats.push_back (beat (0.5, { note (e[i][0], e[i][1], i % 2 == 1 && i > 3) }));
            addBar (beats);
        }
        raw.tracks.push_back (t);
        return std::shared_ptr<const Song> (buildSong (raw).release());
    }

    // 宣传片用的原创练习曲：140 BPM 朋克，前奏 / 主歌 / 副歌 / Solo，吉他 + 贝斯 + 鼓
    std::shared_ptr<const Song> promoSong()
    {
        RawScore raw;
        raw.title = "Punk Practice No.1";
        raw.artist = "Lianqin";
        raw.initialTempo = 140.0;
        RawTrack gtr, bass, drums;
        gtr.name = "Lead Guitar";   gtr.tuning = { 64, 59, 55, 50, 45, 40 };
        bass.name = "Bass";         bass.tuning = { 43, 38, 33, 28 }; bass.program = 34;
        drums.name = "Drums";       drums.percussion = true; drums.tuning = { 0, 0, 0, 0, 0, 0 };
        auto n = [] (int s, int f) { RawNote x; x.string = s; x.fret = f; return x; };
        auto pm = [] (RawNote x) { x.palmMute = true; return x; };
        auto beat = [] (double d, std::vector<RawNote> ns) { RawBeat b; b.duration = d; b.notes = std::move (ns); return b; };
        auto power = [&] (int rootString, int fret, bool mute)
        {
            std::vector<RawNote> v { n (rootString, fret), n (rootString - 1, fret + 2) };
            if (mute) for (auto& x : v) x = pm (x);
            return v;
        };
        // 根音（6 弦 / 5 弦）的品位：E5 G5 A5 C5
        const int prog[4][2] = { {5, 0}, {5, 3}, {5, 5}, {4, 3} };
        auto addBar = [&] (const juce::String& section, std::vector<RawBeat> g, std::vector<RawBeat> b, std::vector<RawBeat> d)
        {
            RawMasterBar mb; mb.section = section;
            raw.masterBars.push_back (mb);
            RawBar gb; gb.voices.push_back ({ std::move (g) }); gtr.bars.push_back (std::move (gb));
            RawBar bb; bb.voices.push_back ({ std::move (b) }); bass.bars.push_back (std::move (bb));
            RawBar db; db.voices.push_back ({ std::move (d) }); drums.bars.push_back (std::move (db));
        };
        auto drumBar = [&] (bool chorus)
        {
            std::vector<RawBeat> d;
            for (int i = 0; i < 8; ++i)
            {
                RawNote hat; hat.string = 0; hat.fret = chorus ? 49 : 42;
                RawNote kick; kick.string = 1; kick.fret = 36;
                RawNote snare; snare.string = 2; snare.fret = 38;
                std::vector<RawNote> v { hat };
                if (i % 4 == 0) v.push_back (kick);
                if (i % 4 == 2) v.push_back (snare);
                if (chorus && i % 2 == 1) v.push_back (kick);
                d.push_back (beat (0.5, v));
                if (! chorus) d.back().notes[0].fret = 42;
            }
            if (chorus) for (int i = 1; i < 8; ++i) d[(size_t) i].notes[0].fret = 42;
            return d;
        };
        auto bassBar = [&] (int chord)
        {
            std::vector<RawBeat> b;
            const int fret = prog[chord][0] == 5 ? prog[chord][1] : prog[chord][1] + 5 - 5;
            const int str = prog[chord][0] == 5 ? 3 : 2;
            for (int i = 0; i < 8; ++i) b.push_back (beat (0.5, { n (str, fret) }));
            return b;
        };
        // 前奏：每小节一个强力和弦（全音符 + 推弦尾巴）
        for (int c = 0; c < 4; ++c)
            addBar (c == 0 ? "Intro" : "", { beat (2.0, power (prog[c][0], prog[c][1], false)), beat (2.0, power (prog[c][0], prog[c][1], false)) },
                    bassBar (c), drumBar (false));
        // 主歌：八分音符闷音
        for (int c = 0; c < 4; ++c)
        {
            std::vector<RawBeat> g;
            for (int i = 0; i < 8; ++i) g.push_back (beat (0.5, power (prog[c][0], prog[c][1], i % 4 != 3)));
            addBar (c == 0 ? "Verse" : "", g, bassBar (c), drumBar (false));
        }
        // 副歌：开放的强力和弦，切分
        for (int c = 0; c < 4; ++c)
        {
            std::vector<RawBeat> g { beat (1.5, power (prog[c][0], prog[c][1], false)), beat (0.5, power (prog[c][0], prog[c][1], false)),
                                     beat (1.0, power (prog[c][0], prog[c][1], false)), beat (1.0, power (prog[(c + 1) % 4][0], prog[(c + 1) % 4][1], false)) };
            addBar (c == 0 ? "Chorus" : "", g, bassBar (c), drumBar (true));
        }
        // Solo：E 小调五声音阶，带击弦、滑音、推弦、揉弦
        {
            auto ho = [&] (int s, int f) { RawNote x = n (s, f); x.hammerPull = true; return x; };
            auto bend = [&] (int s, int f, float semis) { RawNote x = n (s, f); x.bend = semis; return x; };
            auto vib = [&] (int s, int f) { RawNote x = n (s, f); x.vibrato = true; return x; };
            auto slide = [&] (int s, int f) { RawNote x = n (s, f); x.slideOut = true; return x; };
            addBar ("Solo", { beat (0.5, { n (2, 12) }), beat (0.5, { ho (2, 14) }), beat (0.5, { n (1, 12) }), beat (0.5, { ho (1, 15) }),
                              beat (1.0, { bend (1, 15, 2.0f) }), beat (1.0, { vib (0, 12) }) }, bassBar (0), drumBar (true));
            addBar ("", { beat (0.5, { n (0, 15) }), beat (0.5, { n (0, 12) }), beat (0.5, { n (1, 15) }), beat (0.5, { n (1, 12) }),
                          beat (0.5, { slide (2, 12) }), beat (0.5, { n (2, 14) }), beat (1.0, { bend (2, 14, 1.0f) }) }, bassBar (1), drumBar (true));
            addBar ("", { beat (0.5, { n (3, 14) }), beat (0.5, { ho (3, 12) }), beat (0.5, { n (2, 14) }), beat (0.5, { ho (2, 12) }),
                          beat (1.0, { bend (1, 15, 2.0f) }), beat (1.0, { vib (1, 12) }) }, bassBar (2), drumBar (true));
            addBar ("", { beat (2.0, power (5, 0, false)), beat (2.0, { vib (0, 12), vib (1, 12) }) }, bassBar (0), drumBar (true));
        }
        raw.tracks = { gtr, bass, drums };
        return std::shared_ptr<const Song> (buildSong (raw).release());
    }

    void setupPlayer (AudioEngine& engine, std::shared_ptr<const Song> song, int track)
    {
        engine.prepareForOffline (sr, block);
        engine.setOutputGainDb (-60.0f);
        auto& tp = engine.getTabPlayer();
        tp.setSong (song);
        tp.setTrack (track);
        tp.setCountIn (false);
        tp.setDemoOn (false);
        tp.setBackingVolumeDb (-60.0f);
    }

    // 一帧：喂 1600 个采样的演奏（可以为空），推进播放器和判定
    void stepFrame (AudioEngine& engine, PluckSynth& synth, const std::vector<Hit>& hits, size_t& h, int64_t& pos)
    {
        float l[block], r[block], in[block], sl[block], sr2[block];
        for (int b = 0; b < samplesPerFrame / block; ++b)
        {
            while (h < hits.size() && hits[h].at < pos + block)
            {
                auto on = hits[h].on;
                on.delaySamples = (int) juce::jmax ((int64_t) 0, hits[h].at - pos);
                synth.noteOn (on);
                ++h;
            }
            std::fill (sl, sl + block, 0.0f); std::fill (sr2, sr2 + block, 0.0f);
            synth.render (sl, sr2, block);
            for (int i = 0; i < block; ++i) in[i] = 0.5f * (sl[i] + sr2[i]);
            engine.processOffline (in, l, r, block);
            engine.getJudge().processPending();
            pos += block;
        }
    }

    juce::PropertiesFile::Options propsOptions()
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "LianqinPromo";
        return o;
    }
}

int renderPromo (const juce::File& out, const juce::File& songDir)
{
    using namespace promo;
    out.createDirectory();
    TabHighway::clockOverride = [] { return clockNow; };
    juce::PropertiesFile props (out.getChildFile ("promo.settings"), propsOptions());
    PracticeLog plog (out.getChildFile ("promo_log.json"));
    juce::ignoreUnused (songDir);

    // ---- 1. 调音器 + 电平表 ---------------------------------------------------
    {
        auto dir = sceneDir (out, "tuner");
        juce::Component box;
        TunerDisplay tuner;
        LevelMeter inM (T ("输入")), outM (T ("输出"));
        box.setSize (420, 380);
        tuner.setBounds (0, 0, 300, 380);
        inM.setBounds (316, 0, 44, 380);
        outM.setBounds (372, 0, 44, 380);
        for (auto* c : std::initializer_list<juce::Component*> { &tuner, &inM, &outM }) box.addAndMakeVisible (c);
        juce::Random rnd (3);
        for (int f = 0; f < 150; ++f)
        {
            const float t = (float) f / 150.0f;
            PitchDetector::Result r;
            // 先偏低 30 音分，慢慢拧准到 A2
            r.frequency = (float) (110.0 * std::pow (2.0, (-30.0 * juce::jmax (0.0f, 1.0f - t * 1.6f)) / 1200.0));
            r.clarity = 0.95f;
            r.rmsDb = -20.0f;
            tuner.update (r);
            const float env = 0.55f * std::exp (-3.0f * std::fmod (t * 3.0f, 1.0f)) + 0.05f * rnd.nextFloat();
            inM.push (env, false);
            outM.push (env * 0.8f, false);
            writeJpg (box, frameFile (dir, f), 2.4f);
        }
        std::printf ("  tuner 150\n");
    }

    // ---- 2. 效果器板 + 放大的踏板 ---------------------------------------------
    {
        AudioEngine engine;
        engine.prepareForOffline (sr, 512);
        EffectChain chain (engine);
        if (auto xml = juce::parseXML (juce::String::fromUTF8 (DefaultPresets::listV2[3][1])))
        {
            juce::StringArray problems;
            chain.loadFromXml (*xml, problems);
        }
        // 再加几块，看起来更满
        chain.insert (chain.createBuiltin ("chorus"));
        chain.insert (chain.createBuiltin ("delay"));
        PedalBoard board (chain, [] {});
        board.setSize (1320, 330);
        auto dir = sceneDir (out, "board");
        writeJpg (board, dir.getChildFile ("board.jpg"), 1.6f);

        // 过载（TS808 风格）放大，拧驱动旋钮
        auto slot = chain.createBuiltin ("overdrive");
        auto* fx = dynamic_cast<BuiltinEffect*> (&slot->getProcessor());
        fx->setParam ("model", 1.0f);
        fx->setParam ("tone", 0.6f);
        PedalEditorOverlay overlay (slot, {});
        overlay.setSize (1240, 800);
        auto edir = sceneDir (out, "editor_drive");
        for (int f = 0; f < 90; ++f)
        {
            const float t = juce::jlimit (0.0f, 1.0f, (float) (f - 10) / 60.0f);
            fx->setParam ("drive", 4.0f + 28.0f * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * t)));
            fx->setParam ("level", -6.0f + 4.0f * t);
            overlay.repaint();
            writeJpg (overlay, frameFile (edir, f), 1.3f, juce::Rectangle<int> (330, 110, 580, 580));
        }
        std::printf ("  board + editor_drive 90\n");

        // 各个型号（静帧）
        auto mdir = sceneDir (out, "models");
        int idx = 0;
        auto still = [&] (const char* id, const char* param, int value)
        {
            auto s = chain.createBuiltin (id);
            dynamic_cast<BuiltinEffect*> (&s->getProcessor())->setParam (param, (float) value);
            PedalEditorOverlay ov (s, {});
            ov.setSize (1240, 800);
            writeJpg (ov, frameFile (mdir, idx++), 1.3f, juce::Rectangle<int> (330, 110, 580, 580));
        };
        for (int v : { 3, 5, 6, 9, 10 }) still ("amp", "voicing", v);
        for (int v : { 1, 2, 3 }) still ("overdrive", "model", v);
        still ("distortion", "model", 1);
        std::printf ("  models %d\n", idx);
        chain.clear();
        engine.releaseAllChains();
    }

    // ---- 3. 练习页（原创练习曲滚动）------------------------------------------
    {
        AudioEngine engine;
        engine.prepareForOffline (sr, block);
        PracticePanel panel (engine.getTabPlayer(), engine.getDrums(), engine.getJudge(), engine.getRecorder(), plog, props);
        panel.setSize (1280, 720);
        panel.showSong (promoSong());
        auto& hw = panel.getHighway();
        hw.setJudgeVisible (false);
        hw.setZoom (300.0f);
        auto song = engine.getTabPlayer().getSong();
        auto dir = sceneDir (out, "panel_canon");
        const double startSec = song->beatToSeconds (song->bars[3].start);
        for (int f = 0; f < 240; ++f)
        {
            clockNow = f / (double) fps;
            hw.setFrozenPosition (song->secondsToBeat (startSec + f / (double) fps));
            writeJpg (panel, frameFile (dir, f), 1.5f);
        }
        std::printf ("  panel_canon 240\n");

        // A-B 循环 + 慢速（看进度条上的绿色循环段）
        auto ldir = sceneDir (out, "loop");
        const double a = song->bars[12].start, b = song->bars[13].start + song->bars[13].length;
        engine.getTabPlayer().setLoop (a, b, true);
        double beat = a;
        for (int f = 0; f < 180; ++f)
        {
            clockNow = f / (double) fps;
            beat += song->tempoAt (beat) / 60.0 / fps * 0.6;   // 60% 速度
            if (beat >= b) beat = a + (beat - b);
            hw.setFrozenPosition (beat);
            writeJpg (panel, frameFile (ldir, f), 1.5f);
        }
        std::printf ("  loop 180\n");
    }

    // ---- 4. 弹奏判定：合成演奏，一帧一帧跑判定 -----------------------------------
    {
        auto song = exerciseSong();
        AudioEngine engine;
        setupPlayer (engine, song, 0);
        auto js = std::make_shared<JudgeSession> (song, 0);
        engine.getJudge().setSession (js);
        engine.getJudge().setLatencyMs (0.0f);
        engine.getJudge().setEnabled (true);
        engine.getJudge().processPending();
        auto& tp = engine.getTabPlayer();
        tp.setPlaying (true);

        TabHighway hw (tp);
        hw.setSize (1280, 640);
        hw.setSong (song, 0);
        hw.setJudgeSession (js, false);
        hw.setZoom (340.0f);

        const auto hits = planHits (*song, *js, 0, 0.0, 1.0e9, 9, 13);
        PluckSynth synth; synth.prepare (sr);
        size_t h = 0; int64_t pos = 0;
        auto dir = sceneDir (out, "judge");
        const int frames = (int) (song->lengthSeconds() * fps) + 15;
        for (int f = 0; f < frames; ++f)
        {
            stepFrame (engine, synth, hits, h, pos);
            clockNow = f / (double) fps;
            hw.setFrozenPosition (tp.getPosition());
            writeJpg (hw, frameFile (dir, f), 1.5f);
        }
        std::printf ("  judge %d\n", frames);

        JudgeSummaryCard card;
        card.setSize (460, 240);
        card.show (*js);
        writePng (card, out.getChildFile ("summary.png"), 2.4f);
    }

    // ---- 5. 等待模式 ------------------------------------------------------------
    {
        auto song = exerciseSong();
        AudioEngine engine;
        setupPlayer (engine, song, 0);
        auto js = std::make_shared<JudgeSession> (song, 0);
        auto& judge = engine.getJudge();
        judge.setSession (js);
        judge.setEnabled (true);
        judge.setWaitMode (true);
        judge.processPending();
        auto& tp = engine.getTabPlayer();
        tp.setPlaying (true);

        TabHighway hw (tp);
        hw.setSize (1280, 640);
        hw.setSong (song, 0);
        hw.setJudgeSession (js, true);
        hw.setZoom (340.0f);

        PluckSynth synth; synth.prepare (sr);
        const auto& notes = song->tracks[0].notes;
        std::vector<Hit> none;
        size_t h = 0; int64_t pos = 0;
        int played = 0; double stuckSince = -1.0; bool wrongDone = false;
        auto dir = sceneDir (out, "wait");
        for (int f = 0; f < 240; ++f)
        {
            const double now = f / (double) fps;
            int gate = -1;
            for (size_t e = 0; e < js->events.size(); ++e)
                if (js->verdictOf ((int) e) == Verdict::pending) { gate = (int) e; break; }
            if (gate >= 0 && tp.getPosition() >= js->events[(size_t) gate].beat - 1.0e-6)
            {
                if (stuckSince < 0.0) stuckSince = now;
                if (now - stuckSince > (gate == 3 ? 1.0 : 0.45) && played == gate)
                {
                    const bool wrong = gate == 3 && ! wrongDone;
                    const auto& ev = js->events[(size_t) gate];
                    for (int k = ev.firstNote; k < ev.firstNote + ev.numNotes; ++k)
                    {
                        PluckSynth::NoteOn on;
                        on.midi = notes[(size_t) k].midi + (wrong ? 2 : 0);
                        on.velocity = 0.85f;
                        on.lengthSeconds = 1.0;
                        synth.noteOn (on);
                    }
                    if (wrong) { wrongDone = true; stuckSince = now; }
                    else { ++played; stuckSince = -1.0; }
                }
            }
            stepFrame (engine, synth, none, h, pos);
            clockNow = now;
            hw.setFrozenPosition (tp.getPosition());
            writeJpg (hw, frameFile (dir, f), 1.5f);
        }
        std::printf ("  wait 240\n");
    }

    // ---- 6. 技巧标记 + 音名（练习曲的 Solo 段）--------------------------------------
    {
        auto song = promoSong();
        {
            AudioEngine engine;
            setupPlayer (engine, song, 0);
            TabHighway hw (engine.getTabPlayer());
            hw.setSize (1280, 640);
            hw.setSong (song, song->defaultTrack());
            hw.setZoom (280.0f);
            auto dir = sceneDir (out, "techniques");
            const double startSec = song->beatToSeconds (song->bars[12].start) - 1.0;
            for (int f = 0; f < 180; ++f)
            {
                clockNow = f / (double) fps;
                hw.setFrozenPosition (song->secondsToBeat (startSec + f / (double) fps * 0.75));
                writeJpg (hw, frameFile (dir, f), 1.5f);
            }
            std::printf ("  techniques 180\n");
        }
    }

    // ---- 7. 静帧：练习记录、混音、找谱、鼓机 ----------------------------------------
    {
        const auto f = out.getChildFile ("stats_demo.json");
        f.deleteFile();
        PracticeLog pl (f);
        juce::Random rr (5);
        const char* names[] = { "Punk Practice No.1", "Riff Practice", "Blues shuffle in E", "Pentatonic run", "Punk power chords" };
        for (int k = 0; k < 5; ++k)
        {
            const juce::String path = juce::String ("C:/tabs/") + names[k] + ".gp5";
            pl.addTime (path, names[k], 500.0 + 1100.0 * rr.nextFloat());
            float acc = 0.5f + 0.08f * (float) k;
            for (int i = 0; i < 16 + 5 * k; ++i)
            {
                acc = juce::jlimit (0.3f, 1.0f, acc + (rr.nextFloat() - 0.32f) * 0.08f);
                pl.addPass (path, names[k], acc, juce::jmin (1.0f, 0.6f + 0.025f * (float) i));
            }
        }
        StatsView sv (pl);
        sv.setSize (1000, 690);
        sv.refresh();
        writePng (sv, out.getChildFile ("stats.png"), 1.9f);
        f.deleteFile();

        {
            auto song = promoSong();
            std::vector<TrackMixer::Strip> strips (song->tracks.size());
            strips[1].solo = true; strips[2].solo = true; strips[0].db = -6.0f;
            TrackMixer mixer (*song, strips, nullptr);
            writePng (mixer, out.getChildFile ("mixer.png"), 2.4f);
        }

        TabSearchBox search ("Punk Practice", juce::File ("C:\\Users\\Guitar\\Downloads"));
        writePng (search, out.getChildFile ("search.png"), 2.4f);

        DrumMachine drums;
        DrumPanel dp (drums, props);
        dp.setSize (1100, 180);
        writePng (dp, out.getChildFile ("drums.png"), 1.7f);
        std::printf ("  stills\n");
    }

    TabHighway::clockOverride = nullptr;
    return 0;
}

// =============================================================================
// 第二版：完整主窗口（1240x800，放大 1.5 倍）+ 界面元素位置表，给合成脚本画镜头推进和标注
#include "MainComponent.h"

namespace juce::detail { bool dispatchNextMessageOnSystemQueue (bool returnIfNoPendingMessages); }

namespace promo2
{
    // 没有消息循环：把排队的异步消息（链路变化通知等）手动处理掉
    void pump()
    {
        for (int i = 0; i < 200; ++i)
            if (! juce::detail::dispatchNextMessageOnSystemQueue (true))
                break;
    }

    using namespace promo;
    constexpr float S = 1.5f;

    juce::var rectVar (juce::Rectangle<int> r)
    {
        juce::Array<juce::var> a { juce::roundToInt (r.getX() * S), juce::roundToInt (r.getY() * S),
                                   juce::roundToInt (r.getWidth() * S), juce::roundToInt (r.getHeight() * S) };
        return a;
    }

    void collect (juce::Component& root, juce::Component& c, juce::Array<juce::var>& out)
    {
        for (auto* ch : c.getChildren())
        {
            if (! ch->isVisible()) continue;
            juce::String type, text;
            if (auto* b = dynamic_cast<juce::Button*> (ch))           { type = "button"; text = b->getButtonText(); }
            else if (auto* cb = dynamic_cast<juce::ComboBox*> (ch))   { type = "combo";  text = cb->getText(); }
            else if (dynamic_cast<juce::Slider*> (ch))                { type = "slider"; }
            else if (auto* l = dynamic_cast<juce::Label*> (ch))       { type = "label";  text = l->getText(); }
            else if (dynamic_cast<TunerDisplay*> (ch))                type = "tuner";
            else if (auto* m = dynamic_cast<LevelMeter*> (ch))        { type = "meter"; juce::ignoreUnused (m); }
            else if (dynamic_cast<PedalBoard*> (ch))                  type = "pedalboard";
            else if (dynamic_cast<PresetBar*> (ch))                   type = "presetbar";
            else if (dynamic_cast<DrumPanel*> (ch))                   type = "drumpanel";
            else if (dynamic_cast<TabHighway*> (ch))                  type = "highway";
            else if (dynamic_cast<PracticePanel*> (ch))               type = "practicepanel";
            else if (dynamic_cast<StatsView*> (ch))                   type = "statsview";
            else if (dynamic_cast<PedalBoard*> (&c))                  type = "pedal";
            if (type.isNotEmpty())
            {
                auto* o = new juce::DynamicObject();
                o->setProperty ("type", type);
                o->setProperty ("text", text);
                o->setProperty ("rect", rectVar (root.getLocalArea (ch, ch->getLocalBounds())));
                out.add (juce::var (o));
            }
            collect (root, *ch, out);
        }
    }

    void writeUi (juce::Component& root, const juce::File& f)
    {
        juce::Array<juce::var> items;
        collect (root, root, items);
        f.replaceWithText (juce::JSON::toString (juce::var (items), false));
    }

    juce::Image snap (juce::Component& c) { return c.createComponentSnapshot (c.getLocalBounds(), true, S); }

    void saveJpg (const juce::Image& img, const juce::File& f)
    {
        juce::JPEGImageFormat jpg;
        jpg.setQuality (0.93f);
        f.deleteFile();
        if (auto os = f.createOutputStream()) jpg.writeImageToStream (img, *os);
    }

    void savePng (const juce::Image& img, const juce::File& f)
    {
        juce::PNGImageFormat png;
        f.deleteFile();
        if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
    }

    template <typename F>
    void tween (juce::Image base, juce::Rectangle<int> area, juce::Component& comp, const juce::File& dir, int frames, F&& stepFn)
    {
        for (int f = 0; f < frames; ++f)
        {
            stepFn (f);
            auto img = base.createCopy();
            juce::Graphics g (img);
            g.drawImageAt (comp.createComponentSnapshot (comp.getLocalBounds(), true, S),
                           juce::roundToInt (area.getX() * S), juce::roundToInt (area.getY() * S));
            saveJpg (img, frameFile (dir, f));
        }
    }
}

int renderPromo2 (const juce::File& out)
{
    using namespace promo;
    using namespace promo2;
    out.createDirectory();
    TabHighway::clockOverride = [] { return clockNow; };

    // ---- 主程序的设置：打开时就是 JCM800 + TS808 的音色 --------------------------------
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "LianqinHost";
        o.folderName = "LianqinHost";
        o.filenameSuffix = ".settings";
        juce::PropertiesFile p (o);
        if (auto xml = juce::parseXML (juce::String::fromUTF8 (DefaultPresets::listV2[3][1])))
            p.setValue ("currentChain", xml.get());
        p.setValue ("currentPreset", juce::String::fromUTF8 (DefaultPresets::listV2[3][0]));
        p.setValue ("view", "tone");
        p.setValue ("tabLastFile", "");
        p.saveIfNeeded();

        // 练习记录（演示数据）
        auto lf = PracticeLog::defaultFile();
        lf.deleteFile();
        PracticeLog pl (lf);
        juce::Random rr (5);
        const char* names[] = { "Punk Practice No.1", "Riff Practice", "Blues shuffle in E", "Pentatonic run", "Power chord drill" };
        for (int k = 0; k < 5; ++k)
        {
            const juce::String path = juce::String ("C:/tabs/") + names[k] + ".gp5";
            pl.addTime (path, names[k], 500.0 + 1100.0 * rr.nextFloat());
            float acc = 0.5f + 0.08f * (float) k;
            for (int i = 0; i < 16 + 5 * k; ++i)
            {
                acc = juce::jlimit (0.3f, 1.0f, acc + (rr.nextFloat() - 0.32f) * 0.08f);
                pl.addPass (path, names[k], acc, juce::jmin (1.0f, 0.6f + 0.025f * (float) i));
            }
        }
        pl.save();

        // 演示用：把每日时长摊到最近 14 天（连续练了 9 天），每首歌多几次练习
        auto v = juce::JSON::parse (lf);
        if (auto* root = v.getDynamicObject())
        {
            auto* d = new juce::DynamicObject();
            const int mins[] = { 12, 0, 25, 18, 0, 0, 22, 30, 15, 41, 28, 35, 47, 52 };
            const auto now = juce::Time::getCurrentTime();
            for (int i = 0; i < 14; ++i)
                if (mins[i] > 0)
                    d->setProperty (PracticeLog::dayKey (now - juce::RelativeTime::days (13 - i)), mins[i] * 60);
            root->setProperty ("daily", juce::var (d));
            if (auto* arr = v["songs"].getArray())
            {
                const int sess[] = { 9, 6, 4, 7, 11 };
                for (int k = 0; k < arr->size(); ++k)
                    if (auto* o = (*arr)[k].getDynamicObject())
                    {
                        o->setProperty ("sessions", sess[k % 5]);
                        o->setProperty ("last", now.toMilliseconds() - (juce::int64) (k * 26 + 3) * 3600000);
                    }
            }
            lf.replaceWithText (juce::JSON::toString (v, true));
        }
    }

    MainComponent mc;
    mc.setSize (1240, 800);
    mc.setView (MainComponent::View::tone);
    pump();

    // ---- 音色页 ------------------------------------------------------------------
    const auto toneBase = snap (mc);
    saveJpg (toneBase, out.getChildFile ("tone.jpg"));
    writeUi (mc, out.getChildFile ("ui_tone.json"));

    // 调音器 + 电平表动起来（直接在真窗口里）
    {
        TunerDisplay* tuner = nullptr;
        juce::Array<LevelMeter*> meters;
        std::function<void (juce::Component&)> find = [&] (juce::Component& c)
        {
            for (auto* ch : c.getChildren())
            {
                if (auto* t = dynamic_cast<TunerDisplay*> (ch)) tuner = t;
                if (auto* m = dynamic_cast<LevelMeter*> (ch)) meters.add (m);
                find (*ch);
            }
        };
        find (mc);
        auto dir = sceneDir (out, "tone_tuner");
        juce::Random rnd (3);
        for (int f = 0; f < 150; ++f)
        {
            const float t = (float) f / 150.0f;
            PitchDetector::Result r;
            r.frequency = (float) (110.0 * std::pow (2.0, (-30.0 * juce::jmax (0.0f, 1.0f - t * 1.6f)) / 1200.0));
            r.clarity = 0.95f;
            r.rmsDb = -20.0f;
            if (tuner != nullptr) tuner->update (r);
            const float env = 0.55f * std::exp (-3.0f * std::fmod (t * 3.0f, 1.0f)) + 0.05f * rnd.nextFloat();
            for (int i = 0; i < meters.size(); ++i) meters[i]->push (env * (i == 0 ? 1.0f : 0.8f), false);
            saveJpg (snap (mc), frameFile (dir, f));
        }
        std::printf ("  tone_tuner 150\n");
    }

    // 双击放大的踏板（盖在真窗口上）+ 各个型号
    {
        AudioEngine engine;
        engine.prepareForOffline (sr, 512);
        EffectChain chain (engine);
        auto slot = chain.createBuiltin ("overdrive");
        auto* fx = dynamic_cast<BuiltinEffect*> (&slot->getProcessor());
        fx->setParam ("model", 1.0f);
        fx->setParam ("tone", 0.6f);
        {
            PedalEditorOverlay overlay (slot, {});
            mc.addAndMakeVisible (overlay);
            overlay.setBounds (mc.getLocalBounds());
            auto dir = sceneDir (out, "tone_editor");
            for (int f = 0; f < 90; ++f)
            {
                const float t = juce::jlimit (0.0f, 1.0f, (float) (f - 10) / 60.0f);
                fx->setParam ("drive", 4.0f + 28.0f * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * t)));
                fx->setParam ("level", -6.0f + 4.0f * t);
                overlay.repaint();
                saveJpg (snap (mc), frameFile (dir, f));
            }
            mc.removeChildComponent (&overlay);
        }
        auto mdir = sceneDir (out, "tone_models");
        int idx = 0;
        auto still = [&] (const char* id, const char* param, int value)
        {
            auto s = chain.createBuiltin (id);
            dynamic_cast<BuiltinEffect*> (&s->getProcessor())->setParam (param, (float) value);
            PedalEditorOverlay ov (s, {});
            mc.addAndMakeVisible (ov);
            ov.setBounds (mc.getLocalBounds());
            saveJpg (snap (mc), frameFile (mdir, idx++));
            mc.removeChildComponent (&ov);
        };
        for (int v : { 3, 5, 6, 9, 10 }) still ("amp", "voicing", v);
        for (int v : { 1, 2, 3 }) still ("overdrive", "model", v);
        still ("distortion", "model", 1);
        std::printf ("  tone_editor 90, models %d\n", idx);
        chain.clear();
        engine.releaseAllChains();
    }

    // ---- 练习页 ------------------------------------------------------------------
    mc.setView (MainComponent::View::practice);
    auto& panel = mc.getPracticePanel();
    auto song = promoSong();
    panel.showSong (song);
    panel.getHighway().setJudgeVisible (false);
    panel.getHighway().setFrozenPosition (song->bars[4].start);
    pump();
    const auto practiceBase = snap (mc);
    saveJpg (practiceBase, out.getChildFile ("practice.jpg"));
    writeUi (mc, out.getChildFile ("ui_practice.json"));

    // 谱面区域：另开一个同样大小的谱面，逐帧画好贴回窗口
    const auto hb = mc.getLocalArea (&panel.getHighway(), panel.getHighway().getLocalBounds());
    const float zoom = panel.getHighway().getZoom();

    {   // 1) 滚动
        AudioEngine engine;
        setupPlayer (engine, song, 0);
        TabHighway hw (engine.getTabPlayer());
        hw.setSize (hb.getWidth(), hb.getHeight());
        hw.setSong (song, 0);
        hw.setZoom (zoom);
        hw.setJudgeVisible (false);
        const double startSec = song->beatToSeconds (song->bars[4].start);
        tween (practiceBase, hb, hw, sceneDir (out, "practice_scroll"), 240, [&] (int f)
        {
            clockNow = f / (double) fps;
            hw.setFrozenPosition (song->secondsToBeat (startSec + f / (double) fps));
        });
        std::printf ("  practice_scroll 240\n");
    }

    {   // 2) 跟弹打分：合成一段有对有错的演奏
        AudioEngine engine;
        setupPlayer (engine, song, 0);
        auto js = std::make_shared<JudgeSession> (song, 0);
        engine.getJudge().setSession (js);
        engine.getJudge().setLatencyMs (0.0f);
        engine.getJudge().setEnabled (true);
        engine.getJudge().processPending();
        auto& tp = engine.getTabPlayer();
        const double from = song->bars[4].start;
        tp.seek (from);
        tp.setPlaying (true);
        TabHighway hw (tp);
        hw.setSize (hb.getWidth(), hb.getHeight());
        hw.setSong (song, 0);
        hw.setZoom (zoom);
        hw.setJudgeSession (js, false);
        const auto hits = planHits (*song, *js, 0, from, 1.0e9, 7, 11);
        PluckSynth synth; synth.prepare (sr);
        size_t h = 0; int64_t pos = 0;
        const int frames = 330;
        tween (practiceBase, hb, hw, sceneDir (out, "practice_judge"), frames, [&] (int f)
        {
            stepFrame (engine, synth, hits, h, pos);
            clockNow = f / (double) fps;
            hw.setFrozenPosition (tp.getPosition());
        });
        std::printf ("  practice_judge %d\n", frames);

        JudgeSummaryCard card;
        card.setSize (460, 240);
        card.show (*js);
        savePng (card.createComponentSnapshot (card.getLocalBounds(), true, S), out.getChildFile ("pop_summary.png"));
    }

    {   // 3) A-B 循环，慢速
        AudioEngine engine;
        setupPlayer (engine, song, 0);
        auto& tp = engine.getTabPlayer();
        const double a = song->bars[12].start, b = song->bars[13].start + song->bars[13].length;
        tp.setLoop (a, b, true);
        TabHighway hw (tp);
        hw.setSize (hb.getWidth(), hb.getHeight());
        hw.setSong (song, 0);
        hw.setZoom (zoom);
        hw.setJudgeVisible (false);
        double beat = a;
        tween (practiceBase, hb, hw, sceneDir (out, "practice_loop"), 210, [&] (int f)
        {
            clockNow = f / (double) fps;
            beat += song->tempoAt (beat) / 60.0 / fps * 0.6;
            if (beat >= b) beat = a + (beat - b);
            hw.setFrozenPosition (beat);
        });
        std::printf ("  practice_loop 210\n");
    }

    {   // 4) 等待模式（窗口上的“等待模式”打勾）
        panel.keyPressed (juce::KeyPress ('w'));
        const auto waitBase = snap (mc);
        panel.keyPressed (juce::KeyPress ('w'));

        AudioEngine engine;
        setupPlayer (engine, song, 0);
        auto js = std::make_shared<JudgeSession> (song, 0);
        auto& judge = engine.getJudge();
        judge.setSession (js);
        judge.setEnabled (true);
        judge.setWaitMode (true);
        judge.processPending();
        auto& tp = engine.getTabPlayer();
        tp.seek (song->bars[12].start);
        tp.setPlaying (true);
        TabHighway hw (tp);
        hw.setSize (hb.getWidth(), hb.getHeight());
        hw.setSong (song, 0);
        hw.setZoom (zoom);
        hw.setJudgeSession (js, true);
        PluckSynth synth; synth.prepare (sr);
        const auto& notes = song->tracks[0].notes;
        std::vector<Hit> none;
        size_t h = 0; int64_t pos = 0;
        int lastPlayed = -1; double stuckSince = -1.0; bool wrongDone = false;
        tween (waitBase, hb, hw, sceneDir (out, "practice_wait"), 240, [&] (int f)
        {
            const double now = f / (double) fps;
            int gate = -1;
            for (size_t e = 0; e < js->events.size(); ++e)
                if (js->events[e].beat >= song->bars[12].start - 1.0e-6 && js->verdictOf ((int) e) == Verdict::pending) { gate = (int) e; break; }
            if (gate >= 0 && tp.getPosition() >= js->events[(size_t) gate].beat - 1.0e-6)
            {
                if (stuckSince < 0.0) stuckSince = now;
                const bool second = lastPlayed >= 0 && gate > lastPlayed && ! wrongDone;
                if (now - stuckSince > (second ? 1.0 : 0.5) && gate != lastPlayed)
                {
                    const bool wrong = second;
                    const auto& ev = js->events[(size_t) gate];
                    for (int k = ev.firstNote; k < ev.firstNote + ev.numNotes; ++k)
                    {
                        PluckSynth::NoteOn on;
                        on.midi = notes[(size_t) k].midi + (wrong ? 2 : 0);
                        on.velocity = 0.85f;
                        on.hammer = notes[(size_t) k].hammerPull && ! wrong;
                        on.lengthSeconds = 1.0;
                        synth.noteOn (on);
                    }
                    if (wrong) { wrongDone = true; stuckSince = now; }
                    else { lastPlayed = gate; stuckSince = -1.0; }
                }
            }
            stepFrame (engine, synth, none, h, pos);
            clockNow = now;
            hw.setFrozenPosition (tp.getPosition());
        });
        std::printf ("  practice_wait 240\n");
    }

    // 弹窗（和窗口同样的放大倍数）
    {
        TabSearchBox search ("Punk Practice", juce::File ("C:\\Users\\Guitar\\Downloads"));
        savePng (search.createComponentSnapshot (search.getLocalBounds(), true, S), out.getChildFile ("pop_search.png"));
        std::vector<TrackMixer::Strip> strips (song->tracks.size());
        strips[1].solo = true; strips[2].solo = true;
        TrackMixer mixer (*song, strips, nullptr);
        savePng (mixer.createComponentSnapshot (mixer.getLocalBounds(), true, S), out.getChildFile ("pop_mixer.png"));
    }

    // ---- 练习记录页 --------------------------------------------------------------
    mc.setView (MainComponent::View::stats);
    pump();
    saveJpg (snap (mc), out.getChildFile ("stats.jpg"));
    writeUi (mc, out.getChildFile ("ui_stats.json"));
    std::printf ("  stats\n");

    TabHighway::clockOverride = nullptr;
    return 0;
}
