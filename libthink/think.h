/*
 * Copyright (C) 2004-2026 Metaphonic Labs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef THINK_H
#define THINK_H

/* Ask for the M_* constants before <math.h> is read. They are not in any C or
   C++ standard, so a strict-conformance mode -- which -std=c++17 selects, as
   opposed to -std=gnu++17 -- is entitled to hide them, and MinGW's headers
   do exactly that. The tree uses M_PI in 27 places and M_E in 9. */
#ifndef _USE_MATH_DEFINES
# define _USE_MATH_DEFINES 1
#endif

#include <math.h>
#include <stdint.h>
#include <string.h>

/* And a belt to go with the braces: a libc that offers neither spelling still
   has to compile. */
#ifndef M_PI
# define M_PI 3.14159265358979323846
#endif

#ifndef M_E
# define M_E 2.7182818284590452354
#endif

#include <map>
#include <string>
#include <list>
#include <vector>

using namespace std;

/* Sampling Rate */
#define TH_SAMPLE 44100
#define TH_WINDOW_LENGTH 1024

#define TH_DEFAULT_SAMPLES 44100
#define TH_DEFAULT_WINDOW_LENGTH 1024

/* A ceiling on the window, because `-l' is an atoi() away from the command
   line and the output buffer is sized channels * windowlen. In int arithmetic
   that product overflows somewhere above 2^31 / channels, and an overflowed
   `new float[n]' allocates a small buffer that the memset beside it -- which
   widens to size_t correctly -- then writes past.

   16384 rather than something rounder because thMidiChan::process() puts two
   VLAs of this many floats on the stack, so the cap is also a bound on 128kB
   of stack per call. No device period comes anywhere near it; JACK and
   CoreAudio top out in the low thousands. */
#define TH_MAX_WINDOW_LENGTH 16384

/* Signal Range */
#define TH_MAX 1
#define TH_MIN -1
#define TH_RANGE (TH_MAX-TH_MIN)

/* For note amplitude and stuff... */
#define MIDIVALMAX 127

/* how big many channel references should we allocate when we need more */
#define CHANNELCHUNK 16

/* Number of MIDI channel slots, allocated once at construction.
 *
 * This used to grow on demand with calloc/memcpy/free while the audio thread
 * was iterating the array -- a use-after-free waiting to happen, for no gain:
 * MIDI channels are 0-15 by protocol and gthPatchManager tops out at
 * NUM_PATCHES (16) as well. A fixed array removes the race outright. */
#define TH_MIDI_CHANNELS 16

/* Controller numbers are seven bits, so thMidiController's per-channel array
   is this wide. Here rather than in thMidiController.h so it sits beside the
   channel count it is always paired with. */
#define TH_MIDI_CONTROLLERS 128

/* number of node argument references allocated at a time */
#define ARGCHUNK 16

/* Alsa output buffer */
#define TH_BUFFER_PERIOD 1024

/* Language interface stuff... */
#define OUTPUTPREFIX "out"

/* And the other way, for a channel effect.
 *
 * An effect graph is an ordinary .dsp whose io node has in0..in<N-1> written
 * by the engine -- the channel's summed voices -- the way note and velocity
 * are written into a voice's. Everything else about it is a .dsp: the same
 * nodes, the same chanargs, the same out0..out<N-1> read back.
 *
 * `in' rather than `input' because the io node's other engine arg is `out'
 * and the pair has to read as a pair. Nothing else on the io node is called
 * this: a plugin's `in' is a plugin's. */
#define INPUTPREFIX "in"

/* And a voice's per-note timbre, for an instrument.
 *
 * aux0..aux<TH_NOTE_AUX-1> on an instrument's io node carry the four floats a
 * composed note brings besides its pitch and velocity -- a pan, a brightness,
 * an attack, whatever the graph reads them as. Written once, when the voice is
 * built, and never again: a mono slide retunes a voice without touching them,
 * for the reason it leaves `velocity' alone. A graph that reads none of them
 * pays for none of them, because they are only written where the graph
 * mentions them. What a zero means is the graph's to say; a note from MIDI, or
 * from a composer that never set them, carries zeros. */
#define AUXPREFIX "aux"
#define TH_NOTE_AUX 4

