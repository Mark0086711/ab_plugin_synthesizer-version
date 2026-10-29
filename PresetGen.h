#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Műfaj-tudatos presetgenerátor ("AI preset maker").
//
// Ez NEM neurális háló és nem használ internetet: műfaj-profilokból (paramétertartományok és
// valószínűségek) mintavételez egy seedelt véletlenszám-generátorral, majd a szöveges leírás
// kulcsszavai (magyarul és angolul) szabályokkal módosítják az eredményt.
// Ugyanaz a (műfaj, leírás, variáció, seed) mindig ugyanazt a presetet adja.
//
// A kimenet: paraméter-azonosító -> valós (nem normalizált) érték. Amit a generátor nem ad meg,
// azt a hívó az alapértékre állítja.
namespace a2b
{
    using ParamMap = std::map<std::string, float>;

    class PresetGen
    {
    public:
        static std::vector<std::string> genreNames()
        {
            std::vector<std::string> n;
            for (auto& g : genres()) n.push_back (g.name);
            return n;
        }

        static int numGenres() { return (int) genres().size(); }

        static ParamMap generate (int genre, const std::string& descriptors, float variation, uint32_t seed)
        {
            const auto& list = genres();
            genre = std::max (0, std::min ((int) list.size() - 1, genre));
            variation = std::max (0.0f, std::min (1.0f, variation));

            Rng rng (seed);
            ParamMap m;

            // A basszus-műfajok az effekt alapértékeiből indulnak (a szintetizátor általános alapértékei mások),
            // így a basszushangok mindkét pluginban ugyanúgy szólnak.
            if (list[(size_t) genre].bass)
            {
                m["glide"] = 30.0f; m["sublvl"] = 0.5f; m["monosub"] = 1.0f; m["cut"] = 600.0f;
                m["res"] = 1.2f; m["rel"] = 120.0f; m["drive"] = 0.25f;
            }

            for (auto& s : list[(size_t) genre].specs)
            {
                if (s.prob < 1.0f && rng.u() > s.prob) continue;

                float v;
                if (! s.choices.empty())
                    v = s.choices[(size_t) (rng.u() * (float) s.choices.size()) % s.choices.size()];
                else
                {
                    const float mid = 0.5f * (s.lo + s.hi), half = 0.5f * (s.hi - s.lo);
                    v = mid + (rng.u() * 2.0f - 1.0f) * half * variation;
                    if (isDiscrete (s.id)) v = std::round (s.lo + rng.u() * (s.hi - s.lo));
                }
                m[s.id] = v;
            }

            applyDescriptors (m, descriptors, rng);
            sanitise (m);
            return m;
        }

    private:
        struct S { const char* id; float lo, hi, prob; std::vector<float> choices; };
        struct Genre { const char* name; std::vector<S> specs; bool bass = true; };

        struct Rng
        {
            uint32_t s;
            explicit Rng (uint32_t seed) : s (seed * 2654435761u + 0x9e3779b9u) { for (int i = 0; i < 4; ++i) next(); }
            uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
            float u() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
        };

        static S R (const char* id, float lo, float hi, float prob = 1.0f) { return { id, lo, hi, prob, {} }; }
        static S C (const char* id, std::vector<float> ch, float prob = 1.0f) { return { id, 0, 0, prob, std::move (ch) }; }

        static bool isDiscrete (const std::string& id)
        {
            static const char* ids[] = { "o1wave","o2wave","subwave","suboct","dtype","dpos","lshape","lsync","pumprate","ftype",
                                         "uni","o1oct","o1semi","o2oct","o2semi","shift","trans","bits","crate","h1int","h2int","bend","vmode","voices" };
            for (auto* x : ids) if (id == x) return true;
            return false;
        }

        static float getOr (const ParamMap& m, const char* id, float def)
        {
            auto it = m.find (id);
            return it != m.end() ? it->second : def;
        }

