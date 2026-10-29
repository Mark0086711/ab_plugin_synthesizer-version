// Önálló (JUCE nélküli) tesztek a műfaj-tudatos presetgenerátorhoz.
// Fordítás: g++ -O2 -std=c++17 -ISource tests/gen_test.cpp -o gen_test && ./gen_test
#include "PresetGen.h"
#include <cstdio>
#include <set>

using namespace a2b;
static int failures = 0;

static void check (bool ok, const std::string& name, const std::string& detail = "")
{
    std::printf ("[%s] %-56s %s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.c_str());
    if (! ok) ++failures;
}

static std::string fmt (const char* f, double a = 0, double b = 0, double c = 0)
{
    char buf[200]; std::snprintf (buf, sizeof buf, f, a, b, c); return buf;
}

static double meanOf (int genre, const char* id, const std::string& text = "", int n = 300)
{
    double s = 0; int c = 0;
    for (int seed = 1; seed <= n; ++seed)
    {
        auto m = PresetGen::generate (genre, text, 1.0f, (uint32_t) seed);
        auto it = m.find (id);
        if (it != m.end()) { s += it->second; ++c; }
    }
    return c ? s / c : -1e9;
}

static int genreIndex (const char* name)
{
    auto names = PresetGen::genreNames();
    for (size_t i = 0; i < names.size(); ++i) if (names[i] == name) return (int) i;
    return -1;
}

int main()
{
    std::printf ("=== Audio2Bass preset generator tests ===\n");
    const int G = PresetGen::numGenres();
    check (G >= 14, "genre profiles available", fmt ("%.0f genres", (double) G));

    // determinizmus
    {
        bool same = true, differs = false;
        for (int g = 0; g < G; ++g)
        {
            auto a = PresetGen::generate (g, "dark wide", 0.7f, 123), b = PresetGen::generate (g, "dark wide", 0.7f, 123);
            auto c = PresetGen::generate (g, "dark wide", 0.7f, 124);
            if (a != b) same = false;
            if (a != c) differs = true;
        }
        check (same, "same seed gives exactly the same preset");
        check (differs, "different seeds give different presets");
    }

    // minden műfaj kitölti a hangot: nem üres, hallható réteg van
    {
        bool ok = true; std::string bad;
        for (int g = 0; g < G; ++g)
            for (uint32_t seed = 1; seed <= 100; ++seed)
            {
                auto m = PresetGen::generate (g, "", 1.0f, seed);
                const float sub = m.count ("sublvl") ? m["sublvl"] : 0.5f, o1 = m.count ("o1lvl") ? m["o1lvl"] : 0.7f;
                if (m.size() < 10 || (sub < 0.15f && o1 < 0.15f)) { ok = false; bad = PresetGen::genreNames()[(size_t) g]; }
            }
        check (ok, "every genre yields a complete, audible preset", bad);
    }

    // variáció = 0: a folytonos paraméterek középértékek, seedtől függetlenül
    {
        bool same = true;
        for (int g = 0; g < G; ++g)
        {
            auto a = PresetGen::generate (g, "", 0.0f, 1), b = PresetGen::generate (g, "", 0.0f, 99);
            for (const char* id : { "cut", "res", "dec", "rel", "drive" })
                if (a.count (id) && b.count (id) && std::abs (a[id] - b[id]) > 1e-4f) same = false;
        }
        check (same, "variation 0 gives the genre's centre values");
    }

    // műfaj-karakter: átlagok
    {
        const double trapSub = meanOf (genreIndex ("Trap 808"), "sublvl");
        const double acidRes = meanOf (genreIndex ("Acid"), "res"), houseRes = meanOf (genreIndex ("House"), "res");
        const double ambAtk = meanOf (genreIndex ("Ambient / Drone"), "atk"), trapAtk = meanOf (genreIndex ("Trap 808"), "atk");
        const double dubLcut = meanOf (genreIndex ("Dubstep"), "lcut"), trapLcut = meanOf (genreIndex ("Trap 808"), "lcut");
        const double drillGlide = meanOf (genreIndex ("Drill"), "glide"), trapGlide = meanOf (genreIndex ("Trap 808"), "glide");
        const double phonkDrive = meanOf (genreIndex ("Phonk"), "drive"), hiphopDrive = meanOf (genreIndex ("Hip-Hop"), "drive");
        check (trapSub > 0.85, "Trap 808: sub-dominant", fmt ("mean sublvl %.2f", trapSub));
        check (acidRes > houseRes + 3.0, "Acid: much more resonant than House", fmt ("res %.1f vs %.1f", acidRes, houseRes));
        check (ambAtk > 20.0 * trapAtk, "Ambient: slow attack, Trap: instant", fmt ("%.0f ms vs %.1f ms", ambAtk, trapAtk));
        check (dubLcut > 1.5 && trapLcut < 0, "Dubstep wobbles (LFO > cutoff), Trap does not", fmt ("lcut %.2f", dubLcut));
        check (drillGlide > trapGlide + 60.0, "Drill glides more than Trap", fmt ("glide %.0f vs %.0f ms", drillGlide, trapGlide));
        check (phonkDrive > hiphopDrive + 0.3, "Phonk is far more distorted than Hip-Hop", fmt ("drive %.2f vs %.2f", phonkDrive, hiphopDrive));
    }

    // kulcsszavak (azonos seed mellett összehasonlítva)
    {
        const int trap = genreIndex ("Trap 808");
        auto base = PresetGen::generate (trap, "", 0.7f, 7);
        auto dark = PresetGen::generate (trap, "dark", 0.7f, 7);
        auto dirty = PresetGen::generate (trap, "dirty", 0.7f, 7);
        auto wide = PresetGen::generate (trap, "wide", 0.7f, 7);
        auto shortN = PresetGen::generate (trap, "short", 0.7f, 7);
        auto longN = PresetGen::generate (trap, "long", 0.7f, 7);
        auto glide = PresetGen::generate (trap, "slide", 0.7f, 7);
        auto clean = PresetGen::generate (trap, "clean", 0.7f, 7);
        check (dark["cut"] < base["cut"] * 0.6f, "'dark' lowers the cutoff", fmt ("%.0f -> %.0f Hz", base["cut"], dark["cut"]));
        check (dirty["drive"] > base["drive"] + 0.2f || dirty["drive"] >= 0.99f, "'dirty' raises the drive", fmt ("%.2f -> %.2f", base["drive"], dirty["drive"]));
        check (wide["uni"] >= 5 && wide["width"] > 1.3f, "'wide' adds unison and stereo width", fmt ("uni %.0f width %.2f", wide["uni"], wide["width"]));
        check (shortN["dec"] < base["dec"] && shortN["sus"] == 0 && longN["dec"] > base["dec"], "'short' and 'long' change the decay");
        check (glide["glide"] >= 140 && glide["gleg"] == 0, "'slide' sets a long glide", fmt ("glide %.0f ms", glide["glide"]));
        check (clean["drive"] <= 0.05f && clean["subdrive"] == 0, "'clean' removes distortion");

        // magyar kulcsszavak
        auto sotet = PresetGen::generate (trap, "sötét", 0.7f, 7), szeles = PresetGen::generate (trap, "széles", 0.7f, 7);
        auto hosszu = PresetGen::generate (trap, "hosszú", 0.7f, 7), torz = PresetGen::generate (trap, "torzított", 0.7f, 7);
        check (sotet["cut"] < base["cut"] * 0.6f, "Hungarian: 'sötét' works", fmt ("%.0f Hz", sotet["cut"]));
        check (szeles["uni"] >= 5, "Hungarian: 'széles' works");
        check (hosszu["dec"] > base["dec"], "Hungarian: 'hosszú' works");
        check (torz["drive"] > base["drive"] + 0.2f || torz["drive"] >= 0.99f, "Hungarian: 'torzított' works");

        // kis-nagybetű, írásjelek, több szó
        auto many = PresetGen::generate (trap, "DARK, Wide; and LONG!", 0.7f, 7);
        check (many["cut"] < base["cut"] * 0.6f && many["uni"] >= 5 && many["dec"] > base["dec"], "case, punctuation and several words", "");

        auto lofi = PresetGen::generate (trap, "Lo-Fi", 0.7f, 7);
        check (lofi["bits"] <= 12 && lofi["hiss"] >= 0.4f, "'Lo-Fi' with hyphen works", fmt ("bits %.0f", lofi["bits"]));
    }

    // értéktartományok
    {
        bool ok = true; std::string bad;
        for (int g = 0; g < G; ++g)
            for (uint32_t seed = 1; seed <= 200; ++seed)
                for (const char* text : { "", "dark dirty wide long", "short clean punchy" })
                {
                    auto m = PresetGen::generate (g, text, 1.0f, seed);
                    for (auto& kv : m)
                    {
                        const std::string& id = kv.first; const float v = kv.second;
                        bool inRange = std::isfinite (v);
                        if (id == "uni") inRange = inRange && v >= 1 && v <= 7;
                        else if (id == "drive" || id == "subdrive" || id == "chmix" || id == "sublvl" || id == "o1lvl" || id == "o2lvl") inRange = inRange && v >= 0 && v <= 1;
                        else if (id == "cut") inRange = inRange && v >= 20 && v <= 16000;
                        else if (id == "res") inRange = inRange && v >= 0.5f && v <= 10;
                        else if (id == "dec" || id == "rel") inRange = inRange && v >= 5 && v <= 3000;
                        else if (id == "dtype") inRange = inRange && v >= 0 && v <= 6;
                        else if (id == "o1wave" || id == "o2wave") inRange = inRange && v >= 0 && v <= 4;
                        if (! inRange) { ok = false; bad = id + "=" + std::to_string (v); }
                    }
                }
        check (ok, "generated values stay in valid ranges", bad);
    }

    // általános célú (nem basszus) műfajok
    {
        const double padAtk = meanOf (genreIndex ("Warm Pad"), "atk"), pluckAtk = meanOf (genreIndex ("Pluck / Arp"), "atk");
        const double superUni = meanOf (genreIndex ("Supersaw Lead"), "uni"), strUni = meanOf (genreIndex ("Strings"), "uni");
        const double bellFm = meanOf (genreIndex ("Bells / Mallets"), "fm"), bellDec = meanOf (genreIndex ("Bells / Mallets"), "dec");
        const double chipBits = meanOf (genreIndex ("Chiptune / 8-bit"), "bits"), orgH1 = meanOf (genreIndex ("Organ"), "h1int");
        const double pluckSus = meanOf (genreIndex ("Pluck / Arp"), "sus"), ambRel = meanOf (genreIndex ("Ambient Wash"), "rel");
        const double monoVm = meanOf (genreIndex ("Classic Mono Lead"), "vmode"), padVm = meanOf (genreIndex ("Warm Pad"), "vmode");
        check (padAtk > 400 && pluckAtk < 3, "Pad: slow attack; Pluck: instant", fmt ("%.0f ms vs %.1f ms", padAtk, pluckAtk));
        check (superUni >= 5 && strUni >= 4, "Supersaw and Strings use wide unison", fmt ("%.1f and %.1f voices", superUni, strUni));
        check (bellFm > 0.3 && bellDec > 1200, "Bells: FM and a long ring", fmt ("fm %.2f, decay %.0f ms", bellFm, bellDec));
        check (std::abs (orgH1 - 2.0) < 1e-6, "Organ: harmony voice an octave up");
        check (chipBits <= 10 && pluckSus == 0 && ambRel > 2500, "Chiptune is crushed, Pluck has no sustain, Ambient releases slowly", fmt ("bits %.1f, rel %.0f ms", chipBits, ambRel));
        check (monoVm >= 1.0 && padVm == 0.0, "Mono Lead plays mono/legato, Pad plays polyphonic", fmt ("vmode %.1f vs %.1f", monoVm, padVm));

        // a basszus-műfajok az effekt alapértékeiből indulnak, az általános műfajok nem
        auto bass = PresetGen::generate (genreIndex ("Trap 808"), "", 0.7f, 5), pad = PresetGen::generate (genreIndex ("Warm Pad"), "", 0.7f, 5);
        check (bass.count ("monosub") && bass.count ("glide") && ! pad.count ("monosub") && ! pad.count ("sublvl"),
               "bass genres carry the bass baseline, general genres do not");
    }

    std::printf ("=== %s (%d failure%s) ===\n", failures == 0 ? "ALL PASSED" : "SOME FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
