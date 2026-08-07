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
 * NB: written as two one-sided comparisons rather than fabs/isnan because the
 * tree is built with -ffast-math, under which the compiler may assume no NaNs.
 */
static inline float thClampSample (float sample)
{
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
