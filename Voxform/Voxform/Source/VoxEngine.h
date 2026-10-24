// Voxform - formant choir synthesizer engine.
// Header-only and free of any JUCE dependency so it can be unit-tested stand-alone.
//
// Signal path per "singer":
//   band-limited sawtooth (PolyBLEP) -> one-pole low-pass (glottal spectral tilt)
//   + pulse-modulated breath noise
//   -> 5 parallel band-pass resonators (formants, vowel-interpolated)
// A voice (one MIDI note) is 1..8 singers, each slightly detuned / panned / humanised.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>

namespace vox
{
constexpr int   kNumFormants = 5;
constexpr int   kNumVowels   = 5;   // A E I O U
constexpr int   kMaxSingers  = 8;
constexpr int   kMaxVoices   = 8;
constexpr int   kControlSize = 32;  // samples between coefficient updates
constexpr float kPi          = 3.14159265358979323846f;
constexpr float kTwoPi       = 2.0f * kPi;

inline float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }

// ---------------------------------------------------------------------------------------------
// Vowel data: generic phonetic formant centre frequencies (Hz), relative levels (dB) and
// bandwidths (Hz) for a mid-range voice.
// ---------------------------------------------------------------------------------------------
struct VowelData
{
    float freq[kNumFormants];
    float dB[kNumFormants];
    float bw[kNumFormants];
};

inline const VowelData& vowelTable (int i)
{
    static const VowelData table[kNumVowels] = {
        // A
        { { 650.f, 1080.f, 2650.f, 2900.f, 3250.f }, { 0.f, -6.f, -7.f, -8.f, -22.f },   { 80.f, 90.f, 120.f, 130.f, 140.f } },
        // E
        { { 400.f, 1700.f, 2600.f, 3200.f, 3580.f }, { 0.f, -14.f, -12.f, -14.f, -20.f }, { 70.f, 80.f, 100.f, 120.f, 120.f } },
        // I
        { { 290.f, 1870.f, 2800.f, 3250.f, 3540.f }, { 0.f, -15.f, -18.f, -20.f, -30.f }, { 40.f, 90.f, 100.f, 120.f, 120.f } },
        // O
        { { 400.f, 800.f, 2600.f, 2800.f, 3000.f },  { 0.f, -10.f, -12.f, -12.f, -26.f }, { 40.f, 80.f, 100.f, 120.f, 120.f } },
        // U
        { { 350.f, 600.f, 2700.f, 2900.f, 3300.f },  { 0.f, -20.f, -17.f, -14.f, -26.f }, { 40.f, 60.f, 100.f, 120.f, 120.f } }
    };
    return table[i];
}

struct FormantSet
{
    float freq[kNumFormants];
    float amp[kNumFormants];   // linear
    float bw[kNumFormants];
};

// Alternating polarity keeps the valleys between adjacent parallel formants from cancelling.
constexpr float kFormantSign[kNumFormants] = { 1.f, -1.f, 1.f, -1.f, 1.f };

// pos in [0, 4]: 0 = A, 1 = E, 2 = I, 3 = O, 4 = U, fractional values morph between neighbours.
inline FormantSet vowelAt (float pos)
{
    pos = clampf (pos, 0.0f, (float) (kNumVowels - 1));
    int i = (int) pos;
    if (i > kNumVowels - 2) i = kNumVowels - 2;
    const float t = pos - (float) i;
    const VowelData& a = vowelTable (i);
    const VowelData& b = vowelTable (i + 1);

    FormantSet s {};
    for (int k = 0; k < kNumFormants; ++k)
    {
        s.freq[k] = a.freq[k] + (b.freq[k] - a.freq[k]) * t;
        s.bw[k]   = a.bw[k]   + (b.bw[k]   - a.bw[k])   * t;
        s.amp[k]  = dbToGain (a.dB[k] + (b.dB[k] - a.dB[k]) * t);
    }
    return s;
}

// Soft limiter: transparent below 0.8, smoothly saturates towards 1.0 above it.
inline float softClip (float x)
{
    const float a = std::fabs (x);
    if (a <= 0.8f) return x;
    const float y = 0.8f + 0.2f * std::tanh ((a - 0.8f) * 5.0f);
    return x < 0.0f ? -y : y;
}

// Magnitude response (dB) of the parallel formant bank at `hz`. Used by the UI display.
inline float formantResponseDb (const FormantSet& fs, float shiftRatio, float hz)
{
    std::complex<float> sum (0.0f, 0.0f);
    for (int k = 0; k < kNumFormants; ++k)
    {
        const float f0 = fs.freq[k] * shiftRatio;
        const float bw = fs.bw[k] * shiftRatio;
        const std::complex<float> num (0.0f, hz * bw);
        const std::complex<float> den (f0 * f0 - hz * hz, hz * bw);
        sum += (kFormantSign[k] * fs.amp[k]) * (num / den);
    }
    return 20.0f * std::log10 (std::abs (sum) + 1.0e-6f);
}

