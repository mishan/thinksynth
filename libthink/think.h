/* $Id: think.h,v 1.8 2003/04/27 06:49:02 misha Exp $ */

#ifndef THINK_H
#define THINK_H

/* Sampling Rate */
#define TH_SAMPLE 44100

/* Signal Range */
#define TH_MAX 256
#define TH_MIN -256
#define TH_RANGE (TH_MAX-TH_MIN)

/* Handy debug function */
#ifdef USE_DEBUG
#define debug(...) printf("%s:%d: ", __FILE__, __LINE__);printf(...);printf("\n");
#else
#define debug(...) ;
#endif /* USE_DEBUG */

#endif /* THINK_H */
