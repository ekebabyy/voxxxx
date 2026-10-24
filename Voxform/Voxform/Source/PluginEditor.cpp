#include "PluginEditor.h"

using namespace VoxColours;

static juce::Font makeFont (float size, int style = juce::Font::plain)
{
    return juce::Font (juce::FontOptions (size, style));
}

//==============================================================================
VoxLookAndFeel::VoxLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, accent.withAlpha (0.35f));
    setColour (juce::Label::textColourId, text);
    setColour (juce::TextEditor::backgroundColourId, panel);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.4f));
    setColour (juce::CaretComponent::caretColourId, accent);

    setColour (juce::ComboBox::backgroundColourId, panel);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::outlineColourId, edge);
    setColour (juce::ComboBox::arrowColourId, accent);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, text);
}

void VoxLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                       float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const float trackR = radius - 3.0f;
    const bool enabled = slider.isEnabled();

    // track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, trackR, trackR, 0.0f, startAngle, endAngle, true);
    g.setColour (edge);
    g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, trackR, trackR, 0.0f, startAngle, angle, true);
    g.setColour (enabled ? accent : dim);
    g.strokePath (value, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // body
    const float bodyR = trackR - 7.0f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2d3345), centre.x, centre.y - bodyR,
                                             juce::Colour (0xff181b25), centre.x, centre.y + bodyR, false));
    g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
    g.setColour (edge);
    g.drawEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

    // pointer
    const float inner = bodyR * 0.35f, outer = bodyR * 0.85f;
    juce::Path pointer;
    pointer.startNewSubPath (centre.x + inner * std::sin (angle), centre.y - inner * std::cos (angle));
    pointer.lineTo (centre.x + outer * std::sin (angle), centre.y - outer * std::cos (angle));
    g.setColour (text);
    g.strokePath (pointer, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
VowelDisplay::VowelDisplay (juce::AudioProcessorValueTreeState& state) : apvts (state)
{
    startTimerHz (30);
}

void VowelDisplay::timerCallback()
{
    const float v = apvts.getRawParameterValue (ParamID::vowel)->load();
    const float s = apvts.getRawParameterValue (ParamID::formant)->load();
    if (v != vowel || s != shiftSemis)
    {
        vowel = v;
        shiftSemis = s;
        repaint();
    }
}

void VowelDisplay::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (panel);
    g.fillRoundedRectangle (b, 8.0f);
    g.setColour (edge);
    g.drawRoundedRectangle (b.reduced (0.5f), 8.0f, 1.0f);

    auto plot = b.reduced (16.0f, 12.0f);
    plot.removeFromBottom (14.0f);

    constexpr float fMin = 60.0f, fMax = 8000.0f, dbMin = -50.0f, dbMax = 6.0f;
    const float logSpan = std::log (fMax / fMin);
    auto xOf = [&] (float hz) { return plot.getX() + plot.getWidth() * std::log (hz / fMin) / logSpan; };
    auto yOf = [&] (float db)
    {
        return plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, (db - dbMin) / (dbMax - dbMin));
    };

    // frequency grid
    g.setFont (makeFont (11.0f));
    const std::pair<float, const char*> marks[] = { { 100, "100" }, { 200, "200" }, { 500, "500" },
                                                    { 1000, "1k" }, { 2000, "2k" }, { 5000, "5k" } };
    for (const auto& m : marks)
    {
        const float x = xOf (m.first);
        g.setColour (edge.withAlpha (0.6f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (dim);
        g.drawText (m.second, juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 1.0f, 40.0f, 13.0f),
                    juce::Justification::centred);
    }

    // response curve
    const auto formants = vox::vowelAt (vowel);
    const float shift = std::exp2 (shiftSemis / 12.0f);

    juce::Path line, fill;
    const int steps = juce::jmax (2, (int) plot.getWidth() / 2);
    for (int i = 0; i <= steps; ++i)
    {
        const float hz = fMin * std::exp (logSpan * (float) i / (float) steps);
        const float x = plot.getX() + plot.getWidth() * (float) i / (float) steps;
        const float y = yOf (vox::formantResponseDb (formants, shift, hz));
        if (i == 0) { line.startNewSubPath (x, y); fill.startNewSubPath (x, plot.getBottom()); fill.lineTo (x, y); }
        else        { line.lineTo (x, y); fill.lineTo (x, y); }
    }
    fill.lineTo (plot.getRight(), plot.getBottom());
    fill.closeSubPath();

    g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.38f), 0.0f, plot.getY(),
                                             accent.withAlpha (0.02f), 0.0f, plot.getBottom(), false));
    g.fillPath (fill);
    g.setColour (accent);
    g.strokePath (line, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // formant markers
    g.setFont (makeFont (11.0f, juce::Font::bold));
    for (int k = 0; k < 3; ++k)
    {
        const float hz = formants.freq[k] * shift;
        if (hz < fMin || hz > fMax) continue;
        const float x = xOf (hz);
        g.setColour (accent2.withAlpha (0.7f));
        g.drawVerticalLine ((int) x, plot.getY() + 16.0f, plot.getBottom());
        g.drawText ("F" + juce::String (k + 1), juce::Rectangle<float> (x - 15.0f, plot.getY(), 30.0f, 14.0f),
                    juce::Justification::centred);
    }

    // vowel name
    const juce::String name = [this]
    {
        static const char names[] = "AEIOU";
        const int i = juce::jlimit (0, 3, (int) vowel);
        const float f = vowel - (float) i;
        return juce::String::charToString (names[f < 0.5f ? i : i + 1]);
    }();
    g.setColour (text.withAlpha (0.9f));
    g.setFont (makeFont (38.0f, juce::Font::bold));
    g.drawText (name, juce::Rectangle<float> (plot.getX() + 6.0f, plot.getY() + 4.0f, 60.0f, 44.0f),
                juce::Justification::centredLeft);
    g.setColour (dim);
    g.setFont (makeFont (11.0f));
    g.drawText ("formant filter response", plot.removeFromTop (16.0f).withTrimmedRight (6.0f),
                juce::Justification::centredRight);
}

