// Önálló (JUCE nélküli) tesztek a polifonikus motorhoz (PolySynth.h).
// Fordítás: g++ -O2 -std=c++17 -ISource tests/poly_test.cpp -o poly_test && ./poly_test
#include "PolySynth.h"
#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace a2b;
static const double kPi = 3.14159265358979323846;
static int failures = 0;

static void check (bool ok, const std::string& name, const std::string& detail = "")
{
    std::printf ("[%s] %-56s %s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.c_str());
    if (! ok) ++failures;
}
static std::string fmt (const char* f, double a = 0, double b = 0, double c = 0)
{ char buf[220]; std::snprintf (buf, sizeof buf, f, a, b, c); return buf; }

static double goertzel (const std::vector<float>& x, size_t from, size_t to, double fs, double f)
{
    const double w = 2.0 * kPi * f / fs, coef = 2.0 * std::cos (w);
    double s0 = 0, s1 = 0, s2 = 0;
    for (size_t i = from; i < to; ++i) { s0 = x[i] + coef * s1 - s2; s2 = s1; s1 = s0; }
    const double n = (double) (to - from);
    return (s1 * s1 + s2 * s2 - coef * s1 * s2) / (n * n);
}
static double db (double v) { return 10.0 * std::log10 (std::max (v, 1e-20)); }
static double hz (int note) { return 440.0 * std::pow (2.0, (note - 69) / 12.0); }
static double rms (const std::vector<float>& x, size_t a, size_t b) { double e = 0; for (size_t i = a; i < b; ++i) e += (double) x[i] * x[i]; return std::sqrt (e / (double) (b - a)); }

struct Rig
{
    double fs; PolySynth syn; SynthParams P; SampleCtx C;
    std::vector<float> out; long pos = 0;

    explicit Rig (double sr = 48000.0) : fs (sr)
    {
        P.sr = sr;
        P.o1wave = 3; P.o2wave = 3; P.uniN = 1;
        P.atkSamples = 0.005f * (float) sr; P.decSamples = 0.1f * (float) sr; P.sus = 1.0f; P.relSamples = 0.05f * (float) sr;
        P.velAmt = 0.0f; P.f24 = true; P.res = 0.7f; P.driftCoef = 1.0f - std::exp (-1.0f / (0.6f * (float) sr));
        P.pdecCoef = std::exp (-1.0f / (0.08f * (float) sr)); P.fdecCoef = std::exp (-1.0f / (0.25f * (float) sr));
        C.o1l = 1.0f; C.o2l = 0.0f; C.subl = 0.0f; C.cutV = 12000.0f;
        syn.prepare (sr);
    }
    void run (double seconds)
    {
        const long n = (long) (seconds * fs);
        for (long i = 0; i < n; ++i) { float l, r; syn.process (P, C, l, r); out.push_back (l); }
        pos += n;
    }
    size_t at (double s) const { return (size_t) (s * fs); }
};

int main()
{
    std::printf ("=== Audio2Bass polyphonic engine tests ===\n");
    const double fs = 48000.0;

    { // akkord: három hang egyszerre
        Rig r; r.syn.noteOn (48, 110, r.P); r.syn.noteOn (52, 110, r.P); r.syn.noteOn (55, 110, r.P); r.run (1.0);
        const double a = goertzel (r.out, r.at (0.5), r.at (1.0), fs, hz (48)), b = goertzel (r.out, r.at (0.5), r.at (1.0), fs, hz (52)), c = goertzel (r.out, r.at (0.5), r.at (1.0), fs, hz (55));
        const double off = goertzel (r.out, r.at (0.5), r.at (1.0), fs, hz (50));
        const double mn = std::min ({ a, b, c }), mx = std::max ({ a, b, c });
        check (r.syn.activeVoices() == 3 && db (mn) > db (off) + 25.0 && db (mx) - db (mn) < 3.0, "polyphony: a C major chord sounds as three equal notes",
               fmt ("voices %.0f, notes +%.0f dB over the gap, spread %.1f dB", r.syn.activeVoices(), db (mn) - db (off), db (mx) - db (mn)));
    }

    { // a hangok egymástól függetlenül engednek fel
        Rig r; r.syn.noteOn (48, 110, r.P); r.syn.noteOn (52, 110, r.P); r.syn.noteOn (55, 110, r.P); r.run (0.5);
        r.syn.noteOff (52, r.P); r.run (0.5);
        const double a = goertzel (r.out, r.at (0.7), r.at (1.0), fs, hz (48)), b = goertzel (r.out, r.at (0.7), r.at (1.0), fs, hz (52)), c = goertzel (r.out, r.at (0.7), r.at (1.0), fs, hz (55));
        check (db (a) > db (b) + 20.0 && db (c) > db (b) + 20.0 && r.syn.activeVoices() == 2, "polyphony: releasing one key leaves the other two",
               fmt ("released note is %.0f dB lower", db (std::min (a, c)) - db (b)));
    }

    { // hangelvétel: 2 hang esetén a legrégebbi esik ki
        Rig r; r.P.voices = 2;
        r.syn.noteOn (48, 110, r.P); r.run (0.1); r.syn.noteOn (52, 110, r.P); r.run (0.1); r.syn.noteOn (55, 110, r.P); r.run (0.6);
        const double a = goertzel (r.out, r.at (0.5), r.at (0.9), fs, hz (48)), b = goertzel (r.out, r.at (0.5), r.at (0.9), fs, hz (52)), c = goertzel (r.out, r.at (0.5), r.at (0.9), fs, hz (55));
        check (r.syn.activeVoices() == 2 && db (a) < db (std::min (b, c)) - 25.0, "voice limit: the oldest note is stolen", fmt ("stolen note is %.0f dB lower", db (std::min (b, c)) - db (a)));
    }

    { // sustain pedál
        Rig r; r.syn.controller (64, 127, r.P); r.syn.noteOn (48, 110, r.P); r.run (0.2); r.syn.noteOff (48, r.P); r.run (0.5);
        const double held = rms (r.out, r.at (0.5), r.at (0.7));
        r.syn.controller (64, 0, r.P); r.run (0.4);
        const double after = rms (r.out, r.at (1.0), r.at (1.1));
        check (held > 0.2 && after < 0.002 && r.syn.activeVoices() == 0, "sustain pedal holds the note until released", fmt ("held %.3f, after pedal-up %.5f", held, after));
    }

    { // mono mód: csak az utolsó hang; felengedéskor visszatér
        Rig r; r.P.mode = 1;
        r.syn.noteOn (48, 110, r.P); r.run (0.3); r.syn.noteOn (55, 110, r.P); r.run (0.4);
        const double a1 = goertzel (r.out, r.at (0.5), r.at (0.7), fs, hz (48)), g1 = goertzel (r.out, r.at (0.5), r.at (0.7), fs, hz (55));
        r.syn.noteOff (55, r.P); r.run (0.4);
        const double a2 = goertzel (r.out, r.at (0.9), r.at (1.1), fs, hz (48)), g2 = goertzel (r.out, r.at (0.9), r.at (1.1), fs, hz (55));
        check (db (g1) > db (a1) + 30.0 && db (a2) > db (g2) + 30.0 && r.syn.activeVoices() == 1, "mono: last note wins, returns to the held note", "");
    }

    { // legato glide: a hangmagasság a két hang között halad át
        Rig r; r.P.mode = 2; r.P.glideCoef = 1.0f - std::exp (-1.0f / (0.1f * (float) fs));
        r.syn.noteOn (48, 110, r.P); r.run (0.5); r.syn.noteOn (60, 110, r.P); r.run (0.5);
        size_t a = r.at (0.5) + 2000, b = a + 2400; int zc = 0;
        for (size_t i = a + 1; i < b; ++i) if (r.out[i - 1] < 0 && r.out[i] >= 0) ++zc;
        const double mid = zc / ((b - a) / fs);
        const double endP = goertzel (r.out, r.at (0.9), r.at (1.0), fs, hz (60));
        check (mid > 140.0 && mid < 255.0 && db (endP) > -20.0, "legato glide passes through the pitches in between",
               fmt ("%.0f Hz between 130.8 and 261.6 Hz, lands on the target", mid));
    }

    { // teljes hangmagasság-tartomány
        bool ok = true; std::string det;
        for (int note : { 24, 36, 60, 84, 96, 108, 120 })
        {
            Rig r; r.syn.noteOn (note, 110, r.P); r.run (1.2);
            const double p = goertzel (r.out, r.at (0.5), r.at (1.2), fs, hz (note)), lo = goertzel (r.out, r.at (0.5), r.at (1.2), fs, hz (note) / 2.0);
            const double hi = hz (note) * 2.0 < fs * 0.45 ? goertzel (r.out, r.at (0.5), r.at (1.2), fs, hz (note) * 2.0) : 1e-20;
            if (! (db (p) > db (lo) + 25.0 && db (p) > db (hi) + 25.0 && db (p) > -15.0)) { ok = false; det += fmt ("note %.0f failed ", note); }
        }
        check (ok, "pitch range C1 .. C9 (up to ~8 kHz) is accurate", det);
    }

    { // velocity
        auto lvl = [&] (int vel) { Rig r; r.P.velAmt = 1.0f; r.syn.noteOn (48, vel, r.P); r.run (0.4); return rms (r.out, r.at (0.2), r.at (0.4)); };
        const double hi = lvl (127), lo = lvl (40);
        check (hi > 2.5 * lo && lo > 1e-4, "velocity scales the level per voice", fmt ("127: %.3f, 40: %.3f", hi, lo));
    }

    { // mod wheel -> szűrő nyitása
        auto lvl = [&] (int mw) { Rig r; r.P.o1wave = 0; r.C.cutV = 250.0f; r.P.mwCutOct = 3.0f; r.syn.controller (1, mw, r.P); r.syn.noteOn (48, 110, r.P); r.run (0.6); return rms (r.out, r.at (0.4), r.at (0.6)); };
        const double closed = lvl (0), open = lvl (127);
        check (open > closed * 1.3, "mod wheel opens the filter", fmt ("%.3f -> %.3f", closed, open));
    }

    { // pitch bend
        Rig r; r.syn.noteOn (57, 110, r.P); r.run (0.3); r.syn.pitchBend (16383, r.P); r.run (0.5);
        const double target = hz (57) * std::pow (2.0, 2.0 / 12.0);
        const double pb = goertzel (r.out, r.at (0.5), r.at (0.8), fs, target), p0 = goertzel (r.out, r.at (0.5), r.at (0.8), fs, hz (57));
        check (db (pb) > db (p0) + 20.0, "pitch bend +2 semitones", fmt ("%.2f Hz", target));
    }

    { // mintavételi frekvenciák
        bool ok = true;
        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            Rig r (sr); r.syn.noteOn (69, 100, r.P); r.run (0.5);
            const double p = goertzel (r.out, r.at (0.2), r.at (0.5), sr, 440.0), q = goertzel (r.out, r.at (0.2), r.at (0.5), sr, 880.0);
            if (db (p) < db (q) + 30.0) ok = false;
        }
        check (ok, "A4 = 440 Hz at 44.1 / 48 / 96 kHz", "");
    }

    { // felengedés után minden hang elnémul
        Rig r; for (int n : { 48, 52, 55, 59 }) r.syn.noteOn (n, 100, r.P);
        r.run (0.3); for (int n : { 48, 52, 55, 59 }) r.syn.noteOff (n, r.P); r.run (0.3);
        check (r.syn.activeVoices() == 0 && rms (r.out, r.at (0.5), r.at (0.6)) < 1e-4, "all voices go idle after release", fmt ("%.0f active", r.syn.activeVoices()));
    }

    { // véletlenszerű paraméterek és MIDI: soha nem NaN, nem robban
        std::mt19937 rng (4); std::uniform_real_distribution<float> u (0.0f, 1.0f);
        bool fin = true; float pk = 0.0f;
        for (int t = 0; t < 150; ++t)
        {
            Rig r; auto& P = r.P; auto& C = r.C;
            P.o1wave = (int) (u (rng) * 4.99f); P.o2wave = (int) (u (rng) * 4.99f); P.fm = u (rng) * 0.8f; P.uniN = 1 + (int) (u (rng) * 6.99f);
            for (int k = 0; k < 7; ++k) { P.uniRatio[k] = std::exp2 ((u (rng) - 0.5f) * 0.1f); P.uniGL[k] = u (rng); P.uniGR[k] = u (rng); }
            P.h1on = u (rng) > 0.5f; P.h1ratio = 1.5f; P.h1l = u (rng); P.h2on = u (rng) > 0.5f; P.h2ratio = 2.0f; P.h2l = u (rng);
            P.ftype = (int) (u (rng) * 2.99f); P.f24 = u (rng) > 0.5f; P.res = 0.5f + u (rng) * 9.5f; P.fenvAmt = (u (rng) - 0.5f) * 8.0f; P.fkey = u (rng);
            P.dtype = (int) (u (rng) * 6.99f); P.distPost = u (rng) > 0.5f; P.split = u (rng) > 0.5f; P.crateI = 1 + (int) (u (rng) * 6.0f); P.crushStep = u (rng) > 0.5f ? 32.0f : 0.0f;
            P.penvAmt = (u (rng) - 0.5f) * 60.0f; P.driftScale = u (rng) * 0.0069f; P.phrand = u (rng) > 0.5f; P.keysync = u (rng) > 0.5f;
            P.mode = (int) (u (rng) * 2.99f); P.voices = 1 + (int) (u (rng) * 15.99f); P.glideCoef = u (rng) > 0.5f ? 1.0f : 0.001f + u (rng) * 0.05f;
            P.lamp = u (rng); P.nlenSamples = u (rng) > 0.7f ? (int) (u (rng) * 5000.0f) : 0;
            C.o1l = u (rng); C.o2l = u (rng); C.subl = u (rng); C.noil = u (rng) * 0.5f; C.driveV = u (rng); C.cutV = 40.0f + u (rng) * 14000.0f; C.sdV = u (rng);
            for (int step = 0; step < 24; ++step)
            {
                const float a = u (rng);
                const int note = 12 + (int) (u (rng) * 100.0f);
                if (a < 0.45f) r.syn.noteOn (note, 1 + (int) (u (rng) * 126.0f), P);
                else if (a < 0.85f) r.syn.noteOff (note, P);
                else if (a < 0.90f) r.syn.controller (64, u (rng) > 0.5f ? 127 : 0, P);
                else if (a < 0.95f) r.syn.pitchBend ((int) (u (rng) * 16383.0f), P);
                else r.syn.controller (1, (int) (u (rng) * 127.0f), P);
                C.lfo = std::sin (step * 0.7f);
                r.run (0.01);
            }
            for (float v : r.out) { if (! std::isfinite (v)) fin = false; pk = std::max (pk, std::abs (v)); }
        }
        check (fin && pk < 60.0f, "150 random parameter sets with random MIDI: finite and bounded", fmt ("peak %.2f", pk));
    }

    { // CPU: 16 hang, 7 unison, 2 oszcillátor, 24 dB szűrő, torzítás, harmónia, drift
        Rig r; auto& P = r.P; auto& C = r.C;
        P.voices = 16; P.uniN = 7; P.o1wave = 0; P.o2wave = 1; C.o2l = 0.5f; P.f24 = true; P.dtype = 4; P.h1on = P.h2on = true; P.h1l = P.h2l = 0.4f;
        P.h1ratio = 1.5f; P.h2ratio = 2.0f; P.driftScale = 0.005f; P.sus = 1.0f; C.driveV = 0.5f; C.cutV = 3000.0f; P.crushStep = 0.0f;
        for (int k = 0; k < 7; ++k) { P.uniRatio[k] = std::exp2 ((k - 3) * 0.004f); P.uniGL[k] = P.uniGR[k] = 0.4f; }
        for (int k = 0; k < 16; ++k) r.syn.noteOn (36 + k * 3, 100, P);
        const auto t0 = std::chrono::steady_clock::now();
        r.run (2.0);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        const double pct = secs / 2.0 * 100.0;
        std::printf ("[INFO] worst case (16 voices x 7 unison x 2 osc + harmony + 24 dB filter + distortion): %.1f %% of one core at 48 kHz\n", pct);
        check (pct < 80.0, "CPU: worst-case chord fits in real time", fmt ("%.1f %% of one core", pct));
    }

    std::printf ("=== %s (%d failure%s) ===\n", failures == 0 ? "ALL PASSED" : "SOME FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
