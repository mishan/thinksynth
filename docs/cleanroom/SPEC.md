# Clean-room specification: seven DSP plugins and one FFT

This document is everything the implementer of this work is given about what
to build. It describes **behavior**: each plugin's interface and how it
responds, measured from the outside. It does not describe, and was written so
as not to reveal, how any earlier implementation computed anything.

The work replaces code whose origin is uncertain (snippets from a public code
archive and from an IRC channel, and an FFT from a textbook) with code written
from published literature and these measurements alone.

## The rules

1. **Do not look at the earlier implementations**, in any form: not in this
   repository's history (this checkout has none, and must not be given any:
   no `git fetch`, no other remotes or branches), not on the web, not in other
   projects. That includes musicdsp.org and its mirrors, any copy of its
   snippets, the code in Embree & Kimble's *C Language Algorithms for Digital
   Signal Processing*, and anything that quotes them.
2. **You may use** published papers and books, standard DSP texts and course
   notes, and your own knowledge of DSP. Name every source you consult, with
   what you took from it, in `docs/cleanroom/LOG.md` as you go.
3. **Fit the measurements, not a guess at the original.** Where a measured
   behavior is unusual (a scale that is not hertz, a resonance that is not at
   `freq`), match the measurement. Where matching needs a mapping, derive or
   fit your own and say how in the log.
4. **Everything else in the tree is yours to read and reuse.** The plugin API,
   other plugins (`plugins/filt/svf.cpp` is a good model), the tests and the
   docs are the project's own.

## What to build

| Item | File to create | Interface |
| --- | --- | --- |
| `filt::moog` | `plugins/filt/moog.cpp` | below |
| `filt::res1pole` | `plugins/filt/res1pole.cpp` | below |
| `filt::ink2` | `plugins/filt/ink2.cpp` | below |
| `filt::res2pole` | `plugins/filt/res2pole.cpp` | below |
| `filt::allpass` | `plugins/filt/allpass.cpp` | below |
| `filt::resonator` | `plugins/filt/resonator.cpp` | below |
| `dist::waveman` | `plugins/dist/waveman.cpp` | below |
| `padFft` | the body of the function declared in `plugins/osc/pad.cpp` | below |

The build already lists every one of these plugins (`plugins/CMakeLists.txt`),
so it fails until they exist. Each file carries the tree's usual license header
(copy it from any other plugin).

## Conventions every plugin here keeps

- **The interface is fixed.** Each plugin registers its args in exactly the
  order, with exactly the names, directions, descriptions, ranges and units, of
  its section in `docs/NODES.md`, quoted below. A ctest regenerates that file
  from the plugins and fails on any difference. `state` args are yours to use
  as you like, but their names and order are part of the interface.
- **Nothing a .dsp can write makes a non-finite sample.** Clamp every arg into
  the region where your algorithm is stable (`thClampArg`, `thClampMag` in
  `libthink/think.h`), and if a state value is ever non-finite, start the state
  over rather than stay dead. `dspsweep` drives every arg across its range and
  fails a plugin that produces NaN, infinity or runaway output.
- **Cutoff- and resonance-like args** are smoothed with
  `setArgSmooth(arg, TH_SMOOTH_MS)` as `plugins/filt/svf.cpp` does.
- **Units.** Inputs and outputs are "full scale" as elsewhere in the tree; see
  how `svf.cpp` reads `in` and writes its outputs. Sample rate is the callback's
  `samples` argument; never assume 44100.

## How to check your work

```
cmake -S . -B build && cmake --build build
python3 -I docs/cleanroom/measure.py --check docs/cleanroom/targets.json
ctest --test-dir build
```

`measure.py` renders each plugin with white noise or a sine through the tree's
own `genwav` and compares what it measures against `targets.json`, which was
recorded from the plugins being replaced. Tolerances: frequencies within 5%
(plus one analysis bin, about 20 Hz); levels within 1.5 dB (5 dB for resonance
peaks above 25 dB, whose height is ill-conditioned near self-oscillation);
waveshaper outputs within 3%. Every check must pass.

