/* $Id: main.cpp,v 1.107 2003/11/05 02:54:15 joshk Exp $ */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include "think.h"

#include "thArg.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"
#include "thSynth.h"

#include "thException.h"
#include "thAudio.h"
#include "thWav.h"
#include "thOSSAudio.h"
#include "thEndian.h"

#include "parser.h"

string plugin_path;

const char syntax[] = \
PACKAGE_NAME " " PACKAGE_VERSION " by Leif M. Ames, Misha Nasledov, "
"Aaron Lehmann and Joshua Kwan\n"
"Usage: %s [options] dsp-file\n"
"-h\t\tdisplay this help screen\n"
"-p [path]\tmodify the plugin search path\n"
"-m [mod]\tchange the mod that will be used\n";

int main (int argc, char *argv[])
{
	string dspname = "test";   /* XXX: for debugging ONLY */
	string outputfname = "test.wav";
	string inputfname;         /* filename of .dsp file to use */
	int notetoplay = 69;       /* XXX: Remove when sequencing is external */
	int samplerate = TH_SAMPLE;
	int processwindows = 100;  /* how long does sample play */
	int buflen, havearg, i;
	float *synthbuffer = NULL;
	thAudioFmt audiofmt;
	thAudio *outputstream = NULL;

	plugin_path = PLUGIN_PATH;

	while ((havearg = getopt (argc, argv, "hp:m:n:l:o:s:")) != -1) {
		switch (havearg)
		{
			case 'o':
			{
				outputfname = optarg;
				break;
			}
			case 's':
			{
				printf("changing sample rate\n");
				samplerate = atoi(optarg);
				break;
			}
			case 'h':
			{
				printf(syntax, argv[0]);
				exit(0);
				break;
			}
			case 'p':
			{
				if (optarg[strlen(optarg)-1] != '/') {
					plugin_path = optarg + '/';
				}
				else {
					plugin_path = optarg;
				}
				
				break;
			}
			case 'm':
			{
				dspname = optarg;
				break;
			}
			case 'n':  /* XXX: TAKE THIS OUT WHEN SEQUENCING IS EXTERNAL */
			{
				notetoplay = atoi(optarg);
				break;
			}
			case 'l':  /* number of windows to process */
			{
				processwindows = atoi(optarg);
				break;
			}
			default:
			{
				if (optind != argc) {
//					printf ("error: unrecognized parameter\n");
					printf(syntax, argv[0]);
					exit(1);
				}
				break;
			}
		}
	}

	if (optind == argc) {
		/* XXX: thinksynth segfaults if no input is read */
		Synth.LoadMod(stdin);
	}
	else {
		inputfname = argv[optind];
		Synth.LoadMod(inputfname);
	}

	/* seed the random number generator */
	srand(time(NULL));

	Synth.AddChannel(string("chan1"), dspname, 30.0);
	Synth.AddNote(string("chan1"), notetoplay, TH_MAX);

	/* all thAudio classes will work with floating point buffers converting to
	   integer internally based on format data */
	try {
		/* XXX: note that this is actually bad since there are potentially
		   other /dev/dsp devices */
		if (outputfname == "/dev/dsp") {
			audiofmt.channels = Synth.GetChans();
			audiofmt.bits = 16;
			audiofmt.samples = samplerate;
			outputstream = new thOSSAudio(NULL, &audiofmt);
		}
		else {
			audiofmt.channels = Synth.GetChans();
			audiofmt.bits = 16;
			audiofmt.samples = samplerate;
			outputstream = new thWav((char *)outputfname.c_str(), &audiofmt);
		}
	}
	/* XXX: handle these exceptions and consolidate them to one exception
	   datatype */
	catch (thIOException e) {
		printf("thIOEXception on /dev/dsp\n");
		/* XXX */
	}
	catch (thWavException e) {
		printf("thWavException on %s\n", outputfname.c_str());
		/* XXX */
	}

	/* grab address of buffer from synth object */
	synthbuffer = Synth.GetOutput();

	buflen = Synth.GetChans()*Synth.GetWindowLen();

	printf ("Writing to '%s'\n", outputfname.c_str());

	for (i = 0; i < processwindows; i++) {
		Synth.Process();

		/* output the current window */
		outputstream->Write(synthbuffer, buflen);
	}

	delete outputstream;

	return 0;
}