// ---------------------------------------------------------------------------------------------
// Building blocks
// ---------------------------------------------------------------------------------------------

// Trapezoidal-integration state-variable filter (stable under fast modulation).
// bandpass() returns a band-pass normalised to unity gain at its centre frequency.
struct Svf
{
    float ic1 = 0.f, ic2 = 0.f;
    float a1 = 0.f, a2 = 0.f, a3 = 0.f, k = 1.f;

    void set (float fc, float bw, float sampleRate)
    {
        fc = clampf (fc, 20.0f, 0.45f * sampleRate);
        bw = std::max (bw, 5.0f);
        const float g = std::tan (kPi * fc / sampleRate);
        k  = bw / fc;                       // 1/Q
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void reset() { ic1 = ic2 = 0.f; }

    inline float bandpass (float v0)
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return k * v1;
    }
};

struct Rng
{
    uint32_t s = 22222u;
    void seed (uint32_t v) { s = v ? v : 1u; }
    inline float next()   // [-1, 1)
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float) (s & 0xFFFFFFu) * (1.0f / 8388608.0f) - 1.0f;
    }
    inline float next01() { return 0.5f * (next() + 1.0f); }
};

inline float polyBlep (float t, float dt)
{
    if (t < dt)         { t /= dt;                return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt)  { t = (t - 1.0f) / dt;    return t * t + t + t + 1.0f; }
    return 0.0f;
}

// ---------------------------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------------------------
struct Params
{
    float vowel        = 0.0f;   // 0..4  (A E I O U)
    float formantSemis = 0.0f;   // -6..+6 semitones
    float tilt         = 0.5f;   // 0..1  glottal brightness
    float breath       = 0.1f;   // 0..1
    float vibRate      = 5.2f;   // Hz
    float vibDepth     = 20.0f;  // cents
    float vibDelay     = 0.4f;   // seconds before vibrato fades in
    float attack       = 0.25f;  // s
    float decay        = 0.4f;   // s
    float sustain      = 0.85f;  // 0..1
    float release      = 0.6f;   // s
    int   singers      = 4;      // 1..8 per note
    float detune       = 12.0f;  // cents (max spread of the choir)
    float spread       = 0.7f;   // 0..1 stereo width
    float human        = 0.5f;   // 0..1 random drift / variation
    float level        = 0.5f;   // linear output gain
};

// ---------------------------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------------------------
class Engine
{
public:
    void prepare (double sr)
    {
        sampleRate = (float) sr;
        controlHz  = sampleRate / (float) kControlSize;
        smoothA    = 1.0f - std::exp (-1.0f / (0.02f * controlHz));
        driftA     = 1.0f - std::exp (-kTwoPi * 2.5f / controlHz);
        driftNorm  = std::sqrt ((2.0f - driftA) / driftA) * 1.7320508f;
        stealStep  = 1.0f / (0.004f * sampleRate);
        reset();
        setParams (target);
        snapSmoothing();
    }

    void reset()
    {
        for (auto& v : voices) v = Voice();
        bend = 0.f; modWheel = 0.f; sustainDown = false; ageCounter = 0;
    }

    void setParams (const Params& p)
    {
        target = p;
        target.singers = std::max (1, std::min (kMaxSingers, p.singers));
        auto coefOf = [this] (float t, float k)
        {
            t = std::max (t, 0.001f);
            return 1.0f - std::exp (-k / (t * sampleRate));
        };
        atkCoef = coefOf (p.attack, 1.466f);   // reaches 1.0 after `attack` seconds
        decCoef = coefOf (p.decay, 6.9f);      // ~ -60 dB after `decay` seconds
        relCoef = coefOf (p.release, 6.9f);
    }

    void snapSmoothing() { sm = target; }

    void setPitchBend (float normalised) { bend = clampf (normalised, -1.0f, 1.0f); }
    void setModWheel (float v)           { modWheel = clampf (v, 0.0f, 1.0f); }

    void setSustain (bool down)
    {
        sustainDown = down;
        if (down) return;
        for (auto& v : voices)
            if (v.pedal)
            {
                v.pedal = false;
                if (v.stage != Stage::Idle && v.stage != Stage::Release) v.stage = Stage::Release;
            }
    }

