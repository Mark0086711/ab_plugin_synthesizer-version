#pragma once
#include "Dsp.h"
#include <algorithm>
#include <cmath>
#include <vector>

// Monofonikus hangmagasság-követő (YIN) valós idejű használatra.
//  - ~11 kHz-re decimált jelen dolgozik, az ablak a legalacsonyabb követett hangtól függ
//    (magasabb "min Hz" = kisebb késleltetés).
//  - A YIN különbségfüggvény számítása szét van osztva kSteps darab decimált minta között,
//    így nincs CPU-csúcs egy-egy blokkban. Nincs allokáció a push() hívásban.
//  - Konfidencia-elfogadás, hiszterézis, oktávhiba-ellenőrzés, hang-megerősítés (N keret).
namespace a2b
{
    class PitchTracker
    {
    public:
        struct Settings
        {
            float minHz          = 40.0f;
            float maxHz          = 1000.0f;
            float yinThreshold   = 0.15f;
            float minConfidence  = 0.70f;
            float octaveCheck    = 0.08f;   // mennyivel kell jobbnak lennie a hosszabb periódusnak (kisebb = hajlamosabb lefelé oktávozni)
            float hysteresisCents = 60.0f;
            float smoothing      = 0.3f;    // 0..1, csak a hangon belüli ingadozást simítja
            float silenceRms     = 0.003f;  // ez alatt nem elemez
            int   confirmFrames  = 2;       // ennyi egyező keret kell új hanghoz
            int   hopDecimated   = 128;     // elemzési lépés decimált mintában (128 = gyors, 192 = normál, 256 = sima)
        };

        void prepare (double sampleRate)
        {
            sr = sampleRate;
            decim = std::max (1, (int) std::lround (sr / (double) kTargetFs));
            fsDec = (float) (sr / (double) decim);
            lp1.setLowPass (sr, 1200.0);
            lp2.setLowPass (sr, 1200.0);
            ring.assign ((size_t) kRing, 0.0f);
            frame.assign ((size_t) kRing, 0.0f);
            cmnd.assign ((size_t) kMaxTau + 4, 1.0f);
            derive();
            reset();
        }

        void setSettings (const Settings& s)
        {
            const bool rederive = (s.minHz != cfg.minHz) || (s.maxHz != cfg.maxHz) || (s.hopDecimated != cfg.hopDecimated);
            cfg = s;
            cfg.confirmFrames = std::max (1, cfg.confirmFrames);
            cfg.hopDecimated  = std::max (kSteps + 8, cfg.hopDecimated);
            if (rederive) { derive(); jobActive = false; }
        }

        void reset()
        {
            lp1.reset(); lp2.reset();
            std::fill (ring.begin(), ring.end(), 0.0f);
            ringW = 0; decCount = 0; hopCount = 0; filled = 0;
            jobActive = false; misses = 0; candCount = 0;
            voiced_ = false; noteChanged_ = false;
            stableLog2 = 5.78f; rawHz_ = 0.0f; conf_ = 0.0f;
        }

        // Egy bemeneti mintát dolgoz fel. true-t ad, ha ezen a mintán új eredmény született.
        bool push (float x)
        {
            const float lp = lp2.process (lp1.process (x));
            if (++decCount < decim) return false;
            decCount = 0;

            ring[(size_t) ringW] = lp;
            ringW = (ringW + 1) % kRing;
            if (filled < kRing) ++filled;

            bool produced = false;
            if (jobActive)
            {
                stepJob();
                if (jobTau >= tauMax)
                {
                    produced = finalizeJob();
                    jobActive = false;
                }
            }

            if (++hopCount >= cfg.hopDecimated && ! jobActive)
            {
                hopCount = 0;
                if (startJob()) produced = true;
            }
            return produced;
        }

        bool  voiced() const        { return voiced_; }
        float pitchHz() const       { return std::exp2 (stableLog2); }   // stabilizált
        float rawHz() const         { return rawHz_; }                    // utolsó nyers mérés
        float confidence() const    { return conf_; }
        bool  consumeNoteChange()   { const bool c = noteChanged_; noteChanged_ = false; return c; }

        int   decimation() const    { return decim; }
        int   windowLength() const  { return W; }
        int   maxLag() const        { return tauMax; }

    private:
        static constexpr int kTargetFs = 11025;
        static constexpr int kMaxTau   = 700;
        static constexpr int kRing     = 1536;
        static constexpr int kSteps    = 24;

        void derive()
        {
            tauMax = std::min (kMaxTau, std::max (32, (int) (fsDec / std::max (20.0f, cfg.minHz))));
            W      = std::max (tauMax, 192);
            frameLen = W + tauMax;
            minTau = std::max (2, (int) (fsDec / std::max (cfg.minHz + 10.0f, cfg.maxHz)));
            if (minTau > tauMax - 8) minTau = std::max (2, tauMax - 8);
            tausPerStep = (tauMax - 1 + kSteps - 1) / kSteps;
        }