        //----------------------------------------------------------------------
        static const std::vector<Genre>& genres()
        {
            static const std::vector<Genre> g = {
                { "Trap 808", {
                    R ("sublvl", 0.85f, 1.0f), C ("subwave", { 0 }), C ("suboct", { 0 }), C ("o1wave", { 3, 3, 0 }), R ("o1lvl", 0.08f, 0.35f),
                    R ("penv", 5, 14), R ("pdec", 50, 110), R ("atk", 1, 3), R ("dec", 600, 1400), R ("sus", 0, 0.1f), R ("rel", 250, 600),
                    R ("glide", 0, 60), C ("gleg", { 1 }), R ("drive", 0.1f, 0.5f), C ("dtype", { 0, 0, 4, 5 }), R ("subdrive", 0.1f, 0.5f),
                    C ("xon", { 1 }), R ("xover", 130, 170), C ("monosub", { 1 }), R ("cut", 2500, 5000), R ("res", 0.7f, 1.8f),
                    R ("char", 0, 0.5f), R ("tone", 6000, 12000, 0.3f), R ("chmix", 0, 0.15f, 0.3f) } },

                { "Drill", {
                    R ("sublvl", 0.85f, 1.0f), C ("o1wave", { 0, 3 }), R ("o1lvl", 0.15f, 0.4f), R ("penv", 3, 8), R ("pdec", 60, 140),
                    R ("atk", 1, 4), R ("dec", 900, 2000), R ("sus", 0.1f, 0.4f), R ("rel", 300, 800), R ("glide", 90, 220), C ("gleg", { 0 }),
                    C ("retrig", { 0, 1 }), R ("drive", 0.2f, 0.6f), C ("dtype", { 0, 1, 4 }), R ("subdrive", 0.2f, 0.5f), C ("xon", { 1 }),
                    R ("xover", 120, 160), R ("cut", 2200, 4500), R ("res", 0.8f, 2.0f), R ("tone", 5000, 10000), R ("hiss", 0, 0.2f, 0.3f) } },

                { "Hip-Hop", {
                    R ("sublvl", 0.8f, 1.0f), C ("subwave", { 0, 0, 1 }), C ("o1wave", { 3, 2 }), R ("o1lvl", 0.1f, 0.35f), R ("penv", 0, 4),
                    R ("pdec", 40, 80), R ("atk", 2, 8), R ("dec", 300, 700), R ("sus", 0.3f, 0.7f), R ("rel", 150, 300), R ("glide", 0, 40),
                    R ("drive", 0, 0.25f), C ("dtype", { 0, 5 }), R ("subdrive", 0, 0.2f), R ("cut", 600, 1500), R ("res", 0.5f, 1.2f),
                    R ("drift", 0, 0.4f, 0.5f), R ("hiss", 0, 0.3f, 0.4f), C ("xon", { 0, 1 }), R ("xover", 120, 150) } },

                { "Dubstep", {
                    C ("o1wave", { 0, 0, 1 }), R ("o1lvl", 0.6f, 0.9f), C ("o2wave", { 0, 1 }), R ("o2lvl", 0.4f, 0.8f), R ("o2det", 5, 25),
                    C ("o2oct", { -1, 0 }), R ("uni", 1, 5), R ("unid", 10, 30), R ("unis", 0.5f, 1.0f), R ("sublvl", 0.3f, 0.6f),
                    R ("cut", 250, 700), R ("res", 2, 6), R ("lsync", 3, 5), C ("lshape", { 0, 1, 2, 3 }), R ("lcut", 1.5f, 3.5f),
                    R ("drive", 0.3f, 0.7f), C ("dtype", { 0, 1, 2 }), C ("mbon", { 1, 1, 0 }), R ("mbd2", 0.2f, 0.6f, 0.8f),
                    R ("ott", 0.25f, 0.7f), R ("chmix", 0, 0.3f), R ("fm", 0.15f, 0.5f, 0.4f), C ("xon", { 1 }), R ("xover", 90, 130),
                    R ("mfreq", 100, 150), R ("atk", 1, 6), R ("dec", 200, 500), R ("sus", 0.8f, 1.0f), R ("rel", 80, 250), R ("width", 1.1f, 1.5f) } },

                { "Reese / DnB", {
                    C ("o1wave", { 0 }), R ("o1lvl", 0.6f, 0.8f), C ("o2wave", { 0 }), R ("o2lvl", 0.6f, 0.85f), R ("o2det", 8, 24),
                    R ("uni", 3, 5), R ("unid", 10, 25), R ("unis", 0.6f, 1.0f), R ("sublvl", 0.3f, 0.5f), R ("cut", 300, 700), R ("res", 1, 2.5f),
                    R ("lcut", 0.2f, 0.6f), C ("lsync", { 0 }), R ("lrate", 0.1f, 0.5f), R ("drive", 0.2f, 0.5f), C ("dtype", { 0, 1, 4 }),
                    R ("chmix", 0.3f, 0.5f), R ("chdepth", 0.4f, 0.7f), C ("xon", { 1 }), R ("xover", 85, 120), R ("mfreq", 100, 140),
                    R ("atk", 5, 15), R ("dec", 300, 600), R ("sus", 0.9f, 1.0f), R ("rel", 150, 300), R ("width", 1.2f, 1.5f), R ("drift", 0.2f, 0.6f, 0.6f) } },

                { "House", {
                    C ("o1wave", { 0, 1 }), R ("o1lvl", 0.5f, 0.8f), R ("sublvl", 0.4f, 0.7f), R ("cut", 300, 900), R ("res", 1, 2.5f),
                    R ("fenv", 1.5f, 3.5f), R ("fdec", 100, 250), R ("fsus", 0, 0.1f), R ("dec", 150, 300), R ("sus", 0, 0.2f), R ("rel", 60, 150),
                    R ("atk", 1, 4), R ("glide", 0, 30), R ("drive", 0.1f, 0.3f), R ("pump", 0.3f, 0.7f), C ("pumprate", { 2 }), R ("pumprel", 0.5f, 0.8f),
                    R ("tatk", 0, 0.4f), C ("xon", { 0, 1 }), R ("xover", 100, 140) } },

                { "Techno", {
                    C ("o1wave", { 0, 1 }), R ("o1lvl", 0.6f, 0.9f), R ("sublvl", 0.3f, 0.6f), R ("cut", 200, 600), R ("res", 2, 4),
                    R ("fenv", 2, 4), R ("fdec", 80, 200), R ("fsus", 0, 0.1f), R ("dec", 100, 220), R ("sus", 0, 0.15f), R ("rel", 50, 140),
                    C ("lsync", { 4, 5 }), R ("lcut", 0.3f, 0.8f), C ("lshape", { 0, 1, 3 }), R ("drive", 0.2f, 0.5f), C ("dtype", { 0, 1, 3 }),
                    R ("chmix", 0, 0.2f), R ("pump", 0.2f, 0.5f, 0.5f), C ("pumprate", { 2 }) } },

                { "Acid", {
                    R ("sublvl", 0, 0.1f), C ("o1wave", { 0, 1 }), R ("o1lvl", 0.9f, 1.0f), R ("res", 5, 9), R ("cut", 150, 400),
                    R ("fenv", 2.5f, 4.0f), R ("fdec", 120, 250), C ("fsus", { 0 }), R ("glide", 40, 90), C ("gleg", { 0 }), C ("retrig", { 0 }),
                    R ("dec", 100, 250), R ("sus", 0, 0.3f), R ("rel", 40, 100), R ("drive", 0.3f, 0.6f), C ("dtype", { 1, 3, 6 }),
                    C ("f24", { 1 }), R ("lcut", 0, 0.5f, 0.3f), C ("lsync", { 4 }) } },

                { "Synthwave", {
                    C ("o1wave", { 0 }), R ("o1lvl", 0.6f, 0.8f), C ("o2wave", { 0 }), R ("o2lvl", 0.5f, 0.7f), R ("o2det", 8, 15),
                    R ("uni", 2, 5), R ("unid", 8, 18), R ("unis", 0.5f, 0.9f), R ("sublvl", 0.2f, 0.4f), R ("cut", 800, 2000), R ("res", 0.8f, 1.5f),
                    R ("atk", 3, 10), R ("dec", 200, 400), R ("sus", 0.5f, 0.9f), R ("rel", 100, 200), R ("chmix", 0.3f, 0.6f), R ("chdepth", 0.3f, 0.6f),
                    R ("drive", 0, 0.2f), C ("lshape", { 0 }), R ("lrate", 4, 6), R ("lpitch", 0, 10, 0.5f), R ("drift", 0.2f, 0.6f),
                    C ("xon", { 1 }), R ("xover", 90, 120) } },

                { "Funk / Slap", {
                    C ("o1wave", { 1, 0 }), R ("o1lvl", 0.7f, 0.95f), R ("sublvl", 0.2f, 0.45f), R ("cut", 400, 900), R ("res", 1, 2.5f),
                    R ("fenv", 2, 3.5f), R ("fdec", 60, 140), R ("fsus", 0, 0.2f), R ("dec", 150, 300), R ("sus", 0, 0.3f), R ("rel", 50, 120),
                    R ("atk", 1, 2), R ("drive", 0.1f, 0.3f), C ("dtype", { 0, 4 }), R ("tatk", 0.2f, 0.6f, 0.6f), R ("glide", 0, 15) } },

                { "Lo-fi", {
                    R ("sublvl", 0.7f, 1.0f), C ("subwave", { 0, 1 }), C ("o1wave", { 2, 3 }), R ("o1lvl", 0.2f, 0.4f), R ("cut", 500, 1200),
                    R ("res", 0.5f, 1.0f), R ("drive", 0.1f, 0.3f), C ("dtype", { 5 }), R ("bits", 10, 14, 0.6f), R ("crate", 1, 3, 0.5f),
                    R ("hiss", 0.2f, 0.6f), R ("drift", 0.3f, 0.8f), C ("phrand", { 1 }), R ("chmix", 0, 0.3f), R ("atk", 5, 20),
                    R ("dec", 300, 600), R ("sus", 0.5f, 0.9f), R ("rel", 150, 350), R ("tone", 3000, 8000) } },

                { "Phonk", {
                    R ("sublvl", 0.9f, 1.0f), C ("o1wave", { 0, 1 }), R ("o1lvl", 0.2f, 0.5f), R ("penv", 6, 14), R ("pdec", 40, 90),
                    R ("atk", 1, 3), R ("dec", 500, 1100), R ("sus", 0, 0.1f), R ("rel", 250, 500), R ("drive", 0.5f, 0.9f), C ("dtype", { 1, 6 }),
                    R ("subdrive", 0.4f, 0.8f), C ("xon", { 1 }), R ("xover", 140, 180), R ("tone", 4000, 9000), R ("cut", 2500, 5000),
                    R ("res", 1, 2.2f), R ("bits", 8, 12, 0.3f), R ("hiss", 0.1f, 0.3f, 0.5f) } },

                { "Pop / EDM", {
                    R ("sublvl", 0.6f, 0.9f), C ("o1wave", { 0, 1 }), R ("o1lvl", 0.4f, 0.6f), R ("cut", 800, 2500), R ("res", 0.8f, 1.5f),
                    R ("drive", 0, 0.25f), R ("chmix", 0, 0.3f), R ("pump", 0.2f, 0.5f), C ("pumprate", { 2 }), R ("atk", 2, 8), R ("dec", 200, 400),
                    R ("sus", 0.6f, 1.0f), R ("rel", 100, 250), R ("uni", 1, 3), R ("unid", 5, 15), R ("width", 1.0f, 1.3f), R ("boost", 0, 3) } },

                { "Ambient / Drone", {
                    C ("o1wave", { 2, 3 }), R ("o1lvl", 0.4f, 0.7f), R ("sublvl", 0.5f, 0.8f), R ("atk", 150, 800), R ("dec", 500, 1500),
                    R ("sus", 0.9f, 1.0f), R ("rel", 800, 2000), R ("cut", 300, 800), R ("res", 0.6f, 1.2f), R ("drift", 0.4f, 1.0f),
                    R ("hiss", 0.1f, 0.4f), R ("chmix", 0.4f, 0.6f), R ("chdepth", 0.4f, 0.7f), C ("lshape", { 0 }), R ("lrate", 0.1f, 0.4f),
                    R ("lcut", 0.3f, 1.0f, 0.6f), R ("uni", 2, 4), R ("unid", 8, 25), C ("phrand", { 1 }) } },

                { "Amapiano (log drum)", {
                    C ("o1wave", { 3 }), R ("o1lvl", 0.7f, 1.0f), R ("sublvl", 0.2f, 0.5f), R ("penv", 3, 8), R ("pdec", 30, 70), C ("atk", { 1 }),
                    R ("dec", 120, 260), C ("sus", { 0 }), R ("rel", 80, 180), R ("nlen", 200, 400), R ("cut", 1200, 3000), R ("res", 2, 4),
                    R ("drive", 0.2f, 0.5f), C ("dtype", { 0, 4 }), R ("fm", 0.1f, 0.4f), R ("o2lvl", 0, 0.3f, 0.5f), C ("o2oct", { 1 }),
                    R ("chmix", 0, 0.2f) } },

                //---------------- általános célú (nem basszus) műfajok ----------------
                { "Supersaw Lead", {
                    C ("o1wave", { 0 }), R ("o1lvl", 0.7f, 0.9f), C ("o2wave", { 0 }), R ("o2lvl", 0.3f, 0.6f), R ("o2det", 6, 14),
                    R ("uni", 5, 7), R ("unid", 20, 35), R ("unis", 0.7f, 1.0f), R ("cut", 5000, 11000), R ("res", 0.6f, 1.4f),
                    R ("atk", 2, 10), R ("dec", 200, 500), R ("sus", 0.7f, 1.0f), R ("rel", 150, 400), C ("vmode", { 0, 2 }),
                    R ("glide", 0, 40, 0.5f), R ("chmix", 0.2f, 0.4f), R ("width", 1.1f, 1.5f), R ("drive", 0, 0.15f),
                    R ("lpitch", 3, 8, 0.6f), R ("lrate", 4.5f, 6.0f) } , false },

                { "Classic Mono Lead", {
                    C ("o1wave", { 0, 1 }), R ("o1lvl", 0.7f, 0.9f), C ("o2wave", { 0, 1 }), R ("o2lvl", 0.3f, 0.6f), R ("o2det", 5, 12),
                    R ("cut", 2500, 6000), R ("res", 1, 3), R ("fenv", 1, 2.5f), R ("fdec", 150, 400), R ("fsus", 0.3f, 0.7f),
                    R ("atk", 2, 6), R ("dec", 200, 400), R ("sus", 0.7f, 0.95f), R ("rel", 120, 250), C ("vmode", { 1, 2 }),
                    R ("glide", 20, 80), C ("gleg", { 1, 0 }), R ("lpitch", 4, 8), R ("lrate", 4.5f, 6.0f), R ("drive", 0, 0.3f),
                    C ("dtype", { 0, 1 }), R ("chmix", 0, 0.25f) }, false },

                { "Pluck / Arp", {
                    C ("o1wave", { 0, 1 }), R ("o1lvl", 0.7f, 0.9f), C ("o2wave", { 0, 1 }), R ("o2lvl", 0, 0.5f), R ("o2det", 5, 14),
                    R ("cut", 1200, 3500), R ("res", 0.8f, 2.0f), R ("fenv", 2, 3.5f), R ("fdec", 80, 200), R ("fsus", 0, 0.1f),
                    R ("atk", 1, 2), R ("dec", 150, 350), C ("sus", { 0 }), R ("rel", 80, 220), R ("chmix", 0.1f, 0.35f),
                    C ("vmode", { 0 }), R ("uni", 1, 3), R ("unid", 5, 12) }, false },

                { "Warm Pad", {
                    C ("o1wave", { 0, 0, 2 }), R ("o1lvl", 0.5f, 0.7f), C ("o2wave", { 0, 2 }), R ("o2lvl", 0.4f, 0.6f), R ("o2det", 5, 10),
                    R ("uni", 2, 4), R ("unid", 10, 22), R ("unis", 0.6f, 1.0f), R ("cut", 1500, 3500), R ("res", 0.6f, 1.2f),
                    R ("atk", 400, 1000), R ("dec", 400, 800), R ("sus", 0.8f, 1.0f), R ("rel", 1200, 2600), R ("chmix", 0.35f, 0.55f),
                    R ("chdepth", 0.4f, 0.6f), R ("drift", 0.2f, 0.5f), R ("lcut", 0.2f, 0.5f), R ("lrate", 0.1f, 0.3f), C ("lshape", { 0 }),
                    C ("vmode", { 0 }), R ("voices", 8, 12) }, false },

                { "Dark Pad", {
                    C ("o1wave", { 0 }), R ("o1lvl", 0.6f, 0.8f), C ("o2wave", { 1 }), R ("o2lvl", 0.3f, 0.5f), R ("uni", 3, 5), R ("unid", 15, 28),
                    R ("cut", 500, 1100), R ("res", 1, 2), R ("lcut", 0.6f, 1.2f), R ("lrate", 0.08f, 0.25f), R ("atk", 700, 1400),
                    R ("sus", 0.9f, 1.0f), R ("rel", 1800, 3000), R ("chmix", 0.25f, 0.45f), R ("drift", 0.3f, 0.6f), R ("hiss", 0.05f, 0.2f, 0.5f),
                    C ("vmode", { 0 }) }, false },

                { "Strings", {
                    C ("o1wave", { 0 }), R ("o1lvl", 0.6f, 0.8f), R ("uni", 4, 6), R ("unid", 14, 24), R ("unis", 0.8f, 1.0f),
                    R ("cut", 2500, 4500), R ("res", 0.5f, 0.9f), R ("atk", 250, 500), R ("dec", 400, 700), R ("sus", 0.8f, 0.95f),
                    R ("rel", 500, 1000), R ("chmix", 0.4f, 0.6f), R ("chdepth", 0.5f, 0.7f), R ("lpitch", 5, 10), R ("lrate", 4.5f, 5.5f),
                    R ("drift", 0.2f, 0.4f), C ("vmode", { 0 }) }, false },

                { "Brass", {
                    C ("o1wave", { 0 }), R ("o1lvl", 0.7f, 0.9f), C ("o2wave", { 0 }), R ("o2lvl", 0.4f, 0.6f), R ("o2det", 6, 12),
                    R ("uni", 2, 3), R ("unid", 8, 14), R ("cut", 700, 1400), R ("res", 0.8f, 1.5f), R ("fenv", 2.4f, 3.2f),
                    R ("fdec", 180, 320), R ("fsus", 0.4f, 0.6f), R ("atk", 40, 120), R ("dec", 150, 300), R ("sus", 0.75f, 0.9f),
                    R ("rel", 120, 220), R ("drive", 0.1f, 0.3f), R ("lpitch", 4, 7), R ("lrate", 5.0f, 5.5f), R ("velcut", 0.4f, 0.7f),
                    R ("chmix", 0.1f, 0.25f), C ("vmode", { 0 }) }, false },

                { "FM Electric Piano", {
                    C ("o1wave", { 3 }), R ("o1lvl", 0.7f, 0.9f), C ("o2wave", { 3 }), C ("o2oct", { 1 }), R ("fm", 0.15f, 0.35f),
                    R ("cut", 9000, 14000), R ("atk", 1, 2), R ("dec", 700, 1600), R ("sus", 0.05f, 0.25f), R ("rel", 250, 450),
                    R ("velamp", 0.7f, 1.0f), R ("velcut", 0.2f, 0.5f), R ("chmix", 0.15f, 0.3f), R ("tatk", 0.2f, 0.5f, 0.6f),
                    R ("lamp", 0.1f, 0.25f, 0.5f), R ("lrate", 4.0f, 5.5f), C ("vmode", { 0 }) }, false },

                { "Organ", {
                    C ("o1wave", { 3 }), R ("o1lvl", 0.5f, 0.7f), C ("o2wave", { 3 }), C ("o2oct", { 1 }), R ("o2lvl", 0.3f, 0.5f),
                    C ("h1int", { 2 }), R ("h1lvl", 0.3f, 0.6f), C ("h2int", { 3, 2, 6 }), R ("h2lvl", 0.2f, 0.4f),
                    R ("atk", 2, 6), R ("dec", 10, 30), C ("sus", { 1 }), R ("rel", 30, 70), R ("lamp", 0.1f, 0.2f), R ("lrate", 5.5f, 7.5f),
                    R ("chmix", 0.25f, 0.4f), R ("lpitch", 3, 6), R ("drive", 0, 0.2f, 0.5f), R ("velamp", 0, 0.2f), C ("vmode", { 0 }) }, false },

                { "Bells / Mallets", {
                    C ("o1wave", { 3 }), R ("o1lvl", 0.7f, 0.9f), C ("o2wave", { 3 }), C ("o2oct", { 1, 2 }), C ("o2semi", { 0, 3, 4, 7 }),
                    R ("fm", 0.3f, 0.6f), R ("cut", 10000, 15000), C ("atk", { 1 }), R ("dec", 1200, 2600), C ("sus", { 0 }),
                    R ("rel", 800, 1800), R ("chmix", 0.25f, 0.45f), R ("chdepth", 0.3f, 0.5f), R ("velcut", 0.1f, 0.3f),
                    R ("hiss", 0, 0.05f, 0.5f), C ("vmode", { 0 }) }, false },

                { "Chiptune / 8-bit", {
                    C ("o1wave", { 1, 2, 1 }), R ("o1pw", 0.125f, 0.5f), R ("o1lvl", 0.8f, 1.0f), C ("o2wave", { 1, 2 }), R ("o2lvl", 0, 0.4f),
                    R ("cut", 12000, 16000), R ("res", 0.5f, 0.9f), C ("f24", { 0 }), C ("atk", { 1 }), R ("dec", 50, 200), R ("sus", 0.5f, 0.9f),
                    R ("rel", 20, 60), R ("bits", 6, 10), R ("crate", 2, 5), C ("vmode", { 1, 2, 0 }), C ("voices", { 4 }),
                    R ("lpw", 0.3f, 0.6f, 0.4f), R ("lrate", 4, 7), C ("drive", { 0 }) }, false },

                { "Acid Lead", {
                    C ("o1wave", { 0, 1 }), R ("o1lvl", 0.9f, 1.0f), R ("res", 5, 9), R ("cut", 600, 1800), R ("fenv", 2.5f, 4.0f),
                    R ("fdec", 120, 260), R ("fsus", 0, 0.2f), C ("atk", { 1 }), R ("dec", 150, 260), R ("sus", 0.2f, 0.5f), R ("rel", 60, 140),
                    C ("vmode", { 1 }), R ("glide", 40, 90), C ("gleg", { 0 }), R ("drive", 0.3f, 0.6f), C ("dtype", { 1, 3, 6 }) }, false },

                { "Trance Pluck", {
                    C ("o1wave", { 0 }), R ("o1lvl", 0.7f, 0.9f), R ("uni", 5, 7), R ("unid", 22, 35), R ("unis", 0.8f, 1.0f),
                    R ("cut", 2500, 5000), R ("res", 0.7f, 1.4f), R ("fenv", 1.5f, 3.0f), R ("fdec", 90, 180), R ("fsus", 0.3f, 0.5f),
                    R ("atk", 1, 3), R ("dec", 180, 300), R ("sus", 0.3f, 0.5f), R ("rel", 120, 200), R ("chmix", 0.25f, 0.4f),
                    R ("pump", 0.3f, 0.6f, 0.5f), C ("pumprate", { 2 }), C ("vmode", { 0 }) }, false },

                { "Ambient Wash", {
                    C ("o1wave", { 2, 3 }), R ("o1lvl", 0.5f, 0.7f), C ("o2wave", { 2, 3 }), R ("o2lvl", 0.4f, 0.7f), R ("o2det", 4, 8),
                    R ("uni", 3, 5), R ("unid", 18, 30), R ("atk", 900, 1600), R ("dec", 800, 1500), C ("sus", { 1 }), R ("rel", 2500, 3000),
                    R ("cut", 4000, 9000), R ("chmix", 0.5f, 0.7f), R ("chdepth", 0.5f, 0.8f), R ("drift", 0.4f, 0.8f), R ("hiss", 0.05f, 0.15f),
                    R ("lcut", 0.3f, 0.7f), R ("lrate", 0.08f, 0.2f), C ("vmode", { 0 }) }, false },

                { "Riser / FX", {
                    C ("o1wave", { 0 }), R ("o1lvl", 0.6f, 0.9f), R ("uni", 5, 7), R ("unid", 35, 55), R ("cut", 300, 700), R ("res", 1.5f, 3.0f),
                    R ("fenv", 3.5f, 5.0f), R ("fdec", 1500, 2000), R ("fsus", 0.9f, 1.0f), R ("atk", 1000, 1600), C ("sus", { 1 }),
                    R ("rel", 2000, 3000), R ("noise", 0.1f, 0.3f), R ("chmix", 0.4f, 0.6f), R ("lcut", 0.5f, 1.2f), R ("lrate", 0.1f, 0.4f),
                    C ("vmode", { 0 }) }, false },

                { "Wild / Experimental", {
                    C ("o1wave", { 0, 1, 2, 3, 4 }), R ("o1lvl", 0.3f, 1.0f), C ("o2wave", { 0, 1, 2, 3, 4 }), R ("o2lvl", 0, 0.9f), R ("o2det", -50, 50),
                    R ("fm", 0, 0.8f, 0.6f), R ("uni", 1, 7), R ("unid", 0, 50), R ("sublvl", 0.3f, 1.0f), R ("cut", 150, 8000), R ("res", 0.6f, 7),
                    R ("fenv", -3, 4), R ("fdec", 50, 800), R ("lcut", 0, 4), R ("lsync", 0, 5), C ("lshape", { 0, 1, 2, 3, 4 }), R ("lrate", 0.2f, 12),
                    R ("drive", 0, 0.9f), C ("dtype", { 0, 1, 2, 3, 4, 5, 6 }), R ("bits", 6, 16, 0.4f), R ("crate", 1, 8, 0.4f), R ("chmix", 0, 0.6f),
                    R ("penv", -12, 24, 0.6f), R ("pdec", 20, 400), R ("atk", 1, 120), R ("dec", 100, 1500), R ("sus", 0, 1), R ("rel", 50, 1500),
                    R ("glide", 0, 250), R ("drift", 0, 1, 0.5f), R ("hiss", 0, 0.5f, 0.4f), C ("mbon", { 0, 1 }), R ("mbd1", 0, 0.6f, 0.5f),
                    R ("mbd3", 0, 0.6f, 0.5f), R ("ott", 0, 0.6f, 0.5f), R ("tatk", -0.5f, 0.6f, 0.5f), C ("h1int", { 0, 1, 2, 3, 4, 5, 6, 7, 8 }), R ("h1lvl", 0.2f, 0.8f) }, false },
            };
            return g;
        }

