#pragma once
#include "Dsp.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

// Polifonikus szintetizátor-motor (JUCE-mentes, az audio szálon biztonságos: nincs allokáció).
// Egy hang (SynthVoice) = oszcillátorok + unison + FM + harmónia + sub + zaj -> torzítás -> szűrő -> torzítás -> ADSR.
// A PolySynth: hangfoglalás és -elvétel, mono/legato mód, sustain pedál, pitch bend, mod wheel.
namespace a2b
{
    namespace synthdetail
    {
        constexpr float kPi = 3.14159265358979f;

        inline float polyBlep (float t, float dt)
        {
            if (t < dt)        { t /= dt;             return t + t - t * t - 1.0f; }
            if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
            return 0.0f;
        }

        // 0 Saw, 1 Pulse, 2 Triangle, 3 Sine, 4 Half-Sine
        inline float oscSample (int wave, float ph, float dt, float pw)
        {
            switch (wave)
            {
                case 0:  return 2.0f * ph - 1.0f - polyBlep (ph, dt);
                case 1:
                {
                    float p2 = ph - pw; if (p2 < 0.0f) p2 += 1.0f;
                    return (ph < pw ? 1.0f : -1.0f) + polyBlep (ph, dt) - polyBlep (p2, dt);
                }
                case 2:  return 4.0f * std::abs (ph - 0.5f) - 1.0f;
                case 3:  return std::sin (2.0f * kPi * ph);
                default: return 2.0f * std::max (0.0f, std::sin (2.0f * kPi * ph)) - 0.6f;
            }
        }

        inline float rnd (uint32_t& s)   { s = s * 1664525u + 1013904223u; return (float) (int32_t) s * 4.656612875e-10f; }       // [-1, 1)
        inline float rnd01 (uint32_t& s) { s = s * 1664525u + 1013904223u; return (float) (s >> 8) * (1.0f / 16777216.0f); }     // [0, 1)

        inline float quantizeToScale (float f, int root, int scaleIdx)
        {
            static const bool k[7][12] = {
                { 1,1,1,1,1,1,1,1,1,1,1,1 }, { 1,0,1,0,1,1,0,1,0,1,0,1 }, { 1,0,1,1,0,1,0,1,1,0,1,0 }, { 1,0,1,1,0,1,0,1,0,1,1,0 },
                { 1,1,0,1,0,1,0,1,1,0,1,0 }, { 1,0,0,1,0,1,0,1,0,0,1,0 }, { 1,0,1,1,0,1,0,1,1,0,0,1 } };
            scaleIdx = std::max (0, std::min (6, scaleIdx));
            const float midi = 69.0f + 12.0f * std::log2 (f / 440.0f);
            const int n = (int) std::lround (midi);
            float best = (float) n, bestDist = 1.0e9f;
            for (int c = n - 6; c <= n + 6; ++c)
            {
                const int rel = ((c - root) % 12 + 12) % 12;
                if (k[scaleIdx][rel])
                {
                    const float d = std::abs ((float) c - midi);
                    if (d < bestDist) { bestDist = d; best = (float) c; }
                }
            }
            return 440.0f * std::pow (2.0f, (best - 69.0f) / 12.0f);
        }
    }

    // ---- egyszerű ADSR (lineáris), a felengedés a pillanatnyi szintről indul ----
    struct Adsr
    {
        enum State { Idle, Attack, Decay, Sustain, Release };
        State state = Idle;
        float level = 0.0f, relStep = 0.0f;

        void noteOn()  { state = Attack; }
        void noteOff (float relSamples) { if (state != Idle) { state = Release; relStep = level / std::max (1.0f, relSamples); } }
        void kill()    { state = Idle; level = 0.0f; }
        bool active() const { return state != Idle; }

        float next (float atkSamples, float decSamples, float sus)
        {
            switch (state)
            {
                case Attack:
                    level += 1.0f / std::max (1.0f, atkSamples);
                    if (level >= 1.0f) { level = 1.0f; state = Decay; }
                    break;
                case Decay:
                    level -= (1.0f - sus) / std::max (1.0f, decSamples);
                    if (level <= sus) { level = sus; state = Sustain; }
                    break;
                case Sustain: level = sus; break;
                case Release:
                    level -= relStep;
                    if (level <= 0.0f) { level = 0.0f; state = Idle; }
                    break;
                default: level = 0.0f; break;
            }
            return level;
        }
    };

