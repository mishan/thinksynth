/* $Id: gthAudio.h,v 1.3 2004/08/16 09:34:48 misha Exp $ */
/*
 * Copyright (C) 2004 Metaphonic Labs
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

#ifndef GTH_AUDIO_H
#define GTH_AUDIO_H 1

struct gthAudioFmt {
	short format;
	short channels;
	short bits;

	int samples;
	int len;
	int period;
};

class gthAudio
{
public:
	inline virtual ~gthAudio () {
	}

	virtual int Read(void *, int len) = 0;
	virtual int Write(float *, int len) = 0;

	virtual const gthAudioFmt *GetFormat(void) = 0;

	virtual void SetFormat (const gthAudioFmt *fmt) = 0;

	virtual bool ProcessEvents (void) = 0;
private:
};

#endif /* GTH_AUDIO_H */
