#pragma once
#include <JuceHeader.h>

// A2B_SYNTH=1: hanggenerátor (MIDI-vezérelt instrument) mód; 0: effekt (bemeneti hang követése).
#ifndef A2B_SYNTH
 #define A2B_SYNTH 0
#endif
#include "Dsp.h"
#include "PitchTracker.h"
#include "Fx.h"
#include "PolySynth.h"
#include "PresetGen.h"
#include <array>

// Audio2Bass: a bejövő hangból valós időben hangmagasságot követ (YIN, lásd PitchTracker.h),
// és ebből egy testreszabható basszus/808-szintit vezérel.
class Audio2BassProcessor : public juce::AudioProcessor,
                            private juce::Timer
{
public:
    Audio2BassProcessor();
    ~Audio2BassProcessor() override { stopTimer(); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return A2B_SYNTH ? "Audio2Synth" : "Audio2Bass"; }
    bool acceptsMidi() const override { return A2B_SYNTH != 0; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return A2B_SYNTH ? 3.0 : 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // ---- Preset-kezelés (csak a GUI / üzenet szálról hívható) ----
    struct PresetEntry
    {
        juce::String name, category;
        bool user = false;
        int  factoryIndex = -1;
        juce::File file;
    };
    std::vector<PresetEntry> listPresets() const;
    void loadPreset (const PresetEntry& entry);
    bool saveUserPreset (const juce::String& name, const juce::String& category);
    bool deleteUserPreset (const juce::String& name, const juce::String& category);
    juce::String getCurrentPresetName();
    static juce::StringArray getCategories();
    int countInvalidPresetIds() const;          // ellenőrzés: minden gyári preset létező paramétert állít
    juce::File getUserPresetDir() const { return userPresetDir; }
    void setUserPresetDir (const juce::File& dir) { userPresetDir = dir; }

    // ---- Véletlenszerű preset és morfolás (csak a GUI szálról) ----
    // locked: Osc, Filter+Env, LFO, Distortion, FX, Low End. A zárolt csoport nem változik.
    void randomize (const std::array<bool, 6>& locked, float amount);
    void beginMorph();            // az A és B tároló rögzítése
    void setMorph (float t);      // 0 = A, 1 = B

    // ---- AI presetgenerátor (műfaj-tudatos, szabályalapú; csak a GUI szálról) ----
    static juce::StringArray getGenreNames();
    void generatePreset (int genre, const juce::String& descriptors, float variation, uint32_t seed,
                         const std::array<bool, 6>& locked);
    bool genHistoryStep (int delta);                 // -1 = vissza, +1 = előre a generált presetek között
    int  getGenHistoryCount() const { return (int) genHistory.size(); }
    int  getGenHistoryPos() const { return genPos; }

    // ---- A/B összehasonlítás ----
    int  getActiveSlot() const { return activeSlot; }
    void abSelect (int slot);
    void abCopyToOther();

    // A DSP -> GUI irányú, zármentes információ (csak atomic-ok)
    struct TrackInfo
    {
        std::atomic<float> hz { 0.0f };
        std::atomic<float> confidence { 0.0f };
        std::atomic<float> inDb { -100.0f };
        std::atomic<float> outDb { -100.0f };
        std::atomic<bool>  voiced { false };
    } info;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void processChunk (juce::AudioBuffer<float>& buffer, const float* const* scIn, int scCh,
                       juce::MidiBufferIterator& midiIt, const juce::MidiBufferIterator& midiEnd, int chunkStart);
    struct GenEntry { juce::String name; std::vector<std::pair<juce::String, float>> values; };
    void applyGenEntry (const GenEntry& e);
    void timerCallback() override;
    void requestApply();    // elhalkítás + késleltetett állapot-alkalmazás (kattogásmentes váltás)

    float foldToBassRange (float f, int octave) const;
    float quantizeToScale (float f, int root, int scaleIdx) const;

    static constexpr int kMaxUni = 7;
    static constexpr int kChunk  = 2048;   // ennél nagyobb blokkokat részletekben dolgozunk fel (nincs allokáció)

    double sr = 44100.0;
    int currentProgram = 0;
    int pendingProgram = -1;
    std::unique_ptr<juce::XmlElement> pendingXml;
    juce::ValueTree slot[2];
    int activeSlot = 0;
    juce::File userPresetDir;
    std::vector<std::pair<juce::RangedAudioParameter*, float>> pendingSet;
    juce::String pendingName;
    std::vector<GenEntry> genHistory;
    int genPos = -1;

    // ---- Polifonikus szintetizátor-motor (A2B_SYNTH mód) ----
    a2b::PolySynth synth;
    a2b::SynthParams synthP;
    std::vector<std::pair<juce::RangedAudioParameter*, std::pair<float, float>>> morphSet;
    std::atomic<float> duckTarget { 1.0f };  // presetváltáskor rövid elhalkítás
    float duckGain = 1.0f;

    a2b::PitchTracker tracker;
    float targetLog2F = 5.78f;    // log2(55 Hz)
    float curLog2F    = 5.78f;
    float lastFreq    = 55.0f;
    bool  voiced      = false;
    bool  noteChanged = false;

    // ---- Szintézis állapot ----
    float env = 0.0f, envFast = 0.0f, envSlow = 0.0f;
    int   holdCounter = 0, onsetCooldown = 0;
    bool  wasOpen = false;
    float fenv = 0.0f, penv = 0.0f;
    float lfoPhase = 0.0f, lfoSH = 0.0f;
    float ph1[kMaxUni] {}, ph2[kMaxUni] {};
    float subPhase = 0.0f;
    uint32_t rng = 22222u;

    juce::ADSR adsr;
    juce::dsp::StateVariableTPTFilter<float> filtA, filtB;
    a2b::Biquad shelf[2], peak[2], hpf[2];
    a2b::DcBlock dc[2];
    a2b::Biquad xHp[2][2];                        // mid-réteg felüláteresztő (LR4), csatornánként
    a2b::Biquad monoLp[2][2], monoHp[2][2];       // mono low end crossover (LR4)
    float lastXover = -1.0f, lastMfreq = -1.0f;
    int   noteSamples = 0;
    float lastBoost = -999.f, lastEqF = -999.f, lastEqG = -999.f, lastEqQ = -999.f, lastHp = -999.f;
    float toneState[2] {};
    float crushHold[2] {};
    int   crushCount = 0;

    juce::SmoothedValue<float> mixSm, outSm, o1Sm, o2Sm, subSm, noiseSm, driveSm, widthSm, subDriveSm;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> cutSm;

    // ---- Új modulok ----
    a2b::Vocoder vocoder;
    a2b::MultiBand multiband;
    a2b::TransientShaper transient;
    a2b::TruePeakLimiter tpLimiter;
    a2b::PitchSyncShifter srcShifter;      // az eredeti bemenet hangmagasság-szinkron eltolt másolata
    float srcInHz = 110.0f;
    float h1ph = 0.0f, h2ph = 0.0f;
    float drv1[kMaxUni] {}, drv2[kMaxUni] {}, drvT1[kMaxUni] {}, drvT2[kMaxUni] {};
    float drvSub = 0.0f, drvSubT = 0.0f;
    int   driftCount = 0;
    float hissState = 0.0f;
    float tapeState[2] {};
    float scEnv = 0.0f, scX = 0.0f, scGain = 1.0f, pumpGain = 1.0f;
    double pumpPhase = 0.0;

    juce::dsp::Chorus<float> chorus;
    juce::AudioBuffer<float> wetBuf;

    JUCE_DECLARE_WEAK_REFERENCEABLE (Audio2BassProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Audio2BassProcessor)
};
