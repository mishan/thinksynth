/* $Id$ */
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

#ifndef GTH_ALSAAUDIO_H
#define GTH_ALSAAUDIO_H

#include "thException.h"
#include <alsa/asoundlib.h>

//#define ALSA_BUFSIZE 512

#define ALSA_DEFAULT_AUDIO_DEVICE "default"

/* additional arguments are usually bound to the callbacks of this signal */
typedef sigc::signal0<void> sigReadyWrite_t;

class gthALSAAudio : public gthAudio
{
public:
	gthALSAAudio (thSynth *synth)
		throw(thIOException);
	gthALSAAudio (thSynth *synth, const char *device)
		throw(thIOException);

	virtual ~gthALSAAudio ();

	int Write (float *, int len);
	int Read (void *, int len);
	const gthAudioFmt *GetFormat (void) { return &ofmt_; };
	void SetFormat (thSynth *synth);
	void SetFormat (const gthAudioFmt *afmt);

	sigReadyWrite_t signal_ready_write (void);

	bool ProcessEvents (void);

	snd_pcm_t *play_handle_;
	int nfds_;
	struct pollfd *pfds_;

	bool pollAudioEvent (Glib::IOCondition);
protected:
	thSynth *synth_;
	sigReadyWrite_t m_sigReadyWrite_;

	void main (void);
private:
	Glib::Thread *thread_;
	gthAudioFmt ofmt_, ifmt_;
	void *outbuf_;
};

#endif /* GTH_ALSAAUDIO_H */
