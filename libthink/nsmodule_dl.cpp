/* This code ripped outright from ircd-hybrid/src/dynlink.c. GPL. */

/*
** jmallett's dl*(3) shims for NSModule(3) systems.
*/
#include <mach-o/dyld.h>

#ifndef HAVE_DLOPEN

#ifndef	RTLD_LAZY
#define RTLD_LAZY 2185 /* built-in dl*(3) don't care */
#endif /* !RTLD_LAZY */

void undefinedErrorHandler(const char *);
NSModule multipleErrorHandler(NSSymbol, NSModule, NSModule);
void linkEditErrorHandler(NSLinkEditErrors, int, const char *, const char *);
char *dlerror(void);
void *dlopen(char *, int);
int dlclose(void *);
void *dlsym(void *, char *);

static int firstLoad = TRUE;
static int myDlError;
static const char *myErrorTable[] =
{
  "Loading file as object failed\n",
  "Loading file as object succeeded\n",
  "Not a valid shared object\n",
  "Architecture of object invalid on this architecture\n",
  "Invalid or corrupt image\n",
  "Could not access object\n",
  "NSCreateObjectFileImageFromFile failed\n",
  NULL
};

void undefinedErrorHandler(const char *symbolName)
{
  sendto_realops_flags(UMODE_ALL, L_ALL, "Undefined symbol: %s", symbolName);
  ilog(L_WARN, "Undefined symbol: %s", symbolName);
  return;
}

NSModule multipleErrorHandler(NSSymbol s, NSModule old, NSModule new)
{
  /* XXX
  ** This results in substantial leaking of memory... Should free one
  ** module, maybe?
  */
  sendto_realops_flags(UMODE_ALL, L_ALL, "Symbol `%s' found in `%s' and `%s'",
                       NSNameOfSymbol(s), NSNameOfModule(old), NSNameOfModule(new));
  ilog(L_WARN, "Symbol `%s' found in `%s' and `%s'", NSNameOfSymbol(s),
       NSNameOfModule(old), NSNameOfModule(new));
  /* We return which module should be considered valid, I believe */
  return(new);
}

void linkEditErrorHandler(NSLinkEditErrors errorClass, int errnum,
                          const char *fileName, const char *errorString)
{
  sendto_realops_flags(UMODE_ALL, L_ALL, "Link editor error: %s for %s",
                       errorString, fileName);
  ilog(L_WARN, "Link editor error: %s for %s", errorString, fileName);
  return;
}

char *dlerror(void)
{
  return(myDlError == NSObjectFileImageSuccess ? NULL : myErrorTable[myDlError % 7]);
}

void *dlopen(char *filename, int unused)
{
  NSObjectFileImage myImage;
  NSModule myModule;

  if (firstLoad)
  {
    /* If we are loading our first symbol (huzzah!) we should go ahead
     * and install link editor error handling!
     */
    NSLinkEditErrorHandlers linkEditorErrorHandlers;

    linkEditorErrorHandlers.undefined = undefinedErrorHandler;
    linkEditorErrorHandlers.multiple  = multipleErrorHandler;
    linkEditorErrorHandlers.linkEdit  = linkEditErrorHandler;
    NSInstallLinkEditErrorHandlers(&linkEditorErrorHandlers);
    firstLoad = FALSE;
  }

  myDlError = NSCreateObjectFileImageFromFile(filename, &myImage);

  if (myDlError != NSObjectFileImageSuccess)
    return(NULL);

  myModule = NSLinkModule(myImage, filename, NSLINKMODULE_OPTION_PRIVATE);
  return((void *)myModule);
}

int dlclose(void *myModule)
{
  NSUnLinkModule(myModule, FALSE);
  return(0);
}

void *dlsym(void *myModule, char *mySymbolName)
{
  NSSymbol mySymbol;

  mySymbol = NSLookupSymbolInModule((NSModule)myModule, mySymbolName);
  return NSAddressOfSymbol(mySymbol);
}
#endif /* !HAVE_DLOPEN */

