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

#include "config.h"

#include "gthDummyAudio.h"

gthDummyAudio::gthDummyAudio (void) : running_(false)
{
    fmt_.rate = 0;
    fmt_.channels = 0;
    fmt_.frames = 0;
}

gthDummyAudio::~gthDummyAudio (void)
{
}

bool gthDummyAudio::open (const gthAudioFmt &want, gthAudioSource *source)
{
    fmt_ = want;

    /* The source still gets prepared. A patch that only misbehaves once the
       renderer exists should misbehave under -d none too, rather than being
       hidden by the dummy device doing nothing at all. */
    if (source != NULL)
        source->prepare(fmt_.frames ? fmt_.frames : 512,
                        (unsigned)(fmt_.channels ? fmt_.channels : 2));

    return true;
}

bool gthDummyAudio::start (void)
{
    running_ = true;
    return true;
}

void gthDummyAudio::stop (void)
{
    running_ = false;
}