    // ---- TPT állapotváltozós szűrő ----
    struct SvfCoef
    {
        float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, k = 1.0f;
        void set (float fc, float q, double sr)
        {
            const float g = std::tan (synthdetail::kPi * fc / (float) sr);
            k = 1.0f / std::max (0.05f, q);
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }
    };
    struct SvfState
    {
        float ic1 = 0.0f, ic2 = 0.0f;
        inline float process (const SvfCoef& c, float x, int type)
        {
            const float v3 = x - ic2;
            const float v1 = c.a1 * ic1 + c.a2 * v3;
            const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return type == 0 ? v2 : (type == 1 ? v1 : x - c.k * v1 - v2);
        }
        void reset() { ic1 = ic2 = 0.0f; }
    };

    // ---- blokkonként állandó paraméterek (a processzor tölti ki) ----
    struct SynthParams
    {
        double sr = 44100.0;

        // oszcillátorok
        int   o1wave = 0, o2wave = 0;
        float o1ratio = 1.0f, o2ratio = 1.0f, pw1 = 0.5f, pw2 = 0.5f, fm = 0.0f;
        int   uniN = 1;
        float uniRatio[7] = { 1, 1, 1, 1, 1, 1, 1 };
        float uniGL[7] = { 1, 1, 1, 1, 1, 1, 1 }, uniGR[7] = { 1, 1, 1, 1, 1, 1, 1 };
        bool  h1on = false, h2on = false;
        float h1ratio = 1.0f, h2ratio = 1.0f, h1l = 0.0f, h2l = 0.0f;
        int   subWave = 0;
        float subRatio = 0.5f;
        float driftScale = 0.0f, driftCoef = 0.0001f;
        bool  phrand = false, keysync = false;
        float lpw = 0.0f;

        // hangmagasság
        float trans = 0.0f, fine = 0.0f, lpitchCents = 0.0f, mwVibCents = 0.0f;
        float penvAmt = 0.0f, pdecCoef = 0.999f, velPenv = 0.0f;
        float glideCoef = 1.0f;
        bool  gleg = false;

        // szűrő
        int   ftype = 0;
        bool  f24 = true;
        float res = 1.0f, fkey = 0.0f, fenvAmt = 0.0f, fsus = 0.0f, fdecCoef = 0.999f, lcut = 0.0f, velCut = 0.0f, mwCutOct = 0.0f;

        // torzítás
        int   dtype = 0;
        bool  distPost = true;
        float crushStep = 0.0f, tapeCoef = 0.5f;
        int   crateI = 1;
        bool  split = false;
        float xoverHz = 130.0f;

        // burkoló és hangerő
        float atkSamples = 100.0f, decSamples = 1000.0f, sus = 1.0f, relSamples = 1000.0f;
        float velAmt = 0.7f, lamp = 0.0f;
        int   nlenSamples = 0;

        // hangkezelés
        int   mode = 0;              // 0 poly, 1 mono, 2 legato
        int   voices = 8;
        bool  quant = false;
        int   root = 0, scale = 0;
        float shiftOct = 0.0f, bendRange = 2.0f;
    };

    // ---- mintánként változó (simított) értékek ----
    struct SampleCtx
    {
        float lfo = 0.0f;
        float o1l = 0.7f, o2l = 0.0f, subl = 0.0f, noil = 0.0f;
        float driveV = 0.0f, cutV = 8000.0f, sdV = 0.0f;
        // a motor tölti ki
        float bend = 0.0f, mw = 0.0f, dg = 1.0f, dnorm = 1.0f;
    };

    //==========================================================================
    class SynthVoice
    {
    public:
        bool  active = false;      // szól (beleértve a felengedést is)
        bool  gate = false;        // a billentyű le van nyomva
        bool  sustained = false;   // a pedál tartja
        int   note = -1;
        float vel = 1.0f;
        float targetLog2 = 0.0f, curLog2 = 0.0f;
        uint32_t age = 0;
        Adsr  adsr;

