"""Synthesizes the GameOver SFX (retro / cyber facility tone) as 16-bit mono WAVs. Deterministic (fixed seeds).
Run with any Python 3 (e.g. blender -b --python gen_sfx.py).  Output: SourceArt/GameOver/Audio/*.wav"""
import math, os, random, struct, wave

SR = 44100
OUT = 'C:/UnrealGames/REBoarGetch/SourceArt/GameOver/Audio/'
os.makedirs(OUT, exist_ok=True)


def write(name, samples, peak):
    m = max(1e-9, max(abs(s) for s in samples))
    data = b''.join(struct.pack('<h', int(max(-1, min(1, s / m * peak)) * 32767)) for s in samples)
    with wave.open(OUT + name + '.wav', 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR); w.writeframes(data)
    print('wrote', name, round(len(samples) / SR, 2), 's')


def lowpass(x, cutoff):
    a = 1 - math.exp(-2 * math.pi * cutoff / SR)
    y, s = [], 0.0
    for v in x:
        s += a * (v - s); y.append(s)
    return y


def crush(x, bits=6, hold=3):
    q = 2 ** (bits - 1)
    y, last = [], 0.0
    for i, v in enumerate(x):
        if i % hold == 0:
            last = round(v * q) / q
        y.append(last)
    return y


def fade(x, ms=4):
    n = int(SR * ms / 1000)
    for i in range(min(n, len(x))):
        x[-1 - i] *= i / n
    return x


def square(phase, duty=.5):
    return 1.0 if (phase % 1.0) < duty else -1.0


def tri(phase):
    p = phase % 1.0
    return 4 * p - 1 if p < .5 else 3 - 4 * p


# 1) Heavy low impact: pitch-dropping sine body + short filtered noise crack + a bit-crushed square layer.
rnd = random.Random(7)
n = int(SR * .75)
ph = 0.0; body = []
for i in range(n):
    t = i / SR
    f = 38 + 85 * math.exp(-t * 9)
    ph += f / SR
    body.append(math.sin(2 * math.pi * ph) * math.exp(-t * 4.2) + .35 * math.sin(4 * math.pi * ph) * math.exp(-t * 9))
crack = lowpass([rnd.uniform(-1, 1) * math.exp(-i / SR * 55) for i in range(n)], 1800)
sq = [square(110 * i / SR - 40 * (i / SR) ** 2, .3) * math.exp(-i / SR * 14) * .25 for i in range(n)]
write('SFX_GameOver_Impact', fade(crush([b + .9 * c + s for b, c, s in zip(body, crack, sq)], 8, 2)), .92)

# 2) Short digital noise burst: sample&hold noise with stepping rate + high square chirps.
rnd = random.Random(11)
n = int(SR * .2)
x, hold, val = [], 1, 0.0
for i in range(n):
    if i % 220 == 0:
        hold = rnd.choice([2, 3, 5, 8, 13])
    if i % hold == 0:
        val = rnd.uniform(-1, 1)
    t = i / SR
    chirp = square((2400 + 900 * math.sin(t * 90)) * t, .25) * .3 if int(t * 40) % 2 == 0 else 0.0
    env = min(1, t / .004) * (1 - t / .2) ** 1.5
    x.append((val * .7 + chirp) * env)
write('SFX_GameOver_Noise', fade(crush(x, 5, 1)), .5)

# 3) System power-down: exponential downward sweep with tremolo, square+saw blend.
n = int(SR * 1.0)
ph = 0.0; x = []
for i in range(n):
    t = i / SR
    f = 80 + 820 * math.exp(-t * 3.4)
    ph += f / SR
    saw = 2 * (ph % 1.0) - 1
    trem = .75 + .25 * math.sin(2 * math.pi * (14 - 8 * t) * t)
    env = min(1, t / .01) * (1 - t) ** 1.2
    x.append((.55 * square(ph, .5) + .45 * saw) * trem * env)
write('SFX_GameOver_PowerDown', fade(crush(lowpass(x, 3200), 7, 2)), .55)

# 4) Body falls on the ground: soft low thud + short filtered noise (cloth / dirt).
rnd = random.Random(3)
n = int(SR * .4)
ph = 0.0; x = []
for i in range(n):
    t = i / SR
    ph += (45 + 40 * math.exp(-t * 18)) / SR
    x.append(math.sin(2 * math.pi * ph) * math.exp(-t * 11))
dirt = lowpass([rnd.uniform(-1, 1) * math.exp(-i / SR * 30) for i in range(n)], 900)
write('SFX_GameOver_BodyFall', fade([a + .6 * b for a, b in zip(x, dirt)]), .75)

# 5) Short defeat jingle: bright retro "wah-wah-wah-waaah" (square lead + triangle bass, light echo). Not too gloomy.
def midi(m):
    return 440 * 2 ** ((m - 69) / 12)

notes = [(71, 59, .0, .17), (70, 58, .19, .17), (69, 57, .38, .17), (68, 56, .57, .78)]   # B4 Bb4 A4 Ab4 / bass an octave below
n = int(SR * 1.55)
x = [0.0] * n
for lead, bass, start, dur in notes:
    s0 = int(start * SR)
    last = dur > .5
    ph1 = ph2 = 0.0
    for i in range(int(dur * SR)):
        t = i / SR
        vib = 1 + (.012 * math.sin(2 * math.pi * 6 * t) * min(1, t / .2) if last else 0)
        ph1 += midi(lead) * vib / SR
        ph2 += midi(bass) / SR
        env = min(1, t / .006) * (math.exp(-t * 1.6) if last else (1 - t / dur) ** .4)
        if s0 + i < n:
            x[s0 + i] += (.42 * square(ph1, .25) + .5 * tri(ph2)) * env
echo = list(x)
d = int(SR * .11)
for i in range(d, n):
    echo[i] += .28 * echo[i - d]
write('SFX_GameOver_Jingle', fade(crush(lowpass(echo, 5200), 8, 1), 30), .7)