/* And the channel an effect listens to besides its own.
 *
 * side0..side<N-1> on an effect's io node carry another channel's audio --
 * the carrier a vocoder needs, the kick a compressor is keyed from -- written
 * by the engine every window the way in<N> is, and never read back: what an
 * effect returns is its own channel's.
 *
 * A file declares them the way it declares in<N> (`side0 = 0;'), and a graph
 * that declares none is an effect that hears one channel, which is nearly all
 * of them. Which channel it is comes from the `.gen' effect clause's `side',
 * not from the file: the graph is the vocoder, and what is being vocoded is
 * the piece's business. */
#define SIDEPREFIX "side"

/* And the live input, where a host is feeding one.
 *
 * live0..live<N-1> on an effect's io node carry what the machine is hearing
 * this window -- a microphone, a line in, whatever the host opened -- written
 * by the engine the way in<N> and side<N> are, and never read back.
 *
 * It is a *third* family rather than a second kind of side because the two
 * answer different questions. A side names a channel, which is a thing the
 * piece decides and the file cannot; live names nothing, because there is
 * only ever one thing the machine is hearing. So a graph asks for it by
 * declaring it (`live0 = 0;') and no `.gen' clause is involved at all --
 * which is what lets a vocoder driven by a voice be one line on a piece.
 *
 * The capture is mono (thSynth::feedCapture says why), so a graph that
 * declares live0 and live1 is handed the one signal twice, which is the rule
 * side<N> already follows for a mono side.
 *
 * Silence where no host is feeding one, and silence is also what it should
 * sound like: a vocoder with nothing to vocode. Every offline path -- genwav,
 * gencheck, dspcheck -- feeds nothing and therefore reads zeros, which is
 * what keeps a graph with a live input in it reproducible. */
#define LIVEPREFIX "live"

/* And the send bus, on the master effect only.
 *
 * send0..send<N-1> on the master effect's io node carry the sum of every
 * channel's output scaled by that channel's send -- `send = 0.3;' in a
 * `.gen' instrument, `fx.send' from outside -- written by the engine every
 * window beside in<N>, which still carries the whole mix at full level. What
 * to do with the two is the graph's: a reverb puts send<N> through the
 * network and in<N> past it dry, which is one room shared by every channel
 * at a different depth each.
 *
 * Declared like side<N> and live<N> (`send0 = 0;'), and invented for
 * nobody. A master effect that declares none hears no send, and a piece with
 * no master effect has nobody to hear one: the send is then silence, and the
 * channels are still in the mix at full level. */
#define SENDPREFIX "send"

/* The channel's own send, addressed as TH_EFFECT_PREFIX TH_SEND_ARG.
 *
 * It lives on the channel rather than in its effect's map, because a channel
 * with no effect still sends -- a piano dry on its own channel and half of it
 * into the room is the usual case. Under the effect's prefix because it is
 * the effect side of the channel, and so that a bare `send' stays free for
 * the instrument's own graph. An effect that declares its own `@send' is
 * shadowed by this one. */
#define TH_SEND_ARG "send"

/* How a channel effect's chanargs are named from outside.
 *
 * `fx.delay' is the effect's `@delay'; a bare `delay' is the instrument's.
 * The two sets are kept apart rather than merged so that an instrument's `@a'
 * and an effect's cannot collide -- and a prefix rather than a second
 * argument to every call, because the places that name a chanarg are strings
 * in files: a .gen sink's `chanarg', a .patch line, a MIDI controller
 * binding. */
#define TH_EFFECT_PREFIX "fx."

/* How many voices a channel plays at once when its .dsp does not say.
 *
 * `node ionode { poly = N; }' overrides it, and 0 means no limit -- which is
 * what a polymax_ of 0 has always meant to the check in
 * thMidiChan::process(). Ten is the number the constructor has had all
 * along; it is here so that a .dsp reading the reference can be told what it
 * is overriding. */
#define TH_DEFAULT_POLY 10

/* How long a stolen voice takes to get out of the way, in milliseconds.
 *
 * A channel over its `poly' budget used to hand the oldest voice straight to
 * the retire queue, so whatever it was sounding at became a zero between one
 * sample and the next -- a step at the window boundary, which is a click.
 * dsp/bass.dsp asks for `poly = 2' so that a retrigger does not cut the
 * previous note's release; a slide up the keyboard exceeded that on every
 * step and put a step of 0.53 full scale in the output.
 *
 * Three milliseconds is chosen from both ends. Long enough that the ramp is
 * gentler than anything an instrument's own attack does -- so it is the
 * envelope, not the ramp, that is the fastest edge in the signal -- and
 * short enough that the steal is still a steal. The room it frees is free
 * the moment it is asked for: a voice on its way out stops being counted
 * against `poly' when it is stolen, not when the ramp finishes, so this
 * does not lengthen what a channel is holding.
 *
 * See thMidiNote::beginFade for why this is a ramp and not a release. */
