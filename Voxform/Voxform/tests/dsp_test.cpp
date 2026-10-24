// Stand-alone DSP test / renderer for the Voxform engine (no JUCE needed).
//   g++ -std=c++17 -O2 -Wall -Wextra dsp_test.cpp -o dsp_test && ./dsp_test <outdir>
// Writes interleaved stereo float32 files (<name>.f32) that analyze.py inspects,
// and prints peak / RMS / NaN counts per scenario.
#include "../Source/VoxEngine.h"
#include "../Source/VoxReverb.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

struct Event { double t; bool on; int note; float vel; };
struct Out   { std::vector<float> L, R; double sr = 44100.0; };

static int g_failures = 0;
static void check (bool ok, const char* msg)
{
    std::printf ("  [%s] %s\n", ok ? " ok " : "FAIL", msg);
    if (! ok) ++g_failures;
}

using Automation = std::function<void (double, vox::Params&)>;

static Out run (double sr, double seconds, vox::Params base, std::vector<Event> events,
                Automation automation = nullptr, bool useReverb = false, float revSize = 0.6f, float revMix = 0.25f,
                bool sustainPedalTest = false)
{
    vox::Engine eng;
    eng.prepare (sr);
    eng.setParams (base);
    eng.snapSmoothing();

    vox::Reverb rev;
    rev.prepare (sr);
    rev.setParams (revSize, revMix);

    const int N = (int) (seconds * sr);
    Out out; out.sr = sr; out.L.assign ((size_t) N, 0.f); out.R.assign ((size_t) N, 0.f);

    std::sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.t < b.t; });
    size_t ei = 0;
    unsigned rngState = 12345u;
    int pos = 0;
    while (pos < N)
    {
        rngState = rngState * 1664525u + 1013904223u;
        int len = 1 + (int) ((rngState >> 8) % 480u);             // awkward block sizes on purpose
        len = std::min (len, N - pos);

        while (ei < events.size() && (int) (events[ei].t * sr) <= pos)
        {
            const Event& e = events[ei++];
            if (sustainPedalTest && e.note == -64) eng.setSustain (e.on);
            else if (e.on) eng.noteOn (e.note, e.vel);
            else           eng.noteOff (e.note);
        }
        if (ei < events.size()) len = std::min (len, std::max (1, (int) (events[ei].t * sr) - pos));

        vox::Params p = base;
        if (automation) automation ((double) pos / sr, p);
        eng.setParams (p);

        eng.render (out.L.data() + pos, out.R.data() + pos, len);
        if (useReverb) rev.process (out.L.data() + pos, out.R.data() + pos, len);
        for (int i = pos; i < pos + len; ++i)   // same output stage as the plugin
        {
            out.L[(size_t) i] = vox::softClip (out.L[(size_t) i]);
            out.R[(size_t) i] = vox::softClip (out.R[(size_t) i]);
        }
        pos += len;
    }
    return out;
}

struct Stats { double peak = 0, rms = 0; long bad = 0; double maxJump = 0; };
static Stats stats (const Out& o, double t0 = 0.0, double t1 = 1.0e9)
{
    Stats s; double acc = 0; long cnt = 0;
    const size_t a = (size_t) std::max (0.0, t0 * o.sr), b = std::min (o.L.size(), (size_t) (t1 * o.sr));
    for (size_t i = a; i < b; ++i)
    {
        for (float v : { o.L[i], o.R[i] })
        {
            if (! std::isfinite (v)) { ++s.bad; continue; }
            s.peak = std::max (s.peak, (double) std::fabs (v));
            acc += (double) v * v; ++cnt;
        }
        if (i > a && std::isfinite (o.L[i]) && std::isfinite (o.L[i - 1]))
            s.maxJump = std::max (s.maxJump, (double) std::fabs (o.L[i] - o.L[i - 1]));
    }
    s.rms = cnt ? std::sqrt (acc / (double) cnt) : 0.0;
    return s;
}

static std::string g_dir;
static void save (const std::string& name, const Out& o)
{
    const std::string path = g_dir + "/" + name + ".f32";
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) { std::printf ("cannot write %s\n", path.c_str()); std::exit (2); }
    std::vector<float> inter (o.L.size() * 2);
    for (size_t i = 0; i < o.L.size(); ++i) { inter[2 * i] = o.L[i]; inter[2 * i + 1] = o.R[i]; }
    std::fwrite (inter.data(), sizeof (float), inter.size(), f);
    std::fclose (f);
}

static vox::Params plain()
{
    vox::Params p;
    p.singers = 1; p.human = 0.f; p.vibDepth = 0.f; p.detune = 0.f; p.breath = 0.f;
    p.attack = 0.05f; p.sustain = 1.0f; p.spread = 0.f; p.level = 0.5f;
    return p;
}

