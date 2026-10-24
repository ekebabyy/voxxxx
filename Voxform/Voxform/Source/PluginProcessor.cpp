#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Text helpers for parameter display
//==============================================================================
namespace
{
    juce::String vowelText (float v)
    {
        static const char names[] = "AEIOU";
        const int i = juce::jlimit (0, 3, (int) v);
        const float f = v - (float) i;
        if (f < 0.03f) return juce::String::charToString (names[i]);
        if (f > 0.97f) return juce::String::charToString (names[i + 1]);
        return juce::String::charToString (names[i]) + ">" + juce::String::charToString (names[i + 1])
               + " " + juce::String (juce::roundToInt (f * 100.0f)) + "%";
    }

    float vowelFromText (const juce::String& text)
    {
        const juce::String s = text.trim().toUpperCase();
        const juce::String names ("AEIOU");
        const int i = s.isEmpty() ? -1 : names.indexOfChar (s[0]);
        if (i < 0) return juce::jlimit (0.0f, 4.0f, s.getFloatValue());

        float v = (float) i;
        if (s.length() >= 3 && s[1] == '>')
        {
            const int j = names.indexOfChar (s[2]);
            if (j >= 0)
            {
                const float pct = s.fromFirstOccurrenceOf (" ", false, false).getFloatValue() / 100.0f;
                v = (float) i + (j > i ? 1.0f : -1.0f) * pct;
            }
        }
        else if (s.length() == 1 && i >= 0)
        {
            v = (float) i;
        }
        return juce::jlimit (0.0f, 4.0f, v);
    }

    juce::String percentText (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
    float percentFromText (const juce::String& t) { return t.getFloatValue() / 100.0f; }

    juce::String timeText (float v, int)
    {
        return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms" : juce::String (v, 2) + " s";
    }

    float timeFromText (const juce::String& t)
    {
        const float x = t.getFloatValue();
        return t.containsIgnoreCase ("ms") ? x / 1000.0f : x;
    }

    using Attr = juce::AudioParameterFloatAttributes;
}

//==============================================================================
// Factory presets: parameter overrides on top of the defaults
//==============================================================================
namespace
{
    struct Preset
    {
        const char* name;
        std::vector<std::pair<const char*, float>> values;
    };

    const std::vector<Preset>& presets()
    {
        using namespace ParamID;
        static const std::vector<Preset> list = {
            { "Warm Choir",     { { singers, 5 }, { detune, 14 }, { spread, 0.8f }, { human, 0.6f }, { attack, 0.45f },
                                  { release, 1.2f }, { vibDepth, 22 }, { tilt, 0.45f }, { reverbMix, 0.3f }, { reverbSize, 0.7f } } },
            { "Angelic Aah",    { { vowel, 0 }, { formant, 2 }, { tilt, 0.6f }, { singers, 6 }, { detune, 10 }, { attack, 0.8f },
                                  { release, 2.0f }, { vibDepth, 18 }, { vibDelay, 0.7f }, { breath, 0.08f },
                                  { reverbSize, 0.85f }, { reverbMix, 0.4f } } },
            { "Dark Ooh",       { { vowel, 4 }, { formant, -2 }, { tilt, 0.3f }, { singers, 4 }, { attack, 0.6f }, { release, 1.5f },
                                  { breath, 0.12f }, { reverbSize, 0.7f }, { reverbMix, 0.35f } } },
            { "Solo Ah",        { { vowel, 0 }, { singers, 1 }, { human, 0.6f }, { vibDepth, 30 }, { attack, 0.12f }, { release, 0.35f },
                                  { spread, 0 }, { tilt, 0.55f }, { breath, 0.15f }, { reverbSize, 0.4f }, { reverbMix, 0.15f } } },
            { "Whisper Choir",  { { vowel, 3 }, { breath, 0.85f }, { tilt, 0.7f }, { singers, 6 }, { attack, 0.5f }, { vibDepth, 8 },
                                  { release, 1.0f }, { reverbSize, 0.7f }, { reverbMix, 0.35f } } },
            { "Boys' Choir",    { { vowel, 0 }, { formant, 4 }, { tilt, 0.65f }, { singers, 6 }, { vibDepth, 14 }, { attack, 0.3f },
                                  { release, 0.9f }, { reverbSize, 0.75f }, { reverbMix, 0.3f } } },
            { "Bass Ooh",       { { vowel, 3 }, { formant, -4 }, { tilt, 0.4f }, { singers, 4 }, { detune, 10 }, { attack, 0.5f },
                                  { release, 1.0f }, { reverbMix, 0.2f } } },
            { "Slow Swell",     { { singers, 8 }, { detune, 18 }, { human, 0.7f }, { attack, 2.5f }, { release, 3.0f },
                                  { vibDelay, 1.2f }, { reverbSize, 0.9f }, { reverbMix, 0.45f } } },
        };
        return list;
    }
}

//==============================================================================
VoxformProcessor::VoxformProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VOXFORM", createLayout())
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    pVowel = get (ParamID::vowel);           pFormant = get (ParamID::formant);
    pTilt = get (ParamID::tilt);             pBreath = get (ParamID::breath);
    pSingers = get (ParamID::singers);       pDetune = get (ParamID::detune);
    pSpread = get (ParamID::spread);         pHuman = get (ParamID::human);
    pVibRate = get (ParamID::vibRate);       pVibDepth = get (ParamID::vibDepth);
    pVibDelay = get (ParamID::vibDelay);     pAttack = get (ParamID::attack);
    pDecay = get (ParamID::decay);           pSustain = get (ParamID::sustain);
    pRelease = get (ParamID::release);       pReverbSize = get (ParamID::reverbSize);
    pReverbMix = get (ParamID::reverbMix);   pLevel = get (ParamID::level);
}

