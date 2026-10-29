#include "PluginEditor.h"

namespace
{
    constexpr int kCellW = 96;
    constexpr int kCellH = 92;
    constexpr int kHeaderH = 26;
    const juce::Colour kAccent (0xffe8a33d);
}

//==============================================================================
ParamControl::ParamControl (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID)
{
    auto* param = apvts.getParameter (paramID);
    jassert (param != nullptr);

    label.setText (param != nullptr ? param->getName (24) : paramID, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (label);

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param))
    {
        kind = 1;
        combo.addItemList (choice->choices, 1);
        addAndMakeVisible (combo);
        comboAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, paramID, combo);
    }
    else if (dynamic_cast<juce::AudioParameterBool*> (param) != nullptr)
    {
        kind = 2;
        toggle.setButtonText ("On");
        addAndMakeVisible (toggle);
        buttonAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, paramID, toggle);
    }
    else
    {
        kind = 0;
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
        slider.setColour (juce::Slider::rotarySliderFillColourId, kAccent);
        addAndMakeVisible (slider);
        sliderAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramID, slider);
    }
}

void ParamControl::resized()
{
    auto r = getLocalBounds().reduced (2);
    label.setBounds (r.removeFromTop (16));

    if (kind == 0)      slider.setBounds (r);
    else if (kind == 1) combo.setBounds (r.removeFromTop (r.getHeight() / 2 + 8).reduced (0, 14));
    else                toggle.setBounds (r.withSizeKeepingCentre (r.getWidth() - 10, 28));
}

//==============================================================================
ParamPage::ParamPage (juce::AudioProcessorValueTreeState& apvts, const juce::StringArray& items)
{
    for (auto& item : items)
    {
        if (item.startsWithChar ('#'))
        {
            auto* h = headers.add (new juce::Label ({}, item.substring (1)));
            h->setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
            h->setColour (juce::Label::textColourId, kAccent);
            addAndMakeVisible (h);
            entries.push_back ({ h, true });
        }
        else
        {
            auto* c = controls.add (new ParamControl (apvts, item));
            addAndMakeVisible (c);
            entries.push_back ({ c, false });
        }
    }
}

void ParamPage::resized()
{
    const int margin = 10;
    int x = margin, y = 6;

    for (auto& e : entries)
    {
        if (e.header)
        {
            if (x > margin) { x = margin; y += kCellH; }
            e.comp->setBounds (margin, y, getWidth() - 2 * margin, kHeaderH);
            y += kHeaderH;
            x = margin;
        }
        else
        {
            if (x + kCellW > getWidth() - margin) { x = margin; y += kCellH; }
            e.comp->setBounds (x, y, kCellW, kCellH);
            x += kCellW;
        }
    }
}

//==============================================================================
InfoPanel::InfoPanel (Audio2BassProcessor& p) : proc (p)
{
    startTimerHz (30);
}

