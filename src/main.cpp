/* $Id: main.cpp,v 1.100 2003/09/16 00:51:17 ink Exp $ */

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

void print_syntax(char *argv0)
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
	int havearg;
	char *filename;
	string dspname = "test"; /* XXX for debugging */

	int i;
	thAudioFmt audiofmt;
	thAudio *outputstream = NULL;
	string outputfname("test.wav");
//	signed short *outputbuffer;
	int buflen;
	float *synthbuffer;
	int notetoplay = 69;  /* XXX Remove when sequencing is external */
	int samplerate = TH_SAMPLE;
	int processwindows = 100;  /* How long does sample play */
	plugin_path = PLUGIN_PATH;

	if (argc < 2) /* Not enough parameters */
	{
		printf ("error: not enough parameters\n");
		print_syntax(argv[0]);
		exit(1);
	}
	while ((havearg = getopt (argc, argv, "hp:m:n:l:o:s:")) != -1) {
		switch (havearg) {
		case 'o':
			outputfname=optarg;
			break;
		case 's':
		  printf("changing sample rate\n");
		  samplerate = atoi(optarg);
		  break;
		case 'h':
			print_syntax(argv[0]);
			exit(0);
			break;

		case 'p':
			if (optarg[strlen(optarg)-1] != '/') {
				plugin_path = optarg;
				plugin_path += '/';
			}
			else {
				plugin_path = optarg;
			}

			break;

		case 'm':
			dspname = optarg;
			break;

		case 'n':  /* TAKE THIS OUT WHEN SEQUENCING IS EXTERNAL */
			//printf("Note to play: %s\n", optarg);
					notetoplay = (int)atof(optarg);
			break;

		case 'l':  /* windows to process, this should be done some other way too */
			processwindows = atoi(optarg);
			break;

		default:
			if (optind != argc) {
				printf ("error: unrecognized parameter\n");
				print_syntax(argv[1]);
				exit(1);
			}
		}
	}

	if (optind == argc) {
		/* XXX: so what??? read from stdin! */
		printf ("error: no input file\n");
 		print_syntax(argv[1]);
		exit(1);
	}
	else {
		filename = strdup(argv[optind]);
	}

	/* Seed the RNG */
	srand(time(NULL));
	Synth.LoadMod(filename);
	Synth.AddChannel(string("chan1"), dspname, 30.0);
	Synth.AddNote(string("chan1"), notetoplay, TH_MAX);
	//Synth.AddNote(string("chan1"), notetoplay + 4, TH_MAX);
	//Synth.AddNote(string("chan1"), notetoplay + 7, TH_MAX);
	/* changed a bunch of code relating to instantiating buffers */
	try {
		// note that this is actually bad
		// since there are other potential /dev/dsp devices
	  if ( outputfname == "/dev/dsp"){
	  	audiofmt.channels = Synth.GetChans();
		audiofmt.bits = 16;
		audiofmt.samples = samplerate;
	  	outputstream = new thOSSAudio(NULL,&audiofmt);
	  }
	  else {
	  	audiofmt.channels = Synth.GetChans();
		audiofmt.bits = 16;
		audiofmt.samples = samplerate;
	  	outputstream = new thWav((char *)outputfname.c_str(), &audiofmt);
	  }
	}
	/* added second catch block */
	catch (thIOException e) {
		/* pass */
	}
	catch (thWavException e){
		/* pass */
	}

	// changed on 9/15/03 by brandon lewis
	// all thAudio classes will work with floating point buffers
	// converting to integer internally based on format data
	/* allocate space for audio buffer based on returned audio format */

	// grab address of buffer from synthesizer
	synthbuffer = Synth.GetOutput();
	buflen = Synth.GetChans()*Synth.GetWindowLen();
	printf ("Writing %s\n",(char *)outputfname.c_str());
	// for each window
	for(i = 0; i < processwindows; i++) {

	  /* XXX DEBUGGING STUFF XXX */

	  if(i == 10) { Synth.AddNote(string("chan1"), notetoplay+4, TH_MAX); }
	  if(i == 20) { Synth.AddNote(string("chan1"), notetoplay+7, TH_MAX); }
	  if(i == 30) { Synth.AddNote(string("chan1"), notetoplay+4, TH_MAX); }
	  if(i == 40) { Synth.AddNote(string("chan1"), notetoplay, TH_MAX); }
	  if(i == 50) { Synth.AddNote(string("chan1"), notetoplay+3, TH_MAX); }
	  if(i == 60) { Synth.AddNote(string("chan1"), notetoplay+7, TH_MAX); }
	  if(i == 70) { Synth.AddNote(string("chan1"), notetoplay+3, TH_MAX); }
	  if(i == 80) { Synth.AddNote(string("chan1"), notetoplay, TH_MAX); }
	  if(i == 90) { Synth.AddNote(string("chan1"), notetoplay+3, TH_MAX); }
	  if(i == 100) { Synth.AddNote(string("chan1"), notetoplay+8, TH_MAX); }
	  if(i == 110) { Synth.AddNote(string("chan1"), notetoplay+3, TH_MAX); }
	  if(i == 120) { Synth.AddNote(string("chan1"), notetoplay, TH_MAX); }


		Synth.Process();
		// changed on 9/15/03 by brandon lewis
		// all thAudio classes will work with floating point buffers
		// converting to integer internally based on format data

		// send the window to the output device
		outputstream->Write(synthbuffer, buflen);
	}

	if (outputfname != "/dev/dsp")
	  delete (thWav*) outputstream;
	else
	  delete (thOSSAudio*) outputstream;

	//Synth.Process();

	//	Synth.PrintChan(0);
//	free(outputbuffer);
	free(filename);
}
