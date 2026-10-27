#pragma once
#include "Dsp.h"
#include <algorithm>
#include <cmath>
#include <vector>

// JUCE-mentes effekt-építőkövek az Audio2Bass-hoz. Allokáció csak a prepare()-ben történik.
namespace a2b
{
    //==========================================================================
    // Vocoder-szerű timbre-átvitel: a forrás sávonkénti spektrális burkolóját rávisszük a hordozóra.
    // A hordozó sávszintjét normalizáljuk, így a forrás spektrális alakja kerül át,
    // nem az abszolút hangereje.
    class Vocoder
    {
    public:
        static constexpr int kBands = 16;

        void prepare (double sampleRate)
        {
            sr = sampleRate;
            const double lo = 110.0, hi = std::min (7000.0, sr * 0.45);
            for (int k = 0; k < kBands; ++k)
                freq[k] = (float) (lo * std::pow (hi / lo, ((double) k + 0.5) / (double) kBands));
            lastQ = -1.0f;
            setParams (5.0f, 30.0f);
            reset();
        }

        void setParams (float q, float releaseMs)
        {
            if (q != lastQ)
            {
                for (int k = 0; k < kBands; ++k)
                {
                    src[k].setBandPass (sr, freq[k], q);
                    carL[k].setBandPass (sr, freq[k], q);
                    carR[k].setBandPass (sr, freq[k], q);
                }
                lastQ = q;
            }
            atk = 1.0f - std::exp (-1.0f / (0.002f * (float) sr));
            rel = 1.0f - std::exp (-1.0f / (std::max (1.0f, releaseMs) * 0.001f * (float) sr));
        }

        void reset()
        {
            for (int k = 0; k < kBands; ++k)
            {
                src[k].reset(); carL[k].reset(); carR[k].reset();
                eS[k] = eC[k] = 0.0f;
            }
        }

        void process (float source, float cL, float cR, float& oL, float& oR)
        {
            float sumL = 0.0f, sumR = 0.0f;
            for (int k = 0; k < kBands; ++k)
            {
                const float s  = src[k].process (source);
                const float bl = carL[k].process (cL);
                const float br = carR[k].process (cR);

                const float as = std::abs (s);
                const float ac = 0.5f * (std::abs (bl) + std::abs (br));
                eS[k] += (as > eS[k] ? atk : rel) * (as - eS[k]);
                eC[k] += (ac > eC[k] ? atk : rel) * (ac - eC[k]);

                const float g = std::min (12.0f, eS[k] / (eC[k] + 1.0e-4f));
                sumL += bl * g;
                sumR += br * g;
            }
            oL = sumL; oR = sumR;
        }

    private:
        double sr = 44100.0;
        float freq[kBands] {};
        Biquad src[kBands], carL[kBands], carR[kBands];
        float eS[kBands] {}, eC[kBands] {};
        float atk = 0.01f, rel = 0.001f, lastQ = -1.0f;
    };

    //==========================================================================
    // Háromsávos telítés + OTT-szerű (felfelé és lefelé) kompresszió. LR4 crossoverek.
    class MultiBand
    {
    public:
        void prepare (double sampleRate)
        {
            sr = sampleRate;
            lastX1 = lastX2 = -1.0f;
            setCrossovers (150.0f, 1500.0f);
            reset();
        }

        void reset()
        {
            for (int c = 0; c < 2; ++c)
                for (int s = 0; s < 2; ++s) { lp1[c][s].reset(); hp1[c][s].reset(); lp2[c][s].reset(); hp2[c][s].reset(); }
            for (int b = 0; b < 3; ++b) { env[b] = 0.0f; gainLin[b] = 1.0f; gainTgt[b] = 1.0f; }
            cnt = 0;
        }

        void setCrossovers (float x1, float x2)
        {
            x2 = std::max (x2, x1 * 1.5f);
            if (x1 != lastX1)
            {
                for (int c = 0; c < 2; ++c) for (int s = 0; s < 2; ++s)
                { lp1[c][s].setLowPass (sr, x1, 0.70710678); hp1[c][s].setHighPass (sr, x1, 0.70710678); }
                lastX1 = x1;
            }
            if (x2 != lastX2)
            {
                for (int c = 0; c < 2; ++c) for (int s = 0; s < 2; ++s)
                { lp2[c][s].setLowPass (sr, x2, 0.70710678); hp2[c][s].setHighPass (sr, x2, 0.70710678); }
                lastX2 = x2;
            }
        }