juce::String InfoPanel::noteName (float f)
{
    if (f <= 0.0f) return "--";
    static const char* names[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    const int midi = (int) std::lround (69.0f + 12.0f * std::log2 (f / 440.0f));
    return juce::String (names[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
}

void InfoPanel::timerCallback()
{
    const auto& i = proc.info;
    voiced = i.voiced.load();
    hz     = i.hz.load();
    conf   = i.confidence.load();

    // csúcstartás lassú visszaesessel
    inDb  = juce::jmax (i.inDb.load(),  inDb  - 1.5f);
    outDb = juce::jmax (i.outDb.load(), outDb - 1.5f);
    repaint();
}

void InfoPanel::drawMeter (juce::Graphics& g, juce::Rectangle<float> r, float db, const juce::String& label)
{
    g.setColour (juce::Colour (0xff11141a));
    g.fillRoundedRectangle (r, 3.0f);

    const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    auto fill = r.withWidth (r.getWidth() * frac);
    g.setColour (db > -1.0f ? juce::Colour (0xffe5484d) : db > -9.0f ? juce::Colour (0xffe8a33d) : juce::Colour (0xff46c28e));
    g.fillRoundedRectangle (fill, 3.0f);

    g.setColour (juce::Colours::white.withAlpha (0.75f));
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText (label, r.withTrimmedLeft (4.0f), juce::Justification::centredLeft);
}

void InfoPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (8.0f, 2.0f);
    g.setColour (juce::Colour (0xff252b36));
    g.fillRoundedRectangle (r, 6.0f);

    auto inner = r.reduced (10.0f, 6.0f);

    // állapot
    auto status = inner.removeFromLeft (130.0f);
    g.setColour (voiced ? juce::Colour (0xff46c28e) : juce::Colour (0xff6b7280));
    g.fillEllipse (status.getX(), status.getCentreY() - 5.0f, 10.0f, 10.0f);
    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    g.drawText (voiced ? (A2B_SYNTH ? "PLAYING" : "TRACKING") : (A2B_SYNTH ? "IDLE" : "NO PITCH"), status.withTrimmedLeft (18.0f), juce::Justification::centredLeft);

    // hang + frekvencia
    auto noteArea = inner.removeFromLeft (190.0f);
    g.setColour (juce::Colour (0xffe8a33d));
    g.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    g.drawText (voiced ? noteName (hz) : "--", noteArea.removeFromLeft (70.0f), juce::Justification::centredLeft);
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.setFont (juce::Font (juce::FontOptions (14.0f)));
    g.drawText (voiced ? juce::String (hz, 1) + " Hz" : "", noteArea, juce::Justification::centredLeft);

    // konfidencia
    auto confArea = inner.removeFromLeft (150.0f);
    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText (A2B_SYNTH ? "Velocity" : "Confidence", confArea.removeFromTop (14.0f), juce::Justification::centredLeft);
    auto bar = confArea.removeFromTop (12.0f).withTrimmedRight (10.0f);
    g.setColour (juce::Colour (0xff11141a));
    g.fillRoundedRectangle (bar, 3.0f);
    g.setColour (conf > 0.85f ? juce::Colour (0xff46c28e) : conf > 0.6f ? juce::Colour (0xffe8a33d) : juce::Colour (0xffe5484d));
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, conf)), 3.0f);

    // szintmérők
    auto meters = inner.reduced (0.0f, 2.0f);
    drawMeter (g, meters.removeFromTop (meters.getHeight() / 2.0f - 2.0f), inDb,  "IN");
    meters.removeFromTop (4.0f);
    drawMeter (g, meters, outDb, "OUT");
}

//==============================================================================
ToolsPage::ToolsPage (Audio2BassProcessor& p) : proc (p)
{
    auto header = [this] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
        l.setColour (juce::Label::textColourId, kAccent);
        addAndMakeVisible (l);
    };
    auto small = [this] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::Font (juce::FontOptions (12.0f)));
        l.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.75f));
        addAndMakeVisible (l);
    };

    header (morphTitle, "Morph A <-> B");
    small  (morphHint, "Store two settings with the A/B buttons (A > B copies), then slide between them. Choice/switch parameters change at the midpoint.");
    morph.setSliderStyle (juce::Slider::LinearHorizontal);
    morph.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    morph.setRange (0.0, 1.0, 0.001);
    morph.setScrollWheelEnabled (false);
    morph.setColour (juce::Slider::thumbColourId, kAccent);
    morph.onDragStart = [this] { proc.beginMorph(); };
    morph.onValueChange = [this] { proc.setMorph ((float) morph.getValue()); };
    addAndMakeVisible (morph);

    header (randTitle, "Random preset");
    small  (amountLabel, "Amount (low = small variation of the current sound, high = completely new)");
    amount.setSliderStyle (juce::Slider::LinearHorizontal);
    amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    amount.setRange (0.0, 1.0, 0.01);
    amount.setValue (0.7);
    amount.setColour (juce::Slider::thumbColourId, kAccent);
    addAndMakeVisible (amount);

    small (lockLabel, "Lock groups (locked groups are left unchanged; tracking, gate, output and the vocoder/sidechain are never randomised):");
    const char* names[6] = { "Lock Oscillators", "Lock Filter + Env", "Lock LFO", "Lock Distortion", "Lock FX", "Lock Low End" };
    for (int i = 0; i < 6; ++i)
    {
        locks[i].setButtonText (names[i]);
        addAndMakeVisible (locks[i]);
    }

    randomButton.onClick = [this]
    {
        std::array<bool, 6> locked {};
        for (size_t i = 0; i < 6; ++i) locked[i] = locks[i].getToggleState();
        proc.randomize (locked, (float) amount.getValue());
    };
    addAndMakeVisible (randomButton);
}

