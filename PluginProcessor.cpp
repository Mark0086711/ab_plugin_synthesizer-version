#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr float kPi = 3.14159265358979f;

    float polyBlep (float t, float dt)
    {
        if (t < dt)        { t /= dt;             return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }

    // 0 Saw, 1 Pulse, 2 Triangle, 3 Sine, 4 Half-Sine
    float oscSample (int wave, float ph, float dt, float pw)
    {
        switch (wave)
        {
            case 0:  return 2.0f * ph - 1.0f - polyBlep (ph, dt);
            case 1:
            {
                float ph2 = ph - pw; if (ph2 < 0.0f) ph2 += 1.0f;
                return (ph < pw ? 1.0f : -1.0f) + polyBlep (ph, dt) - polyBlep (ph2, dt);
            }
            case 2:  return 4.0f * std::abs (ph - 0.5f) - 1.0f;
            case 3:  return std::sin (2.0f * kPi * ph);
            default: return 2.0f * std::max (0.0f, std::sin (2.0f * kPi * ph)) - 0.6f;
        }
    }

    // Skála maszkok (félhang-fokok a gyökérhez képest)
    const bool kScales[7][12] = {
        { 1,1,1,1,1,1,1,1,1,1,1,1 }, // Chromatic
        { 1,0,1,0,1,1,0,1,0,1,0,1 }, // Major
        { 1,0,1,1,0,1,0,1,1,0,1,0 }, // Natural minor
        { 1,0,1,1,0,1,0,1,0,1,1,0 }, // Dorian
        { 1,1,0,1,0,1,0,1,1,0,1,0 }, // Phrygian
        { 1,0,0,1,0,1,0,1,0,0,1,0 }, // Minor pentatonic
        { 1,0,1,1,0,1,0,1,1,0,0,1 }  // Harmonic minor
    };

    juce::String categoryOf (const juce::String& n)
    {
        if (n.startsWith ("Original")) return "Original";
        static const char* c808[]  = { "Deep 808", "Punchy 808", "Long Glide 808", "Short Kick 808", "Distorted Trap 808", "Saturated 808", "Hard Clip 808", "808 Punch" };
        static const char* sub[]   = { "Init Sub Bass", "Clean Sub", "Smooth Sub", "Wide Chorus Sub" };
        static const char* reese[] = { "Reese Bass", "Dark Reese Bass" };
        static const char* wob[]   = { "Dubstep Wobble", "FM Growl" };
        static const char* mid[]   = { "Wide Mid Bass", "Super Saw Bass", "Dirty Distorted" };
        for (auto* x : c808)  if (n == x) return "808";
        for (auto* x : sub)   if (n == x) return "Sub";
        for (auto* x : reese) if (n == x) return "Reese";
        for (auto* x : wob)   if (n == x) return "Wobble-FM";
        for (auto* x : mid)   if (n == x) return "Mid-Wide";
        return "Pluck-Acid";
    }

    // ---------------- Presetek ----------------
    struct PresetDef
    {
        const char* name;
        std::vector<std::pair<const char*, float>> values;
    };

    const std::vector<PresetDef>& presets()
    {
        static const std::vector<PresetDef> all = {
            { "Init Sub Bass", {} },

            // ---------------- Original: az eredeti hangszín megmarad ----------------
            { "Original Follow",     { {"srclvl",1.f},{"o1lvl",0.f},{"sublvl",0.f},{"follow",1.f},{"cut",12000.f},{"res",0.7f},{"drive",0.f},
                                       {"atk",2.f},{"dec",200.f},{"sus",1.f},{"rel",60.f} } },
            { "Original Octave Down",{ {"srclvl",1.f},{"o1lvl",0.f},{"sublvl",0.f},{"shift",-1.f},{"follow",1.f},{"cut",9000.f},{"res",0.7f},
                                       {"drive",0.1f},{"atk",2.f},{"sus",1.f},{"rel",70.f} } },
            { "Original Two Octaves Down",{ {"srclvl",1.f},{"o1lvl",0.f},{"sublvl",0.f},{"shift",-2.f},{"follow",1.f},{"cut",6000.f},{"res",0.7f},
                                       {"drive",0.1f},{"atk",2.f},{"sus",1.f},{"rel",80.f} } },
            { "Original + Sub",      { {"srclvl",0.9f},{"o1lvl",0.f},{"sublvl",0.6f},{"shift",-1.f},{"follow",1.f},{"cut",9000.f},{"res",0.7f},
                                       {"atk",2.f},{"sus",1.f},{"rel",70.f} } },
            { "Original Formant Saw",{ {"srclvl",0.5f},{"o1lvl",0.6f},{"sublvl",0.3f},{"vocmix",0.7f},{"vocgain",9.f},{"shift",-1.f},{"follow",1.f},
                                       {"cut",5000.f},{"res",1.f},{"atk",3.f},{"sus",1.f},{"rel",80.f} } },
            { "Original Dirty",      { {"srclvl",1.f},{"o1lvl",0.f},{"sublvl",0.2f},{"shift",-1.f},{"follow",1.f},{"dtype",4.f},{"drive",0.5f},
                                       {"cut",3000.f},{"res",1.2f},{"atk",2.f},{"sus",1.f},{"rel",70.f} } },

            // ---------------- Trap / 808 bank ----------------
            { "Clean Sub",        { {"o1lvl",0.f},{"sublvl",1.f},{"subwave",0.f},{"cut",4000.f},{"res",0.8f},{"drive",0.f},{"subdrive",0.f},
                                    {"glide",25.f},{"dec",200.f},{"sus",1.f},{"rel",140.f},{"follow",0.2f} } },
            { "Deep 808",         { {"xon",1.f},{"xover",140.f},{"o1wave",3.f},{"o1lvl",0.15f},{"sublvl",1.f},{"subwave",0.f},
                                    {"penv",7.f},{"pdec",90.f},{"atk",2.f},{"dec",900.f},{"sus",0.f},{"rel",500.f},
                                    {"drive",0.15f},{"subdrive",0.15f},{"glide",40.f},{"gleg",1.f},{"cut",3000.f},{"follow",0.f} } },
            { "Punchy 808",       { {"xon",1.f},{"xover",150.f},{"o1wave",0.f},{"o1lvl",0.2f},{"sublvl",1.f},
                                    {"penv",12.f},{"pdec",55.f},{"atk",1.f},{"dec",650.f},{"sus",0.f},{"rel",300.f},
                                    {"drive",0.3f},{"subdrive",0.2f},{"cut",3500.f},{"res",1.5f},{"glide",20.f},{"gleg",1.f},{"follow",0.f} } },
            { "Long Glide 808",   { {"xon",1.f},{"xover",140.f},{"o1wave",3.f},{"o1lvl",0.12f},{"sublvl",1.f},
                                    {"glide",220.f},{"gleg",0.f},{"retrig",0.f},{"penv",5.f},{"pdec",120.f},
                                    {"dec",1400.f},{"sus",0.2f},{"rel",700.f},{"drive",0.2f},{"subdrive",0.2f},{"cut",3200.f},{"hold",120.f} } },
            { "Short Kick 808",   { {"o1lvl",0.f},{"sublvl",1.f},{"subwave",0.f},{"penv",24.f},{"pdec",40.f},{"atk",1.f},
                                    {"dec",180.f},{"sus",0.f},{"rel",120.f},{"nlen",220.f},{"drive",0.3f},{"cut",5000.f},{"glide",0.f},{"follow",0.f} } },
            { "Distorted Trap 808",{ {"xon",1.f},{"xover",160.f},{"o1wave",0.f},{"o1lvl",0.45f},{"sublvl",1.f},
                                    {"dpos",0.f},{"dtype",1.f},{"drive",0.65f},{"subdrive",0.35f},{"cut",2800.f},{"res",1.8f},
                                    {"penv",9.f},{"pdec",70.f},{"dec",800.f},{"sus",0.f},{"rel",350.f},{"tone",9000.f},{"follow",0.f} } },
            { "Saturated 808",    { {"xon",1.f},{"xover",150.f},{"o1wave",3.f},{"o1lvl",0.3f},{"sublvl",1.f},
                                    {"dtype",0.f},{"drive",0.6f},{"subdrive",0.5f},{"penv",8.f},{"pdec",80.f},
                                    {"dec",1000.f},{"sus",0.f},{"rel",400.f},{"boost",2.f},{"cut",3500.f},{"follow",0.f} } },
            { "Dark Reese Bass",  { {"xon",1.f},{"xover",90.f},{"o1wave",0.f},{"o1lvl",0.7f},{"o2wave",0.f},{"o2lvl",0.8f},{"o2det",18.f},
                                    {"sublvl",0.45f},{"uni",3.f},{"unid",14.f},{"unis",0.6f},{"cut",380.f},{"res",1.6f},
                                    {"lcut",0.4f},{"lrate",0.3f},{"drive",0.3f},{"chmix",0.35f},{"chdepth",0.5f},
                                    {"mfreq",110.f},{"atk",8.f},{"dec",300.f},{"sus",1.f},{"rel",250.f} } },
            { "Smooth Sub",       { {"o1wave",3.f},{"o1lvl",0.3f},{"sublvl",0.9f},{"subwave",1.f},{"cut",900.f},{"res",0.8f},
                                    {"atk",15.f},{"dec",300.f},{"sus",1.f},{"rel",260.f},{"glide",80.f},{"drive",0.f} } },
            { "Hard Clip 808",    { {"xon",1.f},{"xover",170.f},{"o1wave",1.f},{"o1pw",0.4f},{"o1lvl",0.3f},{"sublvl",1.f},
                                    {"dtype",1.f},{"drive",0.9f},{"subdrive",0.6f},{"tone",7000.f},{"penv",10.f},{"pdec",60.f},
                                    {"dec",700.f},{"sus",0.f},{"rel",280.f},{"cut",4200.f},{"follow",0.f} } },
            { "Wide Mid Bass",    { {"xon",1.f},{"xover",130.f},{"o1wave",0.f},{"o1lvl",0.6f},{"o2wave",1.f},{"o2lvl",0.4f},{"o2det",9.f},
                                    {"sublvl",0.6f},{"uni",7.f},{"unid",30.f},{"unis",0.95f},{"mfreq",130.f},{"cut",1400.f},{"res",1.8f},
                                    {"lshape",1.f},{"lsync",3.f},{"lcut",0.8f},{"chmix",0.4f},{"width",1.3f},{"drive",0.35f},
                                    {"atk",4.f},{"dec",300.f},{"sus",0.9f},{"rel",200.f} } },

            // ---------------- Eredeti presetek ----------------
            { "Reese Bass",       { {"o2lvl",0.7f},{"o2det",14.f},{"sublvl",0.4f},{"cut",500.f},{"res",1.5f},{"drive",0.3f},{"chmix",0.3f} } },
            { "Dubstep Wobble",   { {"lsync",4.f},{"lcut",2.5f},{"cut",350.f},{"res",3.5f},{"o2lvl",0.6f},{"o2wave",1.f},{"o2oct",-1.f},{"drive",0.45f},{"sublvl",0.4f} } },
            { "Acid 303",         { {"sublvl",0.f},{"o1lvl",1.f},{"cut",250.f},{"res",6.5f},{"fenv",3.f},{"fdec",180.f},{"fsus",0.f},{"glide",60.f},{"dec",150.f},{"sus",0.4f},{"drive",0.35f},{"follow",0.f} } },
            { "808 Punch",        { {"o1wave",3.f},{"o1lvl",1.f},{"sublvl",0.6f},{"penv",14.f},{"pdec",70.f},{"cut",3000.f},{"rel",300.f},{"drive",0.25f},{"follow",0.5f} } },
            { "FM Growl",         { {"fm",0.55f},{"o2wave",3.f},{"o2oct",1.f},{"lsync",3.f},{"lshape",1.f},{"lcut",1.5f},{"drive",0.5f},{"dtype",2.f},{"cut",1400.f},{"res",2.f} } },
            { "Super Saw Bass",   { {"uni",7.f},{"unid",28.f},{"unis",0.9f},{"sublvl",0.35f},{"cut",1800.f},{"o1lvl",0.6f},{"chmix",0.35f},{"oct",2.f} } },
            { "Dirty Distorted",  { {"o1wave",1.f},{"o1pw",0.35f},{"drive",0.85f},{"dtype",1.f},{"bits",10.f},{"crate",3.f},{"tone",5000.f},{"cut",1200.f},{"res",2.5f},{"dpos",0.f} } },
            { "Pluck Bass",       { {"o1wave",1.f},{"fenv",3.5f},{"fdec",120.f},{"fsus",0.f},{"dec",200.f},{"sus",0.f},{"rel",80.f},{"cut",200.f},{"res",2.f},{"sublvl",0.4f} } },
            { "Wide Chorus Sub",  { {"o1wave",2.f},{"sublvl",0.6f},{"uni",3.f},{"unid",12.f},{"unis",0.6f},{"chmix",0.5f},{"chdepth",0.5f},{"cut",900.f} } }
        };
        static const std::vector<PresetDef> list = [] (const std::vector<PresetDef>& src)
        {
            std::vector<PresetDef> out;
            for (auto& d : src)
                if (! (A2B_SYNTH && juce::String (d.name).startsWith ("Original")))
                    out.push_back (d);
            return out;
        } (all);
        return list;
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout Audio2BassProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto F = [&p] (const char* id, const char* name, float lo, float hi, float def, float skew = 1.0f)
    {
        p.push_back (std::make_unique<AudioParameterFloat> (id, name, NormalisableRange<float> (lo, hi, 0.0f, skew), def));
    };
    auto I = [&p] (const char* id, const char* name, int lo, int hi, int def)
    {
        p.push_back (std::make_unique<AudioParameterInt> (id, name, lo, hi, def));
    };
    auto C = [&p] (const char* id, const char* name, StringArray items, int def)
    {
        p.push_back (std::make_unique<AudioParameterChoice> (id, name, items, def));
    };
    auto B = [&p] (const char* id, const char* name, bool def)
    {
        p.push_back (std::make_unique<AudioParameterBool> (id, name, def));
    };

    const StringArray waves { "Saw", "Pulse", "Triangle", "Sine", "Half-Sine" };

    // --- Makró ---
    F ("char",  "808 Character",  0.f, 1.f, 0.f);

    // --- Tracking ---
    C ("oct",   "Octave Range",   { "E1 (41 Hz)", "E2 (82 Hz)", "E3 (165 Hz)" }, 1);
    B ("fold",  "Octave Fold",    false);
    I ("shift", "Octave Shift",   -3, 3, 0);
    I ("trans", "Transpose (st)", -24, 24, 0);
    F ("fine",  "Fine (cents)",   -100.f, 100.f, 0.f);
    F ("ytr",   "Track Thresh",   0.05f, 0.40f, 0.15f);
    F ("pmin",  "Min Pitch (Hz)", 25.f, 200.f, 40.f, 0.5f);
    F ("pmax",  "Max Pitch (Hz)", 200.f, 2000.f, 1000.f, 0.5f);
    F ("conf",  "Min Confidence", 0.30f, 0.95f, 0.70f);
    F ("ocheck","Octave Check",   0.02f, 0.30f, 0.08f);
    F ("hyst",  "Note Hysteresis",10.f, 200.f, 60.f);
    I ("stab",  "Note Confirm",   1, 4, 2);
    F ("psmooth","Pitch Smooth",  0.f, 1.f, 0.3f);
    C ("tspeed","Track Speed",    { "Fast", "Normal", "Smooth" }, 0);
    B ("quant", "Quantize",       false);
    C ("root",  "Root Note",      { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }, 9);
    C ("scale", "Scale",          { "Chromatic","Major","Minor","Dorian","Phrygian","Minor Pent.","Harm. Minor" }, 2);
    F ("glide", "Glide (ms)",     0.f, 500.f, 30.f, 0.5f);
    F ("thresh","Gate Thr (dB)",  -70.f, -10.f, -45.f);
    F ("hold",  "Gate Hold (ms)", 0.f, 400.f, 60.f);

    // --- Osc 1 ---
    C ("o1wave","O1 Wave",        waves, 0);
    F ("o1lvl", "O1 Level",       0.f, 1.f, 0.7f);
    I ("o1oct", "O1 Octave",      -2, 2, 0);
    I ("o1semi","O1 Semitone",    -12, 12, 0);
    F ("o1fine","O1 Fine (ct)",   -100.f, 100.f, 0.f);
    F ("o1pw",  "O1 Pulse Width", 0.05f, 0.95f, 0.5f);

    // --- Osc 2 ---
    C ("o2wave","O2 Wave",        waves, 0);
    F ("o2lvl", "O2 Level",       0.f, 1.f, 0.f);
    I ("o2oct", "O2 Octave",      -2, 2, 0);
    I ("o2semi","O2 Semitone",    -12, 12, 0);
    F ("o2det", "O2 Detune (ct)", -100.f, 100.f, 12.f);
    F ("o2pw",  "O2 Pulse Width", 0.05f, 0.95f, 0.5f);
    F ("fm",    "FM (O2 > O1)",   0.f, 1.f, 0.f);

    // --- Unison / Sub / Noise ---
    I ("uni",   "Unison Voices",  1, 7, 1);
    F ("unid",  "Unison Detune",  0.f, 60.f, 15.f);
    F ("unis",  "Unison Spread",  0.f, 1.f, 0.5f);
    F ("sublvl","Sub Level",      0.f, 1.f, 0.5f);
    C ("subwave","Sub Wave",      { "Sine", "Triangle", "Square" }, 0);
    C ("suboct","Sub Octave",     { "-1 Oct", "-2 Oct" }, 0);
    F ("noise", "Noise",          0.f, 1.f, 0.f);
    F ("subdrive","Sub Drive",    0.f, 1.f, 0.f);
    B ("xon",   "Layer Split",    false);
    F ("xover", "Crossover (Hz)", 40.f, 400.f, 130.f, 0.5f);
    B ("monosub","Mono Low End",  true);
    F ("mfreq", "Mono Below (Hz)",40.f, 300.f, 120.f, 0.5f);

    // --- Filter ---
    C ("ftype", "Filter Type",    { "Lowpass", "Bandpass", "Highpass" }, 0);
    B ("f24",   "Filter 24 dB",   true);
    F ("cut",   "Cutoff (Hz)",    20.f, 16000.f, 600.f, 0.3f);
    F ("res",   "Resonance",      0.5f, 10.f, 1.2f);
    F ("fkey",  "Key Track",      0.f, 1.f, 0.f);
    F ("fenv",  "Filt Env Amt",   -5.f, 5.f, 0.f);
    F ("fdec",  "Filt Env Decay", 5.f, 2000.f, 250.f, 0.5f);
    F ("fsus",  "Filt Env Sus",   0.f, 1.f, 0.f);
    F ("fin",   "Input Env > Cut",0.f, 1.f, 0.f);

    // --- Amp envelope ---
    F ("atk",   "Attack (ms)",    1.f, 2000.f, 3.f, 0.4f);
    F ("dec",   "Decay (ms)",     5.f, 3000.f, 200.f, 0.4f);
    F ("sus",   "Sustain",        0.f, 1.f, 1.f);
    F ("rel",   "Release (ms)",   5.f, 3000.f, 120.f, 0.4f);
    F ("follow","Dynamics Follow",0.f, 1.f, 0.3f);
    B ("keysync","Key Sync",      false);
    B ("retrig","Retrig on Pitch",true);
    B ("onset", "Retrig on Hit",  false);
    F ("nlen",  "Note Length (ms)",0.f, 2000.f, 0.f, 0.5f);
    B ("gleg",  "Glide Legato Only",false);

    // --- Pitch envelope ---
    F ("penv",  "Pitch Env (st)", -48.f, 48.f, 0.f);
    F ("pdec",  "Pitch Env Dec",  5.f, 1000.f, 80.f, 0.5f);

    // --- LFO ---
    C ("lshape","LFO Shape",      { "Sine", "Triangle", "Saw", "Square", "S&H" }, 0);
    C ("lsync", "LFO Sync",       { "Free", "1/1", "1/2", "1/4", "1/8", "1/16" }, 0);
    F ("lrate", "LFO Rate (Hz)",  0.05f, 30.f, 2.f, 0.4f);
    F ("lpitch","LFO > Pitch (ct)",0.f, 200.f, 0.f);
    F ("lcut",  "LFO > Cutoff",   0.f, 5.f, 0.f);
    F ("lamp",  "LFO > Amp",      0.f, 1.f, 0.f);
    F ("lpw",   "LFO > PW",       0.f, 1.f, 0.f);

    // --- Distortion ---
    C ("dpos",  "Dist Position",  { "Pre-Filter", "Post-Filter" }, 1);
    C ("dtype", "Dist Type",      { "Soft", "Hard", "Fold", "Asym", "Tube", "Tape", "Diode" }, 0);
    F ("drive", "Drive",          0.f, 1.f, 0.25f);
    I ("bits",  "Bit Depth",      4, 16, 16);
    I ("crate", "Rate Reduce",    1, 32, 1);
    F ("tone",  "Tone (Hz)",      200.f, 20000.f, 20000.f, 0.4f);

    // --- FX ---
    F ("chmix", "Chorus Mix",     0.f, 1.f, 0.f);
    F ("chrate","Chorus Rate",    0.1f, 5.f, 0.8f);
    F ("chdepth","Chorus Depth",  0.f, 1.f, 0.4f);
    F ("width", "Stereo Width",   0.f, 2.f, 1.f);
    F ("boost", "Bass Boost (dB)",-12.f, 12.f, 0.f);
    F ("eqf",   "EQ Freq (Hz)",   50.f, 5000.f, 800.f, 0.4f);
    F ("eqg",   "EQ Gain (dB)",   -12.f, 12.f, 0.f);
    F ("eqq",   "EQ Q",           0.3f, 5.f, 1.f);
    F ("hp",    "Low Cut (Hz)",   20.f, 300.f, 20.f, 0.5f);

    // --- Harmónia / analóg hangulat ---
    const StringArray harm { "Off", "Oct -1", "Oct +1", "Fifth +7", "Fourth -5", "Fifth -7", "Min 3rd +3", "Maj 3rd +4", "Fourth +5" };
    C ("h1int", "Harmony 1",       harm, 0);
    F ("h1lvl", "Harmony 1 Level", 0.f, 1.f, 0.5f);
    C ("h2int", "Harmony 2",       harm, 0);
    F ("h2lvl", "Harmony 2 Level", 0.f, 1.f, 0.5f);
    F ("drift", "Analog Drift",    0.f, 1.f, 0.f);
    B ("phrand","Random Phase",    false);
    F ("hiss",  "Analog Hiss",     0.f, 1.f, 0.f);

    // --- Eredeti réteg: a bemenet saját jele, hangmagasság-szinkron eltolva (az eredeti hangszín megmarad) ---
    F ("srclvl","Original Layer",  0.f, 1.f, 0.f);
    F ("srcdly","Original Delay ms",40.f, 200.f, 65.f);
    I ("srcsemi","Original Transpose",-24, 24, 0);

    // --- Vocoder / timbre-átvitel ---
    F ("vocmix","Vocoder Mix",     0.f, 1.f, 0.f);
    F ("vocq",  "Vocoder Q",       2.f, 12.f, 5.f);
    F ("vocrel","Vocoder Release", 5.f, 200.f, 30.f, 0.5f);
    F ("vocgain","Vocoder Gain dB",-12.f, 24.f, 6.f);

    // --- Sidechain és pump ---
    F ("scamt", "Sidechain Amount",0.f, 1.f, 0.f);
    F ("scthr", "Sidechain Thr dB",-60.f, 0.f, -30.f);
    F ("scatk", "SC Attack (ms)",  0.1f, 50.f, 2.f, 0.5f);
    F ("screl", "SC Release (ms)", 10.f, 800.f, 120.f, 0.5f);
    F ("pump",  "Pump Depth",      0.f, 1.f, 0.f);
    C ("pumprate","Pump Rate",     { "1/1", "1/2", "1/4", "1/8" }, 2);
    F ("pumprel","Pump Release",   0.1f, 1.f, 0.6f);

    // --- Többsávos telítés / OTT ---
    B ("mbon",  "Multiband On",    false);
    F ("mbx1",  "MB Crossover 1",  80.f, 400.f, 150.f, 0.5f);
    F ("mbx2",  "MB Crossover 2",  500.f, 4000.f, 1500.f, 0.5f);
    F ("mbd1",  "MB Drive Low",    0.f, 1.f, 0.f);
    F ("mbd2",  "MB Drive Mid",    0.f, 1.f, 0.f);
    F ("mbd3",  "MB Drive High",   0.f, 1.f, 0.f);
    F ("ott",   "OTT Depth",       0.f, 1.f, 0.f);
    F ("ottrel","OTT Release",     20.f, 500.f, 120.f, 0.5f);

    // --- Átmenet-formázó ---
    F ("tatk",  "Transient Attack",-1.f, 1.f, 0.f);
    F ("tsus",  "Transient Sustain",-1.f, 1.f, 0.f);

    // --- Csúcsvédelem ---
    B ("tplim", "True-Peak Limiter",true);
    F ("ceil",  "Limiter Ceiling", -6.f, 0.f, -0.3f);
    F ("lrel",  "Limiter Release", 20.f, 500.f, 80.f, 0.5f);

    // --- MIDI / velocity (szintetizátor mód) ---
    I ("bend",  "Pitch Bend Range", 0, 24, 2);
    F ("velamp","Velocity > Amp",  0.f, 1.f, 0.7f);
    F ("velcut","Velocity > Cutoff",0.f, 1.f, 0.f);
    F ("velpenv","Velocity > Drop",0.f, 1.f, 0.f);

    // --- Output ---
    F ("mix",   "Mix",            0.f, 1.f, 1.f);
    F ("outgain","Output (dB)",   -24.f, 6.f, -3.f);
    B ("limit", "Soft Limiter",   true);

    return { p.begin(), p.end() };
}

Audio2BassProcessor::Audio2BassProcessor()
#if A2B_SYNTH
    : AudioProcessor (BusesProperties()
                        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
#else
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
#endif
      apvts (*this, nullptr, "STATE", createLayout())
{
    userPresetDir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                        .getChildFile ("Audio2Bass").getChildFile ("Presets");
}

bool Audio2BassProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
#if A2B_SYNTH
    const auto out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    if (l.inputBuses.size() > 0)
    {
        const auto sc = l.getChannelSet (true, 0);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
#endif
    if (l.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    if (l.getMainOutputChannelSet() != l.getMainInputChannelSet())
        return false;
    if (l.inputBuses.size() > 1)
    {
        const auto sc = l.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

//==============================================================================
int Audio2BassProcessor::getNumPrograms() { return (int) presets().size(); }

const juce::String Audio2BassProcessor::getProgramName (int index)
{
    if (index >= 0 && index < (int) presets().size())
        return presets()[(size_t) index].name;
    return {};
}

void Audio2BassProcessor::requestApply()
{
    // Kattogásmentes váltás: az audio szál gyorsan elhalkít, 20 ms múlva a GUI szálon
    // alkalmazzuk az új állapotot, majd visszahalkítjuk a hangot.
    duckTarget.store (0.0f);
    juce::WeakReference<Audio2BassProcessor> weak (this);
    auto start = [weak] { if (auto* self = weak.get()) self->startTimer (20); };
    if (juce::MessageManager::getInstance()->isThisTheMessageThread()) start();
    else juce::MessageManager::callAsync (start);
}

void Audio2BassProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= (int) presets().size()) return;
    currentProgram = index;
    pendingProgram = index;
    pendingXml.reset();
    requestApply();
}

void Audio2BassProcessor::timerCallback()
{
    stopTimer();

    if (! pendingSet.empty())
    {
        for (auto& kv : pendingSet) kv.first->setValueNotifyingHost (kv.second);
        pendingSet.clear();
        if (pendingName.isNotEmpty())
        {
            apvts.state.setProperty ("presetName", pendingName, nullptr);
            apvts.state.setProperty ("presetCat", "User", nullptr);
            pendingName = {};
        }
        duckTarget.store (1.0f);
        return;
    }

    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            r->setValueNotifyingHost (r->getDefaultValue());

    if (pendingXml != nullptr)
    {
        auto vt = juce::ValueTree::fromXml (*pendingXml);
        pendingXml.reset();
        if (vt.isValid() && vt.hasType (apvts.state.getType()))
            apvts.replaceState (vt);
    }
    else if (pendingProgram >= 0 && pendingProgram < (int) presets().size())
    {
        const auto& def = presets()[(size_t) pendingProgram];

        // A régi (808/basszus) presetek megtartják az oktávba hajtogatást és a skálára kvantálást;
        // az "Init" és az "Original" presetek pontosan követik az eredeti hangmagasságot.
        const juce::String nm (def.name);
        if (! A2B_SYNTH && ! nm.startsWith ("Original") && nm != "Init Sub Bass")
        {
            if (auto* r = apvts.getParameter ("fold"))  r->setValueNotifyingHost (1.0f);
            if (auto* r = apvts.getParameter ("quant")) r->setValueNotifyingHost (1.0f);
        }

        for (auto& kv : def.values)
            if (auto* r = apvts.getParameter (kv.first))
                r->setValueNotifyingHost (r->convertTo0to1 (kv.second));
        apvts.state.setProperty ("presetName", def.name, nullptr);
        apvts.state.setProperty ("presetCat", categoryOf (def.name), nullptr);
    }

    pendingProgram = -1;
    duckTarget.store (1.0f);
}

juce::StringArray Audio2BassProcessor::getCategories()
{
    return { "808", "Sub", "Reese", "Wobble-FM", "Mid-Wide", "Pluck-Acid", "Original", "User" };
}

juce::String Audio2BassProcessor::getCurrentPresetName()
{
    const auto n = apvts.state.getProperty ("presetName").toString();
    return n.isEmpty() ? juce::String ("Init Sub Bass") : n;
}

std::vector<Audio2BassProcessor::PresetEntry> Audio2BassProcessor::listPresets() const
{
    std::vector<PresetEntry> out;
    for (int i = 0; i < (int) presets().size(); ++i)
    {
        PresetEntry e;
        e.name = presets()[(size_t) i].name;
        e.category = categoryOf (e.name);
        e.factoryIndex = i;
        out.push_back (e);
    }

    auto files = userPresetDir.findChildFiles (juce::File::findFiles, true, "*.a2bpreset");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getFullPathName().compareNatural (b.getFullPathName()) < 0;
    });
    for (auto& f : files)
    {
        PresetEntry e;
        e.name = f.getFileNameWithoutExtension();
        e.category = f.getParentDirectory().getFileName();
        e.user = true;
        e.file = f;
        out.push_back (e);
    }
    return out;
}

void Audio2BassProcessor::loadPreset (const PresetEntry& entry)
{
    if (! entry.user)
    {
        setCurrentProgram (entry.factoryIndex);
        return;
    }
    if (auto xml = juce::parseXML (entry.file))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            pendingXml = std::move (xml);
            pendingProgram = -1;
            requestApply();
        }
    }
}

