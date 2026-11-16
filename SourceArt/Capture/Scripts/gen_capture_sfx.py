"""Synthesizes the capture-success SFX (AKI-137): bright retro / toy-gadget tones, 16-bit mono WAVs. Deterministic.
Run with any Python 3 (e.g. blender -b --python gen_capture_sfx.py).  Output: SourceArt/Capture/Audio/*.wav

SFX_Capture_Impact   0.16 s  net hit "pon!": square chirp falling + short click (UCapturePresentationComponent.ImpactSound)
SFX_Capture_Transfer 0.42 s  data upload: rising square arpeggio with a sweep          (TransferStartSound)
SFX_Capture_Success  0.40 s  two-note chime, transfer complete                          (CaptureSuccessSound)
SFX_Capture_Multi    0.34 s  quick bright 4-note run, only for multi capture            (MultiCaptureSound)"""
import math, os, random, struct, wave

SR = 44100
OUT = 'C:/UnrealGames/REBoarGetch/SourceArt/Capture/Audio/'
os.makedirs(OUT, exist_ok=True)


def write(name, samples, peak):
    m = max(1e-9, max(abs(s) for s in samples))
    data = b''.join(struct.pack('<h', int(max(-1, min(1, s / m * peak)) * 32767)) for s in samples)
    with wave.open(OUT + name + '.wav', 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR); w.writeframes(data)
    print('wrote', name, round(len(samples) / SR, 3), 's')


def lowpass(x, cutoff):
    a = 1 - math.exp(-2 * math.pi * cutoff / SR)
    y, s = [], 0.0
    for v in x:
        s += a * (v - s); y.append(s)
    return y


def crush(x, bits=7, hold=2):
    q = 2 ** (bits - 1)
    y, last = [], 0.0
    for i, v in enumerate(x):
        if i % hold == 0:
            last = round(v * q) / q
        y.append(last)
    return y


def fade(x, ms_in=1.5, ms_out=6):
    n = int(SR * ms_in / 1000)
    for i in range(min(n, len(x))):
        x[i] *= i / n
    n = int(SR * ms_out / 1000)
    for i in range(min(n, len(x))):
        x[-1 - i] *= i / n
    return x


def square(phase, duty=.5):
    return 1.0 if (phase % 1.0) < duty else -1.0


def tri(phase):
    p = phase % 1.0
    return 4 * p - 1 if p < .5 else 3 - 4 * p


def note(midi):
    return 440.0 * 2 ** ((midi - 69) / 12.0)


# 1) Impact: falling square chirp (toy "pon") + sine body + 8 ms noise click.
rnd = random.Random(11)
n = int(SR * .16)
ph = ph2 = 0.0; x = []
for i in range(n):
    t = i / SR
    f = 520 + 1100 * math.exp(-t * 38)
    ph += f / SR
    ph2 += (180 + 140 * math.exp(-t * 30)) / SR
    env = math.exp(-t * 26)
    v = .55 * square(ph, .3) * env + .6 * math.sin(2 * math.pi * ph2) * math.exp(-t * 18)
    if t < .008:
        v += (rnd.random() * 2 - 1) * (1 - t / .008) * .8
    x.append(v)
write('SFX_Capture_Impact', fade(crush(lowpass(x, 7000), 7, 2)), .72)

# 2) Transfer: rising arpeggio (C6 E6 G6 C7 E7) over a sweep, slight vibrato, fades out.
n = int(SR * .42)
steps = [84, 88, 91, 96, 100]
ph = phs = 0.0; x = []
for i in range(n):
    t = i / SR
    k = min(len(steps) - 1, int(t / .055))
    f = note(steps[k]) * (1 + .01 * math.sin(2 * math.pi * 24 * t))
    ph += f / SR
    phs += (300 + 2400 * (t / .42) ** 1.6) / SR
    env = min(1.0, t / .01) * (1 - t / .42) ** 1.2
    x.append((.45 * square(ph, .25) + .3 * tri(phs)) * env)
write('SFX_Capture_Transfer', fade(crush(lowpass(x, 9000), 7, 2)), .55)

# 3) Success: two-note chime E6 -> B6 (triangle + soft square), bell-like decay.
n = int(SR * .40)
x = []
for i in range(n):
    t = i / SR
    v = 0.0
    for start, m, amp in ((0.0, 88, .8), (.075, 95, 1.0)):
        if t >= start:
            u = t - start
            f = note(m)
            env = min(1.0, u / .004) * math.exp(-u * 9)
            v += amp * env * (.7 * tri(f * u) + .25 * square(f * u, .5) + .2 * math.sin(2 * math.pi * 2 * f * u))
    x.append(v)
write('SFX_Capture_Success', fade(crush(lowpass(x, 10000), 8, 2)), .65)

# 4) Multi: quick bright 4-note run G6 C7 E7 G7, sparkly, only for multi capture.
n = int(SR * .34)
seq = [91, 96, 100, 103]
x = []
for i in range(n):
    t = i / SR
    v = 0.0
    for k, m in enumerate(seq):
        start = k * .045
        if t >= start:
            u = t - start
            f = note(m)
            env = min(1.0, u / .003) * math.exp(-u * (14 if k < 3 else 8))
            v += env * (.6 * square(f * u, .125) + .5 * tri(f * u))
    x.append(v)
write('SFX_Capture_Multi', fade(crush(lowpass(x, 10000), 7, 2)), .5)