void ToolsPage::resized()
{
    auto r = getLocalBounds().reduced (16, 10);
    morphTitle.setBounds (r.removeFromTop (26));
    morphHint.setBounds (r.removeFromTop (20));
    morph.setBounds (r.removeFromTop (30).reduced (0, 2));
    r.removeFromTop (22);

    randTitle.setBounds (r.removeFromTop (26));
    amountLabel.setBounds (r.removeFromTop (20));
    amount.setBounds (r.removeFromTop (30).reduced (0, 2));
    r.removeFromTop (8);
    lockLabel.setBounds (r.removeFromTop (20));

    auto grid = r.removeFromTop (64);
    const int colW = grid.getWidth() / 3;
    for (int i = 0; i < 6; ++i)
        locks[i].setBounds (grid.getX() + (i % 3) * colW, grid.getY() + (i / 3) * 30, colW - 8, 28);

    r.removeFromTop (10);
    randomButton.setBounds (r.removeFromTop (36).removeFromLeft (200));
}

//==============================================================================
AiPage::AiPage (Audio2BassProcessor& p) : proc (p)
{
    auto header = [this] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
        l.setColour (juce::Label::textColourId, kAccent);
        addAndMakeVisible (l);
    };
    auto small = [this] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::Font (juce::FontOptions (12.0f)));
        l.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.75f));
        addAndMakeVisible (l);
    };

    header (title, "AI preset maker");
    small  (hint, "Genre-aware generator (rule-based, works offline, no neural network): it picks parameter values typical for the genre. "
                  "Describe the sound in your own words to steer it.");

    small (genreLabel, "Genre");
    genre.addItemList (Audio2BassProcessor::getGenreNames(), 1);
    genre.setSelectedItemIndex (0, juce::dontSendNotification);
    addAndMakeVisible (genre);

    small (descLabel, "Describe the sound (English or Hungarian): dark, bright, punchy, soft, distorted, clean, wide, mono, long, short, "
                      "glide, wobble, growl, analog, lofi, acid, pluck, sub, fat, pump, saw, sine, square...");
    desc.setTextToShowWhenEmpty ("e.g. dark, distorted, long glide, wide   /   sötét, torzított, hosszú", juce::Colours::grey);
    addAndMakeVisible (desc);

    small (varLabel, "Variation (0 = the genre's typical sound, 1 = widest spread)");
    variation.setSliderStyle (juce::Slider::LinearHorizontal);
    variation.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    variation.setRange (0.0, 1.0, 0.01);
    variation.setValue (0.7);
    variation.setColour (juce::Slider::thumbColourId, kAccent);
    addAndMakeVisible (variation);

    small (seedLabel, "Seed (the same genre, description, variation and seed always give the same preset)");
    seed.setInputRestrictions (10, "0123456789");
    addAndMakeVisible (seed);
    addAndMakeVisible (fixedSeed);

    small (lockLabel, "Lock groups (locked groups are left unchanged):");
    const char* names[6] = { "Oscillators", "Filter + Env", "LFO", "Distortion", "FX", "Low End" };
    for (int i = 0; i < 6; ++i) { locks[i].setButtonText (names[i]); addAndMakeVisible (locks[i]); }

    small (info, "");
    addAndMakeVisible (generate);
    addAndMakeVisible (back);
    addAndMakeVisible (forward);

    generate.onClick = [this]
    {
        uint32_t s;
        if (fixedSeed.getToggleState() && seed.getText().isNotEmpty())
            s = (uint32_t) seed.getText().getLargeIntValue();
        else
            s = (uint32_t) juce::Random::getSystemRandom().nextInt (1000000) + 1u;
        seed.setText (juce::String ((juce::int64) s), false);

        std::array<bool, 6> locked {};
        for (size_t i = 0; i < 6; ++i) locked[i] = locks[i].getToggleState();
        proc.generatePreset (genre.getSelectedItemIndex(), desc.getText(), (float) variation.getValue(), s, locked);
        info.setText ("Generated: " + genre.getText() + ", seed " + juce::String ((juce::int64) s)
                      + "  (" + juce::String (proc.getGenHistoryPos() + 1) + "/" + juce::String (proc.getGenHistoryCount()) + ")",
                      juce::dontSendNotification);
    };
    back.onClick = [this]
    {
        if (proc.genHistoryStep (-1))
            info.setText ("History " + juce::String (proc.getGenHistoryPos() + 1) + "/" + juce::String (proc.getGenHistoryCount()), juce::dontSendNotification);
    };
    forward.onClick = [this]
    {
        if (proc.genHistoryStep (+1))
            info.setText ("History " + juce::String (proc.getGenHistoryPos() + 1) + "/" + juce::String (proc.getGenHistoryCount()), juce::dontSendNotification);
    };
}

