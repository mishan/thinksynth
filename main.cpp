#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "Exception.h"
#include "Wav.h"
#include "OSSAudio.h"

Wav *create_wav(char *name)
{
	try {
		Wav *wav = new Wav(name);

		return wav;
	}
	catch (IOException e) {
		switch(e) {
		case NOSUCHFILE:
			fprintf(stderr, "Wav::Wav: no such file/directory: %s\n", name);
			break;
		default:
			fprintf(stderr, "create_wav: unhandled exception %d\n", (int)e);
			break;
		}
	}
	catch (WavException e) {
		fprintf(stderr, "%s\n", WavError(e));
	}

	return NULL;
}

int main (int argc, char *argv[])
{
	Wav *wav;
	WavFormat wfmt;
	OSSAudio *audio;
	AudioFormat afmt;
	int len;

	if(argc < 2) {
		fprintf(stderr, "usage: %s [file]\n", argv[0]);
		return 1;
	}

	if(!(wav = create_wav(argv[1]))) {
		exit(1);
	}
	
	wfmt = wav->get_format();
	
	afmt.channels = wfmt.channels;
	afmt.samples = wfmt.samples;
	afmt.bits = wfmt.bits;
	
	audio = new OSSAudio(NULL, &afmt);
	
	audio->open_audio();
	
	switch(wfmt.bits) {
	case 8:
	{
		unsigned char *buf = new unsigned char[wfmt.len+1];
		while((len = wav->read_wav(buf, BUF_SIZE)) > 0) {
			audio->write_audio(buf, len);
		}
	}
	break;
	case 16:
	{
		signed short *buf = new signed short[wfmt.len+1];
		while((len = wav->read_wav(buf, BUF_SIZE)) > 0) {
			audio->write_audio(buf, len);
		}
	}
	break;
	default:
		break;
	}
	
	delete wav;
	delete audio;
}
