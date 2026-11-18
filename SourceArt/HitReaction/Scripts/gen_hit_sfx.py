"""AKI-163: synthesizes the Red Boar hit / stun SFX (retro / cyber facility tone, same toolkit as GameOver/Scripts/gen_sfx.py)
as 16-bit mono WAVs. Deterministic (fixed seeds).
Run with any Python 3, e.g. "C:/Program Files/Blender Foundation/Blender 5.2/5.2/python/bin/python.exe" gen_hit_sfx.py
Output: SourceArt/HitReaction/Audio/*.wav  (imported by Scripts/HitReactionSetupAssets.py)"""
import math, os, random, struct, wave

SR = 44100
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'Audio') + os.sep
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


def highpass(x, cutoff):
    lp = lowpass(x, cutoff)
    return [a - b for a, b in zip(x, lp)]


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


# 1) Heavy hit: the boar's body slams the player.
#    Low pitch-dropping "thump" body (punchy, longer than a footstep) + bright cracked transient (the impact edge)
#    + a bit-crushed square sub punch so it survives small speakers, + a short metallic ring (the facility suit) for character.
rnd = random.Random(163)
n = int(SR * .62)
ph = 0.0; body = []
for i in range(n):
    t = i / SR
    f = 42 + 118 * math.exp(-t * 16)          # 160 Hz -> 42 Hz drop
    ph += f / SR
    env = min(1, t / .002) * math.exp(-t * 6.0)
    body.append((math.sin(2 * math.pi * ph) + .3 * math.sin(4 * math.pi * ph)) * env)
crack = highpass(lowpass([rnd.uniform(-1, 1) * math.exp(-i / SR * 70) for i in range(n)], 5200), 600)
sub = [square(55 * i / SR - 30 * (i / SR) ** 2, .35) * math.exp(-i / SR * 11) * .28 for i in range(n)]
ring = [(.6 * math.sin(2 * math.pi * 1310 * i / SR) + .4 * math.sin(2 * math.pi * 1870 * i / SR)) * math.exp(-i / SR * 22) * .14
        for i in range(n)]
mix = [b + 1.1 * c + s + r for b, c, s, r in zip(body, crack, sub, ring)]
write('SFX_PlayerHit_Impact', fade(crush(mix, 8, 2), 12), .95)

# 2) Dizzy / stunned: comical retro "tweet-tweet" birds circling the head, slightly descending and wobbly.
#    Two alternating high square/triangle notes with vibrato and a soft echo. Short (does not loop) so it never nags.
n = int(SR * 1.05)
x = [0.0] * n
chirps = [(.00, 2093, 1760), (.13, 1976, 1661), (.30, 2093, 1760), (.43, 1865, 1568), (.62, 1976, 1661), (.75, 1760, 1480)]
for start, f0, f1 in chirps:
    s0 = int(start * SR)
    dur = .11
    p1 = p2 = 0.0
    for i in range(int(dur * SR)):
        t = i / SR
        k = t / dur
        f = f0 + (f1 - f0) * k                    # each chirp slides down a little
        vib = 1 + .035 * math.sin(2 * math.pi * 24 * t)
        p1 += f * vib / SR
        p2 += f * .5 * vib / SR
        env = min(1, t / .004) * (1 - k) ** 1.4
        if s0 + i < n:
            x[s0 + i] += (.45 * square(p1, .25) + .4 * tri(p2)) * env
echo = list(x)
d = int(SR * .09)
for i in range(d, n):
    echo[i] += .3 * echo[i - d]
write('SFX_PlayerStun_Dizzy', fade(crush(lowpass(echo, 6500), 7, 1), 40), .55)