void AiPage::resized()
{
    auto r = getLocalBounds().reduced (16, 8);
    title.setBounds (r.removeFromTop (26));
    hint.setBounds (r.removeFromTop (34));

    auto row = r.removeFromTop (28);
    genreLabel.setBounds (row.removeFromLeft (60));
    genre.setBounds (row.removeFromLeft (260));
    r.removeFromTop (6);

    descLabel.setBounds (r.removeFromTop (34));
    desc.setBounds (r.removeFromTop (28));
    r.removeFromTop (6);

    varLabel.setBounds (r.removeFromTop (20));
    variation.setBounds (r.removeFromTop (28));
    r.removeFromTop (4);

    seedLabel.setBounds (r.removeFromTop (20));
    auto seedRow = r.removeFromTop (28);
    seed.setBounds (seedRow.removeFromLeft (160));
    seedRow.removeFromLeft (12);
    fixedSeed.setBounds (seedRow.removeFromLeft (140));
    r.removeFromTop (6);

    lockLabel.setBounds (r.removeFromTop (20));
    auto grid = r.removeFromTop (60);
    const int colW = grid.getWidth() / 3;
    for (int i = 0; i < 6; ++i)
        locks[i].setBounds (grid.getX() + (i % 3) * colW, grid.getY() + (i / 3) * 28, colW - 8, 26);
    r.removeFromTop (8);

    auto btns = r.removeFromTop (38);
    generate.setBounds (btns.removeFromLeft (200));
    btns.removeFromLeft (12);
    back.setBounds (btns.removeFromLeft (90));
    btns.removeFromLeft (6);
    forward.setBounds (btns.removeFromLeft (90));
    r.removeFromTop (6);
    info.setBounds (r.removeFromTop (22));
}

