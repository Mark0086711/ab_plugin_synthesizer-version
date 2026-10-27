// Önálló (JUCE nélküli) tesztek az Fx.h építőköveihez.
// Fordítás: g++ -O2 -std=c++17 -ISource tests/fx_test.cpp -o fx_test && ./fx_test
#include "Fx.h"
#include "PitchTracker.h"
#include <cstdio>
#include <random>
#include <string>

using namespace a2b;
static const double kPi = 3.14159265358979323846;
static int failures = 0;

static void check (bool ok, const std::string& name, const std::string& detail)
{
    std::printf ("[%s] %-52s %s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.c_str());
    if (! ok) ++failures;
}

static std::string fmt (const char* f, double a = 0, double b = 0, double c = 0)
{
    char buf[200]; std::snprintf (buf, sizeof buf, f, a, b, c); return buf;
}

static double goertzel (const std::vector<float>& x, double fs, double f)
{
    const double w = 2.0 * kPi * f / fs, coef = 2.0 * std::cos (w);
    double s0 = 0, s1 = 0, s2 = 0;
    for (float v : x) { s0 = v + coef * s1 - s2; s2 = s1; s1 = s0; }
    const double p = s1 * s1 + s2 * s2 - coef * s1 * s2;
    return p / ((double) x.size() * (double) x.size());
}

static double rms (const std::vector<float>& x, size_t a, size_t b)
{
    double e = 0; for (size_t i = a; i < b; ++i) e += (double) x[i] * x[i];
    return std::sqrt (e / (double) (b - a));
}

static double db (double v) { return 20.0 * std::log10 (std::max (v, 1e-12)); }

// 8x túlmintavételezett valódi csúcs (hosszú ablakos sinc)
static float truePeak (const std::vector<float>& x, size_t from)
{
    float pk = 0.0f;
    const int N = 16;
    for (size_t n = from; n + N + 1 < x.size(); ++n)
        for (int f = 0; f < 8; ++f)
        {
            double acc = 0;
            for (int k = -N + 1; k <= N; ++k)
            {
                const double t = (double) k - f / 8.0;
                const double sinc = std::abs (t) < 1e-9 ? 1.0 : std::sin (kPi * t) / (kPi * t);
                const double w = 0.5 * (1.0 + std::cos (kPi * t / (double) N));
                acc += x[n + (size_t) k] * sinc * w;
            }
            pk = std::max (pk, (float) std::abs (acc));
        }
    return pk;
}

int main()
{
    const double fs = 44100.0;
    std::printf ("=== Audio2Bass FX block tests ===\n");

    // ---- Vocoder ----
    {
        Vocoder v; v.prepare (fs); v.setParams (6.0f, 30.0f);
        std::mt19937 rng (5); std::normal_distribution<float> nd (0.0f, 0.3f);
        const size_t n = (size_t) fs * 2;
        std::vector<float> out (n);
        for (size_t i = 0; i < n; ++i)
        {
            const float src = (float) (0.5 * std::sin (2.0 * kPi * 1000.0 * (double) i / fs));
            const float c = nd (rng);
            float ol, orr; v.process (src, c, c, ol, orr);
            out[i] = ol;
        }
        std::vector<float> tail (out.begin() + (long) fs, out.end());
        const double near = (goertzel (tail, fs, 900) + goertzel (tail, fs, 1000) + goertzel (tail, fs, 1100)) / 3.0;
        const double far  = (goertzel (tail, fs, 2800) + goertzel (tail, fs, 3000) + goertzel (tail, fs, 3200)) / 3.0;
        const double dB = 10.0 * std::log10 (near / std::max (far, 1e-20));
        check (dB > 15.0, "vocoder: 1 kHz source shapes a noise carrier", fmt ("energy near 1 kHz is %.1f dB above 3 kHz", dB));

        Vocoder v2; v2.prepare (fs); v2.setParams (6.0f, 30.0f);
        double e = 0;
        for (size_t i = 0; i < n; ++i) { float ol, orr; v2.process (0.0f, nd (rng), 0.0f, ol, orr); if (i > 4410) e += ol * ol; }
        check (std::sqrt (e / (double) n) < 1.0e-3, "vocoder: silent source -> silent output", fmt ("rms %.6f", std::sqrt (e / (double) n)));
    }

    // ---- MultiBand: neutrális átvitel, telítés, OTT ----
    {
        bool flat = true; std::string det;
        for (double f : { 50.0, 150.0, 400.0, 1500.0, 4000.0 })
        {
            MultiBand m; m.prepare (fs); m.setCrossovers (150.0f, 1500.0f); m.setDrive (0, 0, 0); m.setOtt (0.0f, 100.0f);
            const size_t n = (size_t) fs;
            std::vector<float> in (n), out (n);
            for (size_t i = 0; i < n; ++i)
            {
                in[i] = (float) (0.3 * std::sin (2.0 * kPi * f * (double) i / fs));
                float l = in[i], r = in[i]; m.process (l, r); out[i] = l;
            }
            const double d = db (rms (out, n / 4, n)) - db (rms (in, n / 4, n));
            det += fmt ("%.0fHz:%+.2f ", f, d);
            if (std::abs (d) > 1.5) flat = false;
        }
        check (flat, "multiband: neutral settings are transparent (+/-1.5 dB)", det);

        auto thirdHarm = [&] (float drive)
        {
            MultiBand m; m.prepare (fs); m.setCrossovers (150.0f, 1500.0f); m.setDrive (0, drive, 0); m.setOtt (0.0f, 100.0f);
            const size_t n = (size_t) fs; std::vector<float> out (n);
            for (size_t i = 0; i < n; ++i)
            { float l = (float) (0.3 * std::sin (2.0 * kPi * 400.0 * (double) i / fs)), r = l; m.process (l, r); out[i] = l; }
            std::vector<float> tail (out.begin() + (long) n / 4, out.end());
            return 10.0 * std::log10 (goertzel (tail, fs, 1200) / std::max (goertzel (tail, fs, 400), 1e-20));
        };
        const double clean = thirdHarm (0.0f), driven = thirdHarm (1.0f);
        check (driven > clean + 20.0, "multiband: mid-band saturation adds harmonics", fmt ("3rd harmonic %.1f dB -> %.1f dB", clean, driven));

        auto levelWith = [&] (double amp, float ott)
        {
            MultiBand m; m.prepare (fs); m.setCrossovers (150.0f, 1500.0f); m.setDrive (0, 0, 0); m.setOtt (ott, 100.0f);
            const size_t n = (size_t) fs * 2; std::vector<float> out (n), in (n);
            for (size_t i = 0; i < n; ++i)
            { in[i] = (float) (amp * std::sin (2.0 * kPi * 400.0 * (double) i / fs)); float l = in[i], r = l; m.process (l, r); out[i] = l; }
            return db (rms (out, n / 2, n)) - db (rms (in, n / 2, n));
        };
        const double quietGain = levelWith (0.00316, 1.0f), loudGain = levelWith (0.9, 1.0f);
        check (quietGain > 3.0 && loudGain < -6.0, "OTT: lifts quiet, tames loud", fmt ("quiet %+.1f dB, loud %+.1f dB", quietGain, loudGain));
    }

    // ---- TransientShaper ----
    {
        auto run = [&] (float atk, float sus, double& attackLvl, double& bodyLvl)
        {
            TransientShaper t; t.prepare (fs); t.setParams (atk, sus);
            const size_t n = (size_t) fs; std::vector<float> out (n);
            const size_t on = 4410;
            for (size_t i = 0; i < n; ++i)
            {
                float x = i < on ? 0.0f : (float) (0.4 * std::sin (2.0 * kPi * 100.0 * (double) (i - on) / fs));
                float l = x, r = x; t.process (l, r); out[i] = l;
            }
            attackLvl = rms (out, on, on + 441);               // az első 10 ms
            bodyLvl = rms (out, on + 17640, on + 30000);       // jóval később
        };
        double a0, b0, aU, bU, aD, bD, aS, bS;
        run (0, 0, a0, b0); run (1, 0, aU, bU); run (-1, 0, aD, bD); run (0, 1, aS, bS);
        check (db (aU) - db (a0) > 1.5 && std::abs (db (bU) - db (b0)) < 1.0, "transient: attack +1 lifts the onset only",
               fmt ("onset %+.1f dB, body %+.1f dB", db (aU) - db (a0), db (bU) - db (b0)));
        check (db (aD) - db (a0) < -1.5 && std::abs (db (bD) - db (b0)) < 1.0, "transient: attack -1 softens the onset only",
               fmt ("onset %+.1f dB, body %+.1f dB", db (aD) - db (a0), db (bD) - db (b0)));
        check (db (bS) - db (b0) > 3.0, "transient: sustain +1 lifts the body", fmt ("body %+.1f dB", db (bS) - db (b0)));
    }

    // ---- TruePeakLimiter ----
    {
        const float ceilLin = std::pow (10.0f, -0.3f / 20.0f);
        auto runLim = [&] (const std::vector<float>& in, bool active)
        {
            TruePeakLimiter lim; lim.prepare (fs); lim.setParams (-0.3f, 80.0f);
            std::vector<float> out (in.size());
            for (size_t i = 0; i < in.size(); ++i) { float l = in[i], r = in[i]; lim.process (l, r, active); out[i] = l; }
            return out;
        };
        const size_t n = (size_t) fs;

        { // átlátszóság
            std::vector<float> in (n);
            for (size_t i = 0; i < n; ++i) in[i] = (float) (0.5 * std::sin (2.0 * kPi * 220.0 * (double) i / fs));
            auto out = runLim (in, true);
            TruePeakLimiter ref; ref.prepare (fs);
            const int D = ref.latencySamples();
            double maxErr = 0;
            for (size_t i = (size_t) D + 10; i < n; ++i) maxErr = std::max (maxErr, (double) std::abs (out[i] - in[i - (size_t) D]));
            check (maxErr < 1.0e-5, "limiter: transparent below the ceiling", fmt ("max error %.2e (delay %.0f samples)", maxErr, (double) D));
        }
        { // túlvezérelt szinusz
            std::vector<float> in (n);
            for (size_t i = 0; i < n; ++i) in[i] = (float) (1.5 * std::sin (2.0 * kPi * 1000.0 * (double) i / fs));
            auto out = runLim (in, true);
            float pk = 0; for (size_t i = 2000; i < n; ++i) pk = std::max (pk, std::abs (out[i]));
            check (pk <= ceilLin + 1.0e-4f, "limiter: sample peaks never exceed the ceiling", fmt ("peak %.4f (ceiling %.4f)", pk, ceilLin));
        }
        { // mintaközi (inter-sample) csúcs: fs/4 szinusz 45 fokos fázissal
            std::vector<float> in (n);
            for (size_t i = 0; i < n; ++i) in[i] = (float) (1.2 * std::sin (2.0 * kPi * (fs / 4.0) * (double) i / fs + kPi / 4.0));
            float inSample = 0; for (float v : in) inSample = std::max (inSample, std::abs (v));
            auto out = runLim (in, true);
            const float tpIn = truePeak (in, 2000), tpOut = truePeak (out, 2000);
            check (tpOut <= ceilLin * 1.03f && tpIn > 1.15f, "limiter: inter-sample peaks are controlled",
                   fmt ("sample peak %.2f, true peak %.2f -> %.3f (ceiling %.3f)", inSample, tpIn, tpOut) + fmt (" / %.3f", ceilLin));
        }
        { // zajlöket
            std::mt19937 rng (2); std::normal_distribution<float> nd (0.0f, 0.8f);
            std::vector<float> in (n);
            for (auto& v : in) v = nd (rng);
            auto out = runLim (in, true);
            float pk = 0; for (size_t i = 2000; i < n; ++i) pk = std::max (pk, std::abs (out[i]));
            check (pk <= ceilLin + 1.0e-4f, "limiter: loud noise stays under the ceiling", fmt ("peak %.4f", pk));
        }
    }

    // ---- PitchSyncShifter ----
    {
        auto harmonic = [&] (double f0, size_t i)
        {
            double v = 0; for (int h = 1; h <= 4; ++h) v += (1.0 / h) * std::sin (2.0 * kPi * f0 * h * (double) i / fs);
            return (float) (0.3 * v);
        };
        auto shift = [&] (double f0, float ratio, size_t n)
        {
            PitchSyncShifter sh; sh.prepare (fs); sh.setBaseDelayMs (65.0f);
            const float T = (float) (fs / f0);
            std::vector<float> out (n);
            for (size_t i = 0; i < n; ++i) out[i] = sh.process (harmonic (f0, i), ratio, T);
            return out;
        };
        auto trackHz = [&] (const std::vector<float>& x)
        {
            PitchTracker t; t.prepare (fs);
            PitchTracker::Settings st; st.minHz = 40.0f; t.setSettings (st);
            for (float v : x) t.push (v);
            return t.voiced() ? t.pitchHz() : 0.0f;
        };
        const size_t n = (size_t) fs * 2;

        { // ratio = 1: átlátszó, csak késleltet
            PitchSyncShifter sh; sh.prepare (fs); sh.setBaseDelayMs (100.0f); sh.reset();     // 4410 minta: egész számú késleltetés
            const size_t D = (size_t) std::lround (100.0 * 0.001 * fs);
            double maxErr = 0;
            std::vector<float> in (n);
            for (size_t i = 0; i < n; ++i) in[i] = harmonic (220.0, i);
            for (size_t i = 0; i < n; ++i) { const float o = sh.process (in[i], 1.0f, (float) (fs / 220.0)); if (i > D + 10) maxErr = std::max (maxErr, (double) std::abs (o - in[i - D])); }
            check (maxErr < 1.0e-3, "shifter: ratio 1 is a clean delay", fmt ("max error %.2e", maxErr));
        }
        { // oktáv le
            auto out = shift (220.0, 0.5f, n);
            std::vector<float> tail (out.begin() + (long) fs, out.end());
            const double p110 = goertzel (tail, fs, 110), p220 = goertzel (tail, fs, 220), p55 = goertzel (tail, fs, 55), p165 = goertzel (tail, fs, 165);
            const float hz = trackHz (out);
            check (p110 > 2.5 * p220 && p110 > 30.0 * std::max (p55, p165) && std::abs (1200.0 * std::log2 (hz / 110.0)) < 15.0,
                   "shifter: octave down (220 -> 110 Hz)", fmt ("tracked %.2f Hz, 110 Hz is %.1f dB above 55 Hz", hz, 10.0 * std::log10 (p110 / std::max (p55, 1e-20))));
            const double ratioDb = db (rms (tail, 0, tail.size())) - db (0.3 * std::sqrt (0.5 * (1 + 0.25 + 1.0 / 9 + 1.0 / 16)));
            check (std::abs (ratioDb) < 2.0, "shifter: level is preserved (octave down)", fmt ("%+.2f dB", ratioDb));
        }
        { // oktáv fel
            auto out = shift (220.0, 2.0f, n);
            const float hz = trackHz (out);
            check (std::abs (1200.0 * std::log2 (hz / 440.0)) < 15.0, "shifter: octave up (220 -> 440 Hz)", fmt ("tracked %.2f Hz", hz));
        }
        { // két oktáv le
            auto out = shift (330.0, 0.25f, n);
            const float hz = trackHz (out);
            check (std::abs (1200.0 * std::log2 (hz / 82.5)) < 20.0, "shifter: two octaves down (330 -> 82.5 Hz)", fmt ("tracked %.2f Hz", hz));
        }
        { // kvint le (0.75), szinusz
            PitchSyncShifter sh; sh.prepare (fs); sh.setBaseDelayMs (65.0f);
            std::vector<float> out (n);
            for (size_t i = 0; i < n; ++i) out[i] = sh.process ((float) (0.5 * std::sin (2.0 * kPi * 200.0 * (double) i / fs)), 0.75f, (float) (fs / 200.0));
            std::vector<float> tail (out.begin() + (long) fs, out.end());
            const double p150 = goertzel (tail, fs, 150), p200 = goertzel (tail, fs, 200);
            check (p150 > 30.0 * p200, "shifter: 200 Hz sine shifted by 0.75 -> 150 Hz", fmt ("150 Hz is %.1f dB above 200 Hz", 10.0 * std::log10 (p150 / std::max (p200, 1e-20))));
        }
        { // folyamatos arányváltás (glide): nincs ugrás
            PitchSyncShifter sh; sh.prepare (fs); sh.setBaseDelayMs (65.0f);
            float prev = 0, maxStep = 0, maxIn = 0;
            for (size_t i = 0; i < n; ++i)
            {
                const float in = (float) (0.5 * std::sin (2.0 * kPi * 150.0 * (double) i / fs));
                const float ratio = 1.0f - 0.5f * std::min (1.0f, (float) i / (float) (fs * 0.5));
                const float o = sh.process (in, ratio, (float) (fs / 150.0));
                if (i > 6000) maxStep = std::max (maxStep, std::abs (o - prev));
                maxIn = std::max (maxIn, (float) (0.5 * 2.0 * kPi * 150.0 / fs));
                prev = o;
            }
            check (maxStep < 2.0f * maxIn, "shifter: glide of the ratio is click-free", fmt ("max step %.4f (input slope %.4f)", maxStep, maxIn));
        }
    }

    std::printf ("=== %s (%d failure%s) ===\n", failures == 0 ? "ALL PASSED" : "SOME FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
