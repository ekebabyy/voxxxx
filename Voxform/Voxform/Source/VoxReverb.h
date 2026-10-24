// Voxform - small stereo reverb (Freeverb-style: 8 damped combs + 4 all-passes per channel).
// Header-only, no JUCE dependency.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace vox
{
class Reverb
{
public:
    void prepare (double sampleRate)
    {
        static const int combTunings[8]    = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static const int allpassTunings[4] = { 556, 441, 341, 225 };
        const float scale  = (float) sampleRate / 44100.0f;
        const int   spread = (int) (23.0f * scale);

        for (int c = 0; c < 2; ++c)
        {
            for (int i = 0; i < 8; ++i) combs[c][i].init ((int) ((float) combTunings[i] * scale) + c * spread);
            for (int i = 0; i < 4; ++i) aps[c][i].init ((int) ((float) allpassTunings[i] * scale) + c * spread);
        }
        smoothA = 1.0f - std::exp (-1.0f / (0.05f * (float) sampleRate));
        dryNow = dryTarget; wetNow = wetTarget;
    }

    // size01: 0 = small room, 1 = long hall.  mix01: 0 = dry, 1 = wet only.
    void setParams (float size01, float mix01)
    {
        size01 = std::max (0.0f, std::min (1.0f, size01));
        mix01  = std::max (0.0f, std::min (1.0f, mix01));
        feedback  = 0.70f + 0.28f * size01;
        const float a = mix01 * 1.57079633f;
        dryTarget = std::cos (a);
        wetTarget = std::sin (a) * kWetScale;
    }

    void process (float* L, float* R, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            dryNow += smoothA * (dryTarget - dryNow);
            wetNow += smoothA * (wetTarget - wetNow);

            const float in = (L[i] + R[i]) * 0.015f;
            float oL = 0.f, oR = 0.f;
            for (int k = 0; k < 8; ++k)
            {
                oL += combs[0][k].process (in, feedback, damp);
                oR += combs[1][k].process (in, feedback, damp);
            }
            for (int k = 0; k < 4; ++k)
            {
                oL = aps[0][k].process (oL);
                oR = aps[1][k].process (oR);
            }
            L[i] = L[i] * dryNow + oL * wetNow;
            R[i] = R[i] * dryNow + oR * wetNow;
        }
    }

private:
    static constexpr float kWetScale = 3.0f;

    struct Comb
    {
        std::vector<float> buf; int idx = 0; float store = 0.f;
        void init (int size) { buf.assign ((size_t) std::max (size, 1), 0.f); idx = 0; store = 0.f; }
        inline float process (float in, float fb, float damp)
        {
            const float out = buf[(size_t) idx];
            store = out * (1.0f - damp) + store * damp;
            buf[(size_t) idx] = in + store * fb;
            if (++idx >= (int) buf.size()) idx = 0;
            return out;
        }
    };

    struct Allpass
    {
        std::vector<float> buf; int idx = 0;
        void init (int size) { buf.assign ((size_t) std::max (size, 1), 0.f); idx = 0; }
        inline float process (float in)
        {
            const float b = buf[(size_t) idx];
            const float out = b - in;
            buf[(size_t) idx] = in + b * 0.5f;
            if (++idx >= (int) buf.size()) idx = 0;
            return out;
        }
    };

    Comb combs[2][8];
    Allpass aps[2][4];
    float feedback = 0.84f, damp = 0.35f;
    float dryTarget = 1.f, wetTarget = 0.f, dryNow = 1.f, wetNow = 0.f, smoothA = 0.001f;
};

} // namespace vox
