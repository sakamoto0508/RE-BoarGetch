"""AKI-166 original synthesis, 48 kHz PCM. No samples or third-party recordings."""
from pathlib import Path
import sys
import numpy as np
import soundfile as sf

SR = 48000
TAU = 2 * np.pi
rng = np.random.default_rng(166)
out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)

def time(seconds):
    return np.arange(round(seconds * SR)) / SR

def sweep(hz):
    return TAU * np.cumsum(hz) / SR

def click_free(t, attack, release):
    return np.minimum(t / attack, 1) * np.clip((t[-1] - t) / release, 0, 1)

def noise_band(t, low, high):
    f = np.fft.rfftfreq(len(t), 1 / SR)
    shape = (1 - np.exp(-(f / low) ** 4)) * np.exp(-(f / high) ** 4)
    y = np.fft.irfft(np.fft.rfft(rng.standard_normal(len(t))) * shape, n=len(t))
    return y / np.std(y)

def save(name, x, peak_db):
    x *= 10 ** (peak_db / 20) / np.max(np.abs(x))
    file = out / (name + '.wav')
    sf.write(file, x, SR, subtype='PCM_24')
    y, rate = sf.read(file)
    assert rate == SR and np.isfinite(y).all() and np.max(np.abs(y)) < .9
    print(name, len(y)/SR, 's peak', round(20*np.log10(np.max(np.abs(y))), 2), 'dBFS')

t = time(.155)
body = .73 * np.sin(sweep(73 + 99*np.exp(-t*48))) * np.exp(-t*38)
grab = .31 * np.sin(sweep(625 + 690*np.exp(-t*67)) + .55*np.exp(-t*56)*np.sin(TAU*830*t)) * np.exp(-t*43)
snap = noise_band(t, 780, 4600) * (.23*np.exp(-t*175) + .04*np.exp(-t*35))
save('SFX_CaptureV2_Impact', (body + grab + snap)*click_free(t,.001,.02), -6)

# All partials and modulation cycles close exactly over the sample.
# Runtime phase progress opens the filter and increases pitch.
t = time(1)
turn = TAU*t
rot = .68 + .23*np.cos(4*turn + .4*np.sin(2*turn)) + .09*np.cos(7*turn)
core = np.sin(168*turn + .37*np.sin(3*turn))
upper = .53*np.sin(337*turn + .84*np.sin(6*turn)) + .24*np.sin(507*turn + 1.4*np.sin(5*turn))
grains = (.025 + .058*(.5+.5*np.cos(17*turn))**6)*(np.sin(1321*turn) + .59*np.sin(1913*turn) + .37*np.sin(2761*turn))
save('SFX_CaptureV2_Spiral', rot*(core*.55 + upper) + grains, -14)

def bell(t, hz, decay, shimmer=.17):
    return np.exp(-t*decay) * (np.sin(TAU*hz*t + .22*np.exp(-t*26)*np.sin(TAU*hz*1.5*t))
        + shimmer*np.exp(-t*11)*np.sin(TAU*hz*2.76*t)) * np.minimum(t/.0025,1)

def chord(seconds, events, tail):
    t = time(seconds)
    x = np.zeros_like(t)
    for start,hz,decay,level in events:
        i = round(start*SR)
        x[i:] += level*bell(t[:len(t)-i], hz, decay)
    return x*click_free(t,.001,tail)

save('SFX_CaptureV2_Confirm', chord(.32,[(0,523.25,17,.64),(.033,783.99,14,.46),
    (.056,1046.5,13,.36),(.069,1318.51,20,.15),(0,261.63,25,.27)],.048), -8)
save('SFX_CaptureV2_Multi', chord(.41,[(0,392,16,.61),(0,523.25,17,.4),(.023,659.25,13,.35),
    (.047,783.99,14,.43),(.063,1046.5,12,.34),(.09,1318.51,15,.22),
    (.113,1567.98,17,.15),(0,196,24,.3)],.056), -8)

