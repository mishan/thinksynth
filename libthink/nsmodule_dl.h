/* dl(3) wrappers  for OS X
 * $Id$
 */

#ifndef INCLUDED_nsmodule_dl_h
#define INCLUDED_nsmodule_dl_h

#include <mach-o/dyld.h>

#ifndef RTLD_LAZY
#  define RTLD_LAZY 2185
#endif
#ifndef RTLD_NOW
#  define RTLD_NOW RTLD_LAZY
#endif

char *dlerror(void);
void *dlopen(char *filename, int unused);
int dlclose(void *myModule);
void *dlsym(void *myModule, char *mySymbolName);

#endif
