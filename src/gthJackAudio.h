/* $Id: gthJackAudio.h,v 1.4 2004/05/11 02:16:31 misha Exp $ */

#ifndef THF_JACKAUDIO_H
#define THF_JACKAUDIO_H

class thfJackAudio : public thAudio
{
public:
	thfJackAudio (thSynth *argsynth)
		throw (thIOException);

	thfJackAudio (thSynth *argsynth, int (*callback)(jack_nframes_t, void *))
		throw (thIOException);

	virtual ~thfJackAudio (void);

	void *GetOutBuf (int chan, jack_nframes_t nframes);

	void Play (thAudio *audioPtr);
	int Write (float *, int len);
	int Read (void *, int len);
	const thAudioFmt *GetFormat (void) { return &ofmt; };
	void SetFormat (thSynth *argsynth);
	void SetFormat (const thAudioFmt *afmt);

	bool ProcessEvents (void);

	jack_client_t *jack_handle;
	jack_port_t *out_1, *out_2;
protected:
	int chans;
	jack_port_t **out_ports;
	thSynth *synth;
private:
	thAudioFmt ofmt, ifmt;
};

#endif /* THF_JACKAUDIO_H */
