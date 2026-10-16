/*
 * Copyright (C) 2004-2026 The thinksynth authors
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

#ifndef IMPULSELEN_H
#define IMPULSELEN_H

/* `len' as a whole number of samples, none for a negative or NaN one and at
   most a second's: cast as it was, -1 asked the audio thread for four
   billion floats, and the throw ended the process. */
static inline unsigned int impulseLen (float len, unsigned int samples)
{
    if (!(len >= 1))
        return 0;

    return len > (float)samples ? samples : (unsigned int)len;
}

#endif /* IMPULSELEN_H */
