/* $Id$ */
/*
 * Copyright (C) 2004-2005 Metaphonic Labs
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

/* number of node argument references allocated at a time */
#define ARGCHUNK 16

/* Alsa output buffer */
#define TH_BUFFER_PERIOD 1024

/* Language interface stuff... */
#define OUTPUTPREFIX "out"

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

#endif /* THINK_H */
