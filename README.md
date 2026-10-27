# Audio2Bass – VST3 effekt FL Studiohoz

A bejövő hangból (mp3/wav/ogg – bármi, ami a mixer insertbe érkezik) valós időben
hangmagasságot detektál (YIN), és abból basszus-szintit játszik: oszcillátor + sub + szűrő + torzítás.

## Fordítás (Windows VST3)

**A) GitHub Actions (nem kell semmit telepíteni)**
1. Hozz létre egy GitHub repót, töltsd fel ennek a mappának a tartalmát (a `.github` mappával együtt).
2. Actions fül → "Build VST3 (Windows)" → futás végén töltsd le az `Audio2Bass-VST3` artifactot.

**B) Helyben:** Visual Studio 2022 (C++ workload) + CMake, majd
```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
Eredmény: `build/Audio2Bass_artefacts/Release/VST3/Audio2Bass.vst3`

## Telepítés FL Studióban
1. Másold a `Audio2Bass.vst3` fájlt (mappát) ide: `C:\Program Files\Common Files\VST3\`
2. FL Studio → Options → Manage plugins → **Find installed plugins** (Verify plugins).
3. Importáld a hangfájlt (Channel rack/Playlist audio clip), irányítsd egy mixer insertre,
   és az insert effekt slotjába töltsd be az **Audio2Bass**-t.

## Paraméterek (v2: ~75 paraméter, 5 fül + 10 gyári preset)
- **Tracking:** oktáv-tartomány, oktáv-hajtogatás, oktáv-eltolás, transzpozíció, finomhangolás, glide, skála-kvantálás (gyökér + 7 skála), kapu küszöb és tartás. Követőmotor: min/max hangmagasság, YIN küszöb, minimális konfidencia, oktávhiba-ellenőrzés, követési sebesség (Fast/Normal/Smooth), hang-hiszterézis, hang-megerősítés (1–4 keret), hangon belüli simítás.
- **Oszcillátorok:** 2 oszcillátor (Saw/Pulse/Triangle/Sine/Half-Sine; szint, oktáv, félhang, finomhangolás, pulzusszélesség), FM (O2 → O1), unison (1–7 hang, detune, sztereó szétnyitás), sub (hullámforma, oktáv), zaj.
- **Szűrő / burkológörbék:** LP/BP/HP, 12/24 dB, cutoff, rezonancia, billentyű-követés, szűrő-burkoló (mélység, decay, sustain), bemeneti dinamika → cutoff, ADSR, pitch-burkoló (808-kick-szerű ejtés), key sync, újraindítás hangmagasság-váltásra vagy ütésre.
- **LFO:** 5 hullámforma, szabad vagy tempó-szinkron (1/1–1/16), célpontok: hangmagasság, cutoff, hangerő, pulzusszélesség.
- **Torzítás:** Soft/Hard/Fold/Asym, szűrő előtt vagy után, bitcrusher, mintavétel-csökkentés, tone.
- **FX / kimenet:** chorus, sztereó szélesség, basszus-emelés, paraméteres EQ sáv, low cut, mix, kimeneti szint, soft limiter.

## 808 / alacsony hangtartomány (v3)
- **808 Main fül:** a legfontosabb vezérlők egy helyen (sub, mid szint, pitch drop, glide, drive, szűrő, burkoló, kimenet).
- **Layer Split + Crossover:** a mid réteg a crossover alatt le van vágva, a sub réteg párhuzamosan, a mid szűrő és torzítás megkerülésével szól. Így a sub tiszta marad, és a torzítás nem dolgozik a sub tartományban.
- **Sub Drive:** a sub rétegre külön telítés, a törzs megmarad, harmonikusok kerülnek hozzá.
- **Mono Low End:** a beállított frekvencia alatt a kimenet mono (LR4 crossover), felette a sztereó kép megmarad.
- **Note Length:** automatikus jegyhossz (0 = ki). **Glide Legato Only:** a glide csak átfedő hangoknál működik, új hangnál ugrik. **Retrig on Pitch** ki = legato mód.
- **DC-szűrő** a jelút végén, **soft limiter** a kimeneten.
- **Presetek:** Clean Sub, Deep 808, Punchy 808, Long Glide 808, Short Kick 808, Distorted Trap 808, Saturated 808, Dark Reese Bass, Smooth Sub, Hard Clip 808, Wide Mid Bass, plusz a korábbi presetek (Reese, Wobble, Acid 303, FM Growl stb.).

## Presetek, A/B, 808 Character (v4)
- **Preset-böngésző:** a felső sávban a preset nevére kattintva kategóriás menü nyílik (808, Sub, Reese, Wobble-FM, Mid-Wide, Pluck-Acid, User). A `<` és `>` gombokkal lépkedhetsz. A felhasználói presetek mellett csillag (★) van.
- **Save:** a "Save" gombbal (vagy a menüből "Save as...") névvel és kategóriával mentheted az aktuális beállítást. Fájl: `%APPDATA%\Audio2Bass\Presets\<Kategória>\<Név>.a2bpreset`. Törlés és mappamegnyitás a preset-menü alján. Azonos névvel mentve felülírja a régit.
- **A/B:** az A és B gomb két külön beállítást tart; az "A > B" a jelenlegit átmásolja a másik tárolóba. Az A/B tároló nem mentődik a projektbe.
- **808 Character** (808 Main fül, első gomb): egy makró, ami együtt növeli a telítést, a sub drive-ot, a mid felharmonikusokat, a pitch drop mélységét és a szűrő nyitottságát. A többi paramétert nem írja át, 0-n nincs hatása.

## Új funkciók (v5)
- **Harmonizálás:** két párhuzamos szólam a felismert hang mellé (oktáv fel/le, kvint, kvart, kis és nagy terc), az 1. oszcillátor hullámformájával. Oszcillátorok fül, "Harmony / Analog".
- **Timbre-átvitel (vocoder):** a bemenet 16 sávos spektrális alakját (formánsok) viszi a basszusra. A hordozó sávszintjét normalizálja, így a forrás alakja kerül át, nem az abszolút hangereje. Hatásos dús hordozóval (saw, unison, torzítás). Timbre/Dyn fül.
- **Sidechain bemenet:** a plugin opcionális külső sidechain bemenetet kap. A "Sidechain Amount", küszöb, attack és release állítja a ducking-ot. **Pump:** külső jel nélkül is működik, a host tempójára szinkronizált ritmikus ducking (1/1–1/8). FL Studióban a sidechain bemenet beállítása a host dolga: a mixer sávjának "sidechain to this track" csatlakoztatása, majd a plugin wrapper beállításai között kell kiválasztani a bemenetet (FL-ben még nem próbáltam ki).
- **Analóg hangulat:** oszcillátor-drift (lassú, véletlen hangolás-ingadozás, legfeljebb kb. 12 cent), véletlen fázis hangindításkor, enyhe szűrt zaj (hiss).
- **Új torzítások:** Tube (cső), Tape (szalag), Diode (dióda). **Többsávos telítés:** 3 sáv, külön drive. **OTT-szerű kompresszió:** felfelé és lefelé kompresszió sávonként. Ez egy saját megvalósítás, nem a Xfer OTT másolata, a hangja ezért eltérhet.
- **Átmenet-formázó:** a támadás és a hangtest külön erősíthető vagy csökkenthető.
- **True-peak limiter:** 4× túlmintavételezett csúcsdetektálás, lookahead (késleltetés ~1,6 ms), állítható plafon és release. Alapból be van kapcsolva, és ilyenkor a korábbi soft clipper (tanh) nem működik. Kikapcsolva a "Soft Limiter" dolgozik tovább.
- **Véletlen preset** zárolható csoportokkal (oszcillátor, szűrő+burkoló, LFO, torzítás, FX, low end) és erősséggel, **A/B morfolás** csúszkával. Tools fül. A követés, kapu, kimenet, vocoder és sidechain beállításait a véletlenszerűsítés sosem változtatja.

## Közelebb az eredeti hanghoz (v6)
- **Pontos hangmagasság-követés alapból:** az "Octave Fold" és a "Quantize" alapból ki van kapcsolva, így minden hang az eredeti hangmagasságán és oktávján szól. Korábban ezek minden hangot egyetlen oktávba és skálába kényszerítettek, ezért tűnt minden billentyű ugyanolyan magasnak. A régi (808/basszus) presetek továbbra is bekapcsolják őket, az "Init" és az "Original" presetek nem.
- **Original Layer (Orig/Timbre fül, `srclvl`):** a bemenet SAJÁT jelét viszi a cél hangmagasságra (pitch-szinkron késleltetővonalas eltoló: a vágásokat a detektált periódus többszöröseire illeszti, így nincs hallható ugrás). A hangszín az eredeti marad, a szűrő, torzítás, vocoder és burkoló ezen is dolgozik. A hangmagasság-leképezést (Octave Shift, Transpose, Glide, pitch-burkoló, LFO) ugyanúgy követi, mint a szintetizátor, plusz "Original Transpose" a saját eltoláshoz. Az "Original Delay" (alapból 65 ms) a követő késleltetéséhez igazítja: ha a kétféle réteg nem fedi egymást, ezt kell állítani.
- **Original presetek:** Original Follow, Original Octave Down, Original Two Octaves Down, Original + Sub, Original Formant Saw, Original Dirty. Dinamikakövetés 100%.
- **Korlátok:** egyszólamú forrásnál működik jól; az eltolás a formánsokat is vele viszi (mint a magnószalag sebességének változtatása), ezért pl. az énekhang oktávval lejjebb sötétebb, "nagyobb" lesz.

## Audio2Bass Synth: hanggenerátor (instrument) változat
Ugyanabból a forráskódból készül egy második VST3, az **Audio2BassSynth**: MIDI-vel játszható, monofonikus basszusszintetizátor. A teljes hangmotor ugyanaz, mint az effektben (két oszcillátor, unison, FM, sub, harmónia, szűrő, burkolók, LFO, torzítások, többsáv/OTT, vocoder, átmenet-formázó, true-peak limiter, presetek, A/B, morfolás, 808 Character).
- **Mi változik:** a hangmagasságot, a hangot és a hosszt a MIDI adja (hangmagasság-követés helyett), a "Tracking" és az "Original Layer" nincs benne. A "Keys / MIDI" fülön: legato/retrigger, glide, jegyhossz, pitch bend tartomány, skálára zárás, oktáv- és félhang-eltolás, valamint velocity hatása a hangerőre, a szűrőre és a pitch drop-ra.
- **Hangkezelés:** monofonikus, "utoljára játszott hang" prioritással. Felengedéskor, ha egy másik billentyű még le van nyomva, arra tér vissza (legato). Pitch bend, All Notes Off (CC123), sample-pontos MIDI-időzítés.
- **Opcionális külső bemenet:** a vocoder modulátora és a sidechain ducking kulcsa. Alapból le van tiltva, a host adja az engedélyezést; engedélyezés nélkül a bemenetet nem olvassa, így nem szivárog át semmi.
- **Telepítés:** `Audio2BassSynth.vst3`. FL Studióban a Find installed plugins után a generátorok között jelenik meg.

## AI presetkészítő ("AI Preset" fül)
Műfaj-tudatos generátor, de **nem neurális háló és nem használ internetet**: műfaj-profilokból (16 műfaj: Trap 808, Drill, Hip-Hop, Dubstep, Reese/DnB, House, Techno, Acid, Synthwave, Funk, Lo-fi, Phonk, Pop/EDM, Ambient, Amapiano log drum, Wild) mintavételez egy seedelt véletlenszám-generátorral.
- **Leírás:** szabad szöveg angolul vagy magyarul ("dark, distorted, long glide, wide" / "sötét, torzított, hosszú"). Ismert szavak: dark/sötét, bright/világos, punchy/ütős, soft/lágy, distorted/torzított/dirty, clean/tiszta, wide/széles, mono, long/hosszú, short/rövid, glide/csúszó, wobble, growl, analog/vintage/warm/meleg, lofi, acid, pluck, sub/deep/mély, fat, pump, 808, saw/sine/square/triangle és továbbiak.
- **Variáció:** 0 = a műfaj tipikus hangja, 1 = a legszélesebb szórás. **Seed:** ugyanaz a (műfaj, leírás, variáció, seed) mindig ugyanazt a presetet adja; a "Fixed seed" kapcsolóval reprodukálható. **Zárolás:** a zárolt csoportok (oszcillátor, szűrő+burkoló, LFO, torzítás, FX, low end) nem változnak. **Előzmények:** a "Back" és "Forward" gombbal visszaléphetsz a korábban generált presetek között.
- A generált hangot elmentheted a "Save" gombbal saját presetként.

## Késleltetés (mért, 44,1 kHz, alapbeállítás)
- Hangváltás → új hang felismerése: **~61 ms** (átlag), csendből induló hang: ~62 ms.
- A fő tényező a **Min Pitch**: 55 Hz-en ~49 ms, 40 Hz-en ~61 ms, 30 Hz-en ~75 ms. A **Note Confirm = 1** további ~12 ms-ot spórol, de több téves hangváltást enged át.
- A plugin ezt nem jelzi a hostnak (PDC), a dry jel nincs késleltetve.

## Tesztek
- `tests/tracker_test.cpp`: önálló, JUCE nélküli: ismert szinuszok (32–880 Hz), 4 mintavételi frekvencia, gyenge alapharmonikus, zaj, torzítás, vibrato, gyors hangváltás, csend, halk jel, CPU. Futtatás: `g++ -O2 -std=c++17 -ISource tests/tracker_test.cpp -o t && ./t`
- `tests/fx_test.cpp`: önálló, JUCE nélküli: vocoder, többsáv/OTT, átmenet-formázó, true-peak limiter (beleértve a mintaközi csúcsokat).
- `tests/gen_test.cpp`: önálló, JUCE nélküli: az AI generátor determinizmusa, műfaj-karakterek, kulcsszavak (magyarul is), értéktartományok.
- `tests/synth_test.cpp`: a hanggenerátor változat: MIDI-hangmagasság, minden billentyű, felengedés, velocity, legato, pitch bend, sample-pontos időzítés, bemenet-szivárgás, sidechain, minden preset és műfaj.
- `tests/processor_test.cpp`: a teljes processzor: blokkméretek (1–4096), mono/sztereó, mintavételi frekvenciák, automatizálás, 40 véletlen paraméterkészlet, minden preset betöltése és mentése/visszaállítása, CPU. A GitHub Actions "Tests (Windows)" workflow futtatja (`-DA2B_BUILD_TESTS=ON`).

## Korlátok
- Egyszólamú (monofonikus) hangmagasság-követés; akkordoknál és teljes keverésnél pontatlan.
- Effekt plugin: MIDI bemenet jelenleg nincs.
- A követés ~50–75 ms-ot késik; ez a pitch-detektálás alapvető ára (az ablaknak át kell fognia a legalacsonyabb követett hang periódusát).