//==============================================================================
Audio2BassEditor::Audio2BassEditor (Audio2BassProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), infoPanel (p)
{
   #if A2B_SYNTH
    setSize (960, 610);
   #else
    setSize (830, 610);
   #endif

    title.setText (A2B_SYNTH ? "AUDIO2SYNTH" : "AUDIO2BASS", juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    title.setColour (juce::Label::textColourId, kAccent);
    addAndMakeVisible (title);

    for (auto* b : { &prevButton, &nextButton, &presetButton, &saveButton, &abA, &abB, &abCopy })
        addAndMakeVisible (*b);

    presetButton.setTooltip ("Preset browser");
    presetButton.onClick = [this] { showPresetMenu(); };
    prevButton.onClick   = [this] { stepPreset (-1); };
    nextButton.onClick   = [this] { stepPreset (+1); };
    saveButton.onClick   = [this] { showSaveDialog(); };

    abA.setClickingTogglesState (false);
    abB.setClickingTogglesState (false);
    abA.onClick = [this] { proc.abSelect (0); };
    abB.onClick = [this] { proc.abSelect (1); };
    abCopy.onClick = [this] { proc.abCopyToOther(); };
    abCopy.setTooltip ("Copy the current settings to the other slot");

    timerCallback();
    startTimerHz (8);
    addAndMakeVisible (infoPanel);

    auto& s = proc.apvts;
    const juce::Colour tabCol (0xff252b36);

    tabs.setTabBarDepth (30);
   #if A2B_SYNTH
    tabs.addTab ("Main", tabCol, new ParamPage (s, {
        "#Oscillators",
        "o1wave", "o1lvl", "o2wave", "o2lvl", "o2det", "fm", "uni", "unid", "unis",
        "#Filter / Envelope",
        "ftype", "cut", "res", "fenv", "fdec", "fsus", "atk", "dec", "sus", "rel",
        "#Voice / Output",
        "vmode", "voices", "glide", "chmix", "outgain" }), true);

    tabs.addTab ("Keys / MIDI", tabCol, new ParamPage (s, {
        "#Voices",
        "vmode", "voices", "glide", "gleg", "nlen", "keysync", "phrand",
        "#Controllers",
        "bend", "mwvib", "mwcut",
        "#Pitch Mapping",
        "shift", "trans", "fine", "quant", "root", "scale",
        "#Velocity",
        "velamp", "velcut", "velpenv" }), true);

    tabs.addTab ("Oscillators", tabCol, new ParamPage (s, {
        "#Oscillator 1",
        "o1wave", "o1lvl", "o1oct", "o1semi", "o1fine", "o1pw",
        "#Oscillator 2 / FM",
        "o2wave", "o2lvl", "o2oct", "o2semi", "o2det", "o2pw", "fm",
        "#Unison / Sub / Noise",
        "uni", "unid", "unis", "sublvl", "subwave", "suboct", "noise",
        "#Harmony / Analog",
        "h1int", "h1lvl", "h2int", "h2lvl", "drift", "hiss" }), true);

    tabs.addTab ("Filter / Env", tabCol, new ParamPage (s, {
        "#Filter",
        "ftype", "f24", "cut", "res", "fkey",
        "#Filter & Pitch Envelope",
        "fenv", "fdec", "fsus", "penv", "pdec",
        "#Amp Envelope",
        "atk", "dec", "sus", "rel" }), true);

    tabs.addTab ("LFO / Dist", tabCol, new ParamPage (s, {
        "#LFO",
        "lshape", "lsync", "lrate", "lpitch", "lcut", "lamp", "lpw",
        "#Distortion / Bitcrush",
        "dpos", "dtype", "drive", "bits", "crate", "tone",
        "#Multiband Saturation / OTT",
        "mbon", "mbx1", "mbx2", "mbd1", "mbd2", "mbd3", "ott", "ottrel" }), true);

    tabs.addTab ("FX / Output", tabCol, new ParamPage (s, {
        "#Chorus / Stereo",
        "chmix", "chrate", "chdepth", "width",
        "#EQ",
        "boost", "eqf", "eqg", "eqq", "hp",
        "#Output",
        "outgain", "limit", "tplim", "ceil", "lrel" }), true);

    tabs.addTab ("Bass", tabCol, new ParamPage (s, {
        "#808 / Sub",
        "char", "sublvl", "subwave", "suboct", "subdrive", "penv", "pdec",
        "#Low End",
        "xon", "xover", "monosub", "mfreq" }), true);

    tabs.addTab ("Timbre/Dyn", tabCol, new ParamPage (s, {
        "#Vocoder (modulator = the optional external input)",
        "vocmix", "vocq", "vocrel", "vocgain",
        "#Sidechain (key = the optional external input) / Pump",
        "scamt", "scthr", "scatk", "screl", "pump", "pumprate", "pumprel",
        "#Transient Shaper",
        "tatk", "tsus" }), true);

    tabs.addTab ("Tools", tabCol, new ToolsPage (proc), true);
    tabs.addTab ("AI Preset", tabCol, new AiPage (proc), true);

   #else
    tabs.addTab ("808 Main", tabCol, new ParamPage (s, {
        "#808 Core",
        "char", "srclvl", "sublvl", "subwave", "suboct", "o1lvl", "penv", "pdec", "glide", "gleg",
        "drive", "subdrive", "cut", "res", "outgain",
        "#Amp Envelope",
        "atk", "dec", "sus", "rel", "nlen",
        "#Low End",
        "xon", "xover", "monosub", "mfreq" }), true);

    tabs.addTab ("Tracking", tabCol, new ParamPage (s, {
        "#Pitch Mapping",
        "oct", "fold", "shift", "trans", "fine", "glide",
        "#Tracking Engine / Note Stability",
        "pmin", "pmax", "ytr", "conf", "ocheck", "tspeed", "hyst", "stab", "psmooth",
        "#Scale Quantizer / Gate",
        "quant", "root", "scale", "thresh", "hold" }), true);

    tabs.addTab ("Oscillators", tabCol, new ParamPage (s, {
        "#Oscillator 1",
        "o1wave", "o1lvl", "o1oct", "o1semi", "o1fine", "o1pw",
        "#Oscillator 2 / FM",
        "o2wave", "o2lvl", "o2oct", "o2semi", "o2det", "o2pw", "fm",
        "#Unison / Sub / Noise",
        "uni", "unid", "unis", "sublvl", "subwave", "suboct", "noise",
        "#Harmony / Analog",
        "h1int", "h1lvl", "h2int", "h2lvl", "drift", "phrand", "hiss" }), true);

    tabs.addTab ("Filter / Env", tabCol, new ParamPage (s, {
        "#Filter",
        "ftype", "f24", "cut", "res", "fkey", "fin",
        "#Filter & Pitch Envelope",
        "fenv", "fdec", "fsus", "penv", "pdec",
        "#Amp Envelope & Triggers",
        "atk", "dec", "sus", "rel", "follow", "keysync", "retrig", "onset" }), true);

    tabs.addTab ("LFO / Dist", tabCol, new ParamPage (s, {
        "#LFO",
        "lshape", "lsync", "lrate", "lpitch", "lcut", "lamp", "lpw",
        "#Distortion / Bitcrush",
        "dpos", "dtype", "drive", "bits", "crate", "tone",
        "#Multiband Saturation / OTT",
        "mbon", "mbx1", "mbx2", "mbd1", "mbd2", "mbd3", "ott", "ottrel" }), true);

    tabs.addTab ("FX / Output", tabCol, new ParamPage (s, {
        "#Chorus / Stereo",
        "chmix", "chrate", "chdepth", "width",
        "#EQ",
        "boost", "eqf", "eqg", "eqq", "hp",
        "#Output",
        "mix", "outgain", "limit", "tplim", "ceil", "lrel" }), true);

    tabs.addTab ("Orig/Timbre", tabCol, new ParamPage (s, {
        "#Original Layer (the input itself, pitch-shifted: keeps the source's own timbre)",
        "srclvl", "srcdly", "srcsemi",
        "#Vocoder (timbre transfer from the input)",
        "vocmix", "vocq", "vocrel", "vocgain",
        "#Sidechain / Pump",
        "scamt", "scthr", "scatk", "screl", "pump", "pumprate", "pumprel",
        "#Transient Shaper",
        "tatk", "tsus" }), true);

    tabs.addTab ("Tools", tabCol, new ToolsPage (proc), true);

    tabs.addTab ("AI Preset", tabCol, new AiPage (proc), true);
   #endif

    addAndMakeVisible (tabs);
}

void Audio2BassEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1f27));
}

