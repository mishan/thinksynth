#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "wav.h"
#include "oss.h"

int main (int argc, char *argv[])
{
	Wav wav;
	Wav_Fmt wfmt;
	OSSAudio *audio;
	AudioFormat afmt;

	int len;

	wav.open_wav(argv[1]);
	wfmt = wav.get_fmt();

	afmt.channels = wfmt.channels;
	afmt.samples = wfmt.samples;
	afmt.bits = wfmt.bits;

	audio = new OSSAudio(NULL, &afmt);

	audio->open_audio();

	switch(wfmt.bits) {
	case 8:
	{
		unsigned char *buf = new unsigned char[wfmt.len+1];
		while((len = wav.read_wav(buf, BUF_SIZE)) > 0) {
			audio->write_audio(buf, len);
		}
	}
		break;
	case 16:
	{
		signed short *buf = new signed short[wfmt.len+1];
		while((len = wav.read_wav(buf, BUF_SIZE)) > 0) {
			audio->write_audio(buf, len);
		}
	}
		break;
	default:
		break;
	}

	delete audio;
}
