#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "Exception.h"
#include "thAudio.h"
#include "AudioBuffer.h"
#include "thWav.h"
#include "thOSSAudio.h"

int main (int argc, char *argv[])
{
	thWav *wav;
	thOSSAudio *audio;
	const thAudioFmt *afmt;
	AudioBuffer *buffer;

	if(argc < 2) {
		fprintf(stderr, "usage: %s [file]\n", argv[0]);
		return 1;
	}

	if(!(wav = new_thWav(argv[1]))) {
		exit(1);
	}
	
	afmt = wav->GetFormat();
	
	if(!(audio = new_thOSSAudio(NULL, afmt))) {
		exit(1);
	}
	
	audio->Play((thAudio *)wav);

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

//	delete buffer;
}