juce::AudioProcessorValueTreeState::ParameterLayout VoxformProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using I = juce::AudioParameterInt;
    using R = juce::NormalisableRange<float>;
    using ID = juce::ParameterID;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<F> (ID { ParamID::vowel, 1 }, "Vowel", R (0.0f, 4.0f, 0.001f), 0.0f,
                                     Attr().withStringFromValueFunction ([] (float v, int) { return vowelText (v); })
                                           .withValueFromStringFunction (vowelFromText)));

    layout.add (std::make_unique<F> (ID { ParamID::formant, 1 }, "Formant Shift", R (-6.0f, 6.0f, 0.01f), 0.0f,
                                     Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " st"; })));

    layout.add (std::make_unique<F> (ID { ParamID::tilt, 1 }, "Brightness", R (0.0f, 1.0f, 0.001f), 0.5f,
                                     Attr().withStringFromValueFunction (percentText).withValueFromStringFunction (percentFromText)));

    layout.add (std::make_unique<F> (ID { ParamID::breath, 1 }, "Breath", R (0.0f, 1.0f, 0.001f), 0.1f,
                                     Attr().withStringFromValueFunction (percentText).withValueFromStringFunction (percentFromText)));

    layout.add (std::make_unique<I> (ID { ParamID::singers, 1 }, "Singers", 1, vox::kMaxSingers, 4));

    layout.add (std::make_unique<F> (ID { ParamID::detune, 1 }, "Choir Detune", R (0.0f, 50.0f, 0.1f), 12.0f,
                                     Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " ct"; })));

    layout.add (std::make_unique<F> (ID { ParamID::spread, 1 }, "Stereo Spread", R (0.0f, 1.0f, 0.001f), 0.7f,
                                     Attr().withStringFromValueFunction (percentText).withValueFromStringFunction (percentFromText)));

    layout.add (std::make_unique<F> (ID { ParamID::human, 1 }, "Humanize", R (0.0f, 1.0f, 0.001f), 0.5f,
                                     Attr().withStringFromValueFunction (percentText).withValueFromStringFunction (percentFromText)));

    layout.add (std::make_unique<F> (ID { ParamID::vibRate, 1 }, "Vibrato Rate", R (2.0f, 8.0f, 0.01f), 5.2f,
                                     Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " Hz"; })));

    layout.add (std::make_unique<F> (ID { ParamID::vibDepth, 1 }, "Vibrato Depth", R (0.0f, 100.0f, 0.1f), 20.0f,
                                     Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " ct"; })));

    layout.add (std::make_unique<F> (ID { ParamID::vibDelay, 1 }, "Vibrato Delay", R (0.0f, 2.0f, 0.001f, 0.6f), 0.4f,
                                     Attr().withStringFromValueFunction (timeText).withValueFromStringFunction (timeFromText)));

    layout.add (std::make_unique<F> (ID { ParamID::attack, 1 }, "Attack", R (0.005f, 4.0f, 0.001f, 0.4f), 0.25f,
                                     Attr().withStringFromValueFunction (timeText).withValueFromStringFunction (timeFromText)));

    layout.add (std::make_unique<F> (ID { ParamID::decay, 1 }, "Decay", R (0.01f, 4.0f, 0.001f, 0.4f), 0.4f,
                                     Attr().withStringFromValueFunction (timeText).withValueFromStringFunction (timeFromText)));

    layout.add (std::make_unique<F> (ID { ParamID::sustain, 1 }, "Sustain", R (0.0f, 1.0f, 0.001f), 0.85f,
                                     Attr().withStringFromValueFunction (percentText).withValueFromStringFunction (percentFromText)));

    layout.add (std::make_unique<F> (ID { ParamID::release, 1 }, "Release", R (0.02f, 6.0f, 0.001f, 0.4f), 0.6f,
                                     Attr().withStringFromValueFunction (timeText).withValueFromStringFunction (timeFromText)));

    layout.add (std::make_unique<F> (ID { ParamID::reverbSize, 1 }, "Reverb Size", R (0.0f, 1.0f, 0.001f), 0.6f,
                                     Attr().withStringFromValueFunction (percentText).withValueFromStringFunction (percentFromText)));

    layout.add (std::make_unique<F> (ID { ParamID::reverbMix, 1 }, "Reverb Mix", R (0.0f, 1.0f, 0.001f), 0.25f,
                                     Attr().withStringFromValueFunction (percentText).withValueFromStringFunction (percentFromText)));

    layout.add (std::make_unique<F> (ID { ParamID::level, 1 }, "Level", R (-24.0f, 12.0f, 0.1f), 0.0f,
                                     Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; })));

    return layout;
}

