#ifndef THINK_H
#define THINK_H

/* Signal Range */
#define TH_MAX 256
#define TH_MIN -256
#define TH_RANGE (TH_MAX-TH_MIN)

/* Handy debug function */
#define debug(x) printf("%s:%d: ", __FILE__, __LINE__);printf(x);printf("\n");

#endif /* THINK_H */
