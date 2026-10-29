#include "DrumMachine.h"
#include <cmath>
#include <cstring>
#include "Text.h"

// =============================================================================
// Groove patterns
namespace
{
    struct Track { const char* key; const char* hits; };

    int instrumentForKey (const juce::String& k)
    {
        if (k == "K")  return kick;
        if (k == "S")  return snare;
        if (k == "R")  return sideStick;
        if (k == "H")  return closedHat;
        if (k == "O")  return openHat;
        if (k == "C")  return crash;
        if (k == "D")  return ride;
        if (k == "T1") return tomHigh;
        if (k == "T2") return tomMid;
        if (k == "T3") return tomLow;
        jassertfalse;
        return -1;
    }

    float velocityForChar (char c)
    {
        switch (c)
        {
            case 'X': return 1.0f;
            case 'x': return 0.7f;
            case 'o': return 0.5f;
            case 'g': return 0.28f;   // ghost note
            default:  return 0.0f;
        }
    }

    DrumPattern pattern (int steps, std::initializer_list<Track> tracks)
    {
        DrumPattern p;
        p.steps = steps;
        for (auto& t : tracks)
        {
            const int inst = instrumentForKey (t.key);
            jassert ((int) std::strlen (t.hits) == steps);
            for (int i = 0; i < steps && t.hits[i] != 0; ++i)
                if (inst >= 0)
                    p.velocity[(size_t) inst][(size_t) i] = velocityForChar (t.hits[i]);
        }
        return p;
    }

    bool hasHits (const DrumPattern& p)
    {
        for (auto& inst : p.velocity)
            for (auto v : inst)
                if (v > 0.0f)
                    return true;
        return false;
    }

    uint32_t mixHash (uint32_t x)
    {
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    }

    DrumStyle style (const char* en, const char* zh, int beats, int stepsPerBeat, float bpm, float swing,
                     DrumPattern verse, DrumPattern chorus, DrumPattern fill,
                     DrumPattern verseB = {}, DrumPattern chorusB = {}, bool clickOnly = false)
    {
        DrumStyle s;
        s.name = TR (en, zh);
        s.beatsPerBar = beats;
        s.stepsPerBeat = stepsPerBeat;
        s.defaultBpm = bpm;
        s.defaultSwing = swing;
        s.clickOnly = clickOnly;
        s.verse = verse; s.verseB = verseB;
        s.chorus = chorus; s.chorusB = chorusB;
        s.fill = fill;
        return s;
    }
}

