# The output stage

Four pieces of engine behaviour that are easy to break by accident and hard to
diagnose once broken: the clamp, the limiter, the per-voice non-finite guard,
and arg initialisation. All four were audible bugs — the third of them audible
as nothing at all — and the reasoning is here so the fixes are not mistaken for
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
(`old/test.dsp` reaches 1.75e5, `old/bd10.dsp` 2e4 — both from the drawers
this corpus still had, and both since removed).

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

The four diverging DSPs were being saved by the limiter rather than fixed. Their
filters were numerically unstable, which is the next section.

## Non-finite samples — why the per-voice guard exists

The limiter saturates an infinity and silences a NaN, which is the right answer
at the output stage and the wrong place to find out. The damage is done one
layer up: `thMidiChan` sums every sounding voice into the channel's buffer, and
a sum with a NaN in it stays NaN for the rest of the window. One bad voice
therefore took *every* voice on that channel with it, the limiter turned the
result into silence, and nothing said why.

Two cases found the slow way:

- `filt::res2pole2` with `res` under about 0.5. Its pole magnitude is
  `1 - w/D`, and `D` crosses zero as `res` approaches 0.5 from above — at DC it
  *is* zero there — so the filter was asked for a pole outside the unit circle
  and diverged.
- `brass.dsp` with `buzz = 0 ms` or `bendl = 0 ms`, which is what
  `gen/attract.gen` ships. `env::adsr` divided the position by the segment
  length, so every brass note was a NaN — 62 in the first twenty seconds — and
  that was not the brass, it was the mix. With both fixed, the same twenty
  seconds go from a peak of 0.412 with the lead missing to 0.522 with it.

Four things now stand between a diverging graph and a silent render.

**The guard** (`thMidiChan::mixNote`). Before any of a voice is mixed, the
samples it put on the io node's `out0..outN-1` are scanned. A non-finite one
anywhere means the voice contributes nothing, the note is retired, and the
channel says so once per load:

```
thMidiChan: channel 3 (ladder.dsp): a voice went non-finite; note retired
```

`thSynth::nonFiniteVoices()` counts them; `genwav` prints the count and exits
4 — beside 3 for clipping and ahead of it, since a piece that clips is loud and
a piece with a non-finite voice in it is not the piece. `thSoftLimit` stays the
backstop: the guard only sees what a voice hands the io node, and a graph can
still diverge somewhere no channel reads.

**Stability floors in the filters.** `res2pole2`, `res2pole`, `res1pole`,
`ink`, `ink2`, `ink3`, `divbuf` and `moog` each clamp their coefficients inside
a region derived from the determinant and trace of their own state matrix,
written out in the callback. Where the region depends on two args at once —
`res2pole`'s damping bound narrows as its cutoff rises — the clamp is on the
coefficient rather than the knob, whose usable range moves.

**A wavelength an oscillator can step through.** Every plugin in `plugins/osc`
divides the rate by a frequency and then divides by the result, or by a
fraction of it, so a frequency of zero or an infinity is a division by zero one
line later. `thBoundFreq` holds a frequency between Nyquist and the slowest
wave a float `position` can still be stepped through, and the two oscillators
that split a cycle by a pulse width hold that off both ends, since `pw = 1`
leaves the second half of the cycle empty and divides by its length anyway.
`misc::midi2freq` stops just short of Nyquist, not on it: a wavelength of
exactly two samples is an exact integer, which is the difference between
approaching that division and landing on it. It also clamps in double —
narrowing first turns note 4210 into an infinity, and an infinity clamped by
magnitude comes back as the *bottom* of the range, answering an absurdly high
note with 0 Hz and silence.

**Zero-length envelope segments are instantaneous.** `env::adsr`, `env::ad` and
`env::adsfr` complete a zero-length segment at once instead of dividing by its
length, so `a = 0` means what it reads as and the millisecond workarounds are
out of the corpus.

**`scripts/dspsweep` is the gate.** Every shipped graph with each declared
control at its `.min`, its `.max` and three points between, one note at each end
of the keyboard: 3840 renders over the corpus in under three seconds, failing if
the guard fired on any of them. One control moves at a time — what goes
non-finite is a coefficient leaving its own range, not two knobs conspiring. It
also gates the guard itself over the two graphs in `scripts/guard/`: one
instrument that cannot help producing a NaN and one that behaves, on two
channels at once, with the good channel keeping its peak.