        void reset (uint32_t seed)
        {
            active = gate = sustained = false; note = -1; adsr.kill();
            for (int v = 0; v < 7; ++v) { ph1[v] = ph2[v] = 0.0f; drv1[v] = drv2[v] = drvT1[v] = drvT2[v] = 0.0f; }
            subPhase = h1ph = h2ph = 0.0f; drvSub = drvSubT = 0.0f; driftCount = 0;
            fenv = penv = 0.0f; noteSamples = 0; fcount = 0;
            for (int c = 0; c < 2; ++c) { f1[c].reset(); f2[c].reset(); crushHold[c] = 0.0f; tapeState[c] = 0.0f; for (auto& b : xHp[c]) b.reset(); }
            rngState = seed * 2654435761u + 12345u;
            lastXover = -1.0f;
        }

        // újraindítás / új hang. startLog2 = ahonnan a glide indul.
        void start (int n, float v, float target, float startLog2, bool retrigEnv, const SynthParams& P)
        {
            const bool wasActive = active && adsr.active();
            note = n; vel = v; targetLog2 = target; curLog2 = startLog2;
            gate = true; sustained = false; active = true;
            fenv = 1.0f; penv = 1.0f; noteSamples = 0;
            if (retrigEnv || ! wasActive) adsr.noteOn();

            if (! wasActive)
            {
                for (int c = 0; c < 2; ++c) { f1[c].reset(); f2[c].reset(); for (auto& b : xHp[c]) b.reset(); }
                if (P.keysync)
                    for (int u = 0; u < 7; ++u) { ph1[u] = 0.0f; ph2[u] = 0.0f; }
                else if (P.phrand)
                {
                    for (int u = 0; u < 7; ++u) { ph1[u] = synthdetail::rnd01 (rngState); ph2[u] = synthdetail::rnd01 (rngState); }
                    subPhase = synthdetail::rnd01 (rngState); h1ph = synthdetail::rnd01 (rngState); h2ph = synthdetail::rnd01 (rngState);
                }
            }
        }

        void retrigger() { adsr.noteOn(); fenv = 1.0f; penv = 1.0f; noteSamples = 0; }

        void release (const SynthParams& P)
        {
            gate = false; sustained = false;
            adsr.noteOff (P.relSamples);
        }

        void kill() { active = gate = sustained = false; adsr.kill(); }

