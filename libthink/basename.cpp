#include <cstring>
#include "basename.h"

/* Stolen from ircd-hybrid */

char *
basename(char *path)
{
  char *s;
  
  if ((s = strrchr(path, '/')) == NULL)
    s = path;
  else
    s++;
  
  return (s);
}