static double db (double x) { return 20.0 * std::log10 (std::max (x, 1.0e-12)); }

int main (int argc, char** argv)
{
    g_dir = argc > 1 ? argv[1] : ".";

    // 1. Each vowel on A2 (110 Hz), single singer, steady.
    {
        const char* names = "AEIOU";
        std::printf ("vowels\n");
        for (int v = 0; v < 5; ++v)
        {
            auto p = plain(); p.vowel = (float) v;
            auto o = run (44100, 2.0, p, { { 0.0, true, 45, 1.0f } });
            save (std::string ("vowel_") + names[v], o);
            auto s = stats (o, 0.3);
            std::printf ("  vowel %c  peak %.3f  rms %.1f dBFS\n", names[v], s.peak, db (s.rms));
            check (s.bad == 0, "no NaN/Inf");
        }
    }

    // 2. Pitch accuracy at several sample rates (A4 = 440 Hz).
    {
        std::printf ("pitch accuracy\n");
        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            auto p = plain();
            auto o = run (sr, 1.5, p, { { 0.0, true, 69, 1.0f } });
            save ("pitch_" + std::to_string ((int) sr), o);
            check (stats (o).bad == 0, "no NaN/Inf");
        }
    }

    // 3. Choir size vs level: 1 singer vs 8 singers should be within a few dB.
    {
        std::printf ("choir level\n");
        auto p1 = plain(); p1.human = 0.5f; p1.detune = 12.f; p1.spread = 0.7f;
        auto p8 = p1; p8.singers = 8;
        auto o1 = run (44100, 3.0, p1, { { 0.0, true, 57, 1.0f } });
        auto o8 = run (44100, 3.0, p8, { { 0.0, true, 57, 1.0f } });
        save ("choir_1", o1); save ("choir_8", o8);
        const auto s1 = stats (o1, 0.5), s8 = stats (o8, 0.5);
        std::printf ("  1 singer %.1f dBFS, 8 singers %.1f dBFS\n", db (s1.rms), db (s8.rms));
        check (std::fabs (db (s1.rms) - db (s8.rms)) < 4.0, "1 vs 8 singers within 4 dB");
    }

    // 4. Vowel sweep A -> U with a chord: look for clicks.
    {
        std::printf ("vowel morph\n");
        auto p = plain(); p.singers = 4; p.human = 0.5f; p.detune = 12.f; p.spread = 0.7f; p.vibDepth = 20.f;
        auto o = run (44100, 4.0, p,
                      { { 0.0, true, 48, 0.9f }, { 0.0, true, 55, 0.9f }, { 0.0, true, 64, 0.9f } },
                      [] (double t, vox::Params& q) { q.vowel = (float) std::min (4.0, t); });
        save ("morph", o);
        const auto s = stats (o, 0.3);
        std::printf ("  peak %.3f rms %.1f dBFS maxJump %.4f\n", s.peak, db (s.rms), s.maxJump);
        check (s.bad == 0, "no NaN/Inf");
        check (s.maxJump < 0.25, "no large sample-to-sample jumps during morph");
    }

    // 5. Polyphony / voice stealing stress.
    {
        std::printf ("voice stealing stress\n");
        auto p = plain(); p.singers = 8; p.human = 0.7f; p.detune = 20.f; p.spread = 1.f;
        p.attack = 0.02f; p.release = 0.4f; p.sustain = 0.8f; p.vibDepth = 30.f;
        std::vector<Event> ev;
        unsigned r = 99u;
        for (int i = 0; i < 80; ++i)
        {
            r = r * 1664525u + 1013904223u;
            const double t = 0.05 * i;
            const int note = 36 + (int) ((r >> 10) % 48u);
            ev.push_back ({ t, true, note, 0.5f + 0.5f * (float) ((r >> 4) & 0xFF) / 255.f });
            if (i % 3 != 0) ev.push_back ({ t + 0.3 + 0.01 * (double) (r % 20u), false, note, 0.f });
        }
        auto o = run (44100, 7.0, p, ev, nullptr, true);
        save ("stress", o);
        const auto s = stats (o);
        std::printf ("  peak %.3f rms %.1f dBFS maxJump %.4f\n", s.peak, db (s.rms), s.maxJump);
        check (s.bad == 0, "no NaN/Inf");
        check (s.peak < 1.0, "peak below 0 dBFS under stress");
    }

    // 6. Envelope timing: attack 0.5 s, sustain 1, release 0.5 s.
    {
        std::printf ("envelope\n");
        auto p = plain(); p.attack = 0.5f; p.decay = 0.05f; p.sustain = 1.0f; p.release = 0.5f;
        auto o = run (44100, 4.0, p, { { 0.5, true, 57, 1.0f }, { 2.5, false, 57, 0.f } });
        save ("adsr", o);
        check (stats (o).bad == 0, "no NaN/Inf");
    }

    // 7. Loudness vs brightness / breath / pitch.
    {
        std::printf ("loudness map\n");
        for (float t : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            auto p = plain(); p.tilt = t;
            auto o = run (44100, 1.5, p, { { 0.0, true, 52, 1.0f } });
            auto s = stats (o, 0.3);
            std::printf ("  tilt %.2f  rms %.1f dBFS peak %.3f\n", t, db (s.rms), s.peak);
        }
        for (float b : { 0.0f, 0.1f, 0.5f, 1.0f })
        {
            auto p = plain(); p.breath = b;
            auto o = run (44100, 1.5, p, { { 0.0, true, 52, 1.0f } });
            auto s = stats (o, 0.3);
            std::printf ("  breath %.2f  rms %.1f dBFS peak %.3f\n", b, db (s.rms), s.peak);
        }
        for (int n : { 36, 43, 48, 55, 60, 67, 72, 79, 84, 96 })
        {
            auto p = plain();
            auto o = run (44100, 1.5, p, { { 0.0, true, n, 1.0f } });
            auto s = stats (o, 0.3);
            std::printf ("  note %3d  rms %.1f dBFS peak %.3f\n", n, db (s.rms), s.peak);
        }
    }

    // 8. Demo: a chord progression with morphing vowels and reverb.
    {
        std::printf ("demo\n");
        vox::Params p;                              // musical defaults
        p.singers = 5; p.level = 0.5f; p.attack = 0.35f; p.release = 0.9f;
        std::vector<Event> ev;
        auto chord = [&] (double t0, double len, std::vector<int> notes)
        {
            for (int n : notes) { ev.push_back ({ t0, true, n, 0.85f }); ev.push_back ({ t0 + len, false, n, 0.f }); }
        };
        chord (0.0, 3.6, { 48, 55, 60, 64 });       // C
        chord (4.0, 3.6, { 45, 52, 60, 64 });       // Am
        chord (8.0, 3.6, { 41, 48, 57, 60 });       // F
        chord (12.0, 5.0, { 43, 50, 59, 62, 67 });  // G
        auto o = run (44100, 19.0, p, ev,
            [] (double t, vox::Params& q)
            {
                // A -> O -> A -> U -> E -> I
                const double keys[][2] = { { 0.0, 0.0 }, { 3.0, 3.0 }, { 6.0, 0.0 }, { 10.0, 4.0 }, { 14.0, 1.0 }, { 17.0, 2.0 } };
                double v = 0.0;
                for (int i = 0; i < 5; ++i)
                    if (t >= keys[i][0] && t <= keys[i + 1][0])
                        v = keys[i][1] + (keys[i + 1][1] - keys[i][1]) * (t - keys[i][0]) / (keys[i + 1][0] - keys[i][0]);
                if (t > keys[5][0]) v = keys[5][1];
                q.vowel = (float) v;
            },
            true, 0.75f, 0.3f);
        save ("demo", o);
        const auto s = stats (o);
        std::printf ("  peak %.3f rms %.1f dBFS\n", s.peak, db (s.rms));
        check (s.bad == 0, "no NaN/Inf");
        check (s.peak < 1.0, "demo does not clip");
    }

    // 9. Sustain pedal: note released while pedal down must keep sounding until pedal up.
    {
        std::printf ("sustain pedal\n");
        auto p = plain(); p.release = 0.2f;
        std::vector<Event> ev = { { 0.0, true, -64, 0.f }, { 0.1, true, 57, 1.0f }, { 0.6, false, 57, 0.f }, { 2.0, false, -64, 0.f } };
        auto o = run (44100, 3.0, p, ev, nullptr, false, 0.6f, 0.f, true);
        const auto during = stats (o, 1.0, 1.8), after = stats (o, 2.6, 3.0);
        std::printf ("  during pedal %.1f dBFS, after release %.1f dBFS\n", db (during.rms), db (after.rms));
        check (during.rms > 0.01, "note keeps sounding while pedal is held");
        check (after.rms < during.rms * 0.01, "note dies after pedal released");
    }

    // 10. Aliasing probe: bright high note (energy between harmonics = aliasing).
    {
        std::printf ("alias probe\n");
        auto p = plain(); p.tilt = 1.0f; p.vowel = 0.f;
        auto o = run (44100, 2.0, p, { { 0.0, true, 88, 1.0f } });
        save ("alias", o);
        check (stats (o).bad == 0, "no NaN/Inf");
    }

    std::printf ("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL CHECKS PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