bool Audio2BassProcessor::saveUserPreset (const juce::String& name, const juce::String& category)
{
    if (name.trim().isEmpty()) return false;

    auto state = apvts.copyState();
    state.setProperty ("presetName", name.trim(), nullptr);
    state.setProperty ("presetCat", category, nullptr);
    state.setProperty ("a2bVersion", 2, nullptr);

    auto xml = state.createXml();
    if (xml == nullptr) return false;

    auto dir = userPresetDir.getChildFile (juce::File::createLegalFileName (category));
    if (! dir.createDirectory()) return false;
    auto file = dir.getChildFile (juce::File::createLegalFileName (name.trim()) + ".a2bpreset");
    if (! file.replaceWithText (xml->toString())) return false;

    apvts.state.setProperty ("presetName", name.trim(), nullptr);
    apvts.state.setProperty ("presetCat", category, nullptr);
    return true;
}

bool Audio2BassProcessor::deleteUserPreset (const juce::String& name, const juce::String& category)
{
    auto file = userPresetDir.getChildFile (juce::File::createLegalFileName (category))
                             .getChildFile (juce::File::createLegalFileName (name) + ".a2bpreset");
    return file.existsAsFile() && file.deleteFile();
}

void Audio2BassProcessor::abSelect (int s)
{
    if (s < 0 || s > 1 || s == activeSlot) return;
    slot[activeSlot] = apvts.copyState();
    if (! slot[s].isValid()) slot[s] = apvts.copyState();
    activeSlot = s;
    pendingXml = slot[s].createXml();
    pendingProgram = -1;
    requestApply();
}

