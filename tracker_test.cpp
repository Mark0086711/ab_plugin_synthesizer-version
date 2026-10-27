// Önálló (JUCE nélküli) teszt az a2b::PitchTracker-hez.
// Fordítás: g++ -O2 -std=c++17 -ISource tests/tracker_test.cpp -o tracker_test && ./tracker_test
#include "PitchTracker.h"
#include <chrono>
#include <cstdio>
#include <functional>
#include <random>
#include <string>

using namespace a2b;

static int failures = 0;
static const double kPi = 3.14159265358979323846;

static float cents (float f, float ref) { return 1200.0f * std::log2 (f / ref); }

static void check (bool ok, const std::string& name, const std::string& detail)
{
    std::printf ("[%s] %-46s %s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.c_str());
    if (! ok) ++failures;
}

using Gen = std::function<float (long)>;

struct RunResult { bool voiced; float hz; float conf; int noteChanges; float minHz, maxHz; double meanHz; };

static RunResult run (PitchTracker& t, const Gen& g, long n, long measureFrom)
{
    RunResult r { false, 0, 0, 0, 1e9f, 0, 0 };
    long cnt = 0;
    for (long i = 0; i < n; ++i)
    {
        t.push (g (i));
        if (t.consumeNoteChange()) ++r.noteChanges;
        if (i >= measureFrom && t.voiced())
        {
            const float h = t.pitchHz();
            r.minHz = std::min (r.minHz, h); r.maxHz = std::max (r.maxHz, h);
            r.meanHz += h; ++cnt;
        }
    }
    if (cnt) r.meanHz /= (double) cnt;
    r.voiced = t.voiced(); r.hz = t.pitchHz(); r.conf = t.confidence();
    return r;
}

static Gen sine (double sr, double f, double a = 0.5)
{
    return [=] (long i) { return (float) (a * std::sin (2.0 * kPi * f * (double) i / sr)); };
}

static Gen harmonic (double sr, double f, std::vector<double> amps)
{
    return [=] (long i) {
        double s = 0.0;
        for (size_t h = 0; h < amps.size(); ++h) s += amps[h] * std::sin (2.0 * kPi * f * (double) (h + 1) * (double) i / sr);
        return (float) s;
    };
}

static PitchTracker make (double sr, float minHz = 40.0f)
{
    PitchTracker t;
    t.prepare (sr);
    PitchTracker::Settings s;
    s.minHz = minHz;
    t.setSettings (s);
    return t;
}

int main()
{
    std::printf ("=== Audio2Bass pitch tracker tests ===\n");

    // 1) Szinuszok ismert frekvencián
    {
        const double freqs[] = { 32.7, 41.2, 55.0, 82.41, 110.0, 220.0, 440.0, 880.0 };
        for (double f : freqs)
        {
            auto t = make (44100.0, 30.0f);
            auto r = run (t, sine (44100.0, f), 44100, 22050);
            const float e = r.voiced ? cents (r.hz, (float) f) : 9999.0f;
            char buf[128]; std::snprintf (buf, sizeof buf, "got %.2f Hz (%.1f cents) conf %.2f", r.hz, e, r.conf);
            check (r.voiced && std::abs (e) < 10.0f, "sine " + std::to_string (f) + " Hz", buf);
        }
    }

    // 2) Mintavételi frekvenciák
    {
        const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0 };
        for (double sr : rates)
        {
            auto t = make (sr);
            auto r = run (t, sine (sr, 82.41), (long) sr, (long) (sr / 2));
            const float e = r.voiced ? cents (r.hz, 82.41f) : 9999.0f;
            char buf[128]; std::snprintf (buf, sizeof buf, "got %.2f Hz (%.1f cents), decim %d", r.hz, e, t.decimation());
            check (r.voiced && std::abs (e) < 10.0f, "sample rate " + std::to_string ((int) sr), buf);
        }
    }

    // 3) Oktáv-bizonytalanság: gyenge alap, erős 2. harmonikus
    {
        auto t = make (44100.0);
        auto r = run (t, harmonic (44100.0, 65.41, { 0.15, 1.0, 0.6, 0.4, 0.2 }), 44100, 22050);
        const float e = r.voiced ? cents (r.hz, 65.41f) : 9999.0f;
        char buf[128]; std::snprintf (buf, sizeof buf, "got %.2f Hz (%.1f cents)", r.hz, e);
        check (r.voiced && std::abs (e) < 20.0f, "weak fundamental (65.41 Hz)", buf);
    }

    // 4) Zajos és torzított jel
    {
        std::mt19937 rng (1); std::normal_distribution<float> nd (0.0f, 1.0f);
        auto g = [&] (long i) { return (float) (0.5 * std::sin (2.0 * kPi * 82.41 * (double) i / 44100.0)) + 0.12f * nd (rng); };
        auto t = make (44100.0);
        auto r = run (t, g, 44100, 22050);
        const float e = r.voiced ? cents (r.hz, 82.41f) : 9999.0f;
        char buf[128]; std::snprintf (buf, sizeof buf, "got %.2f Hz (%.1f cents) conf %.2f", r.hz, e, r.conf);
        check (r.voiced && std::abs (e) < 15.0f, "noisy sine (82.41 Hz, ~12 dB SNR)", buf);

        auto gd = [] (long i) { return (float) (0.8 * std::tanh (8.0 * std::sin (2.0 * kPi * 82.41 * (double) i / 44100.0))); };
        auto t2 = make (44100.0);
        auto r2 = run (t2, gd, 44100, 22050);
        const float e2 = r2.voiced ? cents (r2.hz, 82.41f) : 9999.0f;
        std::snprintf (buf, sizeof buf, "got %.2f Hz (%.1f cents)", r2.hz, e2);
        check (r2.voiced && std::abs (e2) < 15.0f, "heavily distorted sine (82.41 Hz)", buf);
    }

    // 5) Vibrato: egy hang maradjon, ne ugráljon
    {
        auto g = [] (long i) {
            const double tsec = (double) i / 44100.0;
            const double f = 110.0 * std::pow (2.0, 40.0 * std::sin (2.0 * kPi * 5.5 * tsec) / 1200.0);
            static double ph = 0.0; if (i == 0) ph = 0.0;
            ph += 2.0 * kPi * f / 44100.0;
            return (float) (0.5 * std::sin (ph));
        };
        auto t = make (44100.0);
        auto r = run (t, g, 88200, 22050);
        const float eMean = cents ((float) r.meanHz, 110.0f);
        char buf[160]; std::snprintf (buf, sizeof buf, "note changes %d, mean %.1f cents, range %.1f..%.1f Hz", r.noteChanges, eMean, r.minHz, r.maxHz);
        check (r.noteChanges <= 1 && std::abs (eMean) < 20.0f, "vibrato +/-40 cents @ 5.5 Hz (stable note)", buf);
    }

    // 6) Gyors hangváltások + késleltetés mérése
    {
        const double sr = 44100.0;
        const double notes[] = { 55.0, 65.41, 82.41, 98.0, 110.0, 73.42 };
        const long seg = (long) (0.15 * sr);
        auto g = [&] (long i) {
            const int k = (int) std::min<long> (5, i / seg);
            static double ph = 0.0; if (i == 0) ph = 0.0;
            ph += 2.0 * kPi * notes[k] / sr;
            return (float) (0.4 * std::sin (ph) + 0.2 * std::sin (2 * ph) + 0.1 * std::sin (3 * ph));
        };
        auto t = make (sr);
        std::vector<double> lat; int changes = 0; bool allFound = true;
        std::vector<bool> found (6, false);
        for (long i = 0; i < seg * 6; ++i)
        {
            t.push (g (i));
            if (t.consumeNoteChange()) ++changes;
            const int k = (int) (i / seg);
            if (! found[(size_t) k] && t.voiced() && std::abs (cents (t.pitchHz(), (float) notes[k])) < 30.0f)
            {
                found[(size_t) k] = true;
                lat.push_back (1000.0 * (double) (i - k * seg) / sr);
            }
        }
        for (bool f : found) allFound = allFound && f;
        double mx = 0, avg = 0; for (size_t k = 1; k < lat.size(); ++k) { mx = std::max (mx, lat[k]); avg += lat[k]; }
        if (lat.size() > 1) avg /= (double) (lat.size() - 1);
        char buf[200]; std::snprintf (buf, sizeof buf, "note-change latency avg %.1f ms, max %.1f ms; events %d (expected 6)", avg, mx, changes);
        check (allFound && changes == 6, "rapid note changes (6 notes x 150 ms)", buf);
        check (mx < 90.0, "note change latency < 90 ms (default settings)", buf);
    }

    // 7) Csend és halk jel
    {
        auto t = make (44100.0);
        auto r = run (t, [] (long) { return 0.0f; }, 44100, 0);
        check (! r.voiced && r.noteChanges == 0, "silence -> unvoiced", "");

        auto t2 = make (44100.0);
        auto r2 = run (t2, sine (44100.0, 110.0, 0.002), 44100, 0);
        check (! r2.voiced, "very quiet sine (-54 dBFS) -> rejected", "silenceRms=0.003");

        auto t3 = make (44100.0);
        auto r3 = run (t3, sine (44100.0, 110.0, 0.05), 44100, 22050);
        check (r3.voiced && std::abs (cents (r3.hz, 110.0f)) < 10.0f, "quiet sine (-26 dBFS) -> tracked", "");
    }

    // 8) Belépési késleltetés csendből
    {
        const double sr = 44100.0;
        auto t = make (sr);
        const long sil = (long) (0.3 * sr);
        double onsetMs = -1.0;
        for (long i = 0; i < sil + (long) sr; ++i)
        {
            const float x = i < sil ? 0.0f : (float) (0.5 * std::sin (2.0 * kPi * 82.41 * (double) (i - sil) / sr));
            t.push (x);
            if (onsetMs < 0 && t.voiced() && std::abs (cents (t.pitchHz(), 82.41f)) < 30.0f)
                onsetMs = 1000.0 * (double) (i - sil) / sr;
        }
        char buf[128]; std::snprintf (buf, sizeof buf, "onset-to-pitch latency %.1f ms (window %d @ %d kHz-decim)", onsetMs, t.windowLength(), 11);
        check (onsetMs > 0 && onsetMs < 120.0, "onset latency from silence < 120 ms", buf);
    }

    // 9) CPU-terhelés és legrosszabb eset (percentilisek; az OS-ütemező egyedi kiugrásait külön jelezzük)
    {
        const double sr = 44100.0;
        auto t = make (sr);
        std::mt19937 rng (7); std::normal_distribution<float> nd (0.0f, 0.05f);
        const long n = (long) (sr * 10.0);
        std::vector<double> us; us.reserve ((size_t) n);
        double totalS = 0.0;
        for (long i = 0; i < n; ++i)
        {
            const float x = (float) (0.4 * std::sin (2.0 * kPi * (80.0 + 40.0 * std::sin ((double) i * 1e-5)) * (double) i / sr)) + nd (rng);
            const auto a = std::chrono::steady_clock::now();
            t.push (x);
            const auto b = std::chrono::steady_clock::now();
            us.push_back (std::chrono::duration<double, std::micro> (b - a).count());
            totalS += us.back() * 1e-6;
        }
        std::sort (us.begin(), us.end());
        const double p9999 = us[us.size() * 9999 / 10000], mx = us.back();
        const double rt = totalS / 10.0 * 100.0;
        char buf[200]; std::snprintf (buf, sizeof buf, "%.2f%% of one core; p99.99 %.1f us, max %.0f us (64-sample block = 1450 us)", rt, p9999, mx);
        check (rt < 5.0 && p9999 < 100.0, "CPU: smooth load, no systematic spikes", buf);
    }

    std::printf ("=== %s (%d failure%s) ===\n", failures == 0 ? "ALL PASSED" : "SOME FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
