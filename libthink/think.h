/* $Id: think.h,v 1.27 2004/05/26 00:14:04 misha Exp $ */

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
#define __builtin_expect(x, expected_value) (x)
#endif

#ifdef __GNUC__
#if __GNUC__ < 3
#define __builtin_expect(x, expected_value) (x)
#endif
#endif

template <typename T, typename U>
void DestroyMap (map<T,U> themap)
{
	for (typename map<T,U>::iterator i=themap.begin(); i!=themap.end(); i++)
		delete i->second;
};

/* XXX: INCLUDES */
#include "thArg.h"
#include "thEndian.h"
#include "thException.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"
#include "thMidiNote.h"
#include "thMidiChan.h"
#include "thSynth.h"

#include "yygrammar.h"
#include "parser.h"

#endif /* THINK_H */