        // egy minta feldolgozása; visszatér a hang kimenete (a hangerő-burkolóval együtt)
        void process (const SynthParams& P, const SampleCtx& C, float& oL, float& oR)
        {
            using namespace synthdetail;
            oL = oR = 0.0f;
            if (! active) return;

            const float srf = (float) P.sr;

            curLog2 += (targetLog2 - curLog2) * P.glideCoef;
            penv *= P.pdecCoef;
            fenv = P.fsus + (fenv - P.fsus) * P.fdecCoef;

            const float penvScale = (1.0f - P.velPenv) + P.velPenv * vel;
            const float vibCents = P.lpitchCents + C.mw * P.mwVibCents;
            const float log2f = curLog2 + (P.trans + C.bend) / 12.0f + P.fine / 1200.0f
                              + C.lfo * vibCents / 1200.0f + P.penvAmt * penvScale / 12.0f * penv;
            const float f0 = std::min (20000.0f, std::exp2 (log2f));

            // analóg drift
            const float dk = P.driftScale;
            if (dk > 0.0f)
            {
                if (--driftCount <= 0)
                {
                    driftCount = (int) (0.35 * P.sr);
                    for (int v = 0; v < 7; ++v) { drvT1[v] = rnd (rngState); drvT2[v] = rnd (rngState); }
                    drvSubT = rnd (rngState);
                }
                for (int v = 0; v < 7; ++v)
                {
                    drv1[v] += (drvT1[v] - drv1[v]) * P.driftCoef;
                    drv2[v] += (drvT2[v] - drv2[v]) * P.driftCoef;
                }
                drvSub += (drvSubT - drvSub) * P.driftCoef;
            }

            const float pw1 = std::min (0.95f, std::max (0.05f, P.pw1 + P.lpw * C.lfo * 0.45f));
            const float pw2 = std::min (0.95f, std::max (0.05f, P.pw2 + P.lpw * C.lfo * 0.45f));
            const bool useO2 = (C.o2l > 1.0e-4f) || (P.fm > 0.0f);

            float outL = 0.0f, outR = 0.0f;
            for (int v = 0; v < P.uniN; ++v)
            {
                float s2 = 0.0f;
                if (useO2)
                {
                    const float dt2 = std::min (0.45f, std::max (1.0e-5f, f0 * P.o2ratio * P.uniRatio[v] * (1.0f + dk * drv2[v]) / srf));
                    s2 = oscSample (P.o2wave, ph2[v], dt2, pw2);
                    ph2[v] += dt2; if (ph2[v] >= 1.0f) ph2[v] -= 1.0f;
                }
                const float dt1 = std::min (0.45f, std::max (1.0e-5f, f0 * P.o1ratio * P.uniRatio[v] * (1.0f + dk * drv1[v]) / srf));
                float p1 = ph1[v];
                if (P.fm > 0.0f) { p1 += P.fm * 0.5f * s2; p1 -= std::floor (p1); }
                const float s1 = oscSample (P.o1wave, p1, dt1, pw1);
                ph1[v] += dt1; if (ph1[v] >= 1.0f) ph1[v] -= 1.0f;

                const float vs = s1 * C.o1l + s2 * C.o2l;
                outL += vs * P.uniGL[v];
                outR += vs * P.uniGR[v];
            }

            if (P.h1on || P.h2on)
            {
                float hs = 0.0f;
                if (P.h1on)
                {
                    const float dtH = std::min (0.45f, std::max (1.0e-5f, f0 * P.o1ratio * P.h1ratio * (1.0f + dk * drv2[1]) / srf));
                    hs += oscSample (P.o1wave, h1ph, dtH, pw1) * P.h1l;
                    h1ph += dtH; if (h1ph >= 1.0f) h1ph -= 1.0f;
                }
                if (P.h2on)
                {
                    const float dtH = std::min (0.45f, std::max (1.0e-5f, f0 * P.o1ratio * P.h2ratio * (1.0f + dk * drv2[2]) / srf));
                    hs += oscSample (P.o1wave, h2ph, dtH, pw1) * P.h2l;
                    h2ph += dtH; if (h2ph >= 1.0f) h2ph -= 1.0f;
                }
                outL += hs * 0.7f; outR += hs * 0.7f;
            }

            // sub
            float subSig = 0.0f;
            if (C.subl > 1.0e-4f)
            {
                float sub;
                switch (P.subWave)
                {
                    case 0:  sub = std::sin (2.0f * kPi * subPhase); break;
                    case 1:  sub = 4.0f * std::abs (subPhase - 0.5f) - 1.0f; break;
                    default: sub = subPhase < 0.5f ? 0.7f : -0.7f; break;
                }
                subPhase += f0 * P.subRatio * (1.0f + dk * drvSub) / srf;
                if (subPhase >= 1.0f) subPhase -= 1.0f;
                if (C.sdV > 1.0e-3f) { const float g = 1.0f + C.sdV * 8.0f; sub = std::tanh (g * sub) / std::tanh (g); }
                subSig = sub * C.subl * 0.8f;
                if (! P.split) { outL += subSig; outR += subSig; }
            }

            // zaj
            if (C.noil > 1.0e-4f)
            {
                outL += rnd (rngState) * C.noil * 0.5f;
                outR += rnd (rngState) * C.noil * 0.5f;
            }

            outL *= 0.5f; outR *= 0.5f;

            // Layer Split: a mid réteg a crossover alatt le van vágva
            if (P.split)
            {
                if (lastXover != P.xoverHz)
                {
                    for (int c = 0; c < 2; ++c) for (auto& b : xHp[c]) b.setHighPass (P.sr, (double) P.xoverHz, 0.70710678);
                    lastXover = P.xoverHz;
                }
                outL = xHp[0][1].process (xHp[0][0].process (outL));
                outR = xHp[1][1].process (xHp[1][0].process (outR));
            }

            if (++crushCount >= P.crateI) { crushCount = 0; crushTick = true; } else crushTick = false;

            if (! P.distPost) { outL = distort (P, C, outL, 0); outR = distort (P, C, outR, 1); }

            // szűrő
            if ((fcount++ & 7) == 0)
            {
                const float octMod = C.lfo * P.lcut + P.fenvAmt * fenv + P.velCut * 3.0f * vel
                                   + C.mw * P.mwCutOct + P.fkey * (log2f - 6.7814f);
                const float cf = std::min (srf * 0.45f, std::max (20.0f, C.cutV * std::exp2 (octMod)));
                coef1.set (cf, P.res, P.sr);
                coef2.set (cf, std::max (0.5f, P.res * 0.5f), P.sr);
            }
            outL = f1[0].process (coef1, outL, P.ftype);
            outR = f1[1].process (coef1, outR, P.ftype);
            if (P.f24)
            {
                outL = f2[0].process (coef2, outL, P.ftype);
                outR = f2[1].process (coef2, outR, P.ftype);
            }

            if (P.distPost) { outL = distort (P, C, outL, 0); outR = distort (P, C, outR, 1); }

            if (P.split && subSig != 0.0f) { outL += subSig * 0.5f; outR += subSig * 0.5f; }

            // hangerő-burkoló
            const float env = adsr.next (P.atkSamples, P.decSamples, P.sus);
            if (! adsr.active()) { active = false; return; }
            if (P.nlenSamples > 0 && gate && ++noteSamples >= P.nlenSamples)
            {
                gate = false; sustained = false;
                adsr.noteOff (P.relSamples);          // beállított jegyhossz
            }

            const float trem = 1.0f - P.lamp * 0.5f * (1.0f + C.lfo);
            const float g = env * ((1.0f - P.velAmt) + P.velAmt * vel) * trem;
            oL = outL * g;
            oR = outR * g;
        }

