#!/usr/bin/env python3
"""Spectral / timing analysis of the files written by dsp_test.  usage: analyze.py <outdir>"""
import sys
import numpy as np

d = sys.argv[1] if len(sys.argv) > 1 else "."
fails = 0


def load(name):
    x = np.fromfile(f"{d}/{name}.f32", dtype=np.float32).reshape(-1, 2)
    return x[:, 0].astype(np.float64), x[:, 1].astype(np.float64)


def check(ok, msg):
    global fails
    print(f"  [{' ok ' if ok else 'FAIL'}] {msg}")
    fails += 0 if ok else 1


def spectrum(x, sr, t0, t1, nfft=1 << 17):
    seg = x[int(t0 * sr):int(t1 * sr)]
    seg = seg[:nfft] * np.hanning(min(len(seg), nfft))
    sp = np.abs(np.fft.rfft(seg, nfft))
    fr = np.fft.rfftfreq(nfft, 1.0 / sr)
    return fr, sp


def harmonics(x, sr, f0, t0, t1, count=40):
    fr, sp = spectrum(x, sr, t0, t1)
    out = []
    for k in range(1, count + 1):
        m = (fr > k * f0 - 3) & (fr < k * f0 + 3)
        out.append(sp[m].max() if m.any() else 0.0)
    return np.array(out)


# --- 1. formant peaks per vowel (A2 = 110 Hz) ---------------------------------------------------
print("formant structure (110 Hz source)")
sr, f0 = 44100, 110.0
expect = {  # vowel: (F1 band, F2 band or None)
    "A": ((500, 800), None),
    "E": ((330, 520), (1400, 2000)),
    "I": ((220, 440), (1600, 2200)),
    "O": ((330, 520), None),
    "U": ((220, 460), None),
}
cent = {}
for v, (b1, b2) in expect.items():
    l, _ = load(f"vowel_{v}")
    h = harmonics(l, sr, f0, 0.5, 1.9)
    freqs = f0 * np.arange(1, len(h) + 1)
    hdb = 20 * np.log10(h / h.max() + 1e-9)
    m1 = (freqs >= 150) & (freqs <= 1000)
    f1 = freqs[m1][np.argmax(h[m1])]
    msg = f"vowel {v}: strongest low harmonic {f1:.0f} Hz"
    ok = b1[0] <= f1 <= b1[1]
    if b2:
        m2 = (freqs >= 1100) & (freqs <= 3000)
        f2 = freqs[m2][np.argmax(h[m2])]
        msg += f", F2 region peak {f2:.0f} Hz"
        ok = ok and b2[0] <= f2 <= b2[1]
    check(ok, msg)
    fr, sp = spectrum(l, sr, 0.5, 1.9)
    p = sp ** 2
    cent[v] = (p[(fr > 1200) & (fr < 2400)].sum() / p.sum(),   # share in the F2 region of E and I
               p[fr > 1200].sum() / p.sum())                    # share above 1.2 kHz
f2share = {k: 10 * np.log10(c[0]) for k, c in cent.items()}
hishare = {k: 10 * np.log10(c[1]) for k, c in cent.items()}
print("  energy in 1.2-2.4 kHz (dB re total):", {k: round(v, 1) for k, v in f2share.items()})
print("  energy above 1.2 kHz  (dB re total):", {k: round(v, 1) for k, v in hishare.items()})
check(min(f2share["E"], f2share["I"]) > max(f2share["A"], f2share["O"], f2share["U"]),
      "E and I carry the most energy in the 1.2-2.4 kHz F2 region")
check(hishare["U"] == min(hishare.values()), "U is the darkest vowel overall")

# --- 2. pitch accuracy ---------------------------------------------------------------------------
print("pitch accuracy (A4 = 440 Hz)")
for sr_ in (44100, 48000, 96000):
    l, _ = load(f"pitch_{sr_}")
    fr, sp = spectrum(l, sr_, 0.3, 1.4, nfft=1 << 18)
    m = (fr > 400) & (fr < 480)
    i = np.argmax(sp * m)
    # parabolic interpolation
    a, b, c = np.log(sp[i - 1:i + 2] + 1e-12)
    delta = 0.5 * (a - c) / (a - 2 * b + c)
    f = fr[i] + delta * (fr[1] - fr[0])
    check(abs(f - 440.0) < 0.3, f"{sr_} Hz: measured {f:.3f} Hz (cents error {1200*np.log2(f/440):+.2f})")

# --- 3. envelope timing -------------------------------------------------------------------------
print("envelope")
sr = 44100
l, _ = load("adsr")
win = int(0.01 * sr)
rms = np.sqrt(np.convolve(l ** 2, np.ones(win) / win, mode="same"))
plateau = rms[int(2.0 * sr):int(2.4 * sr)].mean()
t90 = np.argmax(rms > 0.9 * plateau) / sr - 0.5
print(f"  plateau {20*np.log10(plateau):.1f} dBFS, time to 90% after note-on: {t90:.3f} s (attack set to 0.5 s)")
check(0.30 < t90 < 0.55, "attack time about right")
check(rms[:int(0.45 * sr)].max() < 1e-6, "silent before note-on")
t_rel = 2.5 + 0.5
level_after = rms[int(t_rel * sr)] / plateau
print(f"  level 0.5 s after note-off: {20*np.log10(level_after):.1f} dB (release set to 0.5 s)")
check(-70 < 20 * np.log10(level_after) < -50, "release reaches about -60 dB at the release time")
check(rms[int(3.9 * sr)] < 1e-4 * plateau, "voice fully silent at the end")

# --- 4. stereo image and DC ---------------------------------------------------------------------
print("stereo / DC")
for n in ("choir_1", "choir_8", "demo"):
    l, r = load(n)
    seg = slice(int(0.5 * 44100), None)
    cc = np.corrcoef(l[seg], r[seg])[0, 1]
    print(f"  {n}: L/R correlation {cc:.3f}, DC L {l.mean():+.5f}")
    check(abs(l.mean()) < 5e-3, f"{n}: negligible DC offset")
l, r = load("choir_8")
check(np.corrcoef(l[22050:], r[22050:])[0, 1] < 0.95, "8 singers are spread across the stereo field")
l, r = load("vowel_A")
check(np.allclose(l, r), "1 centered singer is identical in L and R")

# --- 5. aliasing --------------------------------------------------------------------------------
print("aliasing (note 88, brightest tilt)")
sr = 44100
l, _ = load("alias")
fr, sp = spectrum(l, sr, 0.3, 1.9, nfft=1 << 17)
f0 = 440 * 2 ** ((88 - 69) / 12)
mask = np.zeros_like(fr, dtype=bool)
for k in range(1, int(sr / 2 / f0) + 1):
    mask |= np.abs(fr - k * f0) < 30
total = (sp ** 2).sum()
non_harm = (sp[~mask] ** 2).sum()
ratio_db = 10 * np.log10(non_harm / total)
print(f"  energy between harmonics: {ratio_db:.1f} dB below total")
check(ratio_db < -45, "aliasing / inharmonic energy more than 45 dB down")

# --- 6. demo summary ---------------------------------------------------------------------------
l, r = load("demo")
print(f"demo: {len(l)/44100:.1f} s, peak {max(abs(l).max(), abs(r).max()):.3f}")

print("\nANALYSIS", "FAILED" if fails else "PASSED", f"({fails} failures)")
sys.exit(1 if fails else 0)