    void noteOn (int note, float vel)
    {
        vel = clampf (vel, 0.0f, 1.0f);
        if (vel <= 0.0f) { noteOff (note); return; }

        Voice* chosen = nullptr;

        for (auto& v : voices)                                   // retrigger same note
            if (! v.stealing && v.stage != Stage::Idle && v.note == note) { chosen = &v; break; }

        if (chosen == nullptr)                                   // free voice
            for (auto& v : voices)
                if (! v.stealing && v.stage == Stage::Idle) { chosen = &v; break; }

        if (chosen == nullptr)                                   // steal quietest releasing voice
        {
            float best = 1.0e9f;
            for (auto& v : voices)
                if (! v.stealing && v.stage == Stage::Release && v.env < best) { best = v.env; chosen = &v; }
        }

        if (chosen == nullptr)                                   // steal oldest voice
        {
            uint64_t oldest = ~(uint64_t) 0;
            for (auto& v : voices)
                if (! v.stealing && v.age < oldest) { oldest = v.age; chosen = &v; }
        }

        if (chosen == nullptr) chosen = &voices[0];              // everything is mid-steal

        if (chosen->stage == Stage::Idle && ! chosen->stealing)
        {
            beginNote (*chosen, note, vel);
        }
        else
        {
            if (! chosen->stealing) chosen->stealGain = 1.0f;
            chosen->stealing = true;
            chosen->pendNote = note;
            chosen->pendVel  = vel;
        }
    }

    void noteOff (int note)
    {
        for (auto& v : voices)
        {
            if (v.stealing && v.pendNote == note) { v.pendNote = -1; continue; }
            if (! v.stealing && v.stage != Stage::Idle && v.held && v.note == note)
            {
                v.held = false;
                if (sustainDown) v.pedal = true;
                else if (v.stage != Stage::Release) v.stage = Stage::Release;
            }
        }
    }

    void allNotesOff()
    {
        for (auto& v : voices)
        {
            v.pedal = false; v.held = false; v.pendNote = -1;
            if (v.stage != Stage::Idle && v.stage != Stage::Release) v.stage = Stage::Release;
        }
    }

    int activeVoices() const
    {
        int n = 0;
        for (auto& v : voices) if (v.stage != Stage::Idle || v.stealing) ++n;
        return n;
    }

    // Adds `n` samples of audio into L and R (callers clear the buffers first).
    void render (float* L, float* R, int n)
    {
        while (n > 0)
        {
            const int m = std::min (n, kControlSize);
            updateGlobals (m);

            for (auto& v : voices)
            {
                if (v.stealing && (v.stealGain <= 0.0f || v.stage == Stage::Idle))
                {
                    const int note = v.pendNote;
                    const float vel = v.pendVel;
                    v.stealing = false;
                    v.stage = Stage::Idle;
                    if (note >= 0) beginNote (v, note, vel);
                }
                if (v.stage == Stage::Idle) continue;

                updateVoiceControl (v, m);
                renderVoice (v, L, R, m);
            }
            L += m; R += m; n -= m;
        }
    }

private:
    enum class Stage { Idle, Attack, Decay, Release };

    struct Singer
    {
        std::array<Svf, kNumFormants> f;
        Rng   rng;
        float phase = 0.f, lp = 0.f, dt = 0.f, lpA = 0.f;
        float vibPhase = 0.f, vibRateDev = 0.f, drift = 0.f;
        float detuneRank = 0.f, pitchDev = 0.f, formantDev = 0.f, panPos = 0.f;
        float gainL = 1.f, gainR = 1.f;
    };

    struct Voice
    {
        Stage stage = Stage::Idle;
        int   note = -1;
        float vel = 0.f, env = 0.f, vibTime = 0.f, loudGain = 1.f;
        bool  held = false, pedal = false;
        uint64_t age = 0;
        int   numSingers = 1;
        bool  stealing = false;
        float stealGain = 1.f;
        int   pendNote = -1;
        float pendVel = 0.f;
        std::array<Singer, kMaxSingers> singers;
    };

