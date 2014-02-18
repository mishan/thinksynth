/*
 * Copyright (C) 2004-2014 Metaphonic Labs
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

string thUtil::basename(const char *path)
{
    const char *s;
  
    if ((s = strrchr(path, '/')) == NULL)
        s = path;
    else
        s++;
  
    return (s);
}

/* dirname - by Lars Wirzenius. PD? */

string thUtil::dirname (const char *path)
{
    const char *last_slash;
    string ret;
    size_t len;
    
    last_slash = strrchr(path, '/');

    if (last_slash == NULL) {
        return "";
    }

    if (last_slash == path)
        ++last_slash;
        
    len = last_slash - path;

    ret = path;
    ret = ret.substr(0, len);

    return ret;
}
