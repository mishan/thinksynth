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
 */

/*
 * horizon.js -- the one rule the look-ahead (lookahead.js) and the SDK
 * (thinksynth.js) must agree on.
 *
 * The look-ahead composes up to the start of the bar `bars' past the one
 * the worklet is in, and the SDK stamps every change to what is composed
 * at that same line. Both work it out from the frame of the worklet's
 * latest tape batch -- which the page hears of in the same message that
 * steps the look-ahead -- so a change posted now always reaches the
 * look-ahead before it can step past its stamp, and the look-ahead never
 * has to rewind.
 */

/* The transport time, in seconds, of the line a change made at worklet
   transport time `t' lands on. */
export function horizonOf (t, barSeconds, bars)
{
    return (Math.floor(Math.max(t, 0) / barSeconds) + bars) * barSeconds;
}