namespace
{
    std::vector<DrumStyle>& drumStyleStorage()
    {
        // One row per piece, one character per step:
        //   X accent   x normal   o light   g ghost   . rest
        //   K kick  S snare  R rim  H closed hat  O open hat  C crash  D ride
        //   T1/T2/T3 high/mid/floor tom
        static std::vector<DrumStyle> styles = []
        {
            std::vector<DrumStyle> v {
                style ("Rock", "摇滚", 4, 4, 110, 0,
               pattern (16, { { "H", "x.x.x.x.x.x.x.x." }, { "K", "X.....X.X......." }, { "S", "....X.......X..." } }),
               pattern (16, { { "D", "X.x.X.x.X.x.X.x." }, { "K", "X.....X.X.X....." }, { "S", "....X.......X..." } }),
               pattern (16, { { "H", "x.x.x.x........." }, { "K", "X.....X........." }, { "S", "....X...Xx......" },
                              { "T1", "..........Xx...." }, { "T2", "............Xx.." }, { "T3", "..............XX" } }),
               pattern (16, { { "H", "x.x.x.x.x.x.x.x." }, { "K", "X.....X.X.....x." }, { "S", "....X.......X..g" } }),
               pattern (16, { { "D", "X.x.X.x.X.x.X.x." }, { "K", "X.....X.X.X...x." }, { "S", "....X.......X..g" } })),

        style ("Pop", "流行", 4, 4, 96, 0,
               pattern (16, { { "H", "x.x.x.x.x.x.x.x." }, { "K", "X.......X.X....." }, { "S", "....X.......X..." } }),
               pattern (16, { { "H", "x...x...x...x..." }, { "O", "..x...x...x...x." }, { "K", "X...X...X...X..." }, { "S", "....X.......X..." } }),
               pattern (16, { { "H", "x.x.x.x........." }, { "K", "X.......X......." }, { "S", "....X...X.X.XXXX" } }),
               pattern (16, { { "H", "x.x.x.x.x.x.x.x." }, { "K", "X.....X...X....." }, { "S", "....X.......X..g" } }),
               pattern (16, { { "H", "x.x.x.x.x.x.x.x." }, { "O", "..x...x...x...x." }, { "K", "X...X...X.X.X..." }, { "S", "....X.......X..." } })),

        style ("Ballad", "慢歌", 4, 4, 72, 0,
               pattern (16, { { "H", "x...x...x...x..." }, { "K", "X.......X......." }, { "S", "........X......." } }),
               pattern (16, { { "D", "x...x...x...x..." }, { "K", "X.....X.X......." }, { "S", "........X......." } }),
               pattern (16, { { "K", "X..............." }, { "S", "....X.....xx...." }, { "T1", "........X......." }, { "T3", "............X.X." } }),
               pattern (16, { { "H", "x.x.x.x.x.x.x.x." }, { "K", "X.......X.....o." }, { "S", "........X......." } }),
               pattern (16, { { "D", "x.x.x.x.x.x.x.x." }, { "K", "X.....X.X.....o." }, { "S", "........X......." } })),

        style ("Funk", "放克", 4, 4, 96, 0.15f,
               pattern (16, { { "H", "XxxxXxxxXxxxXxxx" }, { "K", "X..X..X...X..X.." }, { "S", "....X..g.g..X..g" } }),
               pattern (16, { { "H", "XxxxXxxxXxxxXx.." }, { "O", "..............X." }, { "K", "X..X..X...XX..X." }, { "S", "....X..g.g..X..g" } }),
               pattern (16, { { "H", "XxxxXxxx........" }, { "K", "X..X..X........." }, { "S", "....X..gXgXgXXXX" } }),
               pattern (16, { { "H", "XxxxXxxxXxxxXxxx" }, { "K", "X..X..X..X.X...." }, { "S", "....X..g.g..X..g" } }),
               pattern (16, { { "H", "XxxxXxxxXxxxXxxx" }, { "K", "X..X..X..X..X..X" }, { "S", "....X..gXg..X..g" } })),

        style ("Disco", "迪斯科", 4, 4, 118, 0,
               pattern (16, { { "K", "X...X...X...X..." }, { "S", "....X.......X..." }, { "H", "x...x...x...x..." }, { "O", "..X...X...X...X." } }),
               pattern (16, { { "K", "X...X...X...X..." }, { "S", "....X.......X..." }, { "H", "xx.xxx.xxx.xxx.x" }, { "O", "..X...X...X...X." } }),
               pattern (16, { { "K", "X...X...X...X..." }, { "S", "....X...XXXXXXXX" }, { "H", "x...x..........." } }),
               pattern (16, { { "K", "X...X...X...X..." }, { "S", "....X.......X..." }, { "H", "XxxxXxxxXxxxXxxx" }, { "O", "..X...X...X...X." } }),
               pattern (16, { { "K", "X...X...X...X..." }, { "S", "....X.......X..g" }, { "H", "XxxxXxxxXxxxXxxx" }, { "O", "..X...X...X...X." } })),

        style ("Punk", "朋克", 4, 4, 180, 0,
               pattern (16, { { "H", "x.x.x.x.x.x.x.x." }, { "K", "X.....X.X......." }, { "S", "....X.......X..." } }),
               pattern (16, { { "D", "X.x.X.x.X.x.X.x." }, { "K", "X...X...X...X..." }, { "S", "..X...X...X...X." } }),
               pattern (16, { { "K", "X..............." }, { "S", "X.X.X.X.XXXXXXXX" } }),
               pattern (16, { { "H", "x.x.x.x.x.x.x.x." }, { "K", "X...X...X...X..." }, { "S", "....X.......X..." } }),
               pattern (16, { { "D", "X.x.X.x.X.x.X.x." }, { "K", "X...X...X...X..." }, { "S", "....X.......X..." } })),

        style ("Metal", "金属", 4, 4, 150, 0,
               pattern (16, { { "H", "x.x.x.x.x.x.x.x." }, { "K", "X.X.X.X.X.X.X.X." }, { "S", "....X.......X..." } }),
               pattern (16, { { "D", "X...X...X...X..." }, { "K", "XXXXXXXXXXXXXXXX" }, { "S", "....X.......X..." } }),
               pattern (16, { { "K", "X.X.X.X.X.X.X.X." }, { "S", "XxXxXxXx........" }, { "T1", "........XxXx...." }, { "T3", "............XxXx" } }),
               pattern (16, { { "H", "x.x.x.x.x.x.x.x." }, { "K", "XXXXXXXXXXXXXXXX" }, { "S", "....X.......X..." } }),
               pattern (16, { { "D", "X...X...X...X..." }, { "K", "X.X.X.X.X.X.X.X." }, { "S", "....X.......X..." } })),

        style ("Blues Shuffle", "蓝调 Shuffle", 4, 3, 84, 0,
               pattern (12, { { "H", "X.xX.xX.xX.x" }, { "K", "X.....X....." }, { "S", "...X.....X.." } }),
               pattern (12, { { "D", "X.xX.xX.xX.x" }, { "K", "X..X..X..X.." }, { "S", "...X.....X.." } }),
               pattern (12, { { "H", "X.xX.x......" }, { "K", "X..........." }, { "S", "...X..XxxXxx" } }),
               pattern (12, { { "D", "X.xX.xX.xX.x" }, { "K", "X..X..X....." }, { "S", "...X.....X.." } }),
               pattern (12, { { "D", "X.xX.xX.xX.x" }, { "K", "X..X..X..X.." }, { "S", "...X..g..X.." } })),

        style ("Reggae", "雷鬼", 4, 4, 76, 0.2f,
               pattern (16, { { "H", "x...x...x...x..." }, { "K", "........X......." }, { "R", "........X......." } }),
               pattern (16, { { "H", "x...x...x...x..." }, { "O", "..x...x...x...x." }, { "K", "X...X...X...X..." }, { "R", "....X.......X..." } }),
               pattern (16, { { "K", "........X......." }, { "T1", "........XX......" }, { "T2", "..........XX...." }, { "T3", "............XX.." }, { "S", "..............X." } }),
               pattern (16, { { "H", "x...x...x...x..." }, { "K", "....X...X......." }, { "R", "....X...X......." } }),
               pattern (16, { { "H", "x...x...x...x..." }, { "O", "..x...x...x...x." }, { "K", "........X......." }, { "R", "........X......." } })),

        style ("Jazz Swing", "爵士 Swing", 4, 3, 140, 0,
               pattern (12, { { "D", "X..X.xX..X.x" }, { "H", "...X.....X.." }, { "K", "o..o..o..o.." }, { "S", "........g..." } }),
               pattern (12, { { "D", "X..X.xX..X.x" }, { "H", "...X.....X.." }, { "K", "o..o..o..o.." }, { "S", ".....g..g..x" } }),
               pattern (12, { { "D", "X..X.x......" }, { "H", "...X.....X.." }, { "K", "o..o........" }, { "S", "......XxxXxx" } }),
               pattern (12, { { "D", "X..X.xX..X.x" }, { "H", "...X.....X.." }, { "K", "o..o..o..o.." }, { "S", "..g..g..g..x" } }),
               pattern (12, { { "D", "X..X.xX..X.x" }, { "H", "...X.....X.." }, { "K", "o..o..o..o.." }, { "S", ".....g..g..x" } })),

        style ("Waltz 3/4", "华尔兹 3/4", 3, 4, 120, 0,
               pattern (12, { { "K", "X..........." }, { "S", "....x...x..." }, { "H", "x.x.x.x.x.x." } }),
               pattern (12, { { "K", "X..........." }, { "S", "....X...X..." }, { "D", "x.x.x.x.x.x." } }),
               pattern (12, { { "K", "X..........." }, { "S", "....X...XxXx" } }),
               pattern (12, { { "K", "X..........." }, { "S", "....X...X..." }, { "H", "x.x.x.x.x.x." } }),
               pattern (12, { { "K", "X..........." }, { "S", "....X...XxXx" }, { "D", "x.x.x.x.x.x." } })),

        style ("Click only", "仅节拍器", 4, 4, 100, 0,
               pattern (16, {}), pattern (16, {}), pattern (16, {}),
               DrumPattern {}, DrumPattern {}, true),

        // ---- Blues / Mayer pack -------------------------------------------------
        // 12/8 slow blues: ride on every triplet, kick 1 & 3, backbeat 2 & 4.
        style ("Slow Blues", "慢蓝调", 4, 3, 60, 0,
               pattern (12, { { "H", "X.xX.xX.xX.x" }, { "K", "X.....X....." }, { "S", "...X.....X.." } }),
               pattern (12, { { "D", "X.xX.xX.xX.x" }, { "K", "X..X..X..X.." }, { "S", "...X.....X.." } }),
               pattern (12, { { "H", "X.x........." }, { "K", "X..........." }, { "S", "...X..XxxXxx" },
                              { "T1", "......XX...." }, { "T2", "........XX.." }, { "T3", "..........XX" } }),
               pattern (12, { { "D", "X.xX.xX.xX.x" }, { "K", "X..X..X....." }, { "S", "...X..g..X.." } }),
               pattern (12, { { "D", "X.xX.xX.xX.x" }, { "K", "X..X..X..X.." }, { "S", "...X..g..X.." },
                              { "O", "...........X" } })),

        // Slow-soul backbeat in the style of a Mayer ballad: sparse kick, lazy
        // backbeat and ghosted snare, eighth-note hats.
        style ("Mayer Soul", "Mayer 灵魂", 4, 4, 68, 0,
               pattern (16, { { "H", "x...x...x...x..." }, { "K", "X.....X...X....." }, { "S", "....X.......X..." } }),
               pattern (16, { { "H", "x.x.x.x.x.x.x.x." }, { "O", "..x...x...x...x." },
                              { "K", "X...X...X...X..." }, { "S", "....X.......X..." } }),
               pattern (16, { { "H", "x.x.x.x........." }, { "K", "X..............." },
                              { "S", "....X...XxXx...." }, { "T1", "..........Xx...." },
                              { "T2", "............Xx.." }, { "T3", "..............XX" } }),
               pattern (16, { { "H", "x...x...x...x..." }, { "K", "X.....X...X...x." }, { "S", "....X..g....X..g" } }),
               pattern (16, { { "H", "x.x.x.x.x.x.x.x." }, { "O", "..x...x...x...x." },
                              { "K", "X...X...X.X.X..." }, { "S", "....X.......X..g" } })),

        // Medium blues shuffle for the trio material: crisp backbeat, kick
        // answers the vocal, no crash-riding.
        style ("Mayer Shuffle", "Mayer 蓝调 Shuffle", 4, 3, 104, 0,
               pattern (12, { { "H", "X.xX.xX.xX.x" }, { "K", "X.....X....." }, { "S", "...X.....X.." } }),
               pattern (12, { { "D", "X.xX.xX.xX.x" }, { "K", "X..X..X..X.." }, { "S", "...X.....X.." } }),
               pattern (12, { { "D", "X.x........." }, { "K", "X..........." }, { "S", "...X..XxxXxx" },
                              { "T1", "......XX...." }, { "T2", "........XX.." }, { "T3", "..........XX" } }),
               pattern (12, { { "D", "X.xX.xX.xX.x" }, { "K", "X..X..X....." }, { "S", "...X..g..X.." } }),
               pattern (12, { { "D", "X.xX.xX.xX.x" }, { "K", "X...X..X.X.." }, { "S", "...X..g..X.." } })),

        // Texas shuffle: the same triplet feel but with a busier kick and
        // snare chatter, for SRV-style shuffles.
        style ("Texas Shuffle", "德州 Shuffle", 4, 3, 120, 0,
               pattern (12, { { "H", "X.xX.xX.xX.x" }, { "K", "X...X..X.X.." }, { "S", "...X.....X.." } }),
               pattern (12, { { "D", "X.xX.xX.xX.x" }, { "K", "X...X..X.X.." }, { "S", "...X..g..X.." } }),
               pattern (12, { { "H", "X.x........." }, { "K", "X..........." }, { "S", "...X..XxxXxx" },
                              { "T1", "......XX...." }, { "T2", "........XX.." }, { "T3", "..........XX" } }),
               pattern (12, { { "D", "X.xX.xX.xX.x" }, { "K", "X..X..X.X..." }, { "S", "...X.....X.." } }),
               pattern (12, { { "D", "X.xX.xX.xX.x" }, { "K", "X..X.X..X.X." }, { "S", "...X.g...X.." } })),

        // Straight-eighth blues-rock groove, lightly swung.
        style ("Blues Rock", "蓝调摇滚", 4, 4, 100, 0.15f,
               pattern (16, { { "H", "x.x.x.x.x.x.x.x." }, { "K", "X.....X.X......." }, { "S", "....X.......X..." } }),
               pattern (16, { { "D", "X.x.X.x.X.x.X.x." }, { "K", "X.....X.X.X....." }, { "S", "....X.......X..." } }),
               pattern (16, { { "H", "x.x.x.x........." }, { "K", "X.....X........." }, { "S", "....X...XxXx...." },
                              { "T1", "..........Xx...." }, { "T2", "............Xx.." }, { "T3", "..............XX" } }),
               pattern (16, { { "H", "x.x.x.x.x.x.x.x." }, { "K", "X.....X.X.....x." }, { "S", "....X.......X..g" } }),
               pattern (16, { { "D", "X.x.X.x.X.x.X.x." }, { "K", "X...X...X.X.X..." }, { "S", "....X.......X..g" } })),
            };

            // The editable "Custom" style starts as a copy of Mayer Soul.
            DrumStyle custom = v[(size_t) 13];
            custom.name = TR ("Custom", "自定义");
            custom.defaultBpm = 80.0f;
            custom.defaultSwing = 0.0f;
            v.push_back (custom);
            return v;
        }();
        return styles;
    }
}