void Audio2BassProcessor::abCopyToOther()
{
    slot[1 - activeSlot] = apvts.copyState();
}

namespace
{
    // Véletlenszerűsítési csoportok. -1 = soha nem változik (követés, kapu, kimenet, sidechain, vocoder...).
    int randomGroupOf (const juce::String& id)
    {
        static const char* osc[]  = { "o1wave","o1lvl","o1oct","o1semi","o1fine","o1pw","o2wave","o2lvl","o2oct","o2semi","o2det","o2pw",
                                      "fm","uni","unid","unis","sublvl","subwave","suboct","noise","h1int","h1lvl","h2int","h2lvl","drift","phrand" };
        static const char* flt[]  = { "ftype","f24","cut","res","fkey","fenv","fdec","fsus","fin","atk","dec","sus","rel","follow","penv","pdec" };
        static const char* lfo[]  = { "lshape","lsync","lrate","lpitch","lcut","lamp","lpw" };
        static const char* dist[] = { "dtype","dpos","drive","bits","crate","tone","subdrive","char","mbon","mbd1","mbd2","mbd3","ott" };
        static const char* fx[]   = { "chmix","chrate","chdepth","width","boost","eqf","eqg","eqq","hp","tatk","tsus" };
        static const char* low[]  = { "xon","xover","monosub","mfreq" };
        for (auto* x : osc)  if (id == x) return 0;
        for (auto* x : flt)  if (id == x) return 1;
        for (auto* x : lfo)  if (id == x) return 2;
        for (auto* x : dist) if (id == x) return 3;
        for (auto* x : fx)   if (id == x) return 4;
        for (auto* x : low)  if (id == x) return 5;
        return -1;
    }
}

