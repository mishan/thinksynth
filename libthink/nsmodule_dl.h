/* dl(3) wrappers  for OS X
 * $Id: nsmodule_dl.h,v 1.1 2003/12/21 05:57:48 joshk Exp $
 */

#ifndef INCLUDED_nsmodule_dl_h
#define INCLUDED_nsmodule_dl_h

char *dlerror(void);
void *dlopen(char *filename, int unused);
int dlclose(void *myModule);
void *dlsym(void *myModule, char *mySymbolName);

#endif
