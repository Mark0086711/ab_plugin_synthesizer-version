// Fejetlen teszt az Audio2Bass SYNTH (hanggenerátor) változathoz: A2B_SYNTH=1 módban fordul.
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>

#if ! A2B_SYNTH
 #error "A synth_test.cpp-t A2B_SYNTH=1 definícióval kell fordítani."
#endif

static int failures = 0;
static const double kPi = 3.14159265358979323846;

static void check (bool ok, const juce::String& name, const juce::String& detail = {})
{
    std::printf ("[%s] %-56s %s\n", ok ? "PASS" : "FAIL", name.toRawUTF8(), detail.toRawUTF8());
    std::fflush (stdout);
    if (! ok) ++failures;
}

static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

static void setPlain (Audio2BassProcessor& p, const char* id, float plain)
{
    if (auto* r = p.apvts.getParameter (id)) r->setValueNotifyingHost (r->convertTo0to1 (plain));
}

static double goertzelPow (const std::vector<float>& x, double fs, double f)
{
    const double w = 2.0 * kPi * f / fs, coef = 2.0 * std::cos (w);
    double s0 = 0, s1 = 0, s2 = 0;
    for (float v : x) { s0 = v + coef * s1 - s2; s2 = s1; s1 = s0; }
    return (s1 * s1 + s2 * s2 - coef * s1 * s2) / ((double) x.size() * (double) x.size());
}

static double noteHz (int note) { return 440.0 * std::pow (2.0, (note - 69) / 12.0); }

struct Ev { long pos; int b0, b1, b2; };
static Ev noteOnEv  (long pos, int note, int vel) { return { pos, 0x90, note, vel }; }
static Ev noteOffEv (long pos, int note)          { return { pos, 0x80, note, 0 }; }
static Ev ccEv      (long pos, int cc, int val)   { return { pos, 0xB0, cc, val }; }
static Ev bendEv    (long pos, int value14)       { return { pos, 0xE0, value14 & 127, (value14 >> 7) & 127 }; }

struct RunStats { bool finite = true; float peak = 0.0f; double rmsTail = 0.0; };

enum class Fill { None, Noise, Key };

struct SynthRig
{
    Audio2BassProcessor p;
    double sr; int bs;

    SynthRig (double sampleRate, int blockSize) : sr (sampleRate), bs (blockSize)
    {
        p.setRateAndBufferSizeDetails (sr, bs);
        p.prepareToPlay (sr, bs);
    }

    bool enableInput()
    {
        const bool ok = p.getBus (true, 0) != nullptr && p.getBus (true, 0)->enable (true);
        p.prepareToPlay (sr, bs);
        return ok;
    }

    // captureFrom < 0: a "tail" rész; >= 0: ettől a mintától
    RunStats play (std::vector<Ev> events, double seconds, double tailSeconds = 0.5, std::vector<float>* capture = nullptr,
                 Fill fill = Fill::None, float fillLevel = 0.5f, long captureFrom = -1)
    {
        std::sort (events.begin(), events.end(), [] (const Ev& a, const Ev& b) { return a.pos < b.pos; });
        RunStats r;
        juce::AudioBuffer<float> buf (2, bs);
        juce::MidiBuffer midi;
        std::mt19937 rng (1);
        std::uniform_real_distribution<float> u (-1.0f, 1.0f);

        const long total = (long) (seconds * sr);
        const long tailStart = captureFrom >= 0 ? captureFrom : total - (long) (tailSeconds * sr);
        size_t evi = 0; double e = 0; long cnt = 0;

        for (long pos = 0; pos < total; pos += bs)
        {
            const int n = (int) std::min<long> (bs, total - pos);
            buf.setSize (2, n, false, false, true);
            for (int i = 0; i < n; ++i)
            {
                float v = 0.0f;
                if (fill == Fill::Noise) v = fillLevel * u (rng);
                else if (fill == Fill::Key) v = (float) (fillLevel * std::sin (2.0 * kPi * 60.0 * (double) (pos + i) / sr));
                buf.setSample (0, i, v); buf.setSample (1, i, v);
            }
            midi.clear();
            while (evi < events.size() && events[evi].pos < pos + n)
            {
                midi.addEvent (juce::MidiMessage (events[evi].b0, events[evi].b1, events[evi].b2), (int) (events[evi].pos - pos));
                ++evi;
            }
            p.processBlock (buf, midi);

            for (int i = 0; i < n; ++i)
            {
                const float v = buf.getSample (0, i);
                if (! std::isfinite (v)) r.finite = false;
                r.peak = std::max (r.peak, std::abs (v));
                if (pos + i >= tailStart)
                {
                    e += (double) v * v; ++cnt;
                    if (capture != nullptr) capture->push_back (v);
                }
            }
        }
        r.rmsTail = cnt ? std::sqrt (e / (double) cnt) : 0.0;
        return r;
    }