const std::vector<DrumStyle>& getDrumStyles()
{
    return drumStyleStorage();
}

DrumStyle& getCustomDrumStyle()
{
    return drumStyleStorage().back();
}

int getCustomDrumStyleIndex()
{
    return (int) drumStyleStorage().size() - 1;
}

// =============================================================================
// Drum voice synthesis
void DrumVoice::setup (DrumInstrument t, double sampleRate, float pan)
{
    type = t;
    sr = sampleRate;

    const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    gainL = std::cos (angle);
    gainR = std::sin (angle);

    seed = 12345u + (uint32_t) t * 7919u;
    releaseDecay = decayFactor (0.012);
    active = false;
    release = false;

    phase1 = phase2 = 0.0;
    pitchEnv1 = pitchEnv2 = 0.0f;
    f1Start = f1End = f2Start = f2End = 0.0;
    pitchDecay1 = pitchDecay2 = 0.99f;
    tone1Amp = tone1Level = 0.0f; tone1Decay = 0.999f;
    tone2Amp = tone2Level = 0.0f; tone2Decay = 0.999f;
    satDrive = 0.0f;
    clickAmp = clickLevel = 0.0f; clickDecay = 0.999f;
    noiseAmp = noiseLevel = 0.0f; noiseDecay = 0.999f;
    metallic = false;
    metalLevel = 0.0f;
    for (auto& p : metalPhase) p = 0.0;
    for (auto& f : metalFreq) f = 0.0;
    filterA.reset(); filterB.reset(); filterC.reset();

    auto setMetal = [this] (double base, float level)
    {
        static const double ratios[6] = { 1.0, 1.304, 1.466, 1.787, 1.932, 2.536 };
        for (int i = 0; i < 6; ++i)
            metalFreq[i] = base * ratios[i];
        metallic = true;
        metalLevel = level;
    };

    switch (type)
    {
        case kick:
            f1Start = 150; f1End = 45;  pitchDecay1 = decayFactor (0.035);
            tone1Level = 0.95f; tone1Decay = decayFactor (0.30);
            f2Start = 70;  f2End = 43;  pitchDecay2 = decayFactor (0.06);
            tone2Level = 0.55f; tone2Decay = decayFactor (0.45);
            clickLevel = 0.45f; clickDecay = decayFactor (0.004);
            noiseLevel = 0.12f; noiseDecay = decayFactor (0.008);
            filterC.setHighpass (sr, 1500.0);
            filterA.setBandpass (sr, 2500.0, 0.8);
            filterB.setLowpass (sr, 8000.0);
            satDrive = 1.6f;
            break;

        case snare:
            f1Start = 240; f1End = 185; pitchDecay1 = decayFactor (0.02);
            tone1Level = 0.55f; tone1Decay = decayFactor (0.09);
            f2Start = 330; f2End = 300; pitchDecay2 = decayFactor (0.02);
            tone2Level = 0.35f; tone2Decay = decayFactor (0.06);
            clickLevel = 0.20f; clickDecay = decayFactor (0.002);
            noiseLevel = 0.75f; noiseDecay = decayFactor (0.15);
            filterC.setHighpass (sr, 4000.0);
            filterA.setBandpass (sr, 1800.0, 0.7);
            filterB.setLowpass (sr, 9500.0);
            satDrive = 1.2f;
            break;

        case sideStick:
            f1Start = 900; f1End = 820; pitchDecay1 = decayFactor (0.01);
            tone1Level = 0.5f; tone1Decay = decayFactor (0.025);
            clickLevel = 0.15f; clickDecay = decayFactor (0.002);
            noiseLevel = 0.6f; noiseDecay = decayFactor (0.012);
            filterC.setHighpass (sr, 3000.0);
            filterA.setBandpass (sr, 2600.0, 2.0);
            filterB.setLowpass (sr, 9000.0);
            break;

        case closedHat:
            setMetal (410.0, 0.60f);
            noiseLevel = 0.50f; noiseDecay = decayFactor (0.045);
            filterA.setHighpass (sr, 7500.0);
            filterB.setLowpass (sr, 15000.0);
            break;

        case openHat:
            setMetal (410.0, 0.55f);
            noiseLevel = 0.45f; noiseDecay = decayFactor (0.32);
            filterA.setHighpass (sr, 7000.0);
            filterB.setLowpass (sr, 15000.0);
            break;

        case crash:
            setMetal (330.0, 0.50f);
            noiseLevel = 0.50f; noiseDecay = decayFactor (1.3);
            filterA.setHighpass (sr, 4200.0);
            filterB.setLowpass (sr, 13000.0);
            break;

        case ride:
            setMetal (520.0, 0.40f);
            f1Start = f1End = 2350.0; pitchDecay1 = 0.0f;
            tone1Level = 0.05f; tone1Decay = decayFactor (0.5);
            noiseLevel = 0.25f; noiseDecay = decayFactor (0.7);
            filterA.setHighpass (sr, 5200.0);
            filterB.setLowpass (sr, 12000.0);
            break;

        case tomHigh:
        case tomMid:
        case tomLow:
        {
            const double f = type == tomHigh ? 205.0 : (type == tomMid ? 150.0 : 105.0);
            f1Start = f * 1.5;  f1End = f;       pitchDecay1 = decayFactor (0.06);
            tone1Level = 0.90f; tone1Decay = decayFactor (0.35);
            f2Start = f * 1.98; f2End = f * 1.2; pitchDecay2 = decayFactor (0.08);
            tone2Level = 0.35f; tone2Decay = decayFactor (0.20);
            noiseLevel = 0.15f; noiseDecay = decayFactor (0.02);
            filterA.setHighpass (sr, 400.0);
            filterB.setLowpass (sr, 3500.0);
            satDrive = 1.2f;
            break;
        }

        case clickHigh:
        case clickLow:
            f1Start = f1End = (type == clickHigh ? 1760.0 : 1175.0);
            pitchDecay1 = 0.0f;
            tone1Level = type == clickHigh ? 0.55f : 0.42f;
            tone1Decay = decayFactor (0.018);
            break;

        case numDrumInstruments:
        default:
            break;
    }
}