        bool startJob()
        {
            if (filled < frameLen) return false;

            for (int k = 0; k < frameLen; ++k)
                frame[(size_t) k] = ring[(size_t) ((ringW - frameLen + k + 2 * kRing) % kRing)];

            double e = 0.0;
            for (int k = frameLen - W; k < frameLen; ++k) e += (double) frame[(size_t) k] * frame[(size_t) k];
            const float rms = (float) std::sqrt (e / (double) W);

            if (rms < cfg.silenceRms)
            {
                processEstimate (false, 0.0f, 0.0f);
                return true;
            }

            jobTau = 1;
            running = 0.0f;
            jobActive = true;
            return false;
        }

        void stepJob()
        {
            const int end = std::min (tauMax - 1, jobTau + tausPerStep - 1);
            for (int tau = jobTau; tau <= end; ++tau)
            {
                float sum = 0.0f;
                const float* a = frame.data();
                const float* b = frame.data() + tau;
                for (int j = 0; j < W; ++j) { const float d = a[j] - b[j]; sum += d * d; }
                running += sum;
                cmnd[(size_t) tau] = running > 1.0e-12f ? sum * (float) tau / running : 1.0f;
            }
            jobTau = end + 1;
        }

        bool finalizeJob()
        {
            int best = -1;
            const int last = tauMax - 2;

            for (int tau = minTau; tau <= last; ++tau)
            {
                if (cmnd[(size_t) tau] < cfg.yinThreshold)
                {
                    while (tau + 1 <= last && cmnd[(size_t) (tau + 1)] < cmnd[(size_t) tau]) ++tau;
                    best = tau;
                    break;
                }
            }
            if (best < 0)
            {
                float m = 10.0f;
                for (int tau = minTau; tau <= last; ++tau)
                    if (cmnd[(size_t) tau] < m) { m = cmnd[(size_t) tau]; best = tau; }
            }
            if (best < 0) { processEstimate (false, 0.0f, 0.0f); return true; }

            // Oktávhiba-ellenőrzés: ha a dupla periódus érdemben jobb, az az alapharmonikus
            const int t2 = 2 * best;
            if (t2 + 3 <= last)
            {
                int a = t2 - 3; float m = cmnd[(size_t) a];
                for (int t = t2 - 2; t <= t2 + 3; ++t) if (cmnd[(size_t) t] < m) { m = cmnd[(size_t) t]; a = t; }
                if (m < cmnd[(size_t) best] - cfg.octaveCheck) best = a;
            }

            float tauF = (float) best;
            if (best > 1 && best < last)
            {
                const float s0 = cmnd[(size_t) best - 1], s1 = cmnd[(size_t) best], s2 = cmnd[(size_t) best + 1];
                const float den = s0 + s2 - 2.0f * s1;
                if (std::abs (den) > 1.0e-9f) tauF += 0.5f * (s0 - s2) / den;
            }

            const float freq = fsDec / tauF;
            const float conf = std::min (1.0f, std::max (0.0f, 1.0f - cmnd[(size_t) best]));
            const bool ok = conf >= cfg.minConfidence && freq >= cfg.minHz * 0.9f && freq <= cfg.maxHz * 1.1f;

            processEstimate (ok, freq, conf);
            return true;
        }

        void processEstimate (bool ok, float freq, float conf)
        {
            conf_ = ok ? conf : 0.0f;

            if (! ok)
            {
                if (++misses >= 2) { voiced_ = false; candCount = 0; }
                return;
            }
            misses = 0;
            rawHz_ = freq;

            const float lm = std::log2 (freq);
            const float coef = 1.0f - 0.95f * std::min (1.0f, std::max (0.0f, cfg.smoothing));

            if (! voiced_)
            {
                trackCandidate (lm);
                if (candCount >= cfg.confirmFrames)
                {
                    voiced_ = true; stableLog2 = candLog; noteChanged_ = true; candCount = 0;
                }
                return;
            }

            const float dc = 1200.0f * (lm - stableLog2);
            if (std::abs (dc) <= cfg.hysteresisCents)
            {
                stableLog2 += (lm - stableLog2) * coef;
                candCount = 0;
                return;
            }

            trackCandidate (lm);
            const float ad = std::abs (dc);
            const bool octJump = std::abs (ad - 1200.0f) < 120.0f || std::abs (ad - 2400.0f) < 120.0f;
            const int need = cfg.confirmFrames + (octJump ? 1 : 0);
            if (candCount >= need)
            {
                stableLog2 = candLog; noteChanged_ = true; candCount = 0;
            }
        }

        void trackCandidate (float lm)
        {
            if (candCount > 0 && std::abs (1200.0f * (lm - candLog)) < 50.0f)
            {
                ++candCount; candLog = 0.5f * (candLog + lm);
            }
            else { candCount = 1; candLog = lm; }
        }

        Settings cfg;
        double sr = 44100.0;
        int decim = 4;
        float fsDec = 11025.0f;
        int tauMax = 300, W = 300, frameLen = 600, minTau = 11, tausPerStep = 13;

        Biquad lp1, lp2;
        std::vector<float> ring, frame, cmnd;
        int ringW = 0, decCount = 0, hopCount = 0, filled = 0;

        bool  jobActive = false;
        int   jobTau = 1;
        float running = 0.0f;

        int   misses = 0, candCount = 0;
        float candLog = 0.0f;
        bool  voiced_ = false, noteChanged_ = false;
        float stableLog2 = 5.78f, rawHz_ = 0.0f, conf_ = 0.0f;
    };
}
