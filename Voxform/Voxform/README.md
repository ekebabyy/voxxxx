# Voxform

An original formant choir synthesizer (VST3 + standalone) built with JUCE. It is not a copy of any
commercial plugin; the sound comes from its own DSP (sawtooth glottal source, 5 formant filters,
unison choir, vibrato, reverb).

## Get the Windows VST3 (easiest: no compiler needed)

1. Create a free GitHub account and a new empty repository.
2. Upload the contents of this folder to it (drag and drop in the web UI works).
3. Open the **Actions** tab, then **Build Windows VST3**, then **Run workflow** (it also runs on every push).
4. When it finishes (about 10 minutes), open the run and download the **Voxform-Windows-x64** artifact.
5. Unzip it and copy the whole `Voxform.vst3` folder to `C:\Program Files\Common Files\VST3\`.
6. Rescan plugins in your DAW. `Voxform.exe` is a standalone version you can double-click to test.

## Build it yourself on Windows

Install Visual Studio 2022 or newer ("Desktop development with C++") and CMake, then run `build.bat`.
The VST3 ends up in `build\Voxform_artefacts\Release\VST3\`.

## Playing it

- MIDI notes, pitch bend (+/-2 st), mod wheel (CC1, adds vibrato), sustain pedal (CC64).
- Vowel knob morphs A, E, I, O, U. Automate it for talking-choir effects.
- Singers (1-8) per note, with Detune, Spread and Humanize for a bigger or more human choir.
- 8 factory presets in the preset menu. Double-click any knob to reset it.

## Tests

`tests/dsp_test.cpp` renders audio scenarios (vowels, pitch accuracy, voice stealing, envelopes)
without JUCE; `tests/analyze.py` checks the spectra. `demo/Voxform_demo.wav` is a rendered demo.

## License note

JUCE is GPLv3 or commercial. Personal use is fine; if you distribute Voxform publicly you must
comply with the GPL or buy a JUCE license.