void DrumVoice::trigger (float velocity, int variant) noexcept
{
    const float v = juce::jlimit (0.0f, 1.0f, velocity);
    vel = v;
    active = true;
    release = false;

    phase1 = phase2 = 0.0;
    pitchEnv1 = pitchEnv2 = 1.0f;

    tone1Amp = tone1Level * (0.55f + 0.45f * v);
    tone2Amp = tone2Level * (0.45f + 0.55f * v);
    clickAmp = clickLevel * (0.30f + 0.70f * v);
    noiseAmp = noiseLevel * (0.50f + 0.50f * v);

    detune = 1.0f + (float) (variant - 1) * 0.003f + (v - 0.8f) * 0.01f;
    seed = 12345u + (uint32_t) type * 7919u + (uint32_t) variant * 104729u
                 + (uint32_t) (v * 1000.0f);
}

float DrumVoice::next() noexcept
{
    float out = 0.0f;

    if (tone1Amp > 0.0f)
    {
        const double f = f1End + (f1Start - f1End) * (double) pitchEnv1;
        pitchEnv1 *= pitchDecay1;
        phase1 += f * (double) detune / sr;
        if (phase1 >= 1.0) phase1 -= 1.0;
        out += tone1Amp * (float) std::sin (juce::MathConstants<double>::twoPi * phase1);
        tone1Amp *= release ? releaseDecay : tone1Decay;
        if (tone1Amp < 1.0e-4f) tone1Amp = 0.0f;
    }

    if (tone2Amp > 0.0f)
    {
        const double f = f2End + (f2Start - f2End) * (double) pitchEnv2;
        pitchEnv2 *= pitchDecay2;
        phase2 += f * (double) detune / sr;
        if (phase2 >= 1.0) phase2 -= 1.0;
        out += tone2Amp * (float) std::sin (juce::MathConstants<double>::twoPi * phase2);
        tone2Amp *= release ? releaseDecay : tone2Decay;
        if (tone2Amp < 1.0e-4f) tone2Amp = 0.0f;
    }

    if (clickAmp > 0.0f)
    {
        out += clickAmp * filterC.process (noise(), 0);
        clickAmp *= release ? releaseDecay : clickDecay;
        if (clickAmp < 1.0e-4f) clickAmp = 0.0f;
    }

    if (noiseAmp > 0.0f)
    {
        float src = noise();
        if (metallic)
        {
            float m = 0.0f;
            for (int i = 0; i < 6; ++i)
            {
                metalPhase[i] += metalFreq[i] / sr;
                if (metalPhase[i] >= 1.0) metalPhase[i] -= 1.0;
                m += metalPhase[i] < 0.5 ? 1.0f : -1.0f;
            }
            src = 0.5f * src + metalLevel * (m / 6.0f);
        }
        out += noiseAmp * filterB.process (filterA.process (src, 0), 0);
        noiseAmp *= release ? releaseDecay : noiseDecay;
        if (noiseAmp < 1.0e-4f) noiseAmp = 0.0f;
    }

    if (satDrive > 0.0f)
        out = std::tanh (out * satDrive);

    if (tone1Amp <= 0.0f && tone2Amp <= 0.0f && clickAmp <= 0.0f && noiseAmp <= 0.0f)
        active = false;

    return out * vel;
}

