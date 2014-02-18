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

#include "gthAudio.h"
#include "gthDummyAudio.h"

gthDummyAudio::gthDummyAudio (thSynth *synth)
{
}

gthDummyAudio::~gthDummyAudio (void)
{
}

int gthDummyAudio::Write (float *buf, int len)
{
    return 0;
}

int gthDummyAudio::Read (void *buf, int len)
{
    return 0;
}

void gthDummyAudio::SetFormat (const gthAudioFmt *afmt)
{
}

bool gthDummyAudio::ProcessEvents (void)
{
    return false;
}