        void setDrive (float d1, float d2, float d3)
        {
            const float d[3] = { d1, d2, d3 };
            for (int b = 0; b < 3; ++b)
            {
                satOn[b] = d[b] > 0.001f;
                satG[b] = 1.0f + d[b] * 12.0f;
                satN[b] = 1.0f / std::tanh (satG[b]);
            }
        }

        void setOtt (float depthIn, float releaseMs)
        {
            depth = std::min (1.0f, std::max (0.0f, depthIn));
            atk = 1.0f - std::exp (-1.0f / (0.001f * (float) sr));
            rel = 1.0f - std::exp (-1.0f / (std::max (5.0f, releaseMs) * 0.001f * (float) sr));
            gSmooth = 1.0f - std::exp (-1.0f / (0.001f * (float) sr));
        }

        void process (float& l, float& r)
        {
            float band[3][2];
            const float in[2] = { l, r };
            for (int c = 0; c < 2; ++c)
            {
                band[0][c] = lp1[c][1].process (lp1[c][0].process (in[c]));
                const float rest = hp1[c][1].process (hp1[c][0].process (in[c]));
                band[1][c] = lp2[c][1].process (lp2[c][0].process (rest));
                band[2][c] = hp2[c][1].process (hp2[c][0].process (rest));
            }

            const bool updateGain = (++cnt & 3) == 0;
            float outL = 0.0f, outR = 0.0f;
            for (int b = 0; b < 3; ++b)
            {
                float bl = band[b][0], br = band[b][1];

                if (satOn[b])
                {
                    bl = std::tanh (satG[b] * bl) * satN[b];
                    br = std::tanh (satG[b] * br) * satN[b];
                }

                if (depth > 0.001f)
                {
                    const float a = std::max (std::abs (bl), std::abs (br));
                    env[b] += (a > env[b] ? atk : rel) * (a - env[b]);

                    if (updateGain)
                    {
                        const float db = 20.0f * std::log10 (std::max (env[b], 1.0e-6f));
                        float g = 0.0f;
                        if (db > -20.0f) g -= (db + 20.0f) * 0.75f;                           // lefelé: 4:1
                        if (db < -45.0f) g += (-45.0f - db) * 0.667f                           // felfelé: 3:1
                                              * std::min (1.0f, std::max (0.0f, (db + 80.0f) / 20.0f)); // zajpadló védelem
                        g = std::min (24.0f, std::max (-24.0f, g * depth));
                        gainTgt[b] = std::pow (10.0f, g / 20.0f);
                    }
                    gainLin[b] += (gainTgt[b] - gainLin[b]) * gSmooth;
                    bl *= gainLin[b]; br *= gainLin[b];
                }
                outL += bl; outR += br;
            }
            l = outL; r = outR;
        }

    private:
        double sr = 44100.0;
        Biquad lp1[2][2], hp1[2][2], lp2[2][2], hp2[2][2];
        float lastX1 = -1.0f, lastX2 = -1.0f;
        bool  satOn[3] = { false, false, false };
        float satG[3] = { 1, 1, 1 }, satN[3] = { 1, 1, 1 };
        float depth = 0.0f, atk = 0.01f, rel = 0.001f, gSmooth = 0.02f;
        float env[3] {}, gainLin[3] = { 1, 1, 1 }, gainTgt[3] = { 1, 1, 1 };
        unsigned cnt = 0;
    };

    //==========================================================================
    // Átmenet-formázó: a támadás (attack) és a hangtest (sustain) külön erősíthető/csökkenthető.
    // RMS-alapú detektor, hogy a mély hangok hullámzása ne zavarja.
    class TransientShaper
    {
    public:
        void prepare (double sampleRate)
        {
            fastC = 1.0f - std::exp (-1.0f / (0.006f * (float) sampleRate));
            slowC = 1.0f - std::exp (-1.0f / (0.060f * (float) sampleRate));
            gC    = 1.0f - std::exp (-1.0f / (0.0005f * (float) sampleRate));
            reset();
        }
        void reset() { pf = ps = 0.0f; gs = 1.0f; }
        void setParams (float attack, float sustain) { atk = attack; sus = sustain; }

