// Fejetlen (GUI nélküli) teszt a teljes Audio2BassProcessor-ra.
// A GitHub Actions "tests" workflow építi és futtatja (A2B_BUILD_TESTS=ON).
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <chrono>
#include <cstdio>
#include <random>

static int failures = 0;
static const double kPi = 3.14159265358979323846;

static void check (bool ok, const juce::String& name, const juce::String& detail = {})
{
    std::printf ("[%s] %-52s %s\n", ok ? "PASS" : "FAIL", name.toRawUTF8(), detail.toRawUTF8());
    std::fflush (stdout);
    if (! ok) ++failures;
}

static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

static double goertzelPow (const std::vector<float>& x, double fs, double f)
{
    const double w = 2.0 * kPi * f / fs, coef = 2.0 * std::cos (w);
    double s0 = 0, s1 = 0, s2 = 0;
    for (float v : x) { s0 = v + coef * s1 - s2; s2 = s1; s1 = s0; }
    return (s1 * s1 + s2 * s2 - coef * s1 * s2) / ((double) x.size() * (double) x.size());
}

struct Stats { bool finite = true; float peak = 0.0f; double rmsTail = 0.0; float maxStep = 0.0f; double sideRms = 0.0, midRms = 0.0; };

struct Rig
{
    Audio2BassProcessor p;
    double sr; int bs, ch;
    double phase = 0.0;
    float lastSample = 0.0f;

    Rig (double sampleRate, int blockSize, int channels) : sr (sampleRate), bs (blockSize), ch (channels)
    {
        p.setPlayConfigDetails (ch, ch, sr, bs);
        p.prepareToPlay (sr, bs);
    }

    // seconds hosszú szinuszt dolgoz fel; a "tail" részre RMS-t számol
    Stats run (double seconds, double freq, double amp = 0.5, double tailSeconds = 0.5, std::vector<float>* capture = nullptr)
    {
        Stats s;
        juce::AudioBuffer<float> buf (ch, bs);
        juce::MidiBuffer midi;
        const long total = (long) (seconds * sr);
        const long tailStart = total - (long) (tailSeconds * sr);
        double e = 0.0, sideE = 0.0, midE = 0.0; long cnt = 0;

        for (long pos = 0; pos < total; pos += bs)
        {
            const int n = (int) std::min<long> (bs, total - pos);
            buf.setSize (ch, n, false, false, true);
            for (int i = 0; i < n; ++i)
            {
                const float x = (float) (amp * std::sin (phase));
                phase += 2.0 * kPi * freq / sr;
                for (int c = 0; c < ch; ++c) buf.setSample (c, i, x);
            }
            p.processBlock (buf, midi);

            for (int i = 0; i < n; ++i)
            {
                const float v = buf.getSample (0, i);
                if (! std::isfinite (v)) s.finite = false;
                s.peak = std::max (s.peak, std::abs (v));
                s.maxStep = std::max (s.maxStep, std::abs (v - lastSample));
                lastSample = v;
                if (pos + i >= tailStart)
                {
                    e += (double) v * v; ++cnt;
                    if (capture != nullptr) capture->push_back (v);
                    if (ch >= 2)
                    {
                        const float rr = buf.getSample (1, i);
                        sideE += (double) (v - rr) * (v - rr);
                        midE  += (double) (v + rr) * (v + rr);
                    }
                }
            }
        }
        s.rmsTail = cnt ? std::sqrt (e / (double) cnt) : 0.0;
        s.sideRms = cnt ? std::sqrt (sideE / (double) cnt) : 0.0;
        s.midRms  = cnt ? std::sqrt (midE  / (double) cnt) : 0.0;
        return s;
    }
};

static int findProgram (Audio2BassProcessor& p, const juce::String& name)
{
    for (int i = 0; i < p.getNumPrograms(); ++i)
        if (p.getProgramName (i) == name) return i;
    return 0;
}

static void setPlain (Audio2BassProcessor& p, const char* id, float plain)
{
    if (auto* r = p.apvts.getParameter (id)) r->setValueNotifyingHost (r->convertTo0to1 (plain));
}