//==============================================================================
VoxformEditor::VoxformEditor (VoxformProcessor& p)
    : juce::AudioProcessorEditor (&p),
      processor (p),
      display (p.apvts),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&lookAndFeel);

    title.setText ("VOXFORM", juce::dontSendNotification);
    title.setFont (makeFont (26.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, accent);
    addAndMakeVisible (title);

    subtitle.setText ("formant choir synthesizer", juce::dontSendNotification);
    subtitle.setFont (makeFont (12.0f));
    subtitle.setColour (juce::Label::textColourId, dim);
    addAndMakeVisible (subtitle);

    for (int i = 0; i < processor.getNumPrograms(); ++i)
        presetBox.addItem (processor.getProgramName (i), i + 1);
    presetBox.setSelectedItemIndex (processor.getCurrentProgram(), juce::dontSendNotification);
    presetBox.onChange = [this] { processor.setCurrentProgram (presetBox.getSelectedItemIndex()); };
    addAndMakeVisible (presetBox);

    addAndMakeVisible (display);

    initKnob (kVowel,   ParamID::vowel,      "Vowel");
    initKnob (kFormant, ParamID::formant,    "Formant");
    initKnob (kBright,  ParamID::tilt,       "Bright");
    initKnob (kBreath,  ParamID::breath,     "Breath");
    initKnob (kSingers, ParamID::singers,    "Singers");
    initKnob (kDetune,  ParamID::detune,     "Detune");
    initKnob (kSpread,  ParamID::spread,     "Spread");
    initKnob (kHuman,   ParamID::human,      "Human");
    initKnob (kVibRate, ParamID::vibRate,    "Rate");
    initKnob (kVibDepth, ParamID::vibDepth,  "Depth");
    initKnob (kVibDelay, ParamID::vibDelay,  "Delay");
    initKnob (kAttack,  ParamID::attack,     "Attack");
    initKnob (kDecay,   ParamID::decay,      "Decay");
    initKnob (kSustain, ParamID::sustain,    "Sustain");
    initKnob (kRelease, ParamID::release,    "Release");
    initKnob (kSize,    ParamID::reverbSize, "Size");
    initKnob (kMix,     ParamID::reverbMix,  "Mix");
    initKnob (kLevel,   ParamID::level,      "Level");

    keyboard.setKeyWidth (17.0f);
    keyboard.setLowestVisibleKey (36);
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, accent.withAlpha (0.8f));
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, accent.withAlpha (0.35f));
    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xffe9ecf3));
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff12141a));
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, edge);
    addAndMakeVisible (keyboard);

    setSize (900, 610);
}

