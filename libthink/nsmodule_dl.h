/* dl(3) wrappers  for OS X
 * $Id: nsmodule_dl.h,v 1.2 2004/06/21 04:54:40 joshk Exp $
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