#define TH_VOICE_FADE_MS 3

/* How long a transport stop takes to bring everything down -- every voice,
 * its release included, and every effect's tail. Long enough that nothing
 * clicks, which is what the steal ramp above does not have to be at the end
 * of one voice among many and a stop does, since it is everything at once;
 * short enough that a stop is a stop. See thSynth::silence. */
#define TH_STOP_FADE_MS 25

/* How many keys a mono channel remembers are down.
 *
 * MIDI has 128 pitches and the stack holds each at most once, so this cannot
 * be exceeded by a keyboard. A composer writing microtones can ask for
 * pitches between two keys, and the stack drops its oldest entry rather than
 * growing on the audio thread. */
#define TH_MONO_STACK 128

/* Upper bound on a DSP's `channels' setting. The value is read straight out of
   a .dsp file and used to size an allocation, so it needs a sanity limit. Ten
   is also the point at which the out0..out9 naming in
   thMidiChan::indexIOArgs() would need more than one digit, and the size of the
   arg index that function fills in. */
#define TH_MAX_CHANNELS 10

/* Where the output limiter stops being transparent.
 *
 * Below this a sample passes through bit-for-bit; above it the curve bends
 * towards TH_MAX. The measured median .dsp peaks at 0.78 on a single voice, so
 * a knee at 0.7 leaves most single notes entirely untouched and only starts
 * working once voices sum. */
#define TH_LIMIT_KNEE 0.7f

/* Master gain range. The default is unity: the limiter, not a gain cut, is
   what keeps the mix inside the rails, so existing patches keep the level they
   were tuned at. Above unity is allowed for quiet DSPs. */
#define TH_MASTER_GAIN_DEFAULT 1.0f

/* What a channel's amplitude starts at, on the same 0..MIDIVALMAX scale the
   engine mixes with.

   Loading used to pass 0, so a freshly loaded patch was silent until someone
   went looking for the slider -- which read as a broken DSP rather than as a
   volume at the bottom of its range. That is the failure this number exists
   to avoid, and any value clear of the bottom avoids it.

   30 rather than MIDI's nominal 100 because channels are played together, not
   one at a time: a first run comes up with four patches loaded and the
   shipped DSPs are calibrated to peak near full scale at one voice, so four
   of them at 100 arrives at the limiter rather than at the mix. Audible
   immediately, with the headroom left where the user can spend it. */
#define TH_DEFAULT_CHAN_AMP 30.0f
#define TH_MASTER_GAIN_MAX     4.0f

/* Is this sample an ordinary finite number?
 *
 * Deliberately done on the bit pattern rather than with isfinite()/isnan().
 * The tree is built with -ffast-math, which implies -ffinite-math-only, under
 * which the compiler is entitled to assume no NaNs or infinities exist and to
 * fold those predicates to a constant. Inspecting the bits cannot be optimised
 * away on that basis.
 *
 * memcpy rather than a union or a cast through float*: it is the only spelling
 * that is not a strict-aliasing violation, and every compiler turns it into a
 * register move.
 */
static inline bool thIsFinite (float sample)
{
    uint32_t bits;

    memcpy(&bits, &sample, sizeof(bits));

    /* exponent all ones => infinity (zero mantissa) or NaN (non-zero) */
    return (bits & 0x7f800000u) != 0x7f800000u;
}

/* Clamp a control input into the range a plugin's arithmetic is defined over.
 *
 * A .dsp may write any number on any arg, and a node-driven arg carries
 * whatever that node produced -- infinities and NaNs included. Every
 * comparison against a NaN is false, so a plain `if (x > hi) x = hi' lets one
 * through. Non-finite therefore answers `lo', which at every use here is the
 * inert end of the range: no cutoff, no resonance.
 */
static inline float thClampArg (float x, float lo, float hi)
{
    if (!thIsFinite(x))
        return lo;

    if (x < lo)
        return lo;

    if (x > hi)
        return hi;

    return x;
}