        //----------------------------------------------------------------------
        // Kulcsszavas módosítók (magyar és angol szavak)
        struct Mod { std::vector<const char*> words; void (*fn) (ParamMap&, Rng&); };

        static float clampTo (float v, float lo, float hi) { return std::max (lo, std::min (hi, v)); }

        static void set (ParamMap& m, const char* id, float v) { m[id] = v; }
        static void mulv (ParamMap& m, const char* id, float def, float f, float lo, float hi) { m[id] = clampTo (getOr (m, id, def) * f, lo, hi); }
        static void addv (ParamMap& m, const char* id, float def, float a, float lo, float hi) { m[id] = clampTo (getOr (m, id, def) + a, lo, hi); }
        static void atLeast (ParamMap& m, const char* id, float def, float v) { m[id] = std::max (getOr (m, id, def), v); }
        static void atMost (ParamMap& m, const char* id, float def, float v) { m[id] = std::min (getOr (m, id, def), v); }

        static const std::vector<Mod>& mods()
        {
            static const std::vector<Mod> v = {
                { { "dark", "sotet", "sötét", "muffled", "dull" },               [] (ParamMap& m, Rng&) { mulv (m, "cut", 600, 0.5f, 60, 16000); mulv (m, "tone", 20000, 0.5f, 200, 20000); } },
                { { "bright", "vilagos", "világos", "airy", "crisp" },           [] (ParamMap& m, Rng&) { mulv (m, "cut", 600, 2.2f, 60, 16000); addv (m, "res", 1.2f, 0.3f, 0.5f, 10); } },
                { { "punchy", "punch", "utos", "ütős", "snappy" },               [] (ParamMap& m, Rng&) { addv (m, "penv", 0, 4, -48, 48); mulv (m, "pdec", 80, 0.7f, 5, 1000); set (m, "atk", 1); addv (m, "tatk", 0, 0.4f, -1, 1); } },
                { { "tight", "szuk", "szűk", "focused" },                         [] (ParamMap& m, Rng&) { mulv (m, "rel", 120, 0.6f, 5, 3000); set (m, "uni", 1); set (m, "width", 1); } },
                { { "soft", "smooth", "lagy", "lágy", "gentle", "mellow" },      [] (ParamMap& m, Rng&) { mulv (m, "drive", 0.25f, 0.3f, 0, 1); atLeast (m, "atk", 3, 8); mulv (m, "cut", 600, 0.7f, 60, 16000); set (m, "dtype", 0); } },
                { { "distorted", "dirty", "torz", "torzitott", "torzított", "piszkos", "gritty", "aggressive", "agresszív", "agressziv", "harsh" },
                                                                                  [] (ParamMap& m, Rng& r) { addv (m, "drive", 0.25f, 0.3f, 0, 1); addv (m, "subdrive", 0, 0.2f, 0, 1);
                                                                                                           static const float t[3] = { 1, 4, 6 }; set (m, "dtype", t[(int) (r.u() * 3) % 3]); } },
                { { "clean", "tiszta", "pure" },                                  [] (ParamMap& m, Rng&) { atMost (m, "drive", 0.25f, 0.05f); set (m, "subdrive", 0); set (m, "dtype", 0); set (m, "bits", 16); set (m, "crate", 1); } },
                { { "wide", "szeles", "széles", "stereo", "huge" },              [] (ParamMap& m, Rng&) { atLeast (m, "uni", 1, 5); atLeast (m, "unid", 15, 18); set (m, "unis", 0.9f); set (m, "width", 1.4f); atLeast (m, "chmix", 0, 0.25f); } },
                { { "mono", "narrow" },                                           [] (ParamMap& m, Rng&) { set (m, "uni", 1); set (m, "width", 1); set (m, "chmix", 0); } },
                { { "long", "hosszu", "hosszú", "sustained", "sustain" },        [] (ParamMap& m, Rng&) { mulv (m, "dec", 300, 2.0f, 5, 3000); mulv (m, "rel", 120, 2.0f, 5, 3000); atLeast (m, "sus", 0.5f, 0.4f); set (m, "nlen", 0); } },
                { { "short", "rovid", "rövid", "staccato" },                      [] (ParamMap& m, Rng&) { mulv (m, "dec", 300, 0.5f, 5, 3000); mulv (m, "rel", 120, 0.5f, 5, 3000); set (m, "sus", 0); set (m, "nlen", 300); } },
                { { "glide", "slide", "csuszo", "csúszó", "portamento", "csuszas", "csúszás" },
                                                                                  [] (ParamMap& m, Rng&) { atLeast (m, "glide", 30, 140); set (m, "gleg", 0); } },
                { { "wobble", "wub", "wobbly" },                                  [] (ParamMap& m, Rng&) { atLeast (m, "lcut", 0, 2.5f); set (m, "lsync", 4); set (m, "lshape", 0); } },
                { { "growl", "growling", "morgo", "morgó", "snarl" },            [] (ParamMap& m, Rng&) { atLeast (m, "fm", 0, 0.4f); set (m, "dtype", 2); atLeast (m, "res", 1.2f, 3); } },
                { { "analog", "vintage", "warm", "meleg", "retro", "tape" },     [] (ParamMap& m, Rng&) { atLeast (m, "drift", 0, 0.5f); atLeast (m, "hiss", 0, 0.15f); set (m, "phrand", 1); set (m, "dtype", 5); } },
                { { "lofi", "lo-fi", "dusty", "poros" },                          [] (ParamMap& m, Rng&) { atMost (m, "bits", 16, 12); atLeast (m, "crate", 1, 2); atLeast (m, "hiss", 0, 0.4f); atLeast (m, "drift", 0, 0.4f); mulv (m, "cut", 600, 0.7f, 60, 16000); } },
                { { "crushed", "bitcrushed", "digital", "bitcrush" },            [] (ParamMap& m, Rng&) { atMost (m, "bits", 16, 10); atLeast (m, "crate", 1, 3); } },
                { { "acid", "savas", "savas" },                                   [] (ParamMap& m, Rng&) { atLeast (m, "res", 1.2f, 6); atLeast (m, "fenv", 0, 3); set (m, "fdec", 180); atLeast (m, "glide", 30, 50); set (m, "gleg", 0); } },
                { { "pluck", "plucky", "pengetos", "pengetős" },                 [] (ParamMap& m, Rng&) { set (m, "sus", 0); atMost (m, "dec", 300, 200); atLeast (m, "fenv", 0, 3); set (m, "fdec", 120); atMost (m, "rel", 120, 100); } },
                { { "sub", "deep", "mely", "mély", "heavy", "nehez", "nehéz" }, [] (ParamMap& m, Rng&) { atLeast (m, "sublvl", 0.5f, 0.9f); mulv (m, "o1lvl", 0.7f, 0.5f, 0, 1); mulv (m, "cut", 600, 0.8f, 60, 16000); } },
                { { "fat", "thick", "vastag", "massive", "big" },                [] (ParamMap& m, Rng&) { atLeast (m, "o2lvl", 0, 0.6f); atLeast (m, "uni", 1, 3); atLeast (m, "unid", 15, 12); set (m, "boost", 3); atLeast (m, "sublvl", 0.5f, 0.6f); } },
                { { "pump", "pumping", "pumpa", "ducking" },                      [] (ParamMap& m, Rng&) { atLeast (m, "pump", 0, 0.5f); set (m, "pumprate", 2); } },
                { { "808" },                                                      [] (ParamMap& m, Rng&) { atLeast (m, "penv", 0, 6); atMost (m, "pdec", 80, 100); atLeast (m, "dec", 300, 800); set (m, "sus", 0); atLeast (m, "sublvl", 0.5f, 0.9f); } },
                { { "evolving", "moving", "mozgo", "mozgó", "movement" },        [] (ParamMap& m, Rng&) { atLeast (m, "lcut", 0, 1.2f); set (m, "lsync", 0); set (m, "lrate", 0.3f); } },
                { { "dry", "szaraz", "száraz" },                                  [] (ParamMap& m, Rng&) { set (m, "chmix", 0); set (m, "hiss", 0); } },
                { { "saw", "fureszfog", "fűrészfog" },                            [] (ParamMap& m, Rng&) { set (m, "o1wave", 0); atLeast (m, "o1lvl", 0.7f, 0.6f); } },
                { { "square", "pulse", "negyszog", "négyszög" },                  [] (ParamMap& m, Rng&) { set (m, "o1wave", 1); atLeast (m, "o1lvl", 0.7f, 0.6f); } },
                { { "sine", "szinusz" },                                          [] (ParamMap& m, Rng&) { set (m, "o1wave", 3); } },
                { { "triangle", "tri", "haromszog", "háromszög" },               [] (ParamMap& m, Rng&) { set (m, "o1wave", 2); } },
            };
            return v;
        }