    void pureSine()
    {
        setPlain (p, "o1wave", 3.0f); setPlain (p, "o1lvl", 1.0f); setPlain (p, "sublvl", 0.0f);
        setPlain (p, "cut", 12000.0f); setPlain (p, "res", 0.7f); setPlain (p, "drive", 0.0f);
    }
};

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::printf ("=== Audio2Bass SYNTH tests ===\n");
    const double fs = 48000.0;
    auto sec = [&] (double s) { return (long) (s * fs); };

    // 1) A hang a lenyomott billentyű hangmagasságán szól
    {
        SynthRig r (fs, 256); r.pureSine();
        std::vector<float> cap;
        auto s = r.play ({ noteOnEv (0, 45, 127) }, 1.5, 0.5, &cap);
        const double p110 = goertzelPow (cap, fs, 110.0), p55 = goertzelPow (cap, fs, 55.0), p220 = goertzelPow (cap, fs, 220.0);
        check (s.finite && p110 > 30.0 * p55 && p110 > 30.0 * p220, "MIDI note A2 sounds at 110 Hz",
               "110 Hz is " + juce::String (10.0 * std::log10 (p110 / std::max (p220, 1.0e-20)), 1) + " dB above 220 Hz");
    }

    // 2) Minden billentyűnek saját hangmagassága van (az eredeti hiba: mindig ugyanaz volt)
    {
        int correct = 0; juce::String bad;
        for (int k = 0; k < 12; ++k)
        {
            SynthRig r (fs, 256); r.pureSine();
            std::vector<float> cap;
            r.play ({ noteOnEv (0, 36 + k, 110) }, 1.5, 1.0, &cap);
            int best = 0; double bp = -1;
            for (int j = 0; j < 12; ++j)
            {
                const double pw = goertzelPow (cap, fs, noteHz (36 + j));
                if (pw > bp) { bp = pw; best = j; }
            }
            if (best == k) ++correct; else bad += juce::String (36 + k) + "->" + juce::String (36 + best) + " ";
        }
        check (correct == 12, "each of 12 keys gives its own pitch", juce::String (correct) + "/12 correct " + bad);
    }

    // 3) Felengedés: a hang elhal
    {
        SynthRig r (fs, 256); r.pureSine(); setPlain (r.p, "rel", 40.0f);
        auto s = r.play ({ noteOnEv (0, 45, 127), noteOffEv (sec (0.6), 45) }, 1.3, 0.3);
        check (s.finite && s.rmsTail < 0.003, "note-off releases to silence", "tail rms " + juce::String (s.rmsTail, 5));
    }

    // 4) Velocity az erősségre hat
    {
        auto level = [&] (int vel)
        {
            SynthRig r (fs, 256); r.pureSine(); setPlain (r.p, "velamp", 1.0f);
            return r.play ({ noteOnEv (0, 45, vel) }, 1.2, 0.4).rmsTail;
        };
        const double hi = level (127), lo = level (30);
        check (hi > 2.5 * lo && lo > 1.0e-4, "velocity scales the level", "127: " + juce::String (hi, 4) + ", 30: " + juce::String (lo, 4));
    }

    // 5) Legato: átfedő hangoknál a glide a második hangra csúsztat
    {
        SynthRig r (fs, 256); r.pureSine(); setPlain (r.p, "glide", 60.0f); setPlain (r.p, "retrig", 0.0f); setPlain (r.p, "vmode", 2.0f);
        std::vector<float> cap;
        r.play ({ noteOnEv (0, 45, 100), noteOnEv (sec (0.5), 52, 100) }, 1.8, 0.5, &cap);
        const double pNew = goertzelPow (cap, fs, noteHz (52)), pOld = goertzelPow (cap, fs, noteHz (45));
        check (pNew > 30.0 * pOld, "legato glide lands on the second note", "164.8 Hz is " + juce::String (10.0 * std::log10 (pNew / std::max (pOld, 1.0e-20)), 1) + " dB above 110 Hz");
    }

    // 6) A felengedett hang után visszatér a még tartott hangra
    {
        SynthRig r (fs, 256); r.pureSine(); setPlain (r.p, "glide", 0.0f); setPlain (r.p, "vmode", 1.0f);
        std::vector<float> cap;
        r.play ({ noteOnEv (0, 45, 100), noteOnEv (sec (0.3), 52, 100), noteOffEv (sec (0.8), 52) }, 1.6, 0.5, &cap);
        const double p45 = goertzelPow (cap, fs, noteHz (45)), p52 = goertzelPow (cap, fs, noteHz (52));
        check (p45 > 30.0 * p52, "after releasing the top note it returns to the held note", "110 Hz is " + juce::String (10.0 * std::log10 (p45 / std::max (p52, 1.0e-20)), 1) + " dB above 164.8 Hz");
    }

    // 7) Pitch bend (+2 félhang alapból)
    {
        SynthRig r (fs, 256); r.pureSine();
        std::vector<float> cap;
        r.play ({ noteOnEv (0, 45, 110), bendEv (sec (0.3), 16383) }, 1.5, 0.5, &cap);
        const double target = noteHz (45) * std::pow (2.0, 2.0 / 12.0);
        const double pBent = goertzelPow (cap, fs, target), pBase = goertzelPow (cap, fs, noteHz (45));
        check (pBent > 10.0 * pBase, "pitch bend up moves the pitch by 2 semitones", juce::String (target, 2) + " Hz");
    }

    // 8) All Notes Off (CC123)
    {
        SynthRig r (fs, 256); r.pureSine(); setPlain (r.p, "rel", 40.0f);
        auto s = r.play ({ noteOnEv (0, 45, 110), ccEv (sec (0.4), 123, 0) }, 1.0, 0.3);
        check (s.rmsTail < 0.003, "All Notes Off silences the synth", "tail rms " + juce::String (s.rmsTail, 5));
    }

    // 9) Mintapontos időzítés, a belső darabolás határán át (4096-os blokk, 2048-as darab)
    {
        SynthRig r (fs, 4096); r.pureSine();
        std::vector<float> cap;
        r.play ({ noteOnEv (3000, 45, 127) }, 4097.0 / fs, 0.0, &cap, Fill::None, 0.0f, 0);
        float before = 0, after = 0;
        for (size_t i = 0; i < cap.size(); ++i)
        {
            if (i < 3000) before = std::max (before, std::abs (cap[i]));
            if (i >= 3600) after = std::max (after, std::abs (cap[i]));
        }
        check (before < 1.0e-4f && after > 0.02f, "note-on is sample-accurate across the internal chunk boundary",
               "before " + juce::String (before, 6) + ", after " + juce::String (after, 3));
    }

    // 10) A host puffer szemetét nem szivárogtatja ki (a bemenet le van tiltva)
    {
        SynthRig r (fs, 256);
        auto s = r.play ({}, 1.0, 0.5, nullptr, Fill::Noise, 0.5f);
        check (s.peak < 1.0e-3f, "no note: the host's input buffer garbage does not leak", "peak " + juce::String (s.peak, 6));
    }

    // 11) Engedélyezett külső bemenet sem szivárog hang nélkül; sidechain ducking működik
    {
        SynthRig a (fs, 256); const bool okA = a.enableInput();
        auto leak = a.play ({}, 1.0, 0.5, nullptr, Fill::Key, 0.8f);
        check (okA && leak.peak < 1.0e-3f, "external input enabled, no note: no dry leak", "peak " + juce::String (leak.peak, 6));

        auto level = [&] (Fill f)
        {
            SynthRig r (fs, 256); const bool ok = r.enableInput(); r.pureSine();
            setPlain (r.p, "scamt", 1.0f); setPlain (r.p, "scthr", -40.0f); setPlain (r.p, "screl", 50.0f);
            return ok ? r.play ({ noteOnEv (0, 45, 127) }, 1.5, 0.5, nullptr, f, 0.5f).rmsTail : -1.0;
        };
        const double quiet = level (Fill::None), ducked = level (Fill::Key);
        check (quiet > 0.01 && ducked < quiet * 0.25, "sidechain key ducks the synth", "no key " + juce::String (quiet, 4) + ", key " + juce::String (ducked, 4));
    }

    // 12) Minden gyári preset: véges és korlátozott kimenet; nincs "Original" preset
    {
        SynthRig r (fs, 256);
        bool allOk = true, noOriginal = true; juce::String bad;
        for (int i = 0; i < r.p.getNumPrograms(); ++i)
        {
            if (r.p.getProgramName (i).startsWith ("Original")) noOriginal = false;
            r.p.setCurrentProgram (i); pump (80);
            auto s = r.play ({ noteOnEv (0, 48, 110) }, 1.2, 0.2);
            if (! (s.finite && s.peak <= 1.001f && s.peak > 0.003f)) { allOk = false; if (bad.isEmpty()) bad = r.p.getProgramName (i); }
        }
        check (allOk && noOriginal, "all " + juce::String (r.p.getNumPrograms()) + " synth presets play a note (finite, limited)", bad);
        check (r.p.countInvalidPresetIds() == 0, "every factory preset sets only real parameters", juce::String (r.p.countInvalidPresetIds()) + " unknown IDs");
    }

    // 13) AI presetgenerátor: minden műfaj, több seed
    {
        const auto genres = Audio2BassProcessor::getGenreNames();
        bool idsOk = true, playOk = true; juce::String badId, badPlay;
        Audio2BassProcessor probe;
        for (int g = 0; g < genres.size(); ++g)
            for (uint32_t seed : { 1u, 2u, 3u })
            {
                for (auto& kv : a2b::PresetGen::generate (g, "dark wide", 0.8f, seed))
                    if (probe.apvts.getParameter (kv.first) == nullptr) { idsOk = false; badId = juce::String (kv.first); }

                SynthRig r (fs, 256);
                r.p.generatePreset (g, "", 0.8f, seed, {}); pump (80);
                auto s = r.play ({ noteOnEv (0, 48, 110) }, 1.5, 0.2);
                if (! (s.finite && s.peak <= 1.001f && s.peak > 0.005f)) { playOk = false; if (badPlay.isEmpty()) badPlay = genres[g] + " #" + juce::String ((int) seed); }
            }
        check (idsOk, "generator only uses real parameter IDs", badId);
        check (playOk, "every genre x 3 seeds produces an audible, limited sound (" + juce::String (genres.size()) + " genres)", badPlay);
    }

    // 14) Generátor: zárolás, előzmények, név
    {
        SynthRig r (fs, 256);
        const char* oscIds[] = { "o1wave", "o1lvl", "o2lvl", "uni", "sublvl", "subwave" };
        auto snap = [&] { std::vector<float> v; for (auto* id : oscIds) v.push_back (r.p.apvts.getParameter (id)->getValue()); return v; };
        const auto before = snap();
        std::array<bool, 6> locked {}; locked[0] = true;
        r.p.generatePreset (0, "", 0.7f, 11, locked); pump (80);
        const bool oscKept = before == snap();
        const float cut1 = r.p.apvts.getRawParameterValue ("cut")->load();
        const auto name = r.p.getCurrentPresetName();

        r.p.generatePreset (7 /* Acid */, "", 0.7f, 22, {}); pump (80);
        const float cut2 = r.p.apvts.getRawParameterValue ("cut")->load();
        const bool stepped = r.p.genHistoryStep (-1); pump (80);
        const float cutBack = r.p.apvts.getRawParameterValue ("cut")->load();
        check (oscKept && name.startsWith ("AI ") && stepped && std::abs (cutBack - cut1) < 3.0f && std::abs (cut2 - cut1) > 100.0f,
               "generator: locks, name and history (back) work",
               "name '" + name + "', cut " + juce::String (cut1, 0) + " -> " + juce::String (cut2, 0) + " -> back " + juce::String (cutBack, 0));
    }

    // 15) Különböző blokkméretek, MIDI a blokk közepén
    for (int bs : { 1, 7, 64, 512, 3000 })
    {
        SynthRig r (fs, bs); r.pureSine();
        std::vector<float> cap;
        auto s = r.play ({ noteOnEv (sec (0.1), 45, 120), noteOffEv (sec (0.7), 45) }, 1.0, 0.0, &cap, Fill::None, 0.0f, sec (0.3));
        std::vector<float> mid (cap.begin(), cap.begin() + (long) sec (0.3));
        check (s.finite && goertzelPow (mid, fs, 110.0) > 1.0e-4, "block size " + juce::String (bs) + ": MIDI plays correctly", "");
    }

    // 16) CPU
    for (const char* name : { "Init", "Supersaw Lead", "Wide Mid Bass" })
    {
        SynthRig r (fs, 256);
        for (int i = 0; i < r.p.getNumPrograms(); ++i) if (r.p.getProgramName (i) == name) { r.p.setCurrentProgram (i); pump (80); }
        const auto a = std::chrono::steady_clock::now();
        r.play ({ noteOnEv (0, 36, 110) }, 10.0, 0.5);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - a).count();
        check (secs / 10.0 * 100.0 < 30.0, juce::String ("CPU preset '") + name + "'", juce::String (secs / 10.0 * 100.0, 2) + " % of one core (48 kHz, 256 block)");
    }

    // 17) Polifónia: három billentyű egyszerre = három hang (akkord)
    {
        SynthRig r (fs, 256); r.pureSine();
        std::vector<float> cap;
        r.play ({ noteOnEv (0, 48, 110), noteOnEv (0, 52, 110), noteOnEv (0, 55, 110) }, 1.2, 0.5, &cap);
        const double a = goertzelPow (cap, fs, noteHz (48)), b = goertzelPow (cap, fs, noteHz (52)), c = goertzelPow (cap, fs, noteHz (55)), gap = goertzelPow (cap, fs, noteHz (50));
        const double mn = std::min ({ a, b, c });
        check (mn > 100.0 * gap, "polyphonic: a C major chord has three notes", "weakest note is " + juce::String (10.0 * std::log10 (mn / std::max (gap, 1.0e-20)), 1) + " dB above the gap");
    }

    // 18) Az egyik hang felengedése nem érinti a többit
    {
        SynthRig r (fs, 256); r.pureSine(); setPlain (r.p, "rel", 40.0f);
        std::vector<float> cap;
        r.play ({ noteOnEv (0, 48, 110), noteOnEv (0, 52, 110), noteOnEv (0, 55, 110), noteOffEv (sec (0.5), 52) }, 1.4, 0.4, &cap);
        const double a = goertzelPow (cap, fs, noteHz (48)), b = goertzelPow (cap, fs, noteHz (52)), c = goertzelPow (cap, fs, noteHz (55));
        check (a > 10.0 * b && c > 10.0 * b, "polyphonic: releasing one note keeps the other two", "");
    }

    // 19) Hangszám-korlát: a legrégebbi hang esik ki
    {
        SynthRig r (fs, 256); r.pureSine(); setPlain (r.p, "voices", 2.0f);
        std::vector<float> cap;
        r.play ({ noteOnEv (0, 48, 110), noteOnEv (sec (0.1), 52, 110), noteOnEv (sec (0.2), 55, 110) }, 1.2, 0.4, &cap);
        const double a = goertzelPow (cap, fs, noteHz (48)), b = goertzelPow (cap, fs, noteHz (52)), c = goertzelPow (cap, fs, noteHz (55));
        check (a * 20.0 < std::min (b, c), "voice limit: the oldest note is stolen", "");
    }

    // 20) Sustain pedál (CC64)
    {
        SynthRig r (fs, 256); r.pureSine(); setPlain (r.p, "rel", 40.0f);
        auto held = r.play ({ ccEv (0, 64, 127), noteOnEv (sec (0.05), 48, 110), noteOffEv (sec (0.25), 48) }, 0.9, 0.3);
        SynthRig q (fs, 256); q.pureSine(); setPlain (q.p, "rel", 40.0f);
        auto freed = q.play ({ ccEv (0, 64, 127), noteOnEv (sec (0.05), 48, 110), noteOffEv (sec (0.25), 48), ccEv (sec (0.6), 64, 0) }, 1.2, 0.3);
        check (held.rmsTail > 0.1 && freed.rmsTail < 0.003, "sustain pedal holds the note until it is released",
               "held " + juce::String (held.rmsTail, 3) + ", after pedal-up " + juce::String (freed.rmsTail, 5));
    }

    // 21) Teljes hangmagasság-tartomány billentyűkkel (nem csak basszus)
    {
        bool ok = true; juce::String det;
        for (int note : { 36, 60, 84, 96, 108 })
        {
            SynthRig r (fs, 256); r.pureSine();
            std::vector<float> cap;
            r.play ({ noteOnEv (0, note, 110) }, 1.2, 0.6, &cap);
            const double p = goertzelPow (cap, fs, noteHz (note)), lo = goertzelPow (cap, fs, noteHz (note) / 2.0);
            if (! (p > 100.0 * lo && p > 0.01)) { ok = false; det += juce::String (note) + " "; }
        }
        check (ok, "full range: C2 .. C8 play at the right pitch", det);
    }

    // 22) Mono mód: csak az utolsó hang
    {
        SynthRig r (fs, 256); r.pureSine(); setPlain (r.p, "vmode", 1.0f);
        std::vector<float> cap;
        r.play ({ noteOnEv (0, 48, 110), noteOnEv (sec (0.3), 55, 110) }, 1.2, 0.4, &cap);
        const double c3 = goertzelPow (cap, fs, noteHz (48)), g3 = goertzelPow (cap, fs, noteHz (55));
        check (g3 > 100.0 * c3, "mono mode: only the last note sounds", "");
    }

    // 23) Mod wheel (CC1) a szűrőre
    {
        auto level = [&] (int mw)
        {
            SynthRig r (fs, 256); setPlain (r.p, "o1wave", 0.0f); setPlain (r.p, "o1lvl", 1.0f); setPlain (r.p, "cut", 250.0f);
            setPlain (r.p, "mwcut", 1.0f);
            return r.play ({ ccEv (0, 1, mw), noteOnEv (0, 48, 110) }, 1.0, 0.4).rmsTail;
        };
        const double closed = level (0), open = level (127);
        check (open > closed * 1.3, "mod wheel opens the filter", juce::String (closed, 3) + " -> " + juce::String (open, 3));
    }

    // 24) Polifonikus CPU: 8 hangos akkord a Supersaw Lead presettel
    {
        SynthRig r (fs, 256);
        for (int i = 0; i < r.p.getNumPrograms(); ++i) if (r.p.getProgramName (i) == "Supersaw Lead") { r.p.setCurrentProgram (i); pump (80); }
        const auto a = std::chrono::steady_clock::now();
        std::vector<Ev> ev; for (int k = 0; k < 8; ++k) ev.push_back (noteOnEv (0, 48 + k * 2, 100));
        r.play (ev, 10.0, 0.5);
        const double pct = std::chrono::duration<double> (std::chrono::steady_clock::now() - a).count() / 10.0 * 100.0;
        check (pct < 50.0, "CPU: 8-note chord, Supersaw Lead (7 unison voices each)", juce::String (pct, 2) + " % of one core (48 kHz, 256 block)");
    }

    std::printf ("=== %s (%d failure%s) ===\n", failures == 0 ? "ALL PASSED" : "SOME FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