/* A frequency bounded to one an oscillator can turn into a wavelength.
 *
 * Every oscillator in plugins/osc divides the rate by a frequency and then
 * divides by the result, or by a fraction of it. Two things break that. A
 * frequency of zero or an infinity gives a wavelength of infinity or of zero,
 * and a division by a fraction of zero is 0/0 -- a NaN, which costs the whole
 * mix (see the guard in thMidiChan::mixNote). A negative one is not a
 * frequency at all.
 *
 * The ceiling is Nyquist: half the rate is the fastest wave the rate carries,
 * and it is also the wavelength floor of two samples that every caller's
 * arithmetic needs. The floor is the slowest wave a float `position' can
 * still be stepped through -- past about 2^24 samples `position++' stops
 * advancing and the oscillator sticks -- and zero, negative and non-finite
 * all land on it, which is a wave so slow it is a constant. That is the
 * honest answer to "no frequency" and the one every caller already handles.
 *
 * Deliberately a bound on the frequency rather than on the wavelength the
 * caller computes from it: the callers do not all spell that division the
 * same way -- some are `rate/freq' in double, some `rate * (1.0/freq)' --
 * and rounding one into the shape of the other would change what every
 * shipped graph renders for no reason. Bounding the input leaves an in-range
 * frequency bit-for-bit as it was.
 */
#define TH_WAVELENGTH_MAX 16777216.0f

/* How near a pulse width may come to collapsing one half of its cycle.
 *
 * The oscillators that take a `pw' divide by both `wavelength * pw' and
 * `wavelength * (1 - pw)', so an empty half is a division by zero. The bounds
 * are a thousandth of a cycle in from either end, which at the shortest
 * wavelength this allows is still a tenth of a sample. */
#define TH_PW_MIN 0.001f
#define TH_PW_MAX 0.999f

/* The same floor for osc::softsqr2, whose edge length is a fraction of the
   cycle rather than a second frequency, and which divides by it. */
#define TH_SW_MIN 0.001f

static inline double thBoundFreq (double freq, unsigned int rate)
{
    const double slowest = (double)rate / TH_WAVELENGTH_MAX;
    const double fastest = (double)rate / 2.0;

    /* In double, and taking a double, because the callers do not agree on
       which they hold: narrowing one of them here would re-round every
       frequency in the tree and change what every shipped graph renders.
     *
       Written as `not at least' rather than `less than' so that a NaN -- for
       which every comparison is false -- lands on the floor with the zeroes
       and the negatives rather than falling through. */
    if (!(freq >= slowest))
        return slowest;

    return (freq > fastest) ? fastest : freq;
}

/* thClampArg for an arg whose sign the callback ignores -- one it squares,
 * typically. Non-finite answers zero rather than either end.
 */
static inline float thClampMag (float x, float hi)
{
    if (!thIsFinite(x))
        return 0.0f;

    if (x > hi)
        return hi;

    if (x < -hi)
        return -hi;

    return x;
}

/* Soft limiter for the master output.
 *
 * thSynth::process sums voices with no headroom management -- and they sum
 * coherently, because every envelope peaks together on the attack -- so across
 * the shipped DSPs the median peak runs 0.78 / 1.53 / 2.34 / 3.12 for one to
 * four voices. Hard clipping that is audible as buzz on every chord.
 *
 * This is a memoryless waveshaper rather than a compressor, deliberately:
 *
 *   - it has no envelope, so a held note does not change level when other
 *     notes come and go, which is the thing that makes 1/N-style per-voice
 *     scaling unpleasant to play;
 *   - below the knee it is exactly the identity, so quiet material and single
 *     notes are unaltered;
 *   - above the knee it rounds peaks off with low-order harmonic distortion
 *     instead of the discontinuity of a hard clip;
 *   - it needs no lookahead, so it adds no latency and no state to make
 *     RT-unsafe.
 *
 * The curve is continuous in value *and* slope at the knee: tanh'(0) == 1, so
 * it leaves the linear region at unity gain, and tanh -> 1 gives an asymptote
 * of exactly TH_MAX. A wildly diverging DSP (a few of the old ones reach 1e5)
 * therefore saturates gracefully rather than needing a special case.
 */
/* How many floats an interleaved output buffer of this shape holds.
 *
 * In size_t, deliberately. Written as `channels * windowlen' the product is
 * int arithmetic that overflows before it is widened for `new float[]' or for
 * a memset's size argument -- and the two do not overflow the same way, so an
 * allocation and the memset that clears it could disagree about how big the
 * buffer is. Both callers now ask this. */
static inline size_t thOutputSamples (int channels, int windowlen)
{
    if (channels < 1 || windowlen < 1)
        return 0;

    return (size_t)channels * (size_t)windowlen;
}

