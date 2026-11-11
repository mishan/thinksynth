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

/* thinkbox's fourth screen: a piece built into the firmware, played by the
   .gen scheduler on a task of its own, through a pool of a compiled patch
   per channel (composer.cpp). Listen only. */
#ifndef BOX_COMPOSER_H
#define BOX_COMPOSER_H 1

#include <stddef.h>
#include <stdint.h>

#include "pool.h"

#define COMPOSER_CHANNELS 16

/* Each channel's pool, or NULL: the audio task's, set by composerApply,
   and read by both halves as they render. */
extern Patch *composerChannel[COMPOSER_CHANNELS];

/* box.cpp's, under its lock: a note or a knob to a pool as a finger's
   would go, a free voice taken only while the blocks have room. */
void boxPlay (Patch *p, const thhAction &a, int owner, float level);
/* box.cpp's: the samples out that weren't finite, played as silence. */
extern volatile int boxNonFinite;

/* The scheduler's task, started once. */
void composerStart (void);
/* The control task's: the screen shown or left (the piece goes on where
   it paused, its notes let go), and the next piece. */
void composerShow (bool shown);
void composerNext (void);
/* The audio task's, under the lock before a block: what falls due by the
   time the block plays, on the clock of the blocks themselves. */
void composerApply (int64_t blockAt);

/* The display task's: the screen below its top `top' rows drawn, all of
   it or what changed. */
void composerDraw (bool all, int top);
/* The console's: the piece, and what went wrong playing it. */
void composerStatus (char *line, size_t size);

#endif