void DrumVoice::render (float* left, float* right, int numSamples) noexcept
{
    for (int i = 0; i < numSamples && active; ++i)
    {
        const float s = next();
        left[i] += s * gainL;
        right[i] += s * gainR;
    }
}

// =============================================================================
// Drum machine
DrumMachine::DrumMachine()
{
    for (auto& g : pieceGain)
        g.store (1.0f);
}

void DrumMachine::prepare (double sr)
{
    sampleRate = sr;
    static const float pans[numDrumInstruments] = { 0.0f, 0.05f, 0.05f, 0.3f, 0.3f, -0.4f, 0.4f, 0.25f, 0.0f, -0.25f, 0.0f, 0.0f };
    for (int i = 0; i < numDrumInstruments; ++i)
        voices[(size_t) i].setup ((DrumInstrument) i, sr, pans[i]);

    gain.reset (sr, 0.05);
    gain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (volumeDb.load()));

    busHpL.setHighpass (sr, 30.0, 0.7);
    busHpR.setHighpass (sr, 30.0, 0.7);
    busHpL.reset();
    busHpR.reset();
}

void DrumMachine::setPlaying (bool shouldPlay)
{
    playing.store (shouldPlay);
}

void DrumMachine::hit (DrumInstrument inst, float velocity, int variant) noexcept
{
    if (inst == closedHat)
        voices[(size_t) openHat].choke();
    voices[(size_t) inst].trigger (velocity * pieceGain[(size_t) inst].load(), variant);
}