static inline float thSoftLimit (float sample)
{
    const float knee = TH_LIMIT_KNEE;
    const float range = (float)TH_MAX - knee;

    /* Non-finite input has to be caught before the arithmetic, not after.
     * A NaN fails `mag <= knee' (every comparison with NaN is false), so it
     * would fall through to tanhf(NaN) = NaN, sail past thClampSample for the
     * same reason, and reach the output stage -- where the ALSA path casts it
     * to signed short, which is undefined, and the JACK path hands it to a
     * port where it poisons every downstream client.
     *
     * A diverging DSP is the realistic source (a few of the old ones already
     * reach 1e5), so silence is the right answer for NaN: there is no sensible
     * sign to preserve. Infinities do have one, so they saturate. */
    if (!thIsFinite(sample))
    {
        uint32_t bits;

        memcpy(&bits, &sample, sizeof(bits));

        if (bits & 0x007fffffu)         /* non-zero mantissa => NaN */
            return 0.0f;

        return (bits & 0x80000000u) ? (float)TH_MIN : (float)TH_MAX;
    }

    const float mag = (sample < 0.0f) ? -sample : sample;

    if (mag <= knee)
        return sample;

    const float shaped = knee + range * tanhf((mag - knee) / range);

    return (sample < 0.0f) ? -shaped : shaped;
}

/* Clamp one sample to the nominal output range.
 *
 * thSynth::process sums every sounding note into one buffer with no headroom
 * management at all -- each note contributes up to TH_MAX scaled only by the
 * channel amplitude -- so anything past a note or two runs over full scale.
 * Handing that to the output stage is not merely loud:
 *
 *   - the ALSA path casts float to signed short. Converting an out-of-range
 *     float to an integer type is undefined, and in practice it wraps, so a
 *     sample just past +1.0 comes out near -32768. Every overshoot becomes a
 *     full-scale discontinuity -- which is the harsh static on a note's
 *     attack, worsening with each extra voice held down.
 *   - the JACK path hands raw floats to a port that expects -1..1.
 *
 * Clamping is the floor, not the ceiling: it turns wraparound into ordinary
 * hard clipping. Actually keeping the mix inside the rails (per-voice gain
 * staging, or a limiter) is a separate design question -- see docs/AUDIO.md.
 *
 * NaN is handled explicitly rather than left to the comparisons. Both of them
 * are false for NaN, so it would pass straight through this function to the
 * float-to-short cast, which is undefined for it. thSoftLimit normally catches
 * that first, but this is the backstop and should not rely on being second.
 */
static inline float thClampSample (float sample)
{
    if (!thIsFinite(sample))
        return thSoftLimit(sample);

    if (sample > TH_MAX)
        return (float)TH_MAX;

    if (sample < TH_MIN)
        return (float)TH_MIN;

    return sample;
}


/* Handy debug function */

#ifdef USE_DEBUG
#define debug(...) printf("%s:%d: ", __FILE__, __LINE__);printf(__VA_ARGS__);printf("\n");
#else
#define debug(...) ;
#endif /* USE_DEBUG */

#define likely(x)   __builtin_expect((x),1)
#define unlikely(x) __builtin_expect((x),0)

#ifndef __GNUC__
# define __builtin_expect(x, expected_value) (x)
#else
# if __GNUC__ < 3
#  define __builtin_expect(x, expected_value) (x)
# endif
#endif

/* By reference, and emptied afterwards.
 *
 * Taking the map by value deleted the copy's pointers and left the caller's
 * map holding every one of them, dangling. In the five destructors that call
 * this the map dies immediately afterwards, so it never showed. In
 * thPluginManager::unloadPlugins it does not: the manager outlives the call
 * and would go on answering lookups out of plugins_ with freed thPlugins. */
template <typename T, typename U>
void DestroyMap (map<T,U> &themap)
{
    for (typename map<T,U>::iterator i=themap.begin(); i!=themap.end(); i++)
        delete i->second;

    themap.clear();
}

/* DATATYPES */
class thArg;
typedef map<string, thArg *> thArgMap;
class thNode;
typedef list<thNode *> thNodeList;


/* XXX: INCLUDES */
#include "thExport.h"
#include "thRing.h"
#include "thSampleRing.h"
#include "thProbe.h"
#include "thSynthCommand.h"
#include "thArg.h"
#include "thEndian.h"
#include "thException.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thSynthTree.h"
#include "thMidiNote.h"
#include "thChanEffect.h"
#include "thMidiChan.h"
#include "thMidiControllerConnection.h"
#include "thMidiController.h"
#include "thSynth.h"
#include "thUtil.h"

#endif /* THINK_H */
