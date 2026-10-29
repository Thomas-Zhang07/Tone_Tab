#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <vector>
#include "dsp/Biquad.h"
#include "fx/HostTempo.h"

enum DrumInstrument
{
    kick, snare, sideStick, closedHat, openHat, crash, ride, tomHigh, tomMid, tomLow,
    clickHigh, clickLow,    // metronome / count-in
    numDrumInstruments
};

// One bar of a groove: per-instrument velocity at each step (0 = no hit).
struct DrumPattern
{
    int steps = 16;
    std::array<std::array<float, 16>, numDrumInstruments> velocity {};
};

// A style has a verse, a chorus, a fill, and optional A/B variations that alternate
// every other bar so the groove does not repeat mechanically.
struct DrumStyle
{
    juce::String name;
    int beatsPerBar = 4;
    int stepsPerBeat = 4;        // 4 = sixteenth grid, 3 = triplet/shuffle grid
    float defaultBpm = 100.0f;
    float defaultSwing = 0.0f;   // 0..1
    bool clickOnly = false;
    DrumPattern verse, verseB, chorus, chorusB, fill;
};

const std::vector<DrumStyle>& getDrumStyles();

// The last style in getDrumStyles() is an editable copy of "Mayer Soul".
// The Drums view edits it in place; DrumPanel persists it in settings.
DrumStyle& getCustomDrumStyle();
int getCustomDrumStyleIndex();

// =============================================================================
// Synthesised drum voice (no sample files). Each piece can use two pitched body
// oscillators, a click/beater transient, and a filtered noise layer.
class DrumVoice
{
public:
    void setup (DrumInstrument type, double sampleRate, float pan);
    void trigger (float velocity, int variant = 0) noexcept;
    void choke() noexcept { if (active) release = true; }
    void render (float* left, float* right, int numSamples) noexcept;

private:
    float next() noexcept;
    float noise() noexcept { seed = seed * 1664525u + 1013904223u; return (float) (int32_t) seed * (1.0f / 2147483648.0f); }
    float decayFactor (double seconds) const noexcept { return (float) std::exp (-1.0 / (seconds * sr)); }

    DrumInstrument type = kick;
    double sr = 48000.0;
    float gainL = 0.7f, gainR = 0.7f;
    bool active = false, release = false;

    // Pitched body: two modes with independent pitch envelopes.
    double phase1 = 0.0, phase2 = 0.0;
    double f1Start = 100.0, f1End = 50.0, f2Start = 0.0, f2End = 0.0;
    float pitchEnv1 = 0.0f, pitchEnv2 = 0.0f;
    float pitchDecay1 = 0.99f, pitchDecay2 = 0.99f;
    float tone1Amp = 0.0f, tone1Level = 0.0f, tone1Decay = 0.999f;
    float tone2Amp = 0.0f, tone2Level = 0.0f, tone2Decay = 0.999f;
    float satDrive = 0.0f;

    // Click / beater transient.
    float clickAmp = 0.0f, clickLevel = 0.0f, clickDecay = 0.999f;

    // Noise / metallic layer.
    float noiseAmp = 0.0f, noiseLevel = 0.0f, noiseDecay = 0.999f;
    bool metallic = false;
    double metalPhase[6] {}, metalFreq[6] {};
    float metalLevel = 0.0f;

    float vel = 1.0f, releaseDecay = 0.99f, detune = 1.0f;
    uint32_t seed = 22222u;
    Biquad filterA, filterB, filterC;
};

// =============================================================================
// Drum machine: sample-accurate step sequencer driven from the audio thread.
class DrumMachine
{
public:
    DrumMachine();

