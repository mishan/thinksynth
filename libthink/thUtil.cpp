/* $Id: thNode.h 1240 2004-08-16 09:34:48Z misha $ */
/*
 * Copyright (C) 2004-2005 Metaphonic Labs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <cstdlib>
#include <cstring>

#include "thUtil.h"

static int RangeArray[] = {10, 100, 1000, 10000, 100000, 1000000, 10000000,
						   100000000, 1000000000};

static int RangeSize = sizeof(RangeArray)/sizeof(int);

int thUtil::getNumLength (int num)
{
	num = abs(num);
	int i;

	for (i = 0; i < RangeSize; i++) {
		if (num < RangeArray[i]) {
			return i+1;
		}
	}

	return RangeSize+1;
}

/* Stolen from ircd-hybrid */

char* thUtil::basename(char *path)
{
	char *s;
  
	if ((s = strrchr(path, '/')) == NULL)
		s = path;
	else
		s++;
  
	return (s);
}

/* dirname - stolen from glibc in part. GPL 2. */

char *
thUtil::dirname (char *path_)
{
  /* Deviation from standard: allocate a new buffer to hold the string. */
  char *path = strdup(path_);
  static const char dot[] = ".";
  char *last_slash;

  /* Find last '/'.  */
  last_slash = path != NULL ? strrchr (path, '/') : NULL;

  if (last_slash != NULL && last_slash != path && last_slash[1] == '\0')
    {
      /* Determine whether all remaining characters are slashes.  */
      char *runp;

      for (runp = last_slash; runp != path; --runp)
        if (runp[-1] != '/')
          break;

      /* The '/' is the last character, we have to look further.  */
      if (runp != path)
        last_slash = (char*) memrchr ((void*)path, '/', runp - path);
    }

  if (last_slash != NULL)
    {
      /* Determine whether all remaining characters are slashes.  */
      char *runp;

      for (runp = last_slash; runp != path; --runp)
        if (runp[-1] != '/')
          break;

      /* Terminate the path.  */
      if (runp == path)
        {
          /* The last slash is the first character in the string.  We have to
             return "/".  As a special case we have to return "//" if there
             are exactly two slashes at the beginning of the string.  See
             XBD 4.10 Path Name Resolution for more information.  */
          if (last_slash == path + 1)
            ++last_slash;
          else
            last_slash = path + 1;
        }
      else
        last_slash = runp;

      last_slash[0] = '\0';
    }
  else
    /* This assignment is ill-designed but the XPG specs require to
       return a string containing "." in any case no directory part is
       found and so a static and constant string is required.  */
    path = (char *) dot;

  if (path != NULL)
  {
    /* Save space; rval is somewhat smaller than path*/
    char* rval = strdup(path);
    free(path);
    return rval;
  }

  return path;
}