void Audio2BassProcessor::randomize (const std::array<bool, 6>& locked, float amount)
{
    juce::Random rnd;
    amount = juce::jlimit (0.0f, 1.0f, amount);
    std::vector<std::pair<juce::RangedAudioParameter*, float>> set;

    for (auto* prm : getParameters())
    {
        auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm);
        if (r == nullptr) continue;
        const int grp = randomGroupOf (r->paramID);
        if (grp < 0 || locked[(size_t) grp]) continue;

        float v = rnd.nextFloat();
        const auto& id = r->paramID;
        // Hangzatosság-védelem: szélsőséges értékek kerülése
        if (id == "o1lvl" || id == "o2lvl" || id == "h1lvl" || id == "h2lvl") v = 0.2f + 0.8f * v;
        else if (id == "sublvl")                  v = 0.4f + 0.6f * v;
        else if (id == "noise")                   v *= 0.15f;
        else if (id == "res")                     v *= 0.6f;
        else if (id == "drive" || id == "subdrive") v *= 0.8f;
        else if (id == "bits" && v < 0.8f)        v = 1.0f;       // 16 bit
        else if (id == "crate" && v < 0.8f)       v = 0.0f;       // nincs mintavétel-csökkentés
        else if (id == "chmix")                   v *= 0.6f;
        else if (id == "width")                   v = 0.3f + 0.4f * v;
        else if (id == "boost" || id == "eqg")    v = 0.35f + 0.3f * v;
        else if (id == "hp")                      v *= 0.3f;
        else if (id == "sus" || id == "fsus")     v = 0.3f + 0.7f * v;
        else if (id == "mbd1" || id == "mbd2" || id == "mbd3" || id == "ott") v *= 0.7f;
        else if (id == "drift")                   v *= 0.6f;
        else if (id == "tatk" || id == "tsus")    v = 0.35f + 0.3f * v;

        const float cur = r->getValue();
        float nv;
        if (r->isDiscrete()) nv = (rnd.nextFloat() < amount) ? v : cur;
        else                 nv = cur + (v - cur) * amount;
        set.push_back ({ r, nv });
    }

    // Legalább az egyik réteg hallható legyen
    float* o1 = nullptr; float* sb = nullptr;
    for (auto& kv : set)
    {
        if (kv.first->paramID == "o1lvl")   o1 = &kv.second;
        if (kv.first->paramID == "sublvl")  sb = &kv.second;
    }
    if (sb != nullptr && *sb < 0.15f && (o1 == nullptr || *o1 < 0.15f)) *sb = 0.8f;

    pendingSet = std::move (set);
    pendingName = "Random";
    pendingXml.reset();
    pendingProgram = -1;
    requestApply();
}

//==============================================================================
// AI presetgenerátor
juce::StringArray Audio2BassProcessor::getGenreNames()
{
    juce::StringArray a;
    for (auto& n : a2b::PresetGen::genreNames()) a.add (n);
    return a;
}

void Audio2BassProcessor::applyGenEntry (const GenEntry& e)
{
    std::vector<std::pair<juce::RangedAudioParameter*, float>> set;
    for (auto& kv : e.values)
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (kv.first)))
            set.push_back ({ r, kv.second });

    pendingSet = std::move (set);
    pendingName = e.name;
    pendingXml.reset();
    pendingProgram = -1;
    requestApply();
}

void Audio2BassProcessor::generatePreset (int genre, const juce::String& descriptors, float variation, uint32_t seed,
                                          const std::array<bool, 6>& locked)
{
    const auto names = getGenreNames();
    genre = juce::jlimit (0, juce::jmax (0, names.size() - 1), genre);
    const auto values = a2b::PresetGen::generate (genre, descriptors.toStdString(), variation, seed);

    GenEntry entry;
    entry.name = "AI " + names[genre] + " #" + juce::String ((int) (seed % 100000u));

    for (auto* prm : getParameters())
    {
        auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm);
        if (r == nullptr) continue;
        const int grp = randomGroupOf (r->paramID);
        if (grp >= 0 && locked[(size_t) grp]) continue;                 // zárolt csoport

        const auto it = values.find (r->paramID.toStdString());
        if (it != values.end())  entry.values.push_back ({ r->paramID, r->convertTo0to1 (it->second) });
        else if (grp >= 0)       entry.values.push_back ({ r->paramID, r->getDefaultValue() });   // a nem érintett hangparaméterek alapra
    }

    if (genPos + 1 < (int) genHistory.size()) genHistory.resize ((size_t) genPos + 1);
    genHistory.push_back (entry);
    if (genHistory.size() > 40) genHistory.erase (genHistory.begin());
    genPos = (int) genHistory.size() - 1;
    applyGenEntry (genHistory[(size_t) genPos]);
}

bool Audio2BassProcessor::genHistoryStep (int delta)
{
    const int np = genPos + delta;
    if (np < 0 || np >= (int) genHistory.size()) return false;
    genPos = np;
    applyGenEntry (genHistory[(size_t) genPos]);
    return true;
}

void Audio2BassProcessor::beginMorph()
{
    slot[activeSlot] = apvts.copyState();
    const int other = 1 - activeSlot;
    if (! slot[other].isValid()) slot[other] = apvts.copyState();

    auto norm = [] (const juce::ValueTree& vt, juce::RangedAudioParameter* p, float fallback)
    {
        auto child = vt.getChildWithProperty ("id", p->paramID);
        if (child.isValid() && child.hasProperty ("value"))
            return p->convertTo0to1 ((float) child.getProperty ("value"));
        return fallback;
    };

    morphSet.clear();
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            const float a = norm (slot[0], r, r->getValue());
            const float b = norm (slot[1], r, r->getValue());
            if (std::abs (a - b) > 1.0e-5f) morphSet.push_back ({ r, { a, b } });
        }
}

void Audio2BassProcessor::setMorph (float t)
{
    t = juce::jlimit (0.0f, 1.0f, t);
    for (auto& e : morphSet)
    {
        auto* r = e.first;
        const float a = e.second.first, b = e.second.second;
        const float v = r->isDiscrete() ? (t < 0.5f ? a : b) : a + (b - a) * t;
        if (std::abs (r->getValue() - v) > 1.0e-6f) r->setValueNotifyingHost (v);
    }
}

//==============================================================================
void Audio2BassProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;

    tracker.prepare (sr);
    env = envFast = envSlow = 0.0f;
    holdCounter = onsetCooldown = 0;
    wasOpen = false;
    fenv = penv = 0.0f;
    lfoPhase = 0.0f;
    for (int v = 0; v < kMaxUni; ++v) { ph1[v] = 0.0f; ph2[v] = 0.0f; }
    subPhase = 0.0f;
    voiced = false;
    crushCount = 0;
    for (int c = 0; c < 2; ++c) { toneState[c] = 0.0f; crushHold[c] = 0.0f; shelf[c].reset(); peak[c].reset(); hpf[c].reset(); }
    lastBoost = lastEqF = lastEqG = lastEqQ = lastHp = -999.f;
    lastXover = lastMfreq = -1.0f;
    noteSamples = 0;
    for (auto& a : xHp)     for (auto& b : a) b.reset();
    for (auto& a : monoLp)  for (auto& b : a) b.reset();
    for (auto& a : monoHp)  for (auto& b : a) b.reset();

    juce::ignoreUnused (samplesPerBlock);
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) kChunk, 2 };
    filtA.prepare (spec);
    filtB.prepare (spec);
    filtA.reset();  filtB.reset();
    chorus.prepare (spec);
    chorus.reset();

    vocoder.prepare (sr);
    srcShifter.prepare (sr);
    heldCount = 0; curNote = -1; curVel = 1.0f; bendCur = bendTarget = 0.0f; midiNewNote = false;
    srcInHz = 110.0f;
    multiband.prepare (sr);
    transient.prepare (sr);
    tpLimiter.prepare (sr);
    h1ph = h2ph = 0.0f;
    for (int v = 0; v < kMaxUni; ++v) { drv1[v] = drv2[v] = drvT1[v] = drvT2[v] = 0.0f; }
    drvSub = drvSubT = 0.0f; driftCount = 0;
    hissState = 0.0f; tapeState[0] = tapeState[1] = 0.0f;
    scEnv = scX = 0.0f; scGain = pumpGain = 1.0f; pumpPhase = 0.0;

    adsr.setSampleRate (sr);
    adsr.reset();

    wetBuf.setSize (2, kChunk);
    wetBuf.clear();

    for (auto& d : dc) d.prepare (sr, 8.0);
    duckGain = 1.0f;
    for (auto* sm : { &mixSm, &outSm, &o1Sm, &o2Sm, &subSm, &noiseSm, &driveSm, &widthSm, &subDriveSm })
        sm->reset (sr, 0.02);
    cutSm.reset (sr, 0.02);
    cutSm.setCurrentAndTargetValue (600.0f);
}