    // ---- message thread -----------------------------------------------------
    void setPlaying (bool shouldPlay);
    bool isPlaying() const              { return playing.load(); }
    void setStyle (int index)
    {
        styleIndex.store (index);
        const auto& styles = getDrumStyles();
        if (juce::isPositiveAndBelow (index, (int) styles.size()))
            gridBeatsPerBar.store (styles[(size_t) index].beatsPerBar);
    }
    int  getStyle() const               { return styleIndex.load(); }
    void setChorus (bool c)             { chorusSection.store (c); }
    bool isChorus() const               { return chorusSection.load(); }
    void setBpm (float b)               { bpm.store (juce::jlimit (30.0f, 300.0f, b)); HostTempo::bpm.store (bpm.load()); }
    float getBpm() const                { return bpm.load(); }
    void setSwing (float s)             { swing.store (juce::jlimit (0.0f, 1.0f, s)); }
    float getSwing() const              { return swing.load(); }
    void setFillEvery (int bars)        { fillEvery.store (bars); }
    int  getFillEvery() const           { return fillEvery.load(); }
    void setCountIn (bool c)            { countIn.store (c); }
    bool getCountIn() const             { return countIn.load(); }
    void setMetronome (bool m)          { metronome.store (m); }
    bool getMetronome() const           { return metronome.load(); }
    void setVolumeDb (float db)         { volumeDb.store (db); }
    float getVolumeDb() const           { return volumeDb.load(); }
    void setHumanize (float h)          { humanize.store (juce::jlimit (0.0f, 1.0f, h)); }
    float getHumanize() const           { return humanize.load(); }
    void triggerFillNow()               { fillRequested.store (true); }

    // Per-piece mixer (linear gain, 0..2).
    void setPieceGain (DrumInstrument inst, float gain)
    {
        if (juce::isPositiveAndBelow ((int) inst, (int) numDrumInstruments))
            pieceGain[(size_t) inst].store (juce::jlimit (0.0f, 2.0f, gain));
    }
    float getPieceGain (DrumInstrument inst) const
    {
        return juce::isPositiveAndBelow ((int) inst, (int) numDrumInstruments)
                   ? pieceGain[(size_t) inst].load() : 1.0f;
    }

    // Musical grid (used by the loop station). The phase is the position at the
    // start of the current render block; the loop station reads it after
    // DrumMachine::render, in the same audio callback.
    double getGridBeatsAtBlockStart() const { return gridBeatsAtBlockStart.load (std::memory_order_relaxed); }
    int    getGridBeatsPerBar() const       { return gridBeatsPerBar.load (std::memory_order_relaxed); }

    // UI state.
    int getCurrentBeat() const          { return currentBeat.load(); }
    int getCurrentStep() const          { return currentStep.load(); }
    bool isInCountIn() const            { return inCountIn.load(); }
    bool isInFill() const               { return inFill.load(); }

    // ---- audio thread -------------------------------------------------------
    void prepare (double sampleRate);
    void triggerFromSong (DrumInstrument inst, float velocity) noexcept { hit (inst, velocity, 0); }
    void render (float* left, float* right, int numSamples) noexcept;

private:
    void startStep() noexcept;
    double stepLengthSamples (int stepIndex) const noexcept;
    void hit (DrumInstrument inst, float velocity, int variant) noexcept;

    std::atomic<bool> playing { false }, chorusSection { false }, countIn { true }, metronome { false };
    std::atomic<bool> fillRequested { false }, restartRequested { false };
    std::atomic<int> styleIndex { 0 }, fillEvery { 4 };
    std::atomic<float> bpm { 100.0f }, swing { 0.0f }, volumeDb { -6.0f }, humanize { 0.0f };
    std::atomic<int> currentBeat { -1 }, currentStep { -1 };
    std::atomic<bool> inCountIn { false }, inFill { false };
    std::atomic<double> gridBeatsAtBlockStart { 0.0 };
    std::atomic<int> gridBeatsPerBar { 4 };
    std::array<std::atomic<float>, numDrumInstruments> pieceGain;

    double sampleRate = 48000.0;
    std::array<DrumVoice, numDrumInstruments> voices;
    juce::SmoothedValue<float> gain { 0.5f };
    Biquad busHpL, busHpR;

    // Audio-thread state.
    bool running = false;
    int activeStyle = 0, step = 0, bar = 0, countInBeatsLeft = 0;
    bool barIsFill = false, crashNext = false;
    double samplesToNextStep = 0.0;
    int64_t gridSampleCounter = 0;
};