    void beginNote (Voice& v, int note, float vel)
    {
        v.note = note; v.vel = vel; v.stage = Stage::Attack; v.env = 0.f; v.vibTime = 0.f;
        v.held = true; v.pedal = false; v.stealing = false; v.stealGain = 1.f; v.pendNote = -1;
        v.age = ++ageCounter;
        v.numSingers = target.singers;

        const int N = v.numSingers;
        const uint32_t base = (uint32_t) (ageCounter * 2654435761ull) ^ ((uint32_t) note * 7919u + 17u);
        for (int k = 0; k < N; ++k)
        {
            Singer& s = v.singers[(size_t) k];
            s.rng.seed (base + (uint32_t) k * 40503u + 1u);
            for (auto& f : s.f) f.reset();
            s.lp = 0.f; s.drift = 0.f;
            s.phase      = (N == 1) ? 0.0f : s.rng.next01();
            s.vibPhase   = target.human * s.rng.next01();
            s.vibRateDev = 0.10f * s.rng.next();
            s.pitchDev   = s.rng.next();
            s.formantDev = s.rng.next();

            // Interleave detune order against pan order so neighbours in the stereo field
            // are not neighbours in pitch.
            const int detuneIdx = (k % 2 == 0) ? (k / 2) : (N - 1 - k / 2);
            s.detuneRank = (N == 1) ? 0.0f : ((float) detuneIdx / (float) (N - 1)) * 2.0f - 1.0f;
            s.panPos     = (N == 1) ? 0.0f : ((float) k / (float) (N - 1)) * 2.0f - 1.0f;
        }
        updateGlobals (0);
        updateVoiceControl (v, 0);
    }

    void updateGlobals (int m)
    {
        if (m > 0)
        {
            const float a = std::min (1.0f, smoothA * (float) m / (float) kControlSize);
            sm.vowel        += a * (target.vowel        - sm.vowel);
            sm.formantSemis += a * (target.formantSemis - sm.formantSemis);
            sm.tilt         += a * (target.tilt         - sm.tilt);
            sm.breath       += a * (target.breath       - sm.breath);
            sm.level        += a * (target.level        - sm.level);
        }
        curFormants = vowelAt (sm.vowel);
        curShift    = std::exp2 (sm.formantSemis / 12.0f);
        for (int i = 0; i < kNumFormants; ++i) signedAmp[i] = kFormantSign[i] * curFormants.amp[i];

        fcBase       = 300.0f * std::exp2 (5.0f * sm.tilt);
        glottalGain  = 1.0f - 0.8f * sm.breath;
        noiseGain    = sm.breath * kNoiseScale;

        outGain      = sm.level * kBaseGain;
    }

    void updateVoiceControl (Voice& v, int m)
    {
        const float dtSec  = (float) m / sampleRate;
        const float human  = target.human;
        v.vibTime += dtSec;
        const float onset = (target.vibDelay <= 0.001f)
                              ? 1.0f : clampf ((v.vibTime - target.vibDelay) / 0.6f, 0.0f, 1.0f);
        const float depth = clampf (target.vibDepth + modWheel * 50.0f, 0.0f, 150.0f) * onset;
        const float driftStep = driftA * (float) m / (float) kControlSize;

        const float fc  = std::min (fcBase * (0.5f + v.vel), 0.45f * sampleRate);
        const float lpA = 1.0f - std::exp (-kTwoPi * fc / sampleRate);

        // Formant tuning: once the pitch rises above the first formant, singers move F1 up to
        // follow the fundamental (and push the higher formants along), otherwise high notes vanish.
        const float f0Voice = 440.0f * std::exp2 (((float) v.note + bend * 2.0f - 69.0f) / 12.0f);
        static const float minRatio[kNumFormants] = { 0.f, 1.3f, 1.2f, 1.06f, 1.06f };
        float tunedF[kNumFormants], tunedBw[kNumFormants];
        for (int i = 0; i < kNumFormants; ++i)
        {
            const float nominal = curFormants.freq[i] * curShift;
            float f = nominal;
            if (i == 0) f = std::max (f, std::min (f0Voice, 4500.0f));
            else        f = std::max (f, tunedF[i - 1] * minRatio[i]);
            tunedF[i]  = f;
            // Bandwidth keeps Q when a formant is moved, and never drops below a fraction of the
            // pitch so that harmonics cannot fall between narrow peaks (which makes notes vanish).
            tunedBw[i] = std::max (curFormants.bw[i] * curShift * (f / nominal), kBwFloor * f0Voice);
        }

        // Loudness model: output power of a sawtooth source (1/f spectrum) through the low-pass and
        // the formant bank, averaged over harmonic alignment: P ~ sum( amp^2 * bw * f0 / F^2 / (1+(F/fc)^2) ).
        // It uses the *nominal* pitch (vibrato is left out on purpose so its natural amplitude
        // wobble survives). The inverse keeps loudness even across pitch, vowel, formant shift
        // and brightness.
        {
            float power = 0.0f;
            for (int i = 0; i < kNumFormants; ++i)
            {
                const float F = tunedF[i], a = curFormants.amp[i], r = F / fc;
                power += a * a * tunedBw[i] * f0Voice / (F * F * (1.0f + r * r));
            }
            const float wanted = kLoudRef / std::sqrt (power + 1.0e-9f);
            v.loudGain = (m == 0) ? wanted : v.loudGain + 0.1f * (wanted - v.loudGain);
        }

        for (int k = 0; k < v.numSingers; ++k)
        {
            Singer& s = v.singers[(size_t) k];

            s.vibPhase += target.vibRate * (1.0f + human * s.vibRateDev) * dtSec;
            s.vibPhase -= std::floor (s.vibPhase);
            const float vib = std::sin (kTwoPi * s.vibPhase) * depth;

            s.drift += driftStep * (s.rng.next() - s.drift);
            const float driftCents = s.drift * driftNorm * 5.0f * human;

            const float cents = s.detuneRank * target.detune + s.pitchDev * human * 3.0f + vib + driftCents;
            const float semis = (float) v.note + bend * 2.0f + cents * 0.01f;
            const float f0    = 440.0f * std::exp2 ((semis - 69.0f) / 12.0f);
            s.dt  = std::min (f0 / sampleRate, 0.45f);
            s.lpA = lpA;

            const float pan = s.panPos * target.spread;
            const float ang = (pan * 0.5f + 0.5f) * (kPi * 0.5f);
            s.gainL = std::cos (ang) * 1.41421356f;
            s.gainR = std::sin (ang) * 1.41421356f;

            const float dev = 1.0f + human * s.formantDev * 0.04f;   // each singer has a slightly different vocal tract
            for (int i = 0; i < kNumFormants; ++i)
                s.f[(size_t) i].set (tunedF[i] * dev, tunedBw[i] * dev, sampleRate);
        }
    }

