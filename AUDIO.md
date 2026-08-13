# The output stage

Three pieces of engine behaviour that are easy to break by accident and hard to
diagnose once broken: the clamp, the limiter, and arg initialisation. All three
were audible bugs; the reasoning is here so the fixes are not mistaken for
arbitrary choices.

## Integer wraparound — why `thClampSample()` exists

`thSynth::process` sums every sounding note into one buffer with no headroom
management at all. Each voice contributes up to `TH_MAX`, scaled only by the
channel amplitude, so two voices reach roughly twice full scale and three about
three times:

```
dsp/ts1.dsp
  1 voice:  peak  0.617   clipped   0.0%
  2 voices: peak  1.234   clipped   1.0%
  3 voices: peak  1.523   clipped  14.5%
  4 voices: peak  2.050   clipped  21.5%

dsp/anasync.dsp                                    (what EKeyboard.patch uses)
  1 voice:  peak  1.587   clipped   2.3%   <- already hot on one voice
  2 voices: peak  3.017   clipped  14.5%
```

That reached the output stage unclamped, and converting an out-of-range float to
an integer type is undefined. In practice it wraps:

```
float 0.617 ->  20217 -> int16  20217     fine
float 1.234 ->  40434 -> int16 -25102     full-scale sign flip
float 1.523 ->  49904 -> int16 -15632
float 3.017 ->  98858 -> int16 -32214
```

So every sample past full scale became a near-full-scale discontinuity of the
*opposite* sign. That was the static, and it explains every part of the symptom:
loudest on the attack where the envelope peaks, gone by the sustain once the
signal drops back inside the rails, absent on one note for most DSPs, and worse
and longer-lasting with each extra voice held down. The float path had the same
problem more mildly — raw floats handed to a port that expects −1..1.

`thClampSample()` (in `think.h`) guards both paths. That converts wraparound
into ordinary hard clipping, which is a bug fix, not a design.

## Gain staging — why `thSoftLimit()` is a waveshaper

Clamping stopped the wraparound but did nothing about headroom. Measured across
all shipped DSPs before any gain work:

| voices | median peak | clean | hot (1–4×) | very hot (4–100×) | diverging (>100×) |
|---|---|---|---|---|---|
| 1 | 0.78 | 47 | 17 | 7 | 4 |
| 2 | 1.53 | 20 | 39 | 12 | 6 |
| 3 | 2.34 | 8 | 46 | 17 | 7 |
| 4 | 3.12 | 6 | 44 | 18 | 10 |

Three things shaped the fix. The median DSP is *well calibrated at one voice*
(0.78) — whoever tuned these did it by ear against a single note. The median
then scales almost exactly linearly with voice count (0.78 × N), meaning voices
sum coherently, which they would since every envelope peaks together on the
attack — so the overshoot is an attack transient, not a sustained level problem.
And four DSPs do not have a gain problem at all; their filters diverge
(`old/test.dsp` reaches 1.75e5, `old/bd10.dsp` 2e4).

`thSoftLimit()` in `think.h` is a memoryless waveshaper on the master output:

```
        |x|  <= knee :  unchanged
        |x|  >  knee :  knee + range * tanh((|x| - knee) / range)
```

with `TH_LIMIT_KNEE` at 0.7. It is continuous in value *and* slope at the knee
(`tanh'(0) == 1`, so it leaves the linear region at unity gain) and asymptotic to
exactly `TH_MAX`, so even a DSP diverging to 1e5 saturates gracefully with no
special case.

**A waveshaper rather than a compressor, deliberately.** It has no envelope, so
a held note does not change level as other notes come and go — the thing that
makes `1/N` per-voice scaling unpleasant to play. Below the knee it is exactly
the identity, so quiet material and most single notes are bit-unchanged. It needs
no lookahead, so no latency and no state to make RT-unsafe.

Alongside it, `thSynth` has a master gain (`setMasterGain`/`masterGain`),
defaulting to unity so existing patches keep the level they were tuned at, and
persisted in `thinkrc` as `mastergain`. It is read by the audio thread every
window and written from the GUI, so it goes through a relaxed atomic for the
same reason `thArg::setValue` does.

Result across the corpus: **no DSP exceeds `TH_MAX` at any voice count**, and 33
of 81 are untouched entirely at one voice. `ts1.dsp` at four voices has its peak
pulled down 6.2 dB; at one voice it is unaltered.

`scripts/dsplevel` reports peak, the proportion of samples the limiter bent, and
the gain reduction at the peak — the last of those matters because "38% of
samples shaped" sounds alarming while most of those samples sit just above the
knee and move by a fraction of a dB:

```
dsp/ts1.dsp
  1 voice:  peak  0.617  shaped   0.0% of samples  peak cut   0.0 dB
  4 voices: peak  1.000  shaped  38.8% of samples  peak cut   6.2 dB
```

Its exit status is the number of measurements still exceeding `TH_MAX`, which
should be zero.

**Still open:** the four diverging DSPs are being saved by the limiter rather
than fixed. Their filters are numerically unstable and want looking at
separately.

## Uninitialised plugin state — why `allocate()` value-initialises

The giveaway was that the noise sat on a note's **attack** and cleared by the
time it sustained. A race scatters noise uniformly, so a symptom that tracks the
envelope is state, not timing.

`thArg::allocate()` used to hand back raw `new float[]` memory. Plugins keep
their state in args — delay lines, filter history, oscillator phase — and read it
back before writing it; `delay/echo`, `delay/fir`, `filt/comb` and `filt/allpass`
all do. A note's args are copy-constructed from the channel's prototype tree,
where they hold a single placeholder value, so the **first window of every note**
resizes each of them from 1 to `windowlen` and the plugin reads whatever was in
that heap block. From the second window on, `len_` already matches, `allocate()`
returns the same buffer untouched, and it holds real audio. Hence: a burst on the
attack, silence on the sustain.

It also explains why this was not obvious for twenty years. Freshly mapped pages
arrive from the kernel zero-filled, so on a quiet heap the garbage was usually
silence. On a busy one — more notes, more allocation churn — the block comes back
holding the previous note's samples at full scale.

`allocate()` now value-initialises (`new float[elements]()`). **A resize
deliberately does not preserve the old contents**: going from one value to a
whole window means the old value was a placeholder, and zero is the right initial
state for a delay line.

**AddressSanitizer cannot find this** — it tracks addresses, not initialisation.
`dspcheck` catches it instead, by rendering the same note twice from a fresh
synth and comparing bitwise:

```
FAIL  dsp/anasync.dsp (non-deterministic output, first differing window 0
      -- a plugin is reading uninitialised state)
```

19 of the 92 shipped DSPs failed that check before the fix, every one of them
differing at window 0. Zero fail after it. (`renderNote` reseeds `rand()`, or the
twelve DSPs built on `osc::static` would show up as false positives.)

## The harnesses

| Harness | CTest gate | Covers |
|---|---|---|
| `dspcheck` | yes | loads every DSP and patch, plays a chord, overruns polyphony, releases, reloads onto the same channel, tears down; renders each note twice and compares bitwise |
| `dsplevel` | yes | peak, proportion shaped, gain reduction; exit status is the number of measurements over `TH_MAX` |
| `dspstress` | no | a synthetic audio thread calling `process()` while the main thread does what the GUI thread does |
| `dspab` | no | two renders compared for bitwise identity — used when a change is meant to be inaudible |
| `dsplive` | no | the only check that tests the actual sound: renders a note twice, once with a control moved halfway through, and asserts the halves before the move are identical while the halves after differ |

The three that are not gates take a corpus argument or their own build tree, so
they are run by hand.

`dspstress` splits work into levels so a report can be blamed on one kind of
operation rather than on "something concurrent", and each level runs in a forked
child with a watchdog so a level that crashes or wedges does not take the run
with it:

| Level | Exercises |
|---|---|
| 1 `notes` | `addNote` / `delNote` |
| 2 `clear` | + `clearAll` |
| 3 `chanargs` | + `setChanArg` and slider-style `setValue` |
| 4 `reload` | + `loadTree` onto a live channel, `removeChan` |
| 5 `probes` | + arming and draining visualizer probes while the patch is replaced |

ThreadSanitizer cannot be combined with AddressSanitizer, so it needs its own
build tree:

```sh
cmake -S . -B build-tsan -DTHINK_SANITIZE=thread
cmake --build build-tsan
./build-tsan/scripts/dspstress -p build-tsan/plugins/ dsp/ts1.dsp
```

Exit status is the number of levels that failed. The tool sets its own
`__tsan_default_options`, so a race fails the run without anyone having to
remember the environment variable.

**One measurement trap worth knowing.** An earlier round of this work reported
plausible-looking sanitizer results from a build where the sanitizer was not
actually linked, because the build system assigned `CXXFLAGS` rather than
appending to it. `nm -D build/libthink/libthink.so | grep __asan` is the check.
Nothing runs it automatically yet, and a sanitizer job that silently instruments
nothing looks exactly like a clean one.