//==============================================================================
float Audio2BassProcessor::foldToBassRange (float f, int octave) const
{
    const float low = 41.2034f * std::pow (2.0f, (float) octave);
    while (f >= 2.0f * low) f *= 0.5f;
    while (f <  low)        f *= 2.0f;
    return f;
}

float Audio2BassProcessor::quantizeToScale (float f, int root, int scaleIdx) const
{
    const float midi = 69.0f + 12.0f * std::log2 (f / 440.0f);
    const int   n = (int) std::lround (midi);
    float best = (float) n, bestDist = 1.0e9f;

    for (int c = n - 6; c <= n + 6; ++c)
    {
        const int rel = ((c - root) % 12 + 12) % 12;
        if (kScales[scaleIdx][rel])
        {
            const float d = std::abs ((float) c - midi);
            if (d < bestDist) { bestDist = d; best = (float) c; }
        }
    }
    return 440.0f * std::pow (2.0f, (best - 69.0f) / 12.0f);
}

//==============================================================================
void Audio2BassProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    const int mainCh = juce::jmin (buffer.getNumChannels(), getMainBusNumOutputChannels());

    // Opcionális külső bemenet (effekt: a fő csatornák után; szintetizátor: az egyetlen bemeneti sín).
    // Szintetizátornál ez a puffer ugyanazokat a csatornákat használja, mint a kimenet, ezért a
    // feldolgozás a bemenetet olvassa, mielőtt a kimenetet felülírná; engedélyezés nélkül nem olvassuk.
    int scCh = 0;
   #if A2B_SYNTH
    if (getBusCount (true) > 0)
        if (auto* b = getBus (true, 0))
            if (b->isEnabled())
                scCh = juce::jmin (2, juce::jmin (b->getNumberOfChannels(), buffer.getNumChannels()));
    const int scFirst = 0;
   #else
    if (getBusCount (true) > 1)
        if (auto* b = getBus (true, 1))
            if (b->isEnabled())
                scCh = juce::jmin (2, juce::jmin (b->getNumberOfChannels(), buffer.getNumChannels() - mainCh));
    const int scFirst = mainCh;
   #endif

    auto midiIt = midi.begin();
    const auto midiEnd = midi.end();

    for (int pos = 0; pos < total; pos += kChunk)
    {
        const int len = juce::jmin (kChunk, total - pos);
        juce::AudioBuffer<float> chunk (buffer.getArrayOfWritePointers(), mainCh, pos, len);
        const float* sc[2] = { nullptr, nullptr };
        for (int c = 0; c < scCh; ++c) sc[c] = buffer.getReadPointer (scFirst + c, pos);
        processChunk (chunk, sc, scCh, midiIt, midiEnd, pos);
    }
}