VoxformEditor::~VoxformEditor()
{
    setLookAndFeel (nullptr);
}

void VoxformEditor::initKnob (int index, const char* paramId, const char* labelText)
{
    auto& k = knobs[(size_t) index];

    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 16);
    k.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    k.slider.setMouseDragSensitivity (180);
    addAndMakeVisible (k.slider);

    k.label.setText (labelText, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (makeFont (12.0f, juce::Font::bold));
    k.label.setColour (juce::Label::textColourId, dim);
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, paramId, k.slider);
}

void VoxformEditor::layoutPanel (const juce::String& name, juce::Rectangle<int> area, std::initializer_list<int> knobIndices)
{
    panels.push_back ({ name, area });

    auto inner = area.reduced (8, 6);
    inner.removeFromTop (18);   // title strip
    const int n = (int) knobIndices.size();
    const int w = inner.getWidth() / n;
    int i = 0;
    for (int idx : knobIndices)
    {
        auto cell = juce::Rectangle<int> (inner.getX() + i * w, inner.getY(), w, inner.getHeight());
        auto& k = knobs[(size_t) idx];
        k.label.setBounds (cell.removeFromTop (16));
        k.slider.setBounds (cell);
        ++i;
    }
}

void VoxformEditor::resized()
{
    panels.clear();
    auto area = getLocalBounds().reduced (16);

    auto header = area.removeFromTop (46);
    title.setBounds (header.removeFromLeft (190).withTrimmedBottom (14));
    subtitle.setBounds (header.removeFromLeft (220).withTrimmedTop (24).withTrimmedLeft (2));
    presetBox.setBounds (header.removeFromRight (230).withSizeKeepingCentre (230, 28));

    area.removeFromTop (4);
    display.setBounds (area.removeFromTop (150));
    area.removeFromTop (10);

    keyboard.setBounds (area.removeFromBottom (72));
    area.removeFromBottom (10);

    auto row1 = area.removeFromTop ((area.getHeight() - 10) / 2);
    area.removeFromTop (10);
    auto row2 = area;

    // row 1: VOICE | CHOIR
    {
        const int half = (row1.getWidth() - 10) / 2;
        layoutPanel ("VOICE", row1.removeFromLeft (half), { kVowel, kFormant, kBright, kBreath });
        row1.removeFromLeft (10);
        layoutPanel ("CHOIR", row1, { kSingers, kDetune, kSpread, kHuman });
    }

    // row 2: VIBRATO | ENVELOPE | SPACE   (widths proportional to knob count)
    {
        const int usable = row2.getWidth() - 20;
        const int unit = usable / 10;
        layoutPanel ("VIBRATO",  row2.removeFromLeft (unit * 3), { kVibRate, kVibDepth, kVibDelay });
        row2.removeFromLeft (10);
        layoutPanel ("ENVELOPE", row2.removeFromLeft (unit * 4), { kAttack, kDecay, kSustain, kRelease });
        row2.removeFromLeft (10);
        layoutPanel ("SPACE & OUTPUT", row2, { kSize, kMix, kLevel });
    }
}

void VoxformEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (bgTop, 0.0f, 0.0f, bgBottom, 0.0f, (float) getHeight(), false));
    g.fillAll();

    for (const auto& p : panels)
    {
        const auto r = p.bounds.toFloat();
        g.setColour (panel);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (edge);
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

        g.setColour (accent2);
        g.setFont (makeFont (11.0f, juce::Font::bold));
        g.drawText (p.name, juce::Rectangle<int> (p.bounds.getX() + 14, p.bounds.getY() + 6, p.bounds.getWidth() - 20, 14),
                    juce::Justification::centredLeft);
    }
}