//==============================================================================
vox::Params VoxformProcessor::readParams() const
{
    vox::Params p;
    p.vowel        = pVowel->load();
    p.formantSemis = pFormant->load();
    p.tilt         = pTilt->load();
    p.breath       = pBreath->load();
    p.singers      = juce::roundToInt (pSingers->load());
    p.detune       = pDetune->load();
    p.spread       = pSpread->load();
    p.human        = pHuman->load();
    p.vibRate      = pVibRate->load();
    p.vibDepth     = pVibDepth->load();
    p.vibDelay     = pVibDelay->load();
    p.attack       = pAttack->load();
    p.decay        = pDecay->load();
    p.sustain      = pSustain->load();
    p.release      = pRelease->load();
    p.level        = juce::Decibels::decibelsToGain (pLevel->load(), -60.0f);
    return p;
}

void VoxformProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    engine.setParams (readParams());
    engine.snapSmoothing();

    reverb.prepare (sampleRate);
    reverb.setParams (pReverbSize->load(), pReverbMix->load());
}

bool VoxformProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void VoxformProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
        engine.noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())
        engine.noteOff (m.getNoteNumber());
    else if (m.isAllNotesOff() || m.isAllSoundOff())
        engine.allNotesOff();
    else if (m.isPitchWheel())
        engine.setPitchBend ((float) (m.getPitchWheelValue() - 8192) / 8192.0f);
    else if (m.isController())
    {
        if (m.getControllerNumber() == 1)       engine.setModWheel ((float) m.getControllerValue() / 127.0f);
        else if (m.getControllerNumber() == 64) engine.setSustain (m.getControllerValue() >= 64);
    }
}

void VoxformProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    if (buffer.getNumChannels() < 2 || numSamples == 0)
        return;

    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);

    engine.setParams (readParams());
    reverb.setParams (pReverbSize->load(), pReverbMix->load());

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    int pos = 0;
    for (const auto metadata : midi)
    {
        const int eventPos = juce::jlimit (pos, numSamples, metadata.samplePosition);
        if (eventPos > pos)
        {
            engine.render (L + pos, R + pos, eventPos - pos);
            pos = eventPos;
        }
        handleMidi (metadata.getMessage());
    }
    if (pos < numSamples)
        engine.render (L + pos, R + pos, numSamples - pos);

    reverb.process (L, R, numSamples);

    for (int i = 0; i < numSamples; ++i)     // safety limiter
    {
        L[i] = vox::softClip (L[i]);
        R[i] = vox::softClip (R[i]);
    }
}

//==============================================================================
juce::AudioProcessorEditor* VoxformProcessor::createEditor() { return new VoxformEditor (*this); }

int VoxformProcessor::getNumPrograms() { return (int) presets().size(); }

const juce::String VoxformProcessor::getProgramName (int index)
{
    const auto& list = presets();
    return juce::isPositiveAndBelow (index, (int) list.size()) ? juce::String (list[(size_t) index].name) : juce::String();
}

void VoxformProcessor::setCurrentProgram (int index)
{
    const auto& list = presets();
    if (! juce::isPositiveAndBelow (index, (int) list.size()))
        return;

    currentProgram = index;

    for (auto* param : getParameters())            // start from defaults so presets are predictable
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (param))
            p->setValueNotifyingHost (p->getDefaultValue());

    for (const auto& kv : list[(size_t) index].values)
        if (auto* p = apvts.getParameter (kv.first))
            p->setValueNotifyingHost (p->convertTo0to1 (kv.second));
}

void VoxformProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void VoxformProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto vt = juce::ValueTree::fromXml (*xml);
            currentProgram = juce::jlimit (0, juce::jmax (0, getNumPrograms() - 1), (int) vt.getProperty ("program", 0));
            apvts.replaceState (vt);
        }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VoxformProcessor(); }