void Audio2BassEditor::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (44).reduced (10, 6);
    title.setBounds (top.removeFromLeft (A2B_SYNTH ? 170 : 150));
    prevButton.setBounds (top.removeFromLeft (28));
    presetButton.setBounds (top.removeFromLeft (250).reduced (3, 0));
    nextButton.setBounds (top.removeFromLeft (28));
    saveButton.setBounds (top.removeFromLeft (56).reduced (3, 0));
    abCopy.setBounds (top.removeFromRight (56).reduced (3, 0));
    abB.setBounds (top.removeFromRight (34).reduced (3, 0));
    abA.setBounds (top.removeFromRight (34).reduced (3, 0));
    infoPanel.setBounds (r.removeFromTop (54));
    tabs.setBounds (r);
}

//==============================================================================
void Audio2BassEditor::timerCallback()
{
    const auto name = proc.getCurrentPresetName();
    if (presetButton.getButtonText() != name) presetButton.setButtonText (name);

    const bool isA = proc.getActiveSlot() == 0;
    abA.setColour (juce::TextButton::buttonColourId, isA  ? kAccent : juce::Colour (0xff3a4150));
    abB.setColour (juce::TextButton::buttonColourId, ! isA ? kAccent : juce::Colour (0xff3a4150));
    abA.setColour (juce::TextButton::textColourOffId, isA  ? juce::Colours::black : juce::Colours::white);
    abB.setColour (juce::TextButton::textColourOffId, ! isA ? juce::Colours::black : juce::Colours::white);
}