        void process (float& l, float& r)
        {
            const float p = 0.5f * (l * l + r * r);
            pf += (p - pf) * fastC;
            ps += (p - ps) * slowC;
            const float ratio = std::sqrt (pf) / (std::sqrt (ps) + 1.0e-5f);
            const float trans = std::min (1.0f, std::max (0.0f, ratio - 1.0f));
            float g = (1.0f + atk * 1.5f * trans) * (1.0f + sus * 0.7f * (1.0f - trans));
            g = std::max (0.05f, g);
            gs += (g - gs) * gC;
            l *= gs; r *= gs;
        }

    private:
        float fastC = 0.01f, slowC = 0.001f, gC = 0.05f;
        float pf = 0.0f, ps = 0.0f, gs = 1.0f, atk = 0.0f, sus = 0.0f;
    };

    //==========================================================================
    // Pitch-szinkron hangmagasság-eltoló: a bemenet SAJÁT jelét viszi másik hangmagasságra,
    // így az eredeti hangszín megmarad. Késleltetővonalas eltoló, amelynek a "ugrásai" pontosan a
    // detektált periódus egész számú többszörösei, ezért a vágás periodikus jelnél nem hallható.
    // A kimenet a beállított alapkésleltetéssel (base) követi a bemenetet, hogy a hangmagasság-
    // detektor késleltetését kiegyenlítse.
    class PitchSyncShifter
    {
    public:
        void prepare (double sampleRate)
        {
            sr = sampleRate;
            N = 1; while (N < (int) (sr * 0.5)) N <<= 1;
            mask = N - 1;
            buf.assign ((size_t) N, 0.0f);
            xfLen = std::max (16, (int) (0.006 * sr));
            setBaseDelayMs (65.0f);
            reset();
        }

        void setBaseDelayMs (float ms)
        {
            base = std::min ((float) (N - 8), std::max (40.0f, ms) * 0.001f * (float) sr);
        }

        void reset()
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            w = 0; cur = 0; oldTap = 0; fadeLeft = 0;
            d[0] = d[1] = base;
        }

        // csak a előzményt tölti (ha az eltolás ki van kapcsolva)
        void write (float x)
        {
            buf[(size_t) w] = x;
            w = (w + 1) & mask;
        }

        // ratio = kimeneti / bemeneti frekvencia, periodSamples = a bemenet periódusa mintában
        float process (float x, float ratio, float periodSamples)
        {
            write (x);
            ratio = std::min (4.0f, std::max (0.125f, ratio));
            const float dd = 1.0f - ratio;
            const float T = std::min (0.06f * (float) sr, std::max (8.0f, periodSamples));
            const float K = std::max (1.0f, std::round (0.02f * (float) sr / T));
            const float J = K * T;

            d[0] += dd; d[1] += dd;

            if (fadeLeft == 0)
            {
                const float dc = d[cur];
                if (dc > base + 0.5f * J || dc < base - 0.5f * J)
                {
                    const float n = std::round ((dc - base) / J);
                    const int nxt = 1 - cur;
                    d[nxt] = std::max (4.0f, dc - n * J);
                    oldTap = cur; cur = nxt;
                    fadeLeft = xfLen;
                }
            }

            if (fadeLeft > 0)
            {
                const float f = 1.0f - (float) fadeLeft / (float) xfLen;
                --fadeLeft;
                return read (d[oldTap]) * (1.0f - f) + read (d[cur]) * f;
            }
            return read (d[cur]);
        }

    private:
        float read (float delay) const
        {
            const float p = (float) w - 1.0f - delay;            // delay = 0 -> a legfrissebb minta
            const int i = (int) std::floor (p);
            const float f = p - (float) i;
            const float ym1 = buf[(size_t) ((i - 1) & mask)], y0 = buf[(size_t) (i & mask)];
            const float y1 = buf[(size_t) ((i + 1) & mask)], y2 = buf[(size_t) ((i + 2) & mask)];
            const float c1 = 0.5f * (y1 - ym1);
            const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
            const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
            return ((c3 * f + c2) * f + c1) * f + y0;
        }