Measured against the same corpus with the filters as they were: **205 of the
3840 cases went non-finite, across 24 of the 82 graphs the gate covers** — the
corpus less the eleven that reference a plugin the build does not make. (That
corpus included the `effects/`, `old/` and `noargs/` drawers, which have since
been removed; the graphs named below and in the paragraph after it are from
them. The numbers are what they were when the fix was made, and are left
alone for that reason.) Six of
them (`noargs/bd1`, `noargs/bd2`, `noargs/hat1`, `old/acid00`, `old/analog02`,
`old/bd9`) did it at their shipped settings, which is to say they had never made
a sound. Now none do.

A bitwise A/B (`scripts/dspab`, old plugins against new) puts the cost at **23
of 82 graphs changed, 59 bit-identical**. Eight of the 23 differ by under 1e-5,
which is the clamp arithmetic reordering a float. Six are the graphs above,
which used to be silent. The rest were running something outside the range its
arithmetic is defined over and being saved by a reset, by the output clamp, or
by nothing: `noargs/ambient-000` and `ambient-001` swing a `res2pole2` cutoff
*negative* for half of every LFO cycle — |pole| just over 1, every cycle;
`old/bemu1`, `old/analog03` and `old/5-17-03-1407` drive `ink`'s cutoff from an
envelope sustaining at eighty times full scale; and `old/bd4`, `old/bd7` and
`old/bd9` ask an oscillator for a frequency past Nyquist. They sound different
because what they sounded like was partly divergence.

The bounds are on the *inputs* rather than on the wavelengths the callbacks
compute from them, deliberately. The oscillators do not all spell that division
the same way — some are `rate/freq` in double, some `rate * (1.0/freq)` — and
rounding one into the shape of the other changed what 51 of the 82 rendered,
by about 1e-7, for no reason at all. Bounding the input leaves an in-range
frequency bit-for-bit as it was.

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
differing at window 0. Zero fail after it. (Each render builds a fresh synth,
which restarts the noise plugins' generators — `osc::static`'s and
`osc::noise`'s, one per synth apiece, see `plugins/osc/noiseslot.h` — and
reseeds `rand()` for anything else; otherwise the fourteen DSPs built on one
of the two would show up as false positives.)

## Restarted voices — why a finished voice is kept

A note used to be a fresh copy of the channel's prototype tree, every time:
each node, each arg, and each arg's name, label, units and comment strings,
into string-keyed maps. For `grand.dsp` that is about 720k instructions a
note, mostly `malloc` and `std::string`. Natively the GUI thread pays it, but
in the browser the worklet applies its own note-ons between windows, so there
it is the audio thread's.

A voice the audio thread has finished with now goes back, through the retire
queue, to the channel that built it (`thMidiChan::recycle`), which keeps up to
`poly` of them (at most `TH_POOL_MAX`). The next `buildNote` takes one and
restarts it: `thSynthTree::restore` puts every arg back to what the copy
constructor would have given it — length, values, chanarg pointer — in the
buffer the arg already has, and marks every node for its first window. The
structure (nodes, links, active list) and the strings stay as they are. It is
matched to its channel by serial, not by pointer, so a voice from a channel
since replaced is deleted rather than restarted.

Two things make this exact rather than close:

- `thArg` keeps a capacity apart from its length. A restored arg shrinks back
  to the prototype's one value, and the first window's `allocate(windowlen)`
  zero-fills the buffer it has instead of allocating one — the same zeros
  `new float[]()` would give, with no allocation.
- `restore` walks the copy's and the prototype's arg maps side by side, and
  fails if they differ (an arg invented on the copy by name, say); the voice
  is then deleted and the note gets a fresh copy.

`poolcheck` plays every DSP twice, pooled and with `thSynth::setVoicePool(false)`,
and requires the two renders to be bitwise equal.

## The harnesses

| Harness | CTest gate | Covers |
|---|---|---|
| `dspcheck` | yes | loads every DSP and patch, plays a chord, overruns polyphony, releases, reloads onto the same channel, tears down; renders each note twice and compares bitwise |
| `dsplevel` | yes | peak, proportion shaped, gain reduction; exit status is the number of measurements over `TH_MAX` |
| `dspsweep` | yes | every control of every DSP at both ends of its range and three points between, at both ends of the keyboard; exit status is the number of cases the per-voice guard fired on. With `-g` it also gates the guard itself |
| `poolcheck` | yes | every DSP played twice, with finished voices restarted and with every note a fresh copy, compared bitwise |
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