        static std::string normalise (const std::string& in)
        {
            std::string s;
            for (unsigned char c : in) s.push_back ((char) ((c < 128) ? std::tolower (c) : c));
            // "lo-fi" egy szóként
            for (size_t p; (p = s.find ("lo-fi")) != std::string::npos;) s.replace (p, 5, "lofi");
            return s;
        }

        static void applyDescriptors (ParamMap& m, const std::string& text, Rng& rng)
        {
            const std::string s = normalise (text);
            std::vector<std::string> tokens;
            std::string cur;
            for (unsigned char c : s)
            {
                if (std::isalnum (c) || c >= 128 || c == '-') cur.push_back ((char) c);
                else { if (! cur.empty()) tokens.push_back (cur); cur.clear(); }
            }
            if (! cur.empty()) tokens.push_back (cur);

            for (auto& tok : tokens)
                for (auto& mod : mods())
                {
                    bool hit = false;
                    for (auto* w : mod.words) if (tok == w) { hit = true; break; }
                    if (hit) { mod.fn (m, rng); break; }
                }
        }

        // Hallható és biztonságos tartományban tartás
        static void sanitise (ParamMap& m)
        {
            const float sub = getOr (m, "sublvl", 0.5f), o1 = getOr (m, "o1lvl", 0.7f);
            if (sub < 0.15f && o1 < 0.15f) m["sublvl"] = 0.8f;
            auto cl = [&] (const char* id, float lo, float hi) { auto it = m.find (id); if (it != m.end()) it->second = clampTo (it->second, lo, hi); };
            cl ("drive", 0, 1); cl ("subdrive", 0, 1); cl ("res", 0.5f, 10); cl ("cut", 20, 16000);
            cl ("dec", 5, 3000); cl ("rel", 5, 3000); cl ("atk", 1, 2000); cl ("sus", 0, 1);
            cl ("uni", 1, 7); cl ("unid", 0, 60); cl ("unis", 0, 1); cl ("width", 0, 2); cl ("chmix", 0, 1);
            cl ("penv", -48, 48); cl ("pdec", 5, 1000); cl ("glide", 0, 500); cl ("nlen", 0, 2000);
        }
    };
}
