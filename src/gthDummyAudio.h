/* $Id: gthDummyAudio.h 1605 2005-02-17 23:35:55Z misha $ */
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

#ifndef GTH_DUMMYAUDIO_H
#define GTH_DUMMYAUDIO_H

#include "gthAudio.h"
class thSynth;

class gthDummyAudio : public gthAudio
{
public:
	gthDummyAudio (thSynth *synth);
	virtual ~gthDummyAudio (void);

	int Write (float *, int len);
	int Read (void *, int len);
	const gthAudioFmt *GetFormat (void) { return &ofmt; };
	void SetFormat (const gthAudioFmt *fmt);

	bool ProcessEvents (void);

private:
	gthAudioFmt ofmt;
};

#endif /* GTH_DUMMYAUDIO_H */
