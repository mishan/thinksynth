/* $Id: main.cpp,v 1.105 2003/11/04 06:59:11 misha Exp $ */

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

/* the argv0 argument is a hack; we want the print_syntax function to print
   the name of the program executed, but we want it to remain modular, so we
   must pass argv[0] from the main function and hope that we do not need to
   call this function from other places */
static void print_syntax(char *argv0)
{
 	printf (PACKAGE_NAME " " PACKAGE_VERSION " by Leif M. Ames, Misha Nasledov"
	 		", Aaron Lehmann and Joshua Kwan\n");
	/* TODO: insert some helpful text here */
	printf("Usage: %s [options] dsp-file\n", argv0);
	
	printf("-h: display this help screen\n");
	printf("-p PATH: modify the plugin search path\n");
	printf("-m MOD: change the mod that will be used\n");
}

int main (int argc, char *argv[])
{
	string dspname = "test";   /* XXX: for debugging ONLY */
	string outputfname("test.wav");
	string inputfname;         /* filename of .dsp file to use */
	int notetoplay = 69;       /* XXX: Remove when sequencing is external */
	int samplerate = TH_SAMPLE;
	int processwindows = 100;  /* how long does sample play */
	int buflen, havearg, i;
	float *synthbuffer;
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
				print_syntax(argv[0]);
				exit(0);
				break;
			}
			case 'p':
			{
				if (optarg[strlen(optarg)-1] != '/') {
					plugin_path = optarg;
					plugin_path += '/';
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
				notetoplay = (int)atof(optarg);
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
					print_syntax(argv[0]);
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

	printf ("Writing to '%s'\n",(char *)outputfname.c_str());

	for (i = 0; i < processwindows; i++) {
		Synth.Process();

		/* output the current window */
		outputstream->Write(synthbuffer, buflen);
	}

	delete outputstream;

	return 0;
}
