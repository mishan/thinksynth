#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "Exception.h"
#include "Audio.h"
#include "AudioBuffer.h"
#include "Wav.h"
#include "OSSAudio.h"

int main (int argc, char *argv[])
{
	Wav *wav;
	WavFormat wfmt;
	OSSAudio *audio;
	AudioFormat afmt;
	AudioBuffer *buffer;
	int len;

	if(argc < 2) {
		fprintf(stderr, "usage: %s [file]\n", argv[0]);
		return 1;
	}

	if(!(wav = new_Wav(argv[1]))) {
		exit(1);
	}
	
	wfmt = wav->get_format();
	
	afmt.channels = wfmt.channels;
	afmt.samples = wfmt.samples;
	afmt.bits = wfmt.bits;
	
	if(!(audio = new_OSSAudio(NULL, &afmt))) {
		exit(1);
	}
	
	buffer = new AudioBuffer(BUF_SIZE, (Audio *)wav);
	audio->play(buffer);

	/*

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
		}*/
	
	delete wav;
	delete audio;
	delete buffer;
}
