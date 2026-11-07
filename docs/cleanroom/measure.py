#!/usr/bin/env python3
"""Measure the clean-room plugins as black boxes, and check them.

    python3 -I docs/cleanroom/measure.py --check docs/cleanroom/targets.json
    python3 -I docs/cleanroom/measure.py --record OUT.json

Each measurement renders a one-node graph through the tree's own genwav: white
noise (or a sine) into the node, the node's output on the left channel and the
dry signal on the right, so the node's response is the right channel's
transfer to the left. Nothing here knows how a node is built; it only plays
signals into it and listens.

Needs numpy, and a build: THINK_BUILD (default build/ at the top of the tree).
"""

import json
import os
import subprocess
import sys
import tempfile
import wave

import numpy as np

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
BUILD = os.environ.get('THINK_BUILD', os.path.join(REPO, 'build'))
GENWAV = os.path.join(BUILD, 'scripts', 'genwav')
PLUGINS = os.path.join(BUILD, 'plugins')


def render(node, args, out, src='noise', amp=0.1, secs=4.0, freq=None):
    with tempfile.TemporaryDirectory() as tmp:
        d, g, w = (os.path.join(tmp, 'm.' + e) for e in ('dsp', 'gen', 'wav'))
        source = (f'node s osc::noise {{ color = 0; amp = {amp}; }};' if src == 'noise'
                  else f'node s osc::simple {{ freq = {freq}; waveform = 0; amp = {amp}; }};')
        with open(d, 'w') as f:
            f.write(f'name "m";\n'
                    f'node ionode {{ channels = 2; out0 = f->{out}; out1 = s->out; play = e->play; }};\n'
                    f'node e env::ad {{ a = 0; d = {secs + 1} s; }};\n{source}\n'
                    f'node f {node} {{ in = s->out; {args} }};\nio ionode;\n')
        with open(g, 'w') as f:
            f.write(f'tempo 60;\nseed 1;\ninstrument x {{ dsp "{d}"; amp = 127; }};\n'
                    f'chain c {{ stage n gen::euclid {{ steps = 1; fills = 1; notes = "C4"; '
                    f'period = 1000 s; hold = {secs} s; vel = 127; }}; sink {{ instrument = x; }}; }};\n')
        r = subprocess.run([GENWAV, '-p', PLUGINS, '-q', '-s', str(secs), '-o', w, g], cwd=REPO)
        if r.returncode not in (0, 3):          # 3 is "clipped", still a render
            raise RuntimeError(f'genwav failed ({r.returncode}) for {node} {args}')
        wv = wave.open(w)
        a = np.frombuffer(wv.readframes(wv.getnframes()), dtype=np.int16).astype(float).reshape(-1, 2) / 32768
        sr = wv.getframerate()
    a = a[int(0.2 * sr):int((secs - 0.1) * sr)]
    return a[:, 0], a[:, 1], sr