double DrumMachine::stepLengthSamples (int stepIndex) const noexcept
{
    const auto& styles = getDrumStyles();
    const auto& st = styles[(size_t) activeStyle];
    const double base = sampleRate * 60.0 / (double) bpm.load() / (double) st.stepsPerBeat;

    if (st.stepsPerBeat != 4)
        return base;

    // Swing: lengthen the first half of each beat and shorten the second.
    const double s = (double) swing.load() / 3.0;
    return (stepIndex % 4) < 2 ? base * (1.0 + s) : base * (1.0 - s);
}

void DrumMachine::startStep() noexcept
{
    const auto& styles = getDrumStyles();
    currentStep.store (step);

    if (step == 0)
    {
        const int wanted = juce::jlimit (0, (int) styles.size() - 1, styleIndex.load());
        if (wanted != activeStyle && countInBeatsLeft == 0)
            activeStyle = wanted;

        const int fe = fillEvery.load();
        const bool requested = fillRequested.exchange (false);
        barIsFill = countInBeatsLeft == 0 && (requested || (fe > 0 && (bar % fe) == fe - 1));
        inFill.store (barIsFill);
    }

    const auto& st = styles[(size_t) activeStyle];
    const int spb = st.stepsPerBeat;
    const bool onBeat = (step % spb) == 0;

    if (onBeat)
        currentBeat.store (step / spb);

    if (countInBeatsLeft > 0)
    {
        inCountIn.store (true);
        if (onBeat)
            hit (step == 0 ? clickHigh : clickLow, 1.0f, 0);
        return;
    }

    inCountIn.store (false);

    if (! st.clickOnly)
    {
        const DrumPattern* p = nullptr;
        if (barIsFill)
            p = &st.fill;
        else if (chorusSection.load())
            p = (hasHits (st.chorusB) && (bar & 1)) ? &st.chorusB : &st.chorus;
        else
            p = (hasHits (st.verseB) && (bar & 1)) ? &st.verseB : &st.verse;

        if (p != nullptr)
        {
            const float hum = humanize.load();
            for (int inst = 0; inst < clickHigh; ++inst)
            {
                float v = p->velocity[(size_t) inst][(size_t) step];
                if (v > 0.0f)
                {
                    if (hum > 0.0f)
                    {
                        const auto h = mixHash ((uint32_t) (bar * 131 + step * 17 + inst * 7));
                        const float jitter = 1.0f + hum * ((float) (h % 200) / 1000.0f - 0.1f);
                        v = juce::jlimit (0.05f, 1.0f, v * jitter);
                    }
                    hit ((DrumInstrument) inst, v, (bar + step + inst) % 3);
                }
            }
        }

        // Crash on the first beat after a fill.
        if (step == 0 && crashNext)
        {
            hit (crash, 0.9f, 0);
            if (p == nullptr || p->velocity[kick][0] <= 0.0f)
                hit (kick, 0.9f, 0);
        }
    }

    if (step == 0)
        crashNext = false;

    if ((metronome.load() || st.clickOnly) && onBeat)
        hit (step == 0 ? clickHigh : clickLow, 0.9f, 0);
}