    private:
        float distort (const SynthParams& P, const SampleCtx& C, float x, int ch)
        {
            float y;
            const float dg = C.dg, dnorm = C.dnorm;
            switch (P.dtype)
            {
                case 0:  y = std::tanh (x * dg) * dnorm; break;
                case 1:  y = std::min (1.0f, std::max (-1.0f, x * dg)); break;
                case 2:  y = std::sin (x * dg); break;
                case 3:  y = (std::tanh (x * dg + 0.3f) - std::tanh (0.3f)) * dnorm; break;
                case 4:  y = (x >= 0.0f ? std::tanh (x * dg) : 0.85f * std::tanh (x * dg * 1.6f)) * dnorm; break;
                case 5:
                    y = std::atan (x * dg) / std::atan (dg);
                    tapeState[ch] += P.tapeCoef * (y - tapeState[ch]);
                    y = tapeState[ch];
                    break;
                default:
                    if (x >= 0.0f) y = (1.0f - std::exp (-dg * x)) / (1.0f - std::exp (-dg));
                    else           y = -0.8f * (1.0f - std::exp (0.5f * dg * x)) / (1.0f - std::exp (-0.5f * dg));
                    break;
            }
            if (P.crushStep > 0.0f) y = std::round (y * P.crushStep) / P.crushStep;
            if (P.crateI > 1)
            {
                if (crushTick) crushHold[ch] = y;
                y = crushHold[ch];
            }
            return y;
        }

        float ph1[7] {}, ph2[7] {}, subPhase = 0.0f, h1ph = 0.0f, h2ph = 0.0f;
        float drv1[7] {}, drv2[7] {}, drvT1[7] {}, drvT2[7] {}, drvSub = 0.0f, drvSubT = 0.0f;
        int   driftCount = 0, noteSamples = 0, crushCount = 0;
        unsigned fcount = 0;
        bool  crushTick = false;
        float fenv = 0.0f, penv = 0.0f;
        SvfCoef coef1, coef2;
        SvfState f1[2], f2[2];
        Biquad xHp[2][2];
        float lastXover = -1.0f;
        float crushHold[2] {}, tapeState[2] {};
        uint32_t rngState = 1u;
    };

    //==========================================================================
    class PolySynth
    {
    public:
        static constexpr int kMaxVoices = 16;

