/*
 * Copyright (C) 2004-2014 Metaphonic Labs
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

/* number of node argument references allocated at a time */
#define ARGCHUNK 16

/* Alsa output buffer */
#define TH_BUFFER_PERIOD 1024

/* Language interface stuff... */
#define OUTPUTPREFIX "out"

/* Upper bound on a DSP's `channels' setting. The value is read straight out of
   a .dsp file and used to size an allocation, so it needs a sanity limit. Ten
   is also the point at which the out0..out9 naming in thMidiChan::process()
   would need more than one digit. */
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
 * staging, or a limiter) is a separate design question -- see REVIVAL.md.
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

template <typename T, typename U>
void DestroyMap (map<T,U> themap)
{
    for (typename map<T,U>::iterator i=themap.begin(); i!=themap.end(); i++)
        delete i->second;
};

/* DATATYPES */
class thArg;
typedef map<string, thArg *> thArgMap;
class thNode;
typedef list<thNode *> thNodeList;


/* XXX: INCLUDES */
#include "thExport.h"
#include "thRing.h"
#include "thSynthCommand.h"
#include "thArg.h"
#include "thEndian.h"
#include "thException.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thSynthTree.h"
#include "thMidiNote.h"
#include "thMidiChan.h"
#include "thMidiControllerConnection.h"
#include "thMidiController.h"
#include "thSynth.h"
#include "thUtil.h"

#endif /* THINK_H */
