/* $Id: gthJackAudio.h,v 1.6 2004/06/30 03:47:45 misha Exp $ */

#ifndef GTH_JACKAUDIO_H
#define GTH_JACKAUDIO_H

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

	jack_client_t *jack_handle;
protected:
	int chans;
	jack_port_t **out_ports;
	thSynth *synth;
private:
	gthAudioFmt ofmt, ifmt;
};

#endif /* GTH_JACKAUDIO_H */