void DrumMachine::render (float* left, float* right, int numSamples) noexcept
{
    const auto& styles = getDrumStyles();
    const bool wantPlay = playing.load();

    if (wantPlay && ! running)
    {
        running = true;
        activeStyle = juce::jlimit (0, (int) styles.size() - 1, styleIndex.load());
        step = 0;
        bar = 0;
        crashNext = false;
        countInBeatsLeft = countIn.load() ? styles[(size_t) activeStyle].beatsPerBar : 0;
        samplesToNextStep = 0.0;
    }
    else if (! wantPlay && running)
    {
        running = false;
        currentBeat.store (-1);
        currentStep.store (-1);
        inCountIn.store (false);
        inFill.store (false);
    }

    gain.setTargetValue (juce::Decibels::decibelsToGain (volumeDb.load(), -60.0f));

    int pos = 0;
    while (pos < numSamples)
    {
        int chunk = numSamples - pos;

        if (running)
        {
            if (samplesToNextStep <= 0.0)
            {
                startStep();
                samplesToNextStep += stepLengthSamples (step);

                const auto& st = styles[(size_t) activeStyle];
                if (++step >= st.beatsPerBar * st.stepsPerBeat)
                {
                    step = 0;
                    if (countInBeatsLeft > 0)
                        countInBeatsLeft = 0;
                    else
                    {
                        if (barIsFill)
                            crashNext = true;
                        ++bar;
                    }
                }
            }

            chunk = juce::jmin (chunk, juce::jmax (1, (int) std::ceil (samplesToNextStep)));
        }

        float* l = left + pos;
        float* r = right + pos;
        float tmpL[256], tmpR[256];

        for (int done = 0; done < chunk; )
        {
            const int n = juce::jmin (256, chunk - done);
            std::fill (tmpL, tmpL + n, 0.0f);
            std::fill (tmpR, tmpR + n, 0.0f);

            for (auto& v : voices)
                v.render (tmpL, tmpR, n);

            for (int i = 0; i < n; ++i)
            {
                // Gentle bus: remove sub-rumble and glue the kit with soft saturation.
                float sl = busHpL.process (tmpL[i], 0);
                float sr = busHpR.process (tmpR[i], 1);
                sl = std::tanh (sl * 1.2f) * 0.9f;
                sr = std::tanh (sr * 1.2f) * 0.9f;

                const float g = gain.getNextValue();
                l[done + i] += sl * g;
                r[done + i] += sr * g;
            }
            done += n;
        }

        if (running)
            samplesToNextStep -= chunk;
        pos += chunk;
    }
}