        double sr = 44100.0;
        int N = 32768, mask = 32767, w = 0, cur = 0, oldTap = 0, fadeLeft = 0, xfLen = 264;
        std::vector<float> buf;
        float base = 2866.0f;
        float d[2] = { 2866.0f, 2866.0f };
    };

    //==========================================================================
    // Lookahead limiter 4x oversampled (inter-sample) csúcsdetektálással.
    // A késleltetés kb. 1,5 ms + 5 minta. Minden kimeneti minta <= ceiling.
    class TruePeakLimiter
    {
    public:
        void prepare (double sampleRate, double lookaheadMs = 1.5)
        {
            sr = sampleRate;
            L = std::max (16, (int) std::lround (lookaheadMs * 0.001 * sr));
            D = L + 5;
            for (int c = 0; c < 2; ++c) dly[c].assign ((size_t) D + 1, 0.0f);
            rr.assign ((size_t) L, 1.0f);
            mm.assign ((size_t) L, 1.0f);

            const double pi = 3.14159265358979323846;
            const double fr[3] = { 0.25, 0.5, 0.75 };
            for (int f = 0; f < 3; ++f)
                for (int k = 0; k < 12; ++k)
                {
                    const double t = (double) (k - 5) - fr[f];
                    const double sinc = std::abs (t) < 1.0e-9 ? 1.0 : std::sin (pi * t) / (pi * t);
                    const double win = std::abs (t) < 6.0 ? 0.5 * (1.0 + std::cos (pi * t / 6.0)) : 0.0;
                    taps[f][k] = (float) (sinc * win);
                }
            setParams (-0.3f, 80.0f);
            reset();
        }

        void reset()
        {
            for (int c = 0; c < 2; ++c) { std::fill (dly[c].begin(), dly[c].end(), 0.0f); for (auto& h : hist[c]) h = 0.0f; }
            std::fill (rr.begin(), rr.end(), 1.0f);
            std::fill (mm.begin(), mm.end(), 1.0f);
            wi = 0; dpos = 0; rp = 1.0f; sum = (float) L;
        }

        void setParams (float ceilingDb, float releaseMs)
        {
            ceil = std::pow (10.0f, ceilingDb / 20.0f);
            relCoef = 1.0f - std::exp (-1.0f / (std::max (1.0f, releaseMs) * 0.001f * (float) sr));
        }

        int latencySamples() const { return D; }

        void process (float& l, float& r, bool active)
        {
            for (int c = 0; c < 2; ++c) { for (int k = 0; k < 11; ++k) hist[c][k] = hist[c][k + 1]; }
            hist[0][11] = l; hist[1][11] = r;

            float pk = 0.0f;
            for (int c = 0; c < 2; ++c)
            {
                pk = std::max (pk, std::abs (hist[c][5]));
                for (int f = 0; f < 3; ++f)
                {
                    float acc = 0.0f;
                    for (int k = 0; k < 12; ++k) acc += hist[c][k] * taps[f][k];
                    pk = std::max (pk, std::abs (acc));
                }
            }

            const float rNow = pk > ceil ? ceil / pk : 1.0f;
            if (rNow < rp) rp = rNow; else rp += (rNow - rp) * relCoef;

            rr[(size_t) wi] = rp;
            float m = rr[0];
            for (int i = 1; i < L; ++i) m = std::min (m, rr[(size_t) i]);

            sum += m - mm[(size_t) wi];
            mm[(size_t) wi] = m;
            wi = (wi + 1) % L;
            const float g = sum / (float) L;

            const size_t n = (size_t) D + 1;
            dly[0][(size_t) dpos] = l; dly[1][(size_t) dpos] = r;
            const size_t rd = (size_t) ((dpos + 1) % (int) n);
            const float dl = dly[0][rd], dr = dly[1][rd];
            dpos = (dpos + 1) % (int) n;

            if (active) { l = dl * g; r = dr * g; }
            else        { l = dl;     r = dr; }
        }

    private:
        double sr = 44100.0;
        int L = 72, D = 77, wi = 0, dpos = 0;
        std::vector<float> dly[2], rr, mm;
        float hist[2][12] {};
        float taps[3][12] {};
        float ceil = 0.966f, relCoef = 0.001f, rp = 1.0f, sum = 72.0f;
    };
}