void Audio2BassProcessor::processChunk (juce::AudioBuffer<float>& buffer, const float* const* scIn, int scCh,
                                       juce::MidiBufferIterator& midiIt, const juce::MidiBufferIterator& midiEnd, int chunkStart)
{
    juce::ignoreUnused (midiIt, midiEnd, chunkStart);
    const int numCh = buffer.getNumChannels();
    const int n = buffer.getNumSamples();

    auto P = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    // ---- paraméterek ----
    const float bendRange = P ("bend");
    const float velAmt = P ("velamp"), velCut = P ("velcut"), velPenv = P ("velpenv");
    const float bendCoef = 1.0f - std::exp (-1.0f / (0.005f * (float) sr));
    juce::ignoreUnused (bendRange, bendCoef);
    const float charM = P ("char");   // 808 Character makró: több telítés, több felharmonikus, erősebb pitch drop, világosabb hang
    const int   octave  = (int) P ("oct");
    const bool  fold    = P ("fold") > 0.5f;
    const int   shift   = (int) P ("shift");
    const float trans   = P ("trans");
    const float fineC   = P ("fine");
    const float ytr     = P ("ytr");
    const bool  quant   = P ("quant") > 0.5f;
    const int   root    = (int) P ("root");
    const int   scaleI  = (int) P ("scale");
    const float glideMs = P ("glide");
    const float thrLin  = juce::Decibels::decibelsToGain (P ("thresh"));
    const int   holdSamples = (int) (P ("hold") * 0.001f * (float) sr);

    const int   o1wave = (int) P ("o1wave"), o2wave = (int) P ("o2wave");
    const float o1lvl = juce::jlimit (0.0f, 1.0f, P ("o1lvl") + charM * 0.35f), o2lvl = P ("o2lvl");
    const float o1pwP = P ("o1pw"),  o2pwP = P ("o2pw");
    const float fmAmt = P ("fm");
    const float o1ratio = std::exp2 (P ("o1oct") + P ("o1semi") / 12.0f + P ("o1fine") / 1200.0f);
    const float o2ratio = std::exp2 (P ("o2oct") + P ("o2semi") / 12.0f + P ("o2det")  / 1200.0f);

    const int   uniN   = juce::jlimit (1, kMaxUni, (int) P ("uni"));
    const float uniDet = P ("unid"), uniSpr = P ("unis");
    const float subLvl = P ("sublvl");
    const int   subWave = (int) P ("subwave");
    const float subRatio = ((int) P ("suboct") == 0) ? 0.5f : 0.25f;
    const float noiseLvl = P ("noise");
    const float subDrive = juce::jlimit (0.0f, 1.0f, P ("subdrive") + charM * 0.4f);
    const bool  split    = P ("xon") > 0.5f;
    const float xover    = P ("xover");
    const bool  monoSub  = P ("monosub") > 0.5f;
    const float mfreq    = P ("mfreq");

    const int   ft = (int) P ("ftype");
    const bool  f24 = P ("f24") > 0.5f;
    const float cutoff = P ("cut") * std::exp2 (charM * 0.7f), res = P ("res");
    const float fkey = P ("fkey"), fenvAmt = P ("fenv"), fsus = P ("fsus"), fin = P ("fin");
    const float fdecCoef = std::exp (-1.0f / (juce::jmax (1.0f, P ("fdec")) * 0.001f * (float) sr));

    const float follow = P ("follow");
    const bool  keysync = P ("keysync") > 0.5f;
    const bool  retrig  = P ("retrig") > 0.5f;
    const bool  onsetOn = P ("onset") > 0.5f;
    juce::ignoreUnused (onsetOn, holdSamples, octave, fold);
    const int   nlenSamples = (int) (P ("nlen") * 0.001f * (float) sr);
    const bool  gleg    = P ("gleg") > 0.5f;
    const float penvAmt = P ("penv") + charM * 8.0f;
    const float pdecCoef = std::exp (-1.0f / (juce::jmax (1.0f, P ("pdec")) * 0.001f * (float) sr));

    const int   lshape = (int) P ("lshape"), lsync = (int) P ("lsync");
    const float lpitch = P ("lpitch"), lcut = P ("lcut"), lamp = P ("lamp"), lpw = P ("lpw");

    const bool  distPost = (int) P ("dpos") == 1;
    const int   dtype = (int) P ("dtype");
    const float drive = juce::jlimit (0.0f, 1.0f, P ("drive") + charM * 0.45f);
    const int   bitsI = (int) P ("bits");
    const int   crateI = juce::jmax (1, (int) P ("crate"));
    const float toneHz = P ("tone");

    const float chmix = P ("chmix");
    const float width = P ("width");
    const float boost = P ("boost");
    const float eqf = P ("eqf"), eqg = P ("eqg"), eqq = P ("eqq");
    const float hpHz = P ("hp");

    const float mix = P ("mix");
    const float outGain = juce::Decibels::decibelsToGain (P ("outgain"));
    const bool  limiter = P ("limit") > 0.5f;

    // ---- új modulok paraméterei ----
    static const float harmSemi[9] = { 0.0f, -12.0f, 12.0f, 7.0f, -5.0f, -7.0f, 3.0f, 4.0f, 5.0f };
    const int   h1i = juce::jlimit (0, 8, (int) P ("h1int")), h2i = juce::jlimit (0, 8, (int) P ("h2int"));
    const float h1l = P ("h1lvl"), h2l = P ("h2lvl");
    const bool  h1on = h1i > 0 && h1l > 0.001f, h2on = h2i > 0 && h2l > 0.001f;
    const float h1ratio = std::exp2 (harmSemi[h1i] / 12.0f), h2ratio = std::exp2 (harmSemi[h2i] / 12.0f);

    const float srcLvl = A2B_SYNTH ? 0.0f : P ("srclvl");
    srcShifter.setBaseDelayMs (P ("srcdly"));
    const float srcSemiRatio = std::exp2 (P ("srcsemi") / 12.0f);

    const float vocMix = P ("vocmix");
    const float vocGain = juce::Decibels::decibelsToGain (P ("vocgain"));
    if (vocMix > 0.001f) vocoder.setParams (P ("vocq"), P ("vocrel"));

    const float scAmt = P ("scamt"), scThr = P ("scthr");
    const float scAtk = 1.0f - std::exp (-1.0f / (juce::jmax (0.05f, P ("scatk")) * 0.001f * (float) sr));
    const float scRel = 1.0f - std::exp (-1.0f / (juce::jmax (1.0f, P ("screl")) * 0.001f * (float) sr));
    const float scSmooth = 1.0f - std::exp (-1.0f / (0.001f * (float) sr));
    const float pumpAmt = P ("pump"), pumpRel = juce::jmax (0.05f, P ("pumprel"));
    const int   pumpDiv = juce::jlimit (0, 3, (int) P ("pumprate"));

    const float driftAmt = P ("drift");
    const bool  phrand = P ("phrand") > 0.5f;
    const float hissP = P ("hiss");
    const float dk = driftAmt * 0.00693f;                                  // legfeljebb kb. 12 cent
    const float driftCoef = 1.0f - std::exp (-1.0f / (0.6f * (float) sr));
    const float hissAmp = hissP > 0.001f ? std::pow (10.0f, (-72.0f + 34.0f * hissP) / 20.0f) : 0.0f;

    const bool  mbOn = P ("mbon") > 0.5f;
    if (mbOn)
    {
        multiband.setCrossovers (P ("mbx1"), P ("mbx2"));
        multiband.setDrive (P ("mbd1"), P ("mbd2"), P ("mbd3"));
        multiband.setOtt (P ("ott"), P ("ottrel"));
    }
    const float tAtk = P ("tatk"), tSus = P ("tsus");
    const bool  tOn = std::abs (tAtk) > 0.001f || std::abs (tSus) > 0.001f;
    if (tOn) transient.setParams (tAtk, tSus);
    const bool  tpOn = P ("tplim") > 0.5f;
    tpLimiter.setParams (P ("ceil"), P ("lrel"));

    mixSm.setTargetValue (mix);       outSm.setTargetValue (outGain);
    widthSm.setTargetValue (width);   o1Sm.setTargetValue (o1lvl);
    o2Sm.setTargetValue (o2lvl);      subSm.setTargetValue (subLvl);
    noiseSm.setTargetValue (noiseLvl); driveSm.setTargetValue (drive);
    cutSm.setTargetValue (juce::jmax (20.0f, cutoff));
    subDriveSm.setTargetValue (subDrive);

    {
        a2b::PitchTracker::Settings ts;
        static const int hops[3] = { 128, 192, 256 };
        ts.minHz          = P ("pmin");
        ts.maxHz          = P ("pmax");
        ts.yinThreshold   = ytr;
        ts.minConfidence  = P ("conf");
        ts.octaveCheck    = P ("ocheck");
        ts.hysteresisCents = P ("hyst");
        ts.smoothing      = P ("psmooth");
        ts.silenceRms     = thrLin * 0.7f;
        ts.confirmFrames  = (int) P ("stab");
        ts.hopDecimated   = hops[juce::jlimit (0, 2, (int) P ("tspeed"))];
        tracker.setSettings (ts);
    }
    const float duckT = duckTarget.load();
    const float duckCoef = 1.0f - std::exp (-1.0f / (0.004f * (float) sr));

    // ---- ADSR ----
    juce::ADSR::Parameters ap;
    ap.attack  = P ("atk") * 0.001f;
    ap.decay   = P ("dec") * 0.001f;
    ap.sustain = P ("sus");
    ap.release = P ("rel") * 0.001f;
    adsr.setParameters (ap);

    // ---- szűrő ----
    const auto ftType = ft == 0 ? juce::dsp::StateVariableTPTFilterType::lowpass
                      : ft == 1 ? juce::dsp::StateVariableTPTFilterType::bandpass
                                : juce::dsp::StateVariableTPTFilterType::highpass;
    filtA.setType (ftType);  filtB.setType (ftType);
    filtA.setResonance (res);
    filtB.setResonance (juce::jmax (0.5f, res * 0.5f));

    // ---- LFO frekvencia (szabad vagy tempó-szinkron) ----
    double bpm = 120.0, ppq = -1.0;
    bool hostPlaying = false;
    if (auto* playHead = getPlayHead())
        if (auto pos = playHead->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto q = pos->getPpqPosition()) ppq = *q;
            hostPlaying = pos->getIsPlaying();
        }
    static const float syncMult[6] = { 0.0f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
    const float lfoHz = lsync == 0 ? P ("lrate") : (float) (bpm / 60.0) * syncMult[juce::jlimit (0, 5, lsync)];

    // pump: tempóra szinkronizált ducking (a host pozíciójához igazítva, ha jár a lejátszás)
    static const double pumpCyc[4] = { 0.25, 0.5, 1.0, 2.0 };
    const double pumpInc = bpm / 60.0 * pumpCyc[pumpDiv] / sr;
    if (hostPlaying && ppq >= 0.0) pumpPhase = std::fmod (ppq * pumpCyc[pumpDiv], 1.0);

    // ---- unison tömbök ----
    float uniRatio[kMaxUni], uniGL[kMaxUni], uniGR[kMaxUni];
    const float uniNorm = 1.0f / std::sqrt ((float) uniN);
    for (int v = 0; v < uniN; ++v)
    {
        const float pos = uniN > 1 ? (2.0f * (float) v / (float) (uniN - 1) - 1.0f) : 0.0f;
        uniRatio[v] = std::exp2 (pos * uniDet / 1200.0f);
        const float angle = (pos * uniSpr + 1.0f) * kPi * 0.25f;
        uniGL[v] = std::cos (angle) * 1.4142f * uniNorm;
        uniGR[v] = std::sin (angle) * 1.4142f * uniNorm;
    }

    // ---- EQ együtthatók (csak változáskor) ----
    if (boost != lastBoost)
    {
        for (auto& f : shelf) f.setLowShelf (sr, 90.0, 0.707, (double) boost);
        lastBoost = boost;
    }
    if (eqf != lastEqF || eqg != lastEqG || eqq != lastEqQ)
    {
        for (auto& f : peak) f.setPeak (sr, (double) eqf, (double) eqq, (double) eqg);
        lastEqF = eqf; lastEqG = eqg; lastEqQ = eqq;
    }
    if (hpHz != lastHp)
    {
        for (auto& f : hpf) f.setHighPass (sr, (double) juce::jmax (20.0f, hpHz));
        lastHp = hpHz;
    }
    if (xover != lastXover)
    {
        for (auto& a : xHp) for (auto& b : a) b.setHighPass (sr, (double) xover, 0.70710678);
        lastXover = xover;
    }
    if (mfreq != lastMfreq)
    {
        for (auto& a : monoLp) for (auto& b : a) b.setLowPass  (sr, (double) mfreq, 0.70710678);
        for (auto& a : monoHp) for (auto& b : a) b.setHighPass (sr, (double) mfreq, 0.70710678);
        lastMfreq = mfreq;
    }

    // ---- időállandók ----
    const float envAtk     = 1.0f - std::exp (-1.0f / (0.002f * (float) sr));
    const float envRel     = 1.0f - std::exp (-1.0f / (0.050f * (float) sr));
    const float fastAtk    = 1.0f - std::exp (-1.0f / (0.001f * (float) sr));
    const float fastRel    = 1.0f - std::exp (-1.0f / (0.020f * (float) sr));
    const float slowAtk    = 1.0f - std::exp (-1.0f / (0.030f * (float) sr));
    const float slowRel    = 1.0f - std::exp (-1.0f / (0.150f * (float) sr));
    const float glideCoef  = glideMs < 1.0f ? 1.0f : 1.0f - std::exp (-1.0f / (glideMs * 0.001f * (float) sr));
    const float toneCoef   = 1.0f - std::exp (-2.0f * kPi * juce::jmin (toneHz, (float) sr * 0.45f) / (float) sr);
    const bool  toneOn     = toneHz < 19000.0f;

    // ---- torzítás ----
    float dg    = 1.0f + drive * 20.0f;
    float dnorm = 1.0f / std::tanh (dg);
    const float crushStep = bitsI < 16 ? std::pow (2.0f, (float) (bitsI - 1)) : 0.0f;
    const float tapeCoef  = 1.0f - std::exp (-2.0f * kPi * juce::jmin (12000.0f, (float) sr * 0.45f) / (float) sr);

    auto distort = [&] (float x, int ch, bool tick)
    {
        float y;
        switch (dtype)
        {
            case 0:  y = std::tanh (x * dg) * dnorm; break;
            case 1:  y = juce::jlimit (-1.0f, 1.0f, x * dg); break;
            case 2:  y = std::sin (x * dg); break;
            case 3:  y = (std::tanh (x * dg + 0.3f) - std::tanh (0.3f)) * dnorm; break;   // aszimmetrikus
            case 4:  y = (x >= 0.0f ? std::tanh (x * dg) : 0.85f * std::tanh (x * dg * 1.6f)) * dnorm; break;   // cső
            case 5:                                                                        // szalag
                y = std::atan (x * dg) / std::atan (dg);
                tapeState[ch] += tapeCoef * (y - tapeState[ch]);
                y = tapeState[ch];
                break;
            default:                                                                       // dióda
                if (x >= 0.0f) y = (1.0f - std::exp (-dg * x)) / (1.0f - std::exp (-dg));
                else           y = -0.8f * (1.0f - std::exp (0.5f * dg * x)) / (1.0f - std::exp (-0.5f * dg));
                break;
        }
        if (crushStep > 0.0f) y = std::round (y * crushStep) / crushStep;
        if (crateI > 1)
        {
            if (tick) crushHold[ch] = y;
            y = crushHold[ch];
        }
        return y;
    };

    auto* wl = wetBuf.getWritePointer (0);
    auto* wr = wetBuf.getWritePointer (1);
    float inPk = 0.0f, outPk = 0.0f;

   #if A2B_SYNTH
    // MIDI hang -> cél hangmagasság (skálára zárás és oktáveltolás opcionális)
    auto noteFreqLog2 = [&] (int note)
    {
        float f = 440.0f * std::exp2 ((float) (note - 69) / 12.0f);
        if (quant) f = quantizeToScale (f, root, scaleI);
        f *= std::exp2 ((float) shift);
        f = juce::jlimit (18.0f, 4000.0f, f);
        return std::log2 (f);
    };

    // Monofonikus, "utoljára játszott hang" prioritású hangkezelés; a felengedett hang után a
    // még tartott hangra tér vissza (legato).
    auto removeHeld = [&] (int note)
    {
        for (int k = 0; k < heldCount; ++k)
            if (heldNote[k] == note)
            {
                for (int j = k; j < heldCount - 1; ++j) { heldNote[j] = heldNote[j + 1]; heldVel[j] = heldVel[j + 1]; }
                --heldCount;
                return true;
            }
        return false;
    };
    auto noteOn = [&] (int note, int vel)
    {
        const bool wasEmpty = heldCount == 0;
        removeHeld (note);
        if (heldCount >= 16) { for (int j = 0; j < 15; ++j) { heldNote[j] = heldNote[j + 1]; heldVel[j] = heldVel[j + 1]; } --heldCount; }
        heldNote[heldCount] = note; heldVel[heldCount] = (float) vel / 127.0f; ++heldCount;
        curNote = note; curVel = (float) vel / 127.0f;
        targetLog2F = noteFreqLog2 (note);
        if (wasEmpty) midiNewNote = true; else noteChanged = true;
    };
    auto noteOff = [&] (int note)
    {
        if (! removeHeld (note)) return;
        if (note == curNote && heldCount > 0)
        {
            curNote = heldNote[heldCount - 1];
            curVel  = heldVel[heldCount - 1];
            targetLog2F = noteFreqLog2 (curNote);
            noteChanged = true;
        }
    };
   #endif

    for (int i = 0; i < n; ++i)
    {
        // ---- bemenet (mono) ----
        float in = 0.0f;
       #if A2B_SYNTH
        // szintetizátor: csak az (opcionális) külső bemenet; engedélyezés nélkül nem olvasunk a pufferből
        for (int c = 0; c < scCh; ++c) in += scIn[c][i];
        in /= (float) juce::jmax (1, scCh);

        // MIDI események ennél a mintánál
        while (midiIt != midiEnd && (*midiIt).samplePosition <= chunkStart + i)
        {
            const auto meta = *midiIt;
            const uint8_t* d = meta.data;
            if (meta.numBytes >= 3)
            {
                const uint8_t st = d[0] & 0xF0;
                if (st == 0x90 && d[2] > 0)                         noteOn (d[1], d[2]);
                else if (st == 0x80 || st == 0x90)                  noteOff (d[1]);
                else if (st == 0xE0)                                bendTarget = (float) ((((int) d[2] << 7) | (int) d[1]) - 8192) / 8192.0f * bendRange;
                else if (st == 0xB0 && (d[1] == 123 || d[1] == 120)) { heldCount = 0; curNote = -1; }
            }
            ++midiIt;
        }
        bendCur += (bendTarget - bendCur) * bendCoef;
       #else
        for (int c = 0; c < numCh; ++c) in += buffer.getReadPointer (c)[i];
        in /= (float) juce::jmax (1, numCh);
       #endif

        const float a = std::abs (in);
        inPk = juce::jmax (inPk, a);
        env     += (a > env     ? envAtk  : envRel)  * (a - env);
        envFast += (a > envFast ? fastAtk : fastRel) * (a - envFast);
        envSlow += (a > envSlow ? slowAtk : slowRel) * (a - envSlow);

        // ---- simított paraméterek ----
        const float o1l = o1Sm.getNextValue(), o2l = o2Sm.getNextValue();
        const float subl = subSm.getNextValue(), noil = noiseSm.getNextValue();
        const float driveV = driveSm.getNextValue();
        const float cutV = cutSm.getNextValue();
        const float sdV = subDriveSm.getNextValue();
        if ((i & 7) == 0) { dg = 1.0f + driveV * 20.0f; dnorm = 1.0f / std::tanh (dg); }

       #if ! A2B_SYNTH
        // ---- hangmagasság-követés ----
        if (tracker.push (in))
        {
            voiced = tracker.voiced();
            const bool changed = tracker.consumeNoteChange();
            if (voiced)
            {
                float f = tracker.pitchHz();
                srcInHz = f;                                   // az eredeti réteg ehhez viszonyít
                if (fold) f = foldToBassRange (f, octave);
                else      f *= std::exp2 ((float) (octave - 1));
                if (quant) f = quantizeToScale (f, root, scaleI);
                f *= std::exp2 ((float) shift);
                f = juce::jlimit (18.0f, 4000.0f, f);

                if (changed && std::abs (std::log2 (f / lastFreq)) > 0.02f) noteChanged = true;
                lastFreq = f;
                targetLog2F = std::log2 (f);
            }
        }

       #endif

        curLog2F += (targetLog2F - curLog2F) * glideCoef;

        // ---- kapu / triggerek ----
       #if A2B_SYNTH
        // a kapu a lenyomott billentyűket követi
        const bool open = heldCount > 0;
        bool trig = false;
        if (open && ! wasOpen) trig = true;
        if (open && midiNewNote) trig = true;
        if (open && retrig && noteChanged) trig = true;
        midiNewNote = false;
        noteChanged = false;
       #else
        bool onset = false;
        if (onsetCooldown > 0) --onsetCooldown;
        if (onsetOn && onsetCooldown == 0 && envFast > envSlow * 1.6f + 1.0e-4f && envFast > thrLin)
        {
            onset = true;
            onsetCooldown = (int) (0.08 * sr);
        }

        const bool detOpen = voiced && env > thrLin;
        if (detOpen) holdCounter = holdSamples;
        else if (holdCounter > 0) --holdCounter;
        const bool open = detOpen || holdCounter > 0;

        bool trig = false;
        if (open && ! wasOpen) trig = true;
        if (open && retrig && noteChanged) trig = true;
        if (open && onset) trig = true;
        noteChanged = false;

       #endif

        if (trig)
        {
            const bool wasActive = adsr.isActive();
            if (gleg && ! wasOpen) curLog2F = targetLog2F;   // glide csak legato (átfedő) hangoknál
            noteSamples = 0;
            adsr.noteOn();
            fenv = 1.0f;
            penv = 1.0f;
            if (keysync && ! wasActive) for (int v = 0; v < kMaxUni; ++v) { ph1[v] = 0.0f; ph2[v] = 0.0f; }
            else if (phrand && ! wasActive)
            {
                auto r01 = [this] { rng = rng * 1664525u + 1013904223u; return (float) (rng >> 8) * (1.0f / 16777216.0f); };
                for (int v = 0; v < kMaxUni; ++v) { ph1[v] = r01(); ph2[v] = r01(); }
                subPhase = r01(); h1ph = r01(); h2ph = r01();
            }
        }
        if (! open && wasOpen) adsr.noteOff();
        if (open)
        {
            if (nlenSamples > 0 && noteSamples == nlenSamples) adsr.noteOff();   // beállított jegyhossz
            if (noteSamples < 0x3fffffff) ++noteSamples;
        }
        else noteSamples = 0;
        wasOpen = open;

       #if A2B_SYNTH
        const float dyn = curVel;
       #else
        const float dyn = juce::jlimit (0.0f, 1.0f, env * 4.0f);
       #endif

        // ---- LFO ----
        const float prevLfoPhase = lfoPhase;
        lfoPhase += lfoHz / (float) sr;
        if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;
        if (lfoPhase < prevLfoPhase)
        {
            rng = rng * 1664525u + 1013904223u;
            lfoSH = (float) (int32_t) rng * 4.656612875e-10f;
        }
        float lfo;
        switch (lshape)
        {
            case 0:  lfo = std::sin (2.0f * kPi * lfoPhase); break;
            case 1:  lfo = 4.0f * std::abs (lfoPhase - 0.5f) - 1.0f; break;
            case 2:  lfo = 2.0f * lfoPhase - 1.0f; break;
            case 3:  lfo = lfoPhase < 0.5f ? 1.0f : -1.0f; break;
            default: lfo = lfoSH; break;
        }

        // ---- burkológörbék ----
        penv *= pdecCoef;
        fenv = fsus + (fenv - fsus) * fdecCoef;

        // ---- alapfrekvencia ----
        const float penvScale = A2B_SYNTH ? ((1.0f - velPenv) + velPenv * curVel) : 1.0f;
        const float log2f = curLog2F + trans / 12.0f + fineC / 1200.0f + bendCur / 12.0f
                          + lfo * lpitch / 1200.0f + penvAmt * penvScale / 12.0f * penv;
        const float f0 = std::exp2 (log2f);

        // Eredeti réteg: a bemenet saját jele a cél hangmagasságra tolva (hangszín-megőrző)
        float srcSample = 0.0f;
        if (srcLvl > 0.001f)
            srcSample = srcShifter.process (in, f0 * srcSemiRatio / srcInHz, (float) sr / srcInHz);
        else
            srcShifter.write (in);

        const float pw1 = juce::jlimit (0.05f, 0.95f, o1pwP + lpw * lfo * 0.45f);
        const float pw2 = juce::jlimit (0.05f, 0.95f, o2pwP + lpw * lfo * 0.45f);
        const bool useO2 = (o2l > 1.0e-4f) || (fmAmt > 0.0f);

        // Analóg drift: lassú, véletlenszerű hangolás-ingadozás oszcillátoronként
        if (driftAmt > 0.001f)
        {
            if (--driftCount <= 0)
            {
                driftCount = (int) (0.35 * sr);
                for (int v = 0; v < kMaxUni; ++v)
                {
                    rng = rng * 1664525u + 1013904223u; drvT1[v] = (float) (int32_t) rng * 4.656612875e-10f;
                    rng = rng * 1664525u + 1013904223u; drvT2[v] = (float) (int32_t) rng * 4.656612875e-10f;
                }
                rng = rng * 1664525u + 1013904223u; drvSubT = (float) (int32_t) rng * 4.656612875e-10f;
            }
            for (int v = 0; v < kMaxUni; ++v)
            {
                drv1[v] += (drvT1[v] - drv1[v]) * driftCoef;
                drv2[v] += (drvT2[v] - drv2[v]) * driftCoef;
            }
            drvSub += (drvSubT - drvSub) * driftCoef;
        }

        // ---- oszcillátorok + unison ----
        float outL = 0.0f, outR = 0.0f;
        for (int v = 0; v < uniN; ++v)
        {
            float s2 = 0.0f;
            if (useO2)
            {
                const float dt2 = juce::jlimit (1.0e-5f, 0.45f, f0 * o2ratio * uniRatio[v] * (1.0f + dk * drv2[v]) / (float) sr);
                s2 = oscSample (o2wave, ph2[v], dt2, pw2);
                ph2[v] += dt2; if (ph2[v] >= 1.0f) ph2[v] -= 1.0f;
            }

            const float dt1 = juce::jlimit (1.0e-5f, 0.45f, f0 * o1ratio * uniRatio[v] * (1.0f + dk * drv1[v]) / (float) sr);
            float p1 = ph1[v];
            if (fmAmt > 0.0f) { p1 += fmAmt * 0.5f * s2; p1 -= std::floor (p1); }
            const float s1 = oscSample (o1wave, p1, dt1, pw1);
            ph1[v] += dt1; if (ph1[v] >= 1.0f) ph1[v] -= 1.0f;

            const float vs = s1 * o1l + s2 * o2l;
            outL += vs * uniGL[v];
            outR += vs * uniGR[v];
        }

        // Harmonizálás: párhuzamos szólamok a felismert hang mellé (az 1. oszcillátor hullámformájával)
        if (h1on || h2on)
        {
            float hs = 0.0f;
            if (h1on)
            {
                const float dtH = juce::jlimit (1.0e-5f, 0.45f, f0 * o1ratio * h1ratio * (1.0f + dk * drv2[1]) / (float) sr);
                hs += oscSample (o1wave, h1ph, dtH, pw1) * h1l;
                h1ph += dtH; if (h1ph >= 1.0f) h1ph -= 1.0f;
            }
            if (h2on)
            {
                const float dtH = juce::jlimit (1.0e-5f, 0.45f, f0 * o1ratio * h2ratio * (1.0f + dk * drv2[2]) / (float) sr);
                hs += oscSample (o1wave, h2ph, dtH, pw1) * h2l;
                h2ph += dtH; if (h2ph >= 1.0f) h2ph -= 1.0f;
            }
            outL += hs * 0.7f;
            outR += hs * 0.7f;
        }

        // az eredeti réteg (mono) a szintetizált rétegek mellé
        outL += srcSample * srcLvl;
        outR += srcSample * srcLvl;

        // sub
        float subSig = 0.0f;
        {
            float sub;
            switch (subWave)
            {
                case 0:  sub = std::sin (2.0f * kPi * subPhase); break;
                case 1:  sub = 4.0f * std::abs (subPhase - 0.5f) - 1.0f; break;
                default: sub = subPhase < 0.5f ? 0.7f : -0.7f; break;
            }
            subPhase += f0 * subRatio * (1.0f + dk * drvSub) / (float) sr;
            if (subPhase >= 1.0f) subPhase -= 1.0f;
            if (sdV > 1.0e-3f) { const float g = 1.0f + sdV * 8.0f; sub = std::tanh (g * sub) / std::tanh (g); }
            subSig = sub * subl * 0.8f;
            if (! split) { outL += subSig; outR += subSig; }
        }

        // zaj
        if (noil > 1.0e-4f)
        {
            rng = rng * 1664525u + 1013904223u;
            const float nl = (float) (int32_t) rng * 4.656612875e-10f;
            rng = rng * 1664525u + 1013904223u;
            const float nr = (float) (int32_t) rng * 4.656612875e-10f;
            outL += nl * noil * 0.5f;
            outR += nr * noil * 0.5f;
        }

        outL *= 0.5f; outR *= 0.5f;

        // Layer Split: a mid réteg a crossover alatt le van vágva, így nem zavarja a tiszta sub-t,
        // és a torzítás/szűrő sem dolgozik a sub tartományban.
        if (split)
        {
            outL = xHp[0][1].process (xHp[0][0].process (outL));
            outR = xHp[1][1].process (xHp[1][0].process (outR));
        }

        // ---- crush tick ----
        bool tick = false;
        if (++crushCount >= crateI) { crushCount = 0; tick = true; }

        if (! distPost) { outL = distort (outL, 0, tick); outR = distort (outR, 1, tick); }

        // ---- szűrő ----
        if ((i & 7) == 0)
        {
            const float octMod = lfo * lcut + fenvAmt * fenv + (A2B_SYNTH ? velCut * 0.75f : fin) * 4.0f * dyn
                               + fkey * (log2f - 6.7814f);
            const float cf = juce::jlimit (20.0f, (float) sr * 0.45f, cutV * std::exp2 (octMod));
            filtA.setCutoffFrequency (cf);
            filtB.setCutoffFrequency (cf);
        }
        outL = filtA.processSample (0, outL);
        outR = filtA.processSample (1, outR);
        if (f24)
        {
            outL = filtB.processSample (0, outL);
            outR = filtB.processSample (1, outR);
        }

        if (distPost) { outL = distort (outL, 0, tick); outR = distort (outR, 1, tick); }

        if (toneOn)
        {
            toneState[0] += toneCoef * (outL - toneState[0]); outL = toneState[0];
            toneState[1] += toneCoef * (outR - toneState[1]); outR = toneState[1];
        }

        // Timbre-átvitel (vocoder): a forrás spektrális alakját viszi a hordozóra
        if (vocMix > 0.001f)
        {
            float vl, vr;
            vocoder.process (in, outL, outR, vl, vr);
            vl *= vocGain; vr *= vocGain;
            outL += (vl - outL) * vocMix;
            outR += (vr - outR) * vocMix;
        }

        // Layer Split: a sub réteg párhuzamosan, a mid szűrő és torzítás megkerülésével kerül be
        if (split) { outL += subSig * 0.5f; outR += subSig * 0.5f; }

        // Analóg zaj (halk, szűrt)
        if (hissAmp > 0.0f)
        {
            rng = rng * 1664525u + 1013904223u;
            const float nz = (float) (int32_t) rng * 4.656612875e-10f;
            hissState += 0.35f * (nz - hissState);
            outL += hissState * hissAmp;
            outR += hissState * hissAmp;
        }

        // DC-blokkoló (az Asym torzítás és a nem 50%-os pulzus DC-t okozhat)
        outL = dc[0].process (outL);
        outR = dc[1].process (outR);

        duckGain += (duckT - duckGain) * duckCoef;

        // ---- amp ----
        const float ampEnv = adsr.getNextSample();
        const float trem = 1.0f - lamp * 0.5f * (1.0f + lfo);
        const float dynAmt = A2B_SYNTH ? velAmt : follow;
        const float amp = ampEnv * ((1.0f - dynAmt) + dynAmt * dyn) * trem;

        // Sidechain ducking (külső bemenet) és pump
        float scT = 1.0f;
        if (scCh > 0 && scAmt > 0.001f)
        {
            float sIn = 0.0f;
            for (int c = 0; c < scCh; ++c) sIn += scIn[c][i];
            sIn = std::abs (sIn / (float) scCh);
            scEnv += (sIn > scEnv ? scAtk : scRel) * (sIn - scEnv);
            if ((i & 7) == 0)
            {
                const float sdb = 20.0f * std::log10 (juce::jmax (scEnv, 1.0e-6f));
                scX = juce::jlimit (0.0f, 1.0f, (sdb - scThr) / 12.0f);
            }
            scT = 1.0f - scAmt * scX;
        }
        scGain += (scT - scGain) * scSmooth;

        pumpPhase += pumpInc;
        if (pumpPhase >= 1.0) pumpPhase -= 1.0;
        float pT = 1.0f;
        if (pumpAmt > 0.001f)
        {
            const float tt = juce::jmin (1.0f, (float) pumpPhase / pumpRel);
            pT = 1.0f - pumpAmt * (1.0f - tt * tt * (3.0f - 2.0f * tt));
        }
        pumpGain += (pT - pumpGain) * scSmooth;

        float wlI = outL * amp * duckGain * scGain * pumpGain;
        float wrI = outR * amp * duckGain * scGain * pumpGain;
        if (mbOn) multiband.process (wlI, wrI);
        if (tOn)  transient.process (wlI, wrI);
        wl[i] = wlI;
        wr[i] = wrI;
    }

    // ---- chorus ----
    if (chmix > 0.001f)
    {
        chorus.setRate (P ("chrate"));
        chorus.setDepth (P ("chdepth"));
        chorus.setCentreDelay (7.0f);
        chorus.setFeedback (0.0f);
        chorus.setMix (chmix);
        juce::dsp::AudioBlock<float> block (wetBuf);
        auto sub = block.getSubBlock (0, (size_t) n);
        chorus.process (juce::dsp::ProcessContextReplacing<float> (sub));
    }

    // ---- EQ, low cut, szélesség, mix ----
    const bool hpOn = hpHz > 21.0f;
    for (int i = 0; i < n; ++i)
    {
        float l = wl[i], r = wr[i];

        l = peak[0].process (shelf[0].process (l));
        r = peak[1].process (shelf[1].process (r));
        if (hpOn) { l = hpf[0].process (l); r = hpf[1].process (r); }

        const float widthV = widthSm.getNextValue();
        const float gainV  = outSm.getNextValue();
        const float mixV   = mixSm.getNextValue();
        const float m = 0.5f * (l + r);
        const float s = 0.5f * (l - r) * widthV;
        l = (m + s) * gainV;
        r = (m - s) * gainV;

        if (monoSub)
        {
            const float lowL = monoLp[0][1].process (monoLp[0][0].process (l));
            const float lowR = monoLp[1][1].process (monoLp[1][0].process (r));
            const float hiL  = monoHp[0][1].process (monoHp[0][0].process (l));
            const float hiR  = monoHp[1][1].process (monoHp[1][0].process (r));
            const float low  = 0.5f * (lowL + lowR);
            l = hiL + low;
            r = hiR + low;
        }

        tpLimiter.process (l, r, tpOn);                              // mindig késleltet (állandó), csak akkor limitál, ha be van kapcsolva
        if (! tpOn && limiter) { l = std::tanh (l); r = std::tanh (r); }

        for (int c = 0; c < numCh; ++c)
        {
            float* d = buffer.getWritePointer (c);
            const float w = (numCh == 1) ? 0.5f * (l + r) : (c == 0 ? l : r);
           #if A2B_SYNTH
            d[i] = w;                       // hanggenerátor: nincs "dry" jel
            juce::ignoreUnused (mixV);
           #else
            d[i] = d[i] * (1.0f - mixV) + w * mixV;
           #endif
            outPk = juce::jmax (outPk, std::abs (d[i]));
        }
    }

    // GUI-információ (zármentes)
   #if A2B_SYNTH
    info.voiced.store (heldCount > 0);
    info.hz.store (heldCount > 0 ? std::exp2 (targetLog2F) : 0.0f);
    info.confidence.store (heldCount > 0 ? curVel : 0.0f);
   #else
    info.voiced.store (tracker.voiced());
    info.hz.store (tracker.voiced() ? tracker.pitchHz() : 0.0f);
    info.confidence.store (tracker.confidence());
   #endif
    info.inDb.store (juce::Decibels::gainToDecibels (inPk, -100.0f));
    info.outDb.store (juce::Decibels::gainToDecibels (outPk, -100.0f));
}

//==============================================================================
juce::AudioProcessorEditor* Audio2BassProcessor::createEditor()
{
    return new Audio2BassEditor (*this);
}

void Audio2BassProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("a2bVersion", 2, nullptr);
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void Audio2BassProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            currentProgram = juce::jlimit (0, (int) presets().size() - 1, (int) apvts.state.getProperty ("program", 0));
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Audio2BassProcessor();
}
