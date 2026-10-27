#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// Egy paraméterhez tartozó vezérlő (forgatógomb / legördülő / kapcsoló) címkével
class ParamControl : public juce::Component
{
public:
    ParamControl (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID);
    void resized() override;

private:
    int kind = 0; // 0 = slider, 1 = combo, 2 = toggle
    juce::Label label;
    juce::Slider slider;
    juce::ComboBox combo;
    juce::ToggleButton toggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   sliderAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   buttonAtt;
};

// Egy fül: fejlécek ("#Név") és paraméter-azonosítók sorban, automatikus tördeléssel
class ParamPage : public juce::Component
{
public:
    ParamPage (juce::AudioProcessorValueTreeState& apvts, const juce::StringArray& items);
    void resized() override;

private:
    struct Entry { juce::Component* comp; bool header; };
    std::vector<Entry> entries;
    juce::OwnedArray<ParamControl> controls;
    juce::OwnedArray<juce::Label>  headers;
};

// Eszközök fül: A/B morfolás, véletlenszerű preset zárolható csoportokkal
class ToolsPage : public juce::Component
{
public:
    explicit ToolsPage (Audio2BassProcessor& p);
    void resized() override;

private:
    Audio2BassProcessor& proc;
    juce::Label morphTitle, morphHint, randTitle, amountLabel, lockLabel;
    juce::Slider morph, amount;
    juce::TextButton randomButton { "Randomize" };
    juce::ToggleButton locks[6];
};

// AI presetkészítő fül: műfaj + szöveges leírás + variáció + seed -> a gombok beállítása
class AiPage : public juce::Component
{
public:
    explicit AiPage (Audio2BassProcessor& p);
    void resized() override;

private:
    Audio2BassProcessor& proc;
    juce::Label title, hint, genreLabel, descLabel, varLabel, seedLabel, info, lockLabel;
    juce::ComboBox genre;
    juce::TextEditor desc, seed;
    juce::Slider variation;
    juce::ToggleButton fixedSeed { "Fixed seed" };
    juce::TextButton generate { "Generate preset" }, back { "< Back" }, forward { "Forward >" };
    juce::ToggleButton locks[6];
};

// Felső információs sáv: felismert hang, frekvencia, konfidencia, állapot, be/ki szintmérő
class InfoPanel : public juce::Component, private juce::Timer
{
public:
    explicit InfoPanel (Audio2BassProcessor& p);
    ~InfoPanel() override { stopTimer(); }
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    static juce::String noteName (float hz);
    void drawMeter (juce::Graphics&, juce::Rectangle<float> r, float db, const juce::String& label);

    Audio2BassProcessor& proc;
    float hz = 0.0f, conf = 0.0f, inDb = -100.0f, outDb = -100.0f;
    bool voiced = false;
};

class Audio2BassEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Audio2BassEditor (Audio2BassProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Audio2BassProcessor& proc;
    juce::Label title;
    juce::TextButton prevButton { "<" }, nextButton { ">" }, presetButton, saveButton { "Save" };
    juce::TextButton abA { "A" }, abB { "B" }, abCopy { "A > B" };
    std::vector<Audio2BassProcessor::PresetEntry> menuEntries;

    void timerCallback() override;
    void showPresetMenu();
    void showSaveDialog();
    void confirmDelete();
    void stepPreset (int delta);
    InfoPanel infoPanel;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Audio2BassEditor)
};
