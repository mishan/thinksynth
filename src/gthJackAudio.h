/* $Id: gthJackAudio.h,v 1.12 2004/09/18 02:16:47 joshk Exp $ */
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

#ifndef GTH_JACKAUDIO_H
#define GTH_JACKAUDIO_H

#include <jack/jack.h>
#include "gthAudio.h"

class gthJackAudio : public gthAudio
{
public:
	gthJackAudio (thSynth *argsynth)
		throw (thIOException);

	gthJackAudio (thSynth *argsynth, int (*callback)(jack_nframes_t, void *))
		throw (thIOException);

	virtual ~gthJackAudio (void);

	void *GetOutBuf (int chan, jack_nframes_t nframes);

	int Write (float *, int len);
	int Read (void *, int len);
	const gthAudioFmt *GetFormat (void) { return &ofmt; };
	void SetFormat (thSynth *argsynth);
	void SetFormat (const gthAudioFmt *afmt);

	bool ProcessEvents (void);

	int tryConnect (bool connect = true);

	int getSampleRate (void);
	int getBufferSize (void);

	jack_client_t *jack_handle;

	/* Error codes */
	enum { ERR_HANDLE_NULL = 256, ERR_NO_PLAYBACK };

protected:
	int chans;
	jack_port_t **out_ports;
	thSynth *synth;
	int (*jcallback)(jack_nframes_t, void*);

private:
	gthAudioFmt ofmt, ifmt;
	void registerPorts (void);
	void getStats (void);
};

#endif /* GTH_JACKAUDIO_H */