        void prepare (double sampleRate)
        {
            sr = sampleRate;
            bendCoef = 1.0f - std::exp (-1.0f / (0.005f * (float) sr));
            mwCoef   = 1.0f - std::exp (-1.0f / (0.020f * (float) sr));
            gainCoef = 1.0f - std::exp (-1.0f / (0.030f * (float) sr));
            reset();
        }

        void reset()
        {
            for (int v = 0; v < kMaxVoices; ++v) voices[v].reset ((uint32_t) (v + 1));
            heldCount = 0; pedal = false; ageCounter = 0; haveLast = false; lastLog2 = 5.78f;
            bendCur = bendTarget = 0.0f; mwCur = mwTarget = 0.0f; polyGain = 1.0f; tick = 0;
            dg = 1.0f; dnorm = 1.0f / std::tanh (1.0f);
        }

        //---- MIDI ----
        void noteOn (int note, int velocity, const SynthParams& P)
        {
            const float vel = (float) std::max (1, std::min (127, velocity)) / 127.0f;
            const float target = noteLog2 (note, P);
            const bool anyHeld = heldCount > 0;

            removeHeld (note);
            if (heldCount >= kMaxHeld) { for (int j = 0; j < kMaxHeld - 1; ++j) { heldNote[j] = heldNote[j + 1]; heldVel[j] = heldVel[j + 1]; } --heldCount; }
            heldNote[heldCount] = note; heldVel[heldCount] = vel; ++heldCount;

            const bool glideOn = P.glideCoef < 0.9999f;
            const float startFromLast = (glideOn && haveLast && (! P.gleg || anyHeld)) ? lastLog2 : target;

            if (P.mode == 0)
            {
                int idx = -1;
                const int limit = std::max (1, std::min (kMaxVoices, P.voices));
                for (int v = 0; v < kMaxVoices; ++v)
                    if (voices[v].active && voices[v].note == note) { idx = v; break; }   // ugyanaz a billentyű: újraindítás
                if (idx < 0) idx = allocate (limit);
                voices[idx].age = ++ageCounter;
                voices[idx].start (note, vel, target, startFromLast, true, P);
            }
            else
            {
                SynthVoice& v = voices[0];
                for (int k = 1; k < kMaxVoices; ++k) voices[k].kill();
                if (v.active && v.gate)                       // átfedő hang: nem indul új hang, csak a hangmagasság változik
                {
                    v.targetLog2 = target; v.note = note; v.vel = vel;
                    if (P.mode == 1) v.retrigger();           // Mono: újraindítja a burkolót, Legato: nem
                }
                else
                {
                    v.age = ++ageCounter;
                    v.start (note, vel, target, startFromLast, true, P);
                }
            }
            lastLog2 = target; haveLast = true;
        }

        void noteOff (int note, const SynthParams& P)
        {
            const bool wasHeld = removeHeld (note);
            if (P.mode == 0)
            {
                for (int v = 0; v < kMaxVoices; ++v)
                    if (voices[v].active && voices[v].gate && voices[v].note == note)
                    {
                        if (pedal) voices[v].sustained = true;
                        else       voices[v].release (P);
                    }
            }
            else
            {
                SynthVoice& v = voices[0];
                if (! v.active || ! v.gate) return;
                if (heldCount > 0)
                {
                    if (wasHeld && v.note == note)             // vissza a még tartott hangra
                    {
                        v.note = heldNote[heldCount - 1]; v.vel = heldVel[heldCount - 1];
                        v.targetLog2 = noteLog2 (v.note, P);
                        lastLog2 = v.targetLog2;
                        if (P.mode == 1) v.retrigger();
                    }
                }
                else if (pedal) v.sustained = true;
                else            v.release (P);
            }
        }

        void pitchBend (int value14, const SynthParams& P)
        {
            bendTarget = (float) (value14 - 8192) / 8192.0f * P.bendRange;
        }

