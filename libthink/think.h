/* $Id: think.h,v 1.11 2003/05/03 03:50:55 joshk Exp $ */

#ifndef THINK_H
#define THINK_H

/* Sampling Rate */
#define TH_SAMPLE 44100

/* Signal Range */
#define TH_MAX 256
#define TH_MIN -256
#define TH_RANGE (TH_MAX-TH_MIN)

/* For note amplitude and stuff... */
#define MIDIVALMAX 127

/* Language interface stuff... */
#define OUTPUTPREFIX "out"

/* Handy debug function */

#if 0

#ifdef USE_DEBUG
#define debug(...) printf("%s:%d: ", __FILE__, __LINE__);printf(...);printf("\n");
#else
#define debug(...) ;
#endif /* USE_DEBUG */

#endif /* above code is majorly owned */

#endif /* THINK_H */
