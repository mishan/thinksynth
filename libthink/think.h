/* $Id: think.h,v 1.18 2004/01/31 10:08:18 ink Exp $ */

#ifndef THINK_H
#define THINK_H

#include <map>
#include <string>
#include <list>

using namespace std;

/* Sampling Rate */
#define TH_SAMPLE 44100
#define TH_WINDOW_LENGTH 512

/* Signal Range */
#define TH_MAX 127
#define TH_MIN -127
#define TH_RANGE (TH_MAX-TH_MIN)

/* For note amplitude and stuff... */
#define MIDIVALMAX 127

/* Alsa output buffer */
#define TH_BUFFER_PERIOD 1500

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

#endif /* THINK_H */