    void renderVoice (Voice& v, float* L, float* R, int m)
    {
        const float sus      = target.sustain;
        const float velGain  = 0.25f + 0.75f * v.vel;
        const float baseGain = velGain * outGain * v.loudGain / std::sqrt ((float) v.numSingers);
        const int   ns       = v.numSingers;

        for (int i = 0; i < m; ++i)
        {
            switch (v.stage)
            {
                case Stage::Attack:
                    v.env += (1.3f - v.env) * atkCoef;
                    if (v.env >= 1.0f) { v.env = 1.0f; v.stage = Stage::Decay; }
                    break;
                case Stage::Decay:
                    v.env += (sus - v.env) * decCoef;
                    break;
                case Stage::Release:
                    v.env -= v.env * relCoef;
                    if (v.env < 1.0e-4f) { v.env = 0.0f; v.stage = Stage::Idle; }
                    break;
                case Stage::Idle:
                    break;
            }
            if (v.stage == Stage::Idle) return;

            float l = 0.f, r = 0.f;
            for (int k = 0; k < ns; ++k)
            {
                Singer& s = v.singers[(size_t) k];

                s.phase += s.dt;
                if (s.phase >= 1.0f) s.phase -= 1.0f;
                const float saw = 2.0f * s.phase - 1.0f - polyBlep (s.phase, s.dt);
                s.lp += s.lpA * (saw - s.lp);
                const float noise = s.rng.next() * (0.35f + 0.65f * (1.0f - s.phase));
                const float src   = s.lp * glottalGain + noise * noiseGain;

                float y = 0.f;
                for (int f = 0; f < kNumFormants; ++f)
                    y += signedAmp[f] * s.f[(size_t) f].bandpass (src);

                l += y * s.gainL;
                r += y * s.gainR;
            }

            float g = v.env * baseGain;
            if (v.stealing)
            {
                g *= v.stealGain;
                v.stealGain = std::max (0.0f, v.stealGain - stealStep);
            }
            L[i] += l * g;
            R[i] += r * g;
        }
    }

    // Calibration constants (measured with tests/dsp_test.cpp).
    static constexpr float kBaseGain   = 0.4f;
    static constexpr float kLoudRef    = 0.5f;
    static constexpr float kBwFloor    = 0.4f;   // minimum formant bandwidth as a fraction of f0
    static constexpr float kNoiseScale = 2.0f;

    float sampleRate = 44100.f, controlHz = 1378.f;
    float smoothA = 0.05f, driftA = 0.01f, driftNorm = 24.f, stealStep = 0.005f;
    float atkCoef = 0.001f, decCoef = 0.001f, relCoef = 0.001f;
    float bend = 0.f, modWheel = 0.f;
    bool  sustainDown = false;
    uint64_t ageCounter = 0;

    Params target, sm;
    FormantSet curFormants {};
    float curShift = 1.f, signedAmp[kNumFormants] {};
    float fcBase = 1700.f, glottalGain = 1.f, noiseGain = 0.f, outGain = 0.5f;

    std::array<Voice, kMaxVoices> voices;
};

} // namespace vox