void Audio2BassEditor::stepPreset (int delta)
{
    auto list = proc.listPresets();
    if (list.empty()) return;

    const auto current = proc.getCurrentPresetName();
    int idx = 0;
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i].name == current) { idx = i; break; }

    idx = (idx + delta + (int) list.size()) % (int) list.size();
    proc.loadPreset (list[(size_t) idx]);
}

void Audio2BassEditor::showPresetMenu()
{
    menuEntries = proc.listPresets();
    const auto current = proc.getCurrentPresetName();

    juce::StringArray cats;
    for (auto& e : menuEntries) if (! cats.contains (e.category)) cats.add (e.category);

    juce::PopupMenu menu;
    for (auto& cat : cats)
    {
        juce::PopupMenu sub;
        bool sawUser = false;
        for (int i = 0; i < (int) menuEntries.size(); ++i)
        {
            auto& e = menuEntries[(size_t) i];
            if (e.category != cat || e.user) continue;
            sub.addItem (1000 + i, e.name, true, e.name == current);
        }
        for (int i = 0; i < (int) menuEntries.size(); ++i)
        {
            auto& e = menuEntries[(size_t) i];
            if (e.category != cat || ! e.user) continue;
            if (! sawUser) { sub.addSeparator(); sawUser = true; }
            sub.addItem (1000 + i, juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x85 ")) + e.name, true, e.name == current);
        }
        menu.addSubMenu (cat, sub);
    }

    bool canDelete = false;
    for (auto& e : menuEntries) if (e.user && e.name == current) canDelete = true;

    menu.addSeparator();
    menu.addItem (1, "Save as...");
    menu.addItem (2, "Delete user preset '" + current + "'", canDelete);
    menu.addItem (3, "Open preset folder");

    juce::Component::SafePointer<Audio2BassEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton),
                        [safe] (int result)
    {
        if (safe == nullptr || result == 0) return;
        if (result >= 1000)
        {
            const size_t i = (size_t) (result - 1000);
            if (i < safe->menuEntries.size()) safe->proc.loadPreset (safe->menuEntries[i]);
        }
        else if (result == 1) safe->showSaveDialog();
        else if (result == 2) safe->confirmDelete();
        else if (result == 3)
        {
            auto dir = safe->proc.getUserPresetDir();
            dir.createDirectory();
            dir.revealToUser();
        }
    });
}

void Audio2BassEditor::showSaveDialog()
{
    auto* w = new juce::AlertWindow ("Save preset", "Name and category of the new preset:", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", proc.getCurrentPresetName(), "Name");
    w->addComboBox ("cat", Audio2BassProcessor::getCategories(), "Category");
    if (auto* cb = w->getComboBoxComponent ("cat"))
        cb->setSelectedItemIndex (Audio2BassProcessor::getCategories().size() - 1);
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<Audio2BassEditor> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
    {
        if (safe == nullptr || result != 1) return;
        const auto name = w->getTextEditorContents ("name").trim();
        const int idx = w->getComboBoxComponent ("cat") != nullptr ? w->getComboBoxComponent ("cat")->getSelectedItemIndex() : 0;
        const auto cat = Audio2BassProcessor::getCategories()[juce::jmax (0, idx)];
        if (name.isNotEmpty()) safe->proc.saveUserPreset (name, cat);
    }), true);
}

void Audio2BassEditor::confirmDelete()
{
    const auto name = proc.getCurrentPresetName();
    juce::String cat;
    for (auto& e : proc.listPresets()) if (e.user && e.name == name) cat = e.category;
    if (cat.isEmpty()) return;

    juce::Component::SafePointer<Audio2BassEditor> safe (this);
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Delete preset",
                                        "Delete the user preset '" + name + "'?", "Delete", "Cancel", this,
                                        juce::ModalCallbackFunction::create ([safe, name, cat] (int result)
    {
        if (safe != nullptr && result == 1) safe->proc.deleteUserPreset (name, cat);
    }));
}
