#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "VoxEngine.h"
#include "VoxReverb.h"

namespace ParamID
{
    constexpr const char* vowel      = "vowel";
    constexpr const char* formant    = "formant";
    constexpr const char* tilt       = "tilt";
    constexpr const char* breath     = "breath";
    constexpr const char* singers    = "singers";
    constexpr const char* detune     = "detune";
    constexpr const char* spread     = "spread";
    constexpr const char* human      = "human";
    constexpr const char* vibRate    = "vibRate";
    constexpr const char* vibDepth   = "vibDepth";
    constexpr const char* vibDelay   = "vibDelay";
    constexpr const char* attack     = "attack";
    constexpr const char* decay      = "decay";
    constexpr const char* sustain    = "sustain";
    constexpr const char* release    = "release";
    constexpr const char* reverbSize = "reverbSize";
    constexpr const char* reverbMix  = "reverbMix";
    constexpr const char* level      = "level";
}

class VoxformProcessor : public juce::AudioProcessor
{
public:
    VoxformProcessor();
    ~VoxformProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Voxform"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;   // lets the on-screen keyboard play the synth

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    vox::Params readParams() const;
    void handleMidi (const juce::MidiMessage& m);

    vox::Engine engine;
    vox::Reverb reverb;
    int currentProgram = 0;

    std::atomic<float>* pVowel = nullptr;      std::atomic<float>* pFormant = nullptr;
    std::atomic<float>* pTilt = nullptr;       std::atomic<float>* pBreath = nullptr;
    std::atomic<float>* pSingers = nullptr;    std::atomic<float>* pDetune = nullptr;
    std::atomic<float>* pSpread = nullptr;     std::atomic<float>* pHuman = nullptr;
    std::atomic<float>* pVibRate = nullptr;    std::atomic<float>* pVibDepth = nullptr;
    std::atomic<float>* pVibDelay = nullptr;   std::atomic<float>* pAttack = nullptr;
    std::atomic<float>* pDecay = nullptr;      std::atomic<float>* pSustain = nullptr;
    std::atomic<float>* pRelease = nullptr;    std::atomic<float>* pReverbSize = nullptr;
    std::atomic<float>* pReverbMix = nullptr;  std::atomic<float>* pLevel = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxformProcessor)
};
