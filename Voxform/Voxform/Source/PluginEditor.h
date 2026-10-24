#pragma once

#include "PluginProcessor.h"

namespace VoxColours
{
    const juce::Colour bgTop    { 0xff171a23 };
    const juce::Colour bgBottom { 0xff0e1016 };
    const juce::Colour panel    { 0xff1b1f2a };
    const juce::Colour edge     { 0xff2b3040 };
    const juce::Colour accent   { 0xfff2b35e };
    const juce::Colour accent2  { 0xff7fd1c6 };
    const juce::Colour text     { 0xffe9ecf3 };
    const juce::Colour dim      { 0xff8c93a7 };
}

//==============================================================================
class VoxLookAndFeel : public juce::LookAndFeel_V4
{
public:
    VoxLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
};

//==============================================================================
// Shows the formant filter response for the current vowel / formant shift.
class VowelDisplay : public juce::Component, private juce::Timer
{
public:
    explicit VowelDisplay (juce::AudioProcessorValueTreeState& state);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    juce::AudioProcessorValueTreeState& apvts;
    float vowel = 0.0f, shiftSemis = 0.0f;
};

//==============================================================================
class VoxformEditor : public juce::AudioProcessorEditor
{
public:
    explicit VoxformEditor (VoxformProcessor&);
    ~VoxformEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;   // destroyed before slider
    };

    struct Panel { juce::String name; juce::Rectangle<int> bounds; };

    enum KnobIndex
    {
        kVowel, kFormant, kBright, kBreath,
        kSingers, kDetune, kSpread, kHuman,
        kVibRate, kVibDepth, kVibDelay,
        kAttack, kDecay, kSustain, kRelease,
        kSize, kMix, kLevel,
        kNumKnobs
    };

    void initKnob (int index, const char* paramId, const char* labelText);
    void layoutPanel (const juce::String& name, juce::Rectangle<int> area, std::initializer_list<int> knobIndices);

    VoxformProcessor& processor;
    VoxLookAndFeel lookAndFeel;

    juce::Label title, subtitle;
    juce::ComboBox presetBox;
    VowelDisplay display;
    std::array<Knob, kNumKnobs> knobs;
    juce::MidiKeyboardComponent keyboard;
    std::vector<Panel> panels;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxformEditor)
};
