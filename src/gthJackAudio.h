/* $Id: gthJackAudio.h,v 1.5 2004/05/11 19:46:11 misha Exp $ */

#ifndef THF_JACKAUDIO_H
#define THF_JACKAUDIO_H

class thfJackAudio : public thfAudio
{
public:
	thfJackAudio (thSynth *argsynth)
		throw (thIOException);

	thfJackAudio (thSynth *argsynth, int (*callback)(jack_nframes_t, void *))
		throw (thIOException);

	virtual ~thfJackAudio (void);

	void *GetOutBuf (int chan, jack_nframes_t nframes);

	int Write (float *, int len);
	int Read (void *, int len);
	const thfAudioFmt *GetFormat (void) { return &ofmt; };
	void SetFormat (thSynth *argsynth);
	void SetFormat (const thfAudioFmt *afmt);

	bool ProcessEvents (void);

	jack_client_t *jack_handle;
protected:
	int chans;
	jack_port_t **out_ports;
	thSynth *synth;
private:
	thfAudioFmt ofmt, ifmt;
};

#endif /* THF_JACKAUDIO_H */