        void controller (int cc, int value, const SynthParams& P)
        {
            if (cc == 1) mwTarget = (float) value / 127.0f;
            else if (cc == 64)
            {
                const bool down = value >= 64;
                if (pedal && ! down)
                    for (auto& v : voices)
                        if (v.active && v.sustained && (P.mode == 0 || heldCount == 0)) v.release (P);
                pedal = down;
            }
            else if (cc == 123) { heldCount = 0; for (auto& v : voices) if (v.active && v.gate) v.release (P); }
            else if (cc == 120) { heldCount = 0; for (auto& v : voices) v.kill(); }
            else if (cc == 121) { mwTarget = 0.0f; bendTarget = 0.0f; pedal = false; }
        }

        void allNotesOff (const SynthParams& P) { controller (123, 0, P); }

        //---- hangok ----
        void process (const SynthParams& P, const SampleCtx& Cin, float& oL, float& oR)
        {
            bendCur += (bendTarget - bendCur) * bendCoef;
            mwCur   += (mwTarget - mwCur) * mwCoef;
            if ((tick++ & 7u) == 0u) { dg = 1.0f + Cin.driveV * 20.0f; dnorm = 1.0f / std::tanh (dg); }

            SampleCtx C = Cin;
            C.bend = bendCur; C.mw = mwCur; C.dg = dg; C.dnorm = dnorm;

            float sumL = 0.0f, sumR = 0.0f;
            int count = 0;
            for (auto& v : voices)
            {
                if (! v.active) continue;
                float l, r;
                v.process (P, C, l, r);
                sumL += l; sumR += r;
                ++count;
            }

            // a hangok számával arányos (RMS-állandó) hangerő-kiegyenlítés, lassan simítva
            const float target = 1.0f / std::sqrt ((float) std::max (1, count));
            polyGain += (target - polyGain) * gainCoef;
            oL = sumL * polyGain; oR = sumR * polyGain;
        }

        int   activeVoices() const { int n = 0; for (auto& v : voices) if (v.active) ++n; return n; }
        int   heldNotes() const { return heldCount; }
        bool  anyGate() const { for (auto& v : voices) if (v.active && v.gate) return true; return false; }
        float lastHz() const { return std::exp2 (lastLog2); }
        float lastVelocity() const { return heldCount > 0 ? heldVel[heldCount - 1] : 0.0f; }

    private:
        static constexpr int kMaxHeld = 32;

        float noteLog2 (int note, const SynthParams& P) const
        {
            float f = 440.0f * std::exp2 ((float) (note - 69) / 12.0f);
            if (P.quant) f = synthdetail::quantizeToScale (f, P.root, P.scale);
            f *= std::exp2 (P.shiftOct);
            f = std::min (16000.0f, std::max (8.0f, f));
            return std::log2 (f);
        }

        bool removeHeld (int note)
        {
            for (int k = 0; k < heldCount; ++k)
                if (heldNote[k] == note)
                {
                    for (int j = k; j < heldCount - 1; ++j) { heldNote[j] = heldNote[j + 1]; heldVel[j] = heldVel[j + 1]; }
                    --heldCount;
                    return true;
                }
            return false;
        }

        int allocate (int limit)
        {
            for (int v = 0; v < limit; ++v) if (! voices[v].active) return v;               // szabad hang
            int best = -1; uint32_t bestAge = 0xffffffffu;
            for (int v = 0; v < limit; ++v)                                                  // legrégebbi felengedett
                if (! voices[v].gate && voices[v].age < bestAge) { bestAge = voices[v].age; best = v; }
            if (best >= 0) return best;
            best = 0; bestAge = 0xffffffffu;
            for (int v = 0; v < limit; ++v)                                                  // legrégebbi
                if (voices[v].age < bestAge) { bestAge = voices[v].age; best = v; }
            return best;
        }

        double sr = 44100.0;
        SynthVoice voices[kMaxVoices];
        int   heldNote[kMaxHeld] {};
        float heldVel[kMaxHeld] {};
        int   heldCount = 0;
        bool  pedal = false, haveLast = false;
        uint32_t ageCounter = 0, tick = 0;
        float lastLog2 = 5.78f;
        float bendCur = 0.0f, bendTarget = 0.0f, mwCur = 0.0f, mwTarget = 0.0f;
        float bendCoef = 0.01f, mwCoef = 0.001f, gainCoef = 0.001f, polyGain = 1.0f;
        float dg = 1.0f, dnorm = 1.0f;
    };
}
