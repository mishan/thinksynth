/* $Id: gthJackAudio.h,v 1.1 2004/04/15 07:41:17 misha Exp $ */

#ifndef THF_JACKAUDIO_H
#define THF_JACKAUDIO_H

class thfJackAudio : public thAudio
{
public:
	thfJackAudio (thSynth *argsynth)
		throw (thIOException);

	virtual ~thfJackAudio (void);

	void Play (thAudio *audioPtr);
	int Write (float *, int len);
	int Read (void *, int len);
	const thAudioFmt *GetFormat (void) { return &ofmt; };
	void SetFormat (thSynth *argsynth);
	void SetFormat (const thAudioFmt *afmt);

	jack_client_t *jack_handle;
	jack_port_t *output_port;
protected:
	thSynth *synth;
private:
	thAudioFmt ofmt, ifmt;
};

#endif /* THF_JACKAUDIO_H */