The tables below are those targets, for reading. `targets.json` is the
authority.

## filt::moog

Moog Filter

| Arg | Dir | Description | Default | Range | Units | Values |
|---|---|---|---|---|---|---|
| `buffer` | state |  |  |  |  |  |
| `in` | in | Signal in |  | -1 to 1 | full scale |  |
| `cutoff` | in | Cutoff, 0 to 1 -- the fit's own scale, not hertz and not a fraction of the rate; see `cutoffhz'. Clamped: the fit means nothing past 1 |  | 0 to 0.999 | 0..1 |  |
| `cutoffhz` | in | Cutoff in hertz. Overrides `cutoff' when above 0, which is what it is unless a graph says otherwise |  |  | Hz |  |
| `res` | in | Resonance, 0 to 1; 1 self-oscillates. Clamped for the same reason the cutoff is |  | 0 to 1 |  |  |
| `out_low` | out | Low pass, the ladder's fourth stage |  |  | full scale |  |
| `out_high` | out | High pass |  |  | full scale |  |
| `out_bandpass` | out | Band pass |  |  | full scale |  |

**What it is.** A four-pole (24 dB per octave) resonant low-pass in the
manner of the Moog transistor ladder, as analyzed by Stilson and Smith,
"Analyzing the Moog VCF with Considerations for Digital Implementation"
(ICMC 1996). Resonance runs from none at 0 to the onset of self-oscillation at
1, and as it rises the passband level falls, as a ladder's does (about -10 dB
at 0.99). There is a gentle saturation in the loop: driven hard near
resonance, the output gains odd harmonics (third harmonic about -51 dB at full
scale, below -60 dB at a quarter of it).

`cutoff` is on the plugin's own 0 to 1 scale, which is neither hertz nor a
fraction of the rate. Shipped instruments are tuned in it (`dsp/ladder.dsp`
and others), so the map from `cutoff` to the -3 dB corner below must be kept,
at 44.1 kHz; at other rates, keep it as a fraction of the rate. `cutoffhz`,
when above 0, overrides `cutoff` and puts the four-pole response's -3 dB point
at that frequency. `scripts/statecheck.cpp` tests `cutoffhz`; it must pass.

`out_high` and `out_bandpass` are a high-pass and a band-pass from the same
filter; their levels at one setting are below.

**moog: cutoff -> corner Hz (res 0, out_low)**

| setting | result |
| --- | --- |
| 0.02 | 231 |
| 0.05 | 592 |
| 0.1 | 1265 |
| 0.2 | 2934 |
| 0.3 | 5125 |
| 0.4 | 7897 |
| 0.5 | 11117 |
| 0.6 | 14416 |
| 0.7 | 17318 |
| 0.8 | 19563 |

**moog: cutoffhz -> corner Hz (res 0, out_low)**

| setting | result |
| --- | --- |
| 100 | 108 |
| 300 | 301 |
| 1000 | 1001 |
| 3000 | 2998 |
| 8000 | 7989 |
| 15000 | 14992 |

**moog: res -> peak (cutoff 0.3, out_low)**

| setting | peak Hz | peak dB | dB at 15 Hz |
| --- | --- | --- | --- |
| 0 | 38 | -0.0 | -0.0 |
| 0.2 | 4942 | -1.2 | -3.0 |
| 0.4 | 6019 | 1.0 | -5.2 |
| 0.6 | 6449 | 5.1 | -7.0 |
| 0.8 | 6751 | 14.4 | -8.5 |
| 0.9 | 6826 | 34.6 | -9.2 |
| 0.95 | 6842 | 44.6 | -9.8 |
| 0.99 | 6848 | 43.2 | -10.4 |

**moog: levels dB (cutoff 0.3, res 0.5, out_high)**

| setting | result |
| --- | --- |
| 100 | -35.9 |
| 300 | -26.4 |
| 1000 | -15.7 |
| 3000 | -4.4 |
| 6000 | 9.2 |
| 10000 | 2.1 |

**moog: levels dB (cutoff 0.3, res 0.5, out_bandpass)**

| setting | result |
| --- | --- |
| 100 | -38.4 |
| 300 | -28.9 |
| 1000 | -18.3 |
| 3000 | -7.0 |
| 6000 | 6.3 |
| 10000 | -2.2 |

**moog: sine 220 Hz at amp (cutoff 0.6, res 0.9, out_low)**

| input amplitude | output peak | 3rd harmonic, dB re fundamental |
| --- | --- | --- |
| 0.25 | 0.115 | -76.0 |
| 0.5 | 0.23 | -63.9 |
| 1.0 | 0.456 | -51.2 |

## filt::res1pole

Resonant 1-pole LPF

| Arg | Dir | Description | Default | Range | Units | Values |
|---|---|---|---|---|---|---|
| `out` | out | Filtered signal |  |  | full scale |  |
| `buffer` | state |  |  |  |  |  |
| `in` | in | Signal in |  | -1 to 1 | full scale |  |
| `cutoff` | in | Cutoff, 0 to 1 -- a fraction of the sample rate, not hertz |  | 0 to 0.999 | fraction of the rate |  |
| `res` | in | Resonance, 0 to 1; 1 is self-oscillation and is the edge of the stable region, so it is clamped short |  | 0 to 0.999 |  |  |

**What it is.** A resonant low-pass, its slope approaching 12 dB per octave
well above the corner (10 to 12 in the tables), whose passband stays at 0 dB as resonance rises (unlike the ladder): resonance
only adds a peak near the corner, from none at 0 to about +34 dB at 0.99.
`cutoff` is a fraction of the sample rate on the plugin's own curve, tabled
below at 44.1 kHz.

**res1pole: cutoff -> corner Hz (res 0)**

| setting | result |
| --- | --- |
| 0.02 | 97 |
| 0.05 | 237 |
| 0.1 | 479 |
| 0.2 | 1012 |
| 0.3 | 1626 |
| 0.4 | 2342 |
| 0.5 | 3219 |
| 0.6 | 4350 |
| 0.7 | 5943 |
| 0.8 | 8592 |

**res1pole: res -> peak (cutoff 0.2)**

| setting | peak Hz | peak dB | dB at 15 Hz |
| --- | --- | --- | --- |
| 0 | 32 | -0.0 | -0.0 |
| 0.2 | 38 | 0.0 | -0.0 |
| 0.4 | 894 | 0.6 | -0.0 |
| 0.6 | 1244 | 3.2 | -0.0 |
| 0.8 | 1378 | 8.9 | 0.0 |
| 0.9 | 1405 | 14.9 | 0.0 |
| 0.95 | 1405 | 20.9 | 0.0 |
| 0.99 | 1405 | 34.6 | 0.0 |

## filt::ink2

INK Filter ][

| Arg | Dir | Description | Default | Range | Units | Values |
|---|---|---|---|---|---|---|
| `in` | in | Signal in |  | -1 to 1 | full scale |  |
| `cutoff` | in | Cutoff, 0 to 1 -- a fraction of the sample rate, squared, not hertz |  | 0 to 0.999 | fraction of the rate |  |
| `res` | in | Resonance, 0 to 1; what is stable near 1 narrows as the cutoff rises, and is clamped |  | 0 to 0.999 |  |  |
| `out` | out | Filtered signal |  |  | full scale |  |
| `buffer` | state |  |  |  |  |  |

**What it is.** A resonant low-pass with a pronounced peak: very high at low
cutoffs (about +50 dB at a cutoff of 0.02, with the region below the peak
lifted too) and modest at high ones, falling about 6 dB per octave above the
peak. `cutoff` is the square of a fraction of the rate, per its description;
the tables give the peak's position and height across `cutoff` and `res`.

**ink2: cutoff -> peak (res 0.6)**

| setting | peak Hz | peak dB | dB at 15 Hz |
| --- | --- | --- | --- |
| 0.02 | 108 | 50.0 | 12.5 |
| 0.05 | 275 | 43.0 | 2.7 |
| 0.1 | 549 | 31.7 | 0.3 |
| 0.2 | 1120 | 20.6 | 0.0 |
| 0.3 | 1760 | 14.7 | 0.0 |
| 0.4 | 2460 | 11.2 | 0.0 |
| 0.5 | 3321 | 8.9 | 0.0 |
| 0.6 | 4360 | 7.5 | 0.0 |
| 0.7 | 5846 | 6.7 | 0.0 |
| 0.8 | 8032 | 6.5 | 0.0 |

**ink2: res -> peak (cutoff 0.3)**

| setting | peak Hz | peak dB | dB at 15 Hz |
| --- | --- | --- | --- |
| 0 | 22 | -0.0 | -0.0 |
| 0.2 | 980 | 9.8 | 0.0 |
| 0.4 | 1427 | 12.9 | 0.0 |
| 0.6 | 1760 | 14.7 | 0.0 |
| 0.8 | 2051 | 15.9 | 0.0 |
| 0.9 | 2180 | 16.4 | 0.0 |
| 0.95 | 2256 | 16.6 | 0.0 |
| 0.99 | 2272 | 16.7 | 0.0 |

## filt::res2pole

Resonant 2-pole Chamberlin filter

| Arg | Dir | Description | Default | Range | Units | Values |
|---|---|---|---|---|---|---|
| `out` | out | Low pass |  |  | full scale |  |
| `out_high` | out | High pass |  |  | full scale |  |
| `out_band` | out | Band pass |  |  | full scale |  |
| `out_notch` | out | Notch |  |  | full scale |  |
| `delay` | state |  |  |  |  |  |
| `in` | in | Signal in |  | -1 to 1 | full scale |  |
| `cutoff` | in | Cutoff in hertz; honest to about a sixth of the sample rate, clamped above that |  |  | Hz |  |
| `res` | in | Resonance as Q: 0.5 is damped, higher rings. The damping it can ask for is bounded by the cutoff |  |  |  |  |

**What it is.** Hal Chamberlin's digital state-variable filter, as described
in *Musical Applications of Microprocessors*, 2nd ed. (Hayden, 1985), pp.
490-492, with low-pass, high-pass, band-pass and notch outputs from one
filter. `cutoff` is in hertz and `res` is its Q: 0.5 is damped, and the peak
at the cutoff grows about 6 dB per doubling of Q above 1. The stability bound
on Q depends on the cutoff, as its arg description says; derive yours from
the algorithm.

**res2pole: res -> peak (cutoff 1000 Hz, out)**

| setting | peak Hz | peak dB | dB at 15 Hz |
| --- | --- | --- | --- |
| 0.5 | 700 | 1.1 | -0.0 |
| 0.7 | 883 | 3.3 | -0.0 |
| 1 | 953 | 6.2 | 0.0 |
| 2 | 996 | 12.0 | 0.0 |
| 4 | 1001 | 18.1 | 0.0 |
| 8 | 1001 | 24.1 | 0.0 |
| 16 | 1001 | 29.9 | 0.0 |

**res2pole: cutoff -> peak (res 8, out)**

| setting | peak Hz | peak dB | dB at 15 Hz |
| --- | --- | --- | --- |
| 100 | 102 | 20.1 | 0.2 |
| 300 | 301 | 22.9 | 0.0 |
| 1000 | 1001 | 24.1 | 0.0 |
| 3000 | 3015 | 24.2 | 0.0 |
| 6000 | 6083 | 24.8 | -0.0 |

**res2pole: levels dB (cutoff 1000 Hz, res 2, out_high)**

| setting | result |
| --- | --- |
| 100 | -39.9 |
| 300 | -20.1 |
| 1000 | 12.0 |
| 3000 | 1.2 |
| 6000 | 0.4 |
| 10000 | 0.3 |

**res2pole: levels dB (cutoff 1000 Hz, res 2, out_band)**

| setting | result |
| --- | --- |
| 100 | -19.9 |
| 300 | -9.7 |
| 1000 | 12.0 |
| 3000 | -8.3 |
| 6000 | -14.9 |
| 10000 | -19.0 |

**res2pole: levels dB (cutoff 1000 Hz, res 2, out_notch)**

| setting | result |
| --- | --- |
| 100 | -0.0 |
| 300 | -0.0 |
| 1000 | -37.4 |
| 3000 | 0.1 |
| 6000 | 0.2 |
| 10000 | 0.2 |

## filt::allpass

Allpass Filter

| Arg | Dir | Description | Default | Range | Units | Values |
|---|---|---|---|---|---|---|
| `in` | in | Signal in |  | -1 to 1 | full scale |  |
| `freq` | in | Where the phase shift passes 90 degrees |  |  | Hz |  |
| `last` | state |  |  |  |  |  |
| `out` | out | The input, phase-shifted |  |  | full scale |  |

**What it is.** A first-order allpass filter: unity gain at every frequency,
its phase falling from 0 to -180 degrees, passing -90 degrees near `freq`.
Near `freq` in the table below, not exactly at it, and further from it as
`freq` rises; match the table.

**allpass: freq -> Hz where the phase passes -90 deg**

| setting | result |
| --- | --- |
| 50 | 54 |
| 200 | 205 |
| 1000 | 1001 |
| 3000 | 2961 |
| 6000 | 5674 |

## filt::resonator

Resonator filter

| Arg | Dir | Description | Default | Range | Units | Values |
|---|---|---|---|---|---|---|
| `in` | in | Signal in |  | -1 to 1 | full scale |  |
| `freq` | in | Where the resonance sits |  |  | Hz |  |
| `fb` | in | Feedback round the allpass; 1 never decays |  | 0 to 1 |  |  |
| `last` | state |  |  |  |  |  |
| `out` | out | Filtered signal |  |  | full scale |  |

**What it is.** A first-order allpass section in a feedback loop, per its arg
descriptions, which rings. The peak gain is set by `fb` alone (0 dB at 0,
about 20 dB at 0.9, 40 dB at 0.99), and the peak's frequency moves with
`freq` but is **not** at `freq`: match the table. Its output can exceed full
scale at high `fb`; that is expected.

**resonator: freq -> peak (fb 0.9)**

| setting | peak Hz | peak dB | dB at 15 Hz |
| --- | --- | --- | --- |
| 50 | 829 | 20.0 | -5.2 |
| 110 | 1254 | 20.0 | -5.5 |
| 220 | 1760 | 20.0 | -5.6 |
| 440 | 2466 | 20.0 | -5.6 |
| 880 | 3451 | 20.0 | -5.6 |
| 1760 | 4764 | 20.0 | -5.6 |
| 3000 | 6067 | 20.0 | -5.6 |
| 6000 | 8118 | 20.0 | -5.6 |

**resonator: fb -> peak (freq 440)**

| setting | peak Hz | peak dB | dB at 15 Hz |
| --- | --- | --- | --- |
| 0 | 19057 | 0.0 | -0.0 |
| 0.3 | 2541 | 3.1 | -2.3 |
| 0.5 | 2557 | 6.0 | -3.5 |
| 0.7 | 2471 | 10.5 | -4.6 |
| 0.9 | 2466 | 20.0 | -5.6 |
| 0.95 | 2460 | 26.0 | -5.8 |
| 0.99 | 2460 | 40.1 | -6.0 |

## dist::waveman

Applies waveman's waveshaper

| Arg | Dir | Description | Default | Range | Units | Values |
|---|---|---|---|---|---|---|
| `in` | in | Signal in |  | -1 to 1 | full scale |  |
| `gain` | in | How much the shaper is softened; larger is gentler, and 0 is not allowed |  | 0.001 to 8 |  |  |
| `out` | out | The shaped signal |  |  | full scale |  |

## dyn

**What it is.** A memoryless waveshaper: the output depends only on the input
sample and `gain`, and is odd-symmetric (the output for -x is minus the output
for x). Larger `gain` makes it gentler and quieter overall. At very small
`gain` the curve steps at zero: an input of 0.05 already gives 0.30.
The table gives the output for inputs from 0.05 to 0.9 at each gain; build a
curve through them.

**waveman: input level -> output (gain g)**

| gain | in 0.05 | in 0.1 | in 0.2 | in 0.3 | in 0.4 | in 0.5 | in 0.6 | in 0.7 | in 0.8 | in 0.9 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0.001 | 0.3011 | 0.3255 | 0.3697 | 0.413 | 0.4561 | 0.4989 | 0.5417 | 0.5848 | 0.6292 | 0.688 |
| 0.25 | 0.0551 | 0.0998 | 0.1703 | 0.2276 | 0.2779 | 0.3238 | 0.367 | 0.4085 | 0.45 | 0.5033 |
| 0.5 | 0.0315 | 0.0605 | 0.1117 | 0.1572 | 0.199 | 0.2381 | 0.2752 | 0.3112 | 0.3471 | 0.3933 |
| 1 | 0.0175 | 0.0345 | 0.0667 | 0.0973 | 0.1266 | 0.1548 | 0.1821 | 0.2089 | 0.2359 | 0.2709 |
| 2 | 0.0094 | 0.0189 | 0.0373 | 0.0553 | 0.0731 | 0.0905 | 0.1077 | 0.1247 | 0.1422 | 0.1649 |
| 4 | 0.005 | 0.01 | 0.0199 | 0.0297 | 0.0395 | 0.0492 | 0.0589 | 0.0685 | 0.0785 | 0.0916 |
| 8 | 0.0026 | 0.0052 | 0.0103 | 0.0154 | 0.0206 | 0.0257 | 0.0308 | 0.0359 | 0.0412 | 0.0482 |

## padFft, in plugins/osc/pad.cpp

`osc::pad` builds each of its wavetables with an inverse FFT of 2^18 points,
done as a forward FFT of a conjugate (see the caller). Write the body of the
function declared there:

```
static void padFft (std::vector<double> &re, std::vector<double> &im, int m);
```

- A forward complex DFT, X[k] = sum over n of x[n] e^(-2 pi i k n / N), with
  N = 2^m, unnormalized, in place, natural order in and out.
- Double precision throughout. Results within 1e-9 of an exact DFT relative
  to the largest output.
- `padConst()` in the same file already holds cos(2 pi j / N) and
  -sin(2 pi j / N) for j < N/2 at N = 2^PAD_BITS; use them or not.
- Speed matters: a table is built when a voice starts, on the audio thread.
  O(N log N) with no allocation, and no slower than a plain radix-2.

## Acceptance

1. `measure.py --check` passes every check.
2. `ctest --test-dir build` passes, except `dsphash.reference`, which compares
   renders against `scripts/corpushash.txt`. The new plugins change some
   instruments' sound within tolerance, so regenerate it with
   `cmake --build build --target corpushash-update` and list in the log
   which instruments changed.
3. `docs/cleanroom/LOG.md` lists every source consulted and what was taken
   from it, and how each scale mapping (moog's `cutoff`, `allpass` and
   `resonator`'s `freq`, waveman's curve) was derived or fitted.