static void randomiseAll (Audio2BassProcessor& p, std::mt19937& rng)
{
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    for (auto* prm : p.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            r->setValueNotifyingHost (u (rng));
    if (auto* lim = p.apvts.getParameter ("limit")) lim->setValueNotifyingHost (1.0f);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::printf ("=== Audio2Bass processor tests ===\n");

    // 1) Alap követés + kimenet
    {
        Rig r (48000.0, 256, 2);
        auto s = r.run (1.5, 82.41);
        const float hz = r.p.info.hz.load();
        const float e = hz > 0 ? 1200.0f * std::log2 (hz / 82.41f) : 9999.0f;
        check (s.finite && s.peak <= 1.001f, "default: output finite and limited", juce::String (s.peak, 3));
        check (r.p.info.voiced.load() && std::abs (e) < 15.0f, "default: detects 82.41 Hz", juce::String (hz, 2) + " Hz");
        check (s.rmsTail > 0.01, "default: bass is audible", "rms " + juce::String (s.rmsTail, 4));
    }

    // 2) Blokkméretek (a 3000 és 4096 a belső darabolást teszteli)
    for (int bs : { 1, 7, 64, 256, 512, 3000, 4096 })
    {
        Rig r (44100.0, bs, 2);
        auto s = r.run (1.0, 110.0, 0.5, 0.3);
        check (s.finite && s.peak <= 1.001f && s.rmsTail > 0.01, "block size " + juce::String (bs),
               "peak " + juce::String (s.peak, 3) + " rms " + juce::String (s.rmsTail, 3));
    }

    // 3) Mintavételi frekvenciák
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0 })
    {
        Rig r (sr, 256, 2);
        r.run (1.5, 98.0);
        const float hz = r.p.info.hz.load();
        const float e = hz > 0 ? 1200.0f * std::log2 (hz / 98.0f) : 9999.0f;
        check (r.p.info.voiced.load() && std::abs (e) < 20.0f, "sample rate " + juce::String ((int) sr), juce::String (hz, 2) + " Hz");
    }

    // 4) Mono működés
    {
        Rig r (48000.0, 256, 1);
        auto s = r.run (1.5, 82.41);
        check (s.finite && s.rmsTail > 0.01 && s.peak <= 1.001f, "mono layout", "rms " + juce::String (s.rmsTail, 3));
    }

    // 5) Csend
    {
        Rig r (48000.0, 256, 2);
        auto s = r.run (1.0, 82.41, 0.0, 0.3);
        check (s.finite && s.peak < 1.0e-3f, "silence in -> silence out", "peak " + juce::String (s.peak, 6));
    }

    // 6) Paraméter-automatizálás blokkonként
    {
        Rig r (48000.0, 256, 2);
        std::mt19937 rng (3);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        const char* ids[] = { "cut", "res", "drive", "mix", "outgain", "o2lvl", "fm", "lcut", "ftype", "o1wave",
                              "uni", "unid", "dtype", "bits", "crate", "tone", "chmix", "boost", "eqg", "width", "hp" };
        bool fin = true; float pk = 0.0f;
        for (int block = 0; block < 150; ++block)
        {
            for (auto* id : ids)
                if (auto* prm = r.p.apvts.getParameter (id)) prm->setValueNotifyingHost (u (rng));
            auto s = r.run (0.02, 82.41, 0.5, 0.01);
            fin = fin && s.finite; pk = std::max (pk, s.peak);
        }
        check (fin && pk <= 1.001f, "automation of 21 parameters", "peak " + juce::String (pk, 3));
    }

    // 7) Véletlen paraméterkombinációk
    {
        std::mt19937 rng (11);
        bool fin = true; float pk = 0.0f; int bad = 0;
        for (int t = 0; t < 40; ++t)
        {
            Rig r (48000.0, 256, 2);
            randomiseAll (r.p, rng);
            auto s = r.run (0.4, 82.41, 0.5, 0.1);
            if (! s.finite) { fin = false; ++bad; }
            pk = std::max (pk, s.peak);
        }
        check (fin && pk <= 1.001f, "40 random parameter sets (finite, limited)", "non-finite sets " + juce::String (bad) + ", peak " + juce::String (pk, 3));
    }

    // 8) Presetek: betöltés, állapotmentés és visszaállítás
    {
        Rig r (48000.0, 256, 2);
        const int num = r.p.getNumPrograms();
        bool allOk = true; juce::String firstBad;

        for (int i = 0; i < num; ++i)
        {
            r.p.setCurrentProgram (i);
            pump (80);

            std::vector<float> before;
            for (auto* prm : r.p.getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm)) before.push_back (rp->getValue());

            auto s = r.run (0.3, 82.41, 0.5, 0.1);

            juce::MemoryBlock mb;
            r.p.getStateInformation (mb);
            Rig q (48000.0, 256, 2);
            q.p.setStateInformation (mb.getData(), (int) mb.getSize());

            std::vector<float> after;
            for (auto* prm : q.p.getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm)) after.push_back (rp->getValue());

            bool same = before.size() == after.size() && q.p.getCurrentProgram() == i;
            for (size_t k = 0; same && k < before.size(); ++k) same = std::abs (before[k] - after[k]) < 1.0e-5f;

            if (! (same && s.finite && s.peak <= 1.001f)) { allOk = false; if (firstBad.isEmpty()) firstBad = r.p.getProgramName (i); }
        }
        check (allOk, "all " + juce::String (num) + " presets: load, finite, save/restore", firstBad.isEmpty() ? "" : "first bad: " + firstBad);

        r.p.setCurrentProgram (findProgram (r.p, "Reese Bass")); pump (80);
        const float o2 = r.p.apvts.getRawParameterValue ("o2lvl")->load();
        check (std::abs (o2 - 0.7f) < 1.0e-3f, "preset 'Reese Bass' really sets o2lvl = 0.7", juce::String (o2, 3));
    }

    // 9) Presetváltás kattogása (tájékoztató mérés)
    {
        Rig r (48000.0, 128, 2);
        r.run (0.6, 82.41);
        float maxStep = 0.0f;
        r.p.setCurrentProgram (findProgram (r.p, "808 Punch"));
        for (int k = 0; k < 20; ++k)
        {
            auto s = r.run (0.01, 82.41, 0.5, 0.005);
            maxStep = std::max (maxStep, s.maxStep);
            pump (5);
        }
        std::printf ("[INFO] max sample-to-sample step during preset change: %.3f (full scale = 2.0)\n", maxStep);
        check (maxStep < 1.0f, "preset change: no full-scale discontinuity", juce::String (maxStep, 3));
    }

    // 10) CPU-terhelés
    for (const char* presetName : { "Init Sub Bass", "Super Saw Bass", "Wide Mid Bass", "Distorted Trap 808" })
    {
        Rig r (48000.0, 256, 2);
        const int preset = findProgram (r.p, presetName);
        r.p.setCurrentProgram (preset); pump (80);
        const auto a = std::chrono::steady_clock::now();
        r.run (10.0, 82.41);
        const auto b = std::chrono::steady_clock::now();
        const double secs = std::chrono::duration<double> (b - a).count();
        const double pct = secs / 10.0 * 100.0;
        check (pct < 30.0, "CPU preset '" + r.p.getProgramName (preset) + "'", juce::String (pct, 2) + " % of one core (48 kHz, 256 block)");
    }

    // 11) Mono low end: a crossover alatt a jel mono, felette sztereó maradhat
    {
        auto measure = [] (bool mono)
        {
            Rig r (48000.0, 256, 2);
            setPlain (r.p, "uni", 7.0f); setPlain (r.p, "unid", 40.0f); setPlain (r.p, "unis", 1.0f);
            setPlain (r.p, "o1lvl", 0.8f); setPlain (r.p, "sublvl", 0.0f);
            setPlain (r.p, "cut", 400.0f); setPlain (r.p, "mfreq", 300.0f);
            setPlain (r.p, "monosub", mono ? 1.0f : 0.0f);
            auto s = r.run (1.5, 55.0, 0.5, 0.5);
            return s.sideRms / std::max (1.0e-9, s.midRms);
        };
        const double on = measure (true), off = measure (false);
        check (on < 0.35 && on < off, "mono low end narrows the stereo image",
               "side/mid ON " + juce::String (on, 3) + " vs OFF " + juce::String (off, 3));
    }

    // 12) Jegyhossz: a beállított idő után a hang elhal, nélküle szól
    {
        Rig a (48000.0, 256, 2);
        setPlain (a.p, "nlen", 200.0f); setPlain (a.p, "sus", 1.0f); setPlain (a.p, "rel", 20.0f);
        auto sa = a.run (1.5, 82.41, 0.5, 0.4);

        Rig b (48000.0, 256, 2);
        setPlain (b.p, "nlen", 0.0f); setPlain (b.p, "sus", 1.0f);
        auto sb = b.run (1.5, 82.41, 0.5, 0.4);
        check (sa.rmsTail < 0.005 && sb.rmsTail > 0.01, "note length ends the note (200 ms)",
               "with " + juce::String (sa.rmsTail, 4) + ", without " + juce::String (sb.rmsTail, 4));
    }

    // 13) Layer Split + sub drive: tiszta, hallható, korlátozott kimenet
    {
        Rig r (48000.0, 256, 2);
        r.p.setCurrentProgram (findProgram (r.p, "Distorted Trap 808")); pump (80);
        auto s = r.run (1.5, 82.41, 0.5, 0.5);
        check (s.finite && s.peak <= 1.001f && s.rmsTail > 0.005, "Distorted Trap 808: layered, finite, limited",
               "peak " + juce::String (s.peak, 3) + " rms " + juce::String (s.rmsTail, 4));
    }

    // 14) Felhasználói preset: mentés -> módosítás -> betöltés -> törlés
    {
        Rig r (48000.0, 256, 2);
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("a2b_test_presets_" + juce::String (juce::Random::getSystemRandom().nextInt (1000000)));
        r.p.setUserPresetDir (dir);

        setPlain (r.p, "cut", 1234.0f); setPlain (r.p, "drive", 0.77f);
        const bool saved = r.p.saveUserPreset ("My Test", "User");
        setPlain (r.p, "cut", 200.0f); setPlain (r.p, "drive", 0.1f);

        bool found = false; Audio2BassProcessor::PresetEntry entry;
        for (auto& e : r.p.listPresets())
            if (e.user && e.name == "My Test" && e.category == "User") { found = true; entry = e; }
        if (found) { r.p.loadPreset (entry); pump (80); }

        const float cut = r.p.apvts.getRawParameterValue ("cut")->load();
        const float dr  = r.p.apvts.getRawParameterValue ("drive")->load();
        check (saved && found && std::abs (cut - 1234.0f) < 3.0f && std::abs (dr - 0.77f) < 0.01f,
               "user preset: save, change, load restores values", "cut " + juce::String (cut, 1) + ", drive " + juce::String (dr, 3));
        check (r.p.getCurrentPresetName() == "My Test", "user preset: name is remembered", r.p.getCurrentPresetName());

        const bool deleted = r.p.deleteUserPreset ("My Test", "User");
        bool still = false;
        for (auto& e : r.p.listPresets()) if (e.user && e.name == "My Test") still = true;
        check (deleted && ! still, "user preset: delete removes it from the list");
        dir.deleteRecursively();
    }

    // 15) Gyári presetek kategóriái
    {
        Audio2BassProcessor p;
        int factory = 0; bool allCat = true;
        for (auto& e : p.listPresets()) if (! e.user) { ++factory; if (e.category.isEmpty()) allCat = false; }
        check (factory >= 21 && allCat, "factory presets are categorised", juce::String (factory) + " presets");
    }

    // 16) A/B összehasonlítás
    {
        Rig r (48000.0, 256, 2);
        setPlain (r.p, "cut", 300.0f);
        r.p.abSelect (1); pump (80);                       // A elmentve, B = másolat
        setPlain (r.p, "cut", 900.0f);                     // B módosítása
        r.p.abSelect (0); pump (80);
        const float a = r.p.apvts.getRawParameterValue ("cut")->load();
        r.p.abSelect (1); pump (80);
        const float b = r.p.apvts.getRawParameterValue ("cut")->load();
        check (std::abs (a - 300.0f) < 2.0f && std::abs (b - 900.0f) < 3.0f, "A/B slots keep separate settings",
               "A " + juce::String (a, 1) + ", B " + juce::String (b, 1));
    }

    // 17) 808 Character makró hangolhatóan változtat a hangon
    {
        Rig a (48000.0, 256, 2);
        auto sa = a.run (1.5, 82.41, 0.5, 0.5);
        Rig b (48000.0, 256, 2);
        setPlain (b.p, "char", 1.0f);
        auto sb = b.run (1.5, 82.41, 0.5, 0.5);
        const double diff = std::abs (sb.rmsTail - sa.rmsTail) / std::max (1.0e-9, sa.rmsTail);
        check (sa.finite && sb.finite && sb.peak <= 1.001f && diff > 0.02, "808 Character macro changes the sound",
               "rms " + juce::String (sa.rmsTail, 4) + " -> " + juce::String (sb.rmsTail, 4));
    }

    // 18) Új torzítástípusok (cső, szalag, dióda)
    for (int dt : { 4, 5, 6 })
    {
        Rig r (48000.0, 256, 2);
        setPlain (r.p, "dtype", (float) dt); setPlain (r.p, "drive", 1.0f);
        auto s = r.run (1.0, 82.41, 0.5, 0.4);
        check (s.finite && s.peak <= 1.001f && s.rmsTail > 0.005, "distortion type " + juce::String (dt) + " (tube/tape/diode)",
               "peak " + juce::String (s.peak, 3) + " rms " + juce::String (s.rmsTail, 4));
    }

    // 19) Harmonizálás: a kvint hozzáadása megváltoztatja a hangot
    {
        Rig a (48000.0, 256, 2);
        auto sa = a.run (1.5, 82.41, 0.5, 0.5);
        Rig b (48000.0, 256, 2);
        setPlain (b.p, "h1int", 3.0f); setPlain (b.p, "h1lvl", 1.0f);
        auto sb = b.run (1.5, 82.41, 0.5, 0.5);
        const double diff = std::abs (sb.rmsTail - sa.rmsTail) / std::max (1.0e-9, sa.rmsTail);
        check (sb.finite && sb.peak <= 1.001f && diff > 0.03, "harmony (fifth up) adds a voice",
               "rms " + juce::String (sa.rmsTail, 4) + " -> " + juce::String (sb.rmsTail, 4));
    }

    // 20) Pump: a szinkron ducking ritmikusan változtatja a hangerőt
    {
        auto ratioFor = [] (float depth)
        {
            Rig r (48000.0, 256, 2);
            setPlain (r.p, "pump", depth); setPlain (r.p, "pumprate", 2.0f);   // 1/4
            r.run (1.0, 82.41);
            double mn = 1.0e9, mx = 0.0;
            for (int w = 0; w < 20; ++w)
            {
                auto s = r.run (0.05, 82.41, 0.5, 0.05);
                mn = std::min (mn, s.rmsTail); mx = std::max (mx, s.rmsTail);
            }
            return mx / std::max (1.0e-9, mn);
        };
        const double on = ratioFor (1.0f), off = ratioFor (0.0f);
        check (on > 3.0 && off < 1.5, "pump ducks rhythmically (1/4 @ 120 bpm)", "max/min level ON " + juce::String (on, 2) + ", OFF " + juce::String (off, 2));
    }

    // 21) Sidechain bemenet: a külső jel elhalkítja a basszust (valódi 4 csatornás puffer)
    {
        auto levelWith = [] (float scLevel, bool& ok)
        {
            Audio2BassProcessor p;
            ok = p.getBus (true, 1) != nullptr && p.getBus (true, 1)->enable (true);
            p.prepareToPlay (48000.0, 256);
            setPlain (p, "scamt", 1.0f); setPlain (p, "scthr", -40.0f); setPlain (p, "screl", 50.0f);
            juce::AudioBuffer<float> buf (4, 256);
            juce::MidiBuffer midi;
            double ph = 0.0, e = 0.0; long cnt = 0;
            const int blocks = (int) (1.5 * 48000.0 / 256.0);
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < 256; ++i)
                {
                    const float x = (float) (0.5 * std::sin (ph));
                    const float k = (float) (scLevel * std::sin (2.0 * kPi * 60.0 * (double) (b * 256 + i) / 48000.0));
                    ph += 2.0 * kPi * 82.41 / 48000.0;
                    buf.setSample (0, i, x); buf.setSample (1, i, x);
                    buf.setSample (2, i, k); buf.setSample (3, i, k);
                }
                p.processBlock (buf, midi);
                if (b > blocks / 2) for (int i = 0; i < 256; ++i) { const float v = buf.getSample (0, i); e += (double) v * v; ++cnt; }
            }
            return std::sqrt (e / (double) cnt);
        };
        bool ok1 = false, ok2 = false;
        const double quiet = levelWith (0.0f, ok1), ducked = levelWith (0.5f, ok2);
        check (ok1 && ok2 && quiet > 0.01 && ducked < quiet * 0.25, "sidechain input ducks the bass",
               "no key " + juce::String (quiet, 4) + ", with key " + juce::String (ducked, 4));
    }

    // 22) Vocoder, többsáv, OTT és átmenet-formázó együtt: stabil, korlátozott kimenet
    {
        Rig r (48000.0, 256, 2);
        setPlain (r.p, "vocmix", 1.0f);
        setPlain (r.p, "mbon", 1.0f); setPlain (r.p, "mbd1", 0.5f); setPlain (r.p, "mbd2", 0.8f); setPlain (r.p, "mbd3", 0.5f); setPlain (r.p, "ott", 1.0f);
        setPlain (r.p, "tatk", 0.8f); setPlain (r.p, "tsus", 0.5f);
        setPlain (r.p, "o1lvl", 0.8f);
        auto s = r.run (1.5, 220.0, 0.5, 0.5);
        check (s.finite && s.peak <= 1.001f, "vocoder + multiband/OTT + transient: finite and limited", "peak " + juce::String (s.peak, 3));
    }

    // 23) True-peak limiter: a plafon betartása erős túlvezérlésnél
    {
        for (float ceilDb : { -0.3f, -6.0f })
        {
            Rig r (48000.0, 256, 2);
            setPlain (r.p, "ceil", ceilDb); setPlain (r.p, "outgain", 6.0f); setPlain (r.p, "drive", 0.8f);
            setPlain (r.p, "o1lvl", 1.0f); setPlain (r.p, "sublvl", 1.0f);
            auto s = r.run (1.5, 82.41, 0.5, 0.5);
            const float lim = std::pow (10.0f, ceilDb / 20.0f);
            check (s.finite && s.peak <= lim + 2.0e-3f && s.rmsTail > 0.01, "true-peak limiter holds ceiling " + juce::String (ceilDb, 1) + " dB",
                   "peak " + juce::String (s.peak, 4) + " (limit " + juce::String (lim, 4) + ")");
        }
    }

    // 24) Véletlenszerű preset: a zárolt csoport és a kimenet nem változik, a többi igen
    {
        Rig r (48000.0, 256, 2);
        const char* oscIds[] = { "o1wave", "o1lvl", "o2lvl", "uni", "sublvl", "subwave" };
        const char* neverIds[] = { "mix", "outgain", "pmin", "conf", "tplim", "vocmix", "scamt" };
        const char* filterIds[] = { "ftype", "cut", "res", "atk", "dec", "sus", "rel", "fenv" };

        auto snap = [&] (const char* const* ids, size_t n)
        {
            std::vector<float> v; for (size_t i = 0; i < n; ++i) v.push_back (r.p.apvts.getParameter (ids[i])->getValue()); return v;
        };
        const auto oscBefore = snap (oscIds, 6), neverBefore = snap (neverIds, 7), fltBefore = snap (filterIds, 8);

        std::array<bool, 6> locked {}; locked[0] = true;     // az oszcillátor-csoport zárolva
        r.p.randomize (locked, 1.0f); pump (80);

        const auto oscAfter = snap (oscIds, 6), neverAfter = snap (neverIds, 7), fltAfter = snap (filterIds, 8);
        bool oscSame = oscBefore == oscAfter, neverSame = neverBefore == neverAfter;
        int changed = 0; for (size_t i = 0; i < fltBefore.size(); ++i) if (std::abs (fltBefore[i] - fltAfter[i]) > 1.0e-4f) ++changed;

        auto s = r.run (0.8, 82.41, 0.5, 0.3);
        check (oscSame && neverSame && changed >= 3 && s.finite && s.rmsTail > 0.001, "randomize: locks and excluded groups respected",
               "locked unchanged " + juce::String ((int) oscSame) + ", excluded unchanged " + juce::String ((int) neverSame) + ", filter params changed " + juce::String (changed));
    }

    // 25) Morfolás A és B között
    {
        Rig r (48000.0, 256, 2);
        setPlain (r.p, "cut", 300.0f); setPlain (r.p, "drive", 0.1f);
        r.p.abSelect (1); pump (80);                    // A = (300, 0.1), B = másolat
        setPlain (r.p, "cut", 900.0f); setPlain (r.p, "drive", 0.5f);   // B módosítása
        r.p.beginMorph();
        auto cutNow = [&] { return r.p.apvts.getRawParameterValue ("cut")->load(); };
        auto drvNow = [&] { return r.p.apvts.getRawParameterValue ("drive")->load(); };
        r.p.setMorph (0.0f); const float c0 = cutNow(), d0 = drvNow();
        r.p.setMorph (1.0f); const float c1 = cutNow(), d1 = drvNow();
        r.p.setMorph (0.5f); const float cm = cutNow(), dm = drvNow();
        check (std::abs (c0 - 300.0f) < 3.0f && std::abs (c1 - 900.0f) < 5.0f && cm > 310.0f && cm < 890.0f
               && std::abs (d0 - 0.1f) < 0.01f && std::abs (d1 - 0.5f) < 0.01f && std::abs (dm - 0.3f) < 0.02f,
               "morph interpolates between A and B",
               "cut " + juce::String (c0, 0) + "/" + juce::String (cm, 0) + "/" + juce::String (c1, 0) + ", drive " + juce::String (dm, 2) + " at 0.5");
    }

    // 26) Alapból pontos hangmagasság-követés (nincs oktávhajtogatás), az eredeti réteg az eredeti hangot adja
    {
        auto measure = [] (int shiftOct, double inHz, double f1, double f2, double& p1, double& p2)
        {
            Rig r (48000.0, 256, 2);
            setPlain (r.p, "srclvl", 1.0f); setPlain (r.p, "o1lvl", 0.0f); setPlain (r.p, "sublvl", 0.0f);
            setPlain (r.p, "cut", 12000.0f); setPlain (r.p, "follow", 1.0f); setPlain (r.p, "shift", (float) shiftOct);
            std::vector<float> cap;
            auto s = r.run (2.0, inHz, 0.5, 0.5, &cap);
            p1 = goertzelPow (cap, 48000.0, f1); p2 = goertzelPow (cap, 48000.0, f2);
            return s.finite && s.peak <= 1.001f;
        };
        double a1 = 0, a2 = 0, b1 = 0, b2 = 0;
        const bool okA = measure (0, 300.0, 300.0, 150.0, a1, a2);
        const bool okB = measure (-1, 300.0, 150.0, 300.0, b1, b2);
        check (okA && a1 > 10.0 * a2, "default follows the exact pitch (300 Hz stays 300 Hz)",
               "300 Hz is " + juce::String (10.0 * std::log10 (a1 / std::max (a2, 1.0e-20)), 1) + " dB above 150 Hz");
        check (okB && b1 > 10.0 * b2, "original layer one octave down (300 -> 150 Hz)",
               "150 Hz is " + juce::String (10.0 * std::log10 (b1 / std::max (b2, 1.0e-20)), 1) + " dB above 300 Hz");
    }

    // 27) A régi presetek megtartják az oktávhajtogatást, az Original presetek nem
    {
        Rig r (48000.0, 256, 2);
        r.p.setCurrentProgram (findProgram (r.p, "Deep 808")); pump (80);
        const float legacyFold = r.p.apvts.getRawParameterValue ("fold")->load();
        const float legacyQuant = r.p.apvts.getRawParameterValue ("quant")->load();
        r.p.setCurrentProgram (findProgram (r.p, "Original Follow")); pump (80);
        const float origFold = r.p.apvts.getRawParameterValue ("fold")->load();
        const float origSrc = r.p.apvts.getRawParameterValue ("srclvl")->load();
        check (legacyFold > 0.5f && legacyQuant > 0.5f && origFold < 0.5f && origSrc > 0.9f, "legacy presets fold, Original presets follow exactly",
               "Deep 808 fold " + juce::String (legacyFold, 0) + ", Original Follow fold " + juce::String (origFold, 0) + " srclvl " + juce::String (origSrc, 1));
    }

    std::printf ("=== %s (%d failure%s) ===\n", failures == 0 ? "ALL PASSED" : "SOME FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