def transfer(y, x, sr, n=8192):
    win = np.hanning(n)
    pxy = pxx = 0
    for i in range(0, len(x) - n, n // 2):
        X = np.fft.rfft(x[i:i + n] * win)
        Y = np.fft.rfft(y[i:i + n] * win)
        pxy = pxy + Y * np.conj(X)
        pxx = pxx + np.abs(X) ** 2
    return np.fft.rfftfreq(n, 1 / sr), pxy / pxx


def response(node, args, out, amp=0.1):
    y, x, sr = render(node, args, out, amp=amp)
    fr, H = transfer(y, x, sr)
    band = (fr > 15) & (fr < 21000)
    return fr[band], 20 * np.log10(np.abs(H[band]) + 1e-12), H[band]


def corner(f, m):
    """First frequency 3 dB under the response at the bottom of the band."""
    i = np.where(m < m[0] - 3)[0]
    return round(float(f[i[0]])) if len(i) else None


def peak(f, m):
    i = int(np.argmax(m))
    return {'hz': round(float(f[i])), 'db': round(float(m[i]), 1), 'lowDb': round(float(m[0]), 1)}


def levels(f, m):
    return {hz: round(float(np.interp(hz, f, m)), 1) for hz in (100, 300, 1000, 3000, 6000, 10000)}


def sine(node, args, out, amp, freq):
    y, x, sr = render(node, args, out, src='sine', amp=amp, freq=freq)
    n = len(y) - len(y) % 4096
    Y = np.abs(np.fft.rfft(y[:n] * np.hanning(n)))
    fr = np.fft.rfftfreq(n, 1 / sr)
    h = lambda k: Y[np.argmin(np.abs(fr - k * freq))]
    return {'outPeak': round(float(np.max(np.abs(y))), 3),
            'h3Db': round(float(20 * np.log10(h(3) / h(1) + 1e-12)), 1)}


def curve(gain):
    """waveman: output at each input level, read off a slow rising sine."""
    y, x, sr = render('dist::waveman', f'gain = {gain};', 'out', src='sine', amp=1.0, freq=5)
    rising = np.gradient(x) >= 0
    return {t: round(float(y[np.argmin(np.abs(x - t) + (~rising) * 9)]), 4)
            for t in (0.05, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9)}


def allpass90(freq):
    f, m, H = response('filt::allpass', f'freq = {freq};', 'out')
    ph = np.unwrap(np.angle(H))
    i = np.where(ph < -np.pi / 2)[0]
    return round(float(f[i[0]])) if len(i) else None


C = (0.02, 0.05, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8)
Q = (0, 0.2, 0.4, 0.6, 0.8, 0.9, 0.95, 0.99)


def measure():
    R = {}
    R['moog: cutoff -> corner Hz (res 0, out_low)'] = {c: corner(*response('filt::moog', f'cutoff = {c}; res = 0;', 'out_low')[:2]) for c in C}
    R['moog: cutoffhz -> corner Hz (res 0, out_low)'] = {h: corner(*response('filt::moog', f'cutoffhz = {h}; res = 0;', 'out_low')[:2]) for h in (100, 300, 1000, 3000, 8000, 15000)}
    R['moog: res -> peak (cutoff 0.3, out_low)'] = {q: peak(*response('filt::moog', f'cutoff = 0.3; res = {q};', 'out_low')[:2]) for q in Q}
    for o in ('out_high', 'out_bandpass'):
        R[f'moog: levels dB (cutoff 0.3, res 0.5, {o})'] = levels(*response('filt::moog', 'cutoff = 0.3; res = 0.5;', o)[:2])
    R['moog: sine 220 Hz at amp (cutoff 0.6, res 0.9, out_low)'] = {a: sine('filt::moog', 'cutoff = 0.6; res = 0.9;', 'out_low', a, 220) for a in (0.25, 0.5, 1.0)}
    R['res1pole: cutoff -> corner Hz (res 0)'] = {c: corner(*response('filt::res1pole', f'cutoff = {c}; res = 0;', 'out')[:2]) for c in C}
    R['res1pole: res -> peak (cutoff 0.2)'] = {q: peak(*response('filt::res1pole', f'cutoff = 0.2; res = {q};', 'out')[:2]) for q in Q}
    R['ink2: cutoff -> peak (res 0.6)'] = {c: peak(*response('filt::ink2', f'cutoff = {c}; res = 0.6;', 'out')[:2]) for c in C}
    R['ink2: res -> peak (cutoff 0.3)'] = {q: peak(*response('filt::ink2', f'cutoff = 0.3; res = {q};', 'out')[:2]) for q in Q}
    R['res2pole: res -> peak (cutoff 1000 Hz, out)'] = {q: peak(*response('filt::res2pole', f'cutoff = 1000; res = {q};', 'out')[:2]) for q in (0.5, 0.7, 1, 2, 4, 8, 16)}
    R['res2pole: cutoff -> peak (res 8, out)'] = {c: peak(*response('filt::res2pole', f'cutoff = {c}; res = 8;', 'out')[:2]) for c in (100, 300, 1000, 3000, 6000)}
    for o in ('out_high', 'out_band', 'out_notch'):
        R[f'res2pole: levels dB (cutoff 1000 Hz, res 2, {o})'] = levels(*response('filt::res2pole', 'cutoff = 1000; res = 2;', o)[:2])
    R['allpass: freq -> Hz where the phase passes -90 deg'] = {f: allpass90(f) for f in (50, 200, 1000, 3000, 6000)}
    R['resonator: freq -> peak (fb 0.9)'] = {f: peak(*response('filt::resonator', f'freq = {f}; fb = 0.9;', 'out', amp=0.002)[:2]) for f in (50, 110, 220, 440, 880, 1760, 3000, 6000)}
    R['resonator: fb -> peak (freq 440)'] = {b: peak(*response('filt::resonator', f'freq = 440; fb = {b};', 'out', amp=0.002)[:2]) for b in (0, 0.3, 0.5, 0.7, 0.9, 0.95, 0.99)}
    R['waveman: input level -> output (gain g)'] = {g: curve(g) for g in (0.001, 0.25, 0.5, 1, 2, 4, 8)}
    return json.loads(json.dumps(R))        # keys as strings, as a file has them


def close(kind, want, got):
    """Whether one measured value is within tolerance of its target."""
    if want is None or got is None:
        return want == got
    if kind in ('hz', 'corner'):
        return abs(got - want) <= 0.05 * want + 20          # 5 %, and a bin
    # A peak 25 dB up is a resonance near self-oscillation, whose height
    # turns on very small differences; within 5 dB is the same sound.
    if kind == 'db':
        return abs(got - want) <= (1.5 if want <= 25 else 5)
    if kind in ('lowDb', 'level'):
        return abs(got - want) <= 1.5 + 0.25 * abs(want) if want > -40 else got < -30
    if kind == 'outPeak':
        return abs(got - want) <= 0.1 * want
    if kind == 'h3Db':
        return abs(got - want) <= 6 if want > -70 else got < -60
    if kind == 'curve':
        return abs(got - want) <= 0.03 * abs(want) + 0.002
    raise ValueError(kind)


def check(targets, measured):
    failures = total = 0
    for name, rows in targets.items():
        for key, want in rows.items():
            got = measured[name][key]
            if isinstance(want, dict):
                for field, w in want.items():
                    kind = field if field in ('hz', 'db', 'lowDb', 'outPeak', 'h3Db') else (
                        'curve' if name.startswith('waveman') else 'level')
                    ok = close(kind, w, got[field])
                    total += 1
                    failures += not ok
                    if not ok:
                        print(f'FAIL  {name} [{key}] {field}: want {w}, got {got[field]}')
            else:
                ok = close('corner' if 'corner' in name or 'allpass' in name else 'hz', want, got)
                total += 1
                failures += not ok
                if not ok:
                    print(f'FAIL  {name} [{key}]: want {want}, got {got}')
    print(f'{total - failures} of {total} within tolerance')
    return failures == 0


if __name__ == '__main__':
    if len(sys.argv) != 3 or sys.argv[1] not in ('--record', '--check'):
        sys.exit(__doc__)
    measured = measure()
    if sys.argv[1] == '--record':
        with open(sys.argv[2], 'w') as f:
            json.dump(measured, f, indent=1)
    else:
        with open(sys.argv[2]) as f:
            sys.exit(0 if check(json.load(f), measured) else 1)
