/* $Id: main.cpp,v 1.96 2003/09/14 20:43:19 ink Exp $ */

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

int main (int argc, char *argv[])
{
	int havearg;
	char *filename;
	string dspname = "test"; /* XXX for debugging */

	int i,j,k;
	thAudioFmt audiofmt;
	thAudio *outputstream = NULL;
	string outputfname("test.wav");
	signed short *outputbuffer;
	int buflen;
	float *synthbuffer;
	int notetoplay = 69;  /* XXX Remove when sequencing is external */
	int samplerate = TH_SAMPLE;
	int processwindows = 100;  /* How long does sample play */
	plugin_path = PLUGIN_PATH;

	if (argc < 2) /* Not enough parameters */
	{
		printf ("error: not enough parameters\n");
syntax:
		printf("Usage: %s [options] dsp-file\n", argv[0]);
		printf("Try %s -h for help\n", argv[0]);
		exit(1);
	}
// changed this line here
	while ((havearg = getopt (argc, argv, "hp:m:n:l:o:s:")) != -1) {
		switch (havearg) {
// added this case here
		case 'o':
			outputfname=optarg;
			break;
// and this case here
		case 's':
		  printf("changing sample rate\n");
		  samplerate = atoi(optarg);
		  break;
		case 'h':
			printf (PACKAGE_NAME " " PACKAGE_VERSION " by Leif M. Ames, Misha Nasledov, Aaron Lehmann and Joshua Kwan\n");
			/* TODO: insert some helpful text here */
			printf("Usage: %s [options] dsp-file\n", argv[0]); /* i'd goto syntax but -h shouldn't exit 1 */

			printf("-h: display this help screen\n");
			printf("-p PATH: modify the plugin search path\n");
			printf("-m MOD: change the mod that will be used\n");
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
				goto syntax;
			}
		}
	}

	if (optind == argc) {
		printf ("error: no input file\n");
		goto syntax;
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

	/* changing all the following code do deal with the
	potentiality that the output file may not support
	the intended audio format */

	memcpy(&audiofmt,outputstream->GetFormat(),sizeof(thAudioFmt));

	/* allocate space for audio buffer based on returned audio format */
	buflen = Synth.GetWindowLen() * Synth.GetChans();
	outputbuffer = (signed short *) malloc(buflen*sizeof(signed short));
	printf("length of buffer %d", buflen,audiofmt.bits/8);
	// grab address of buffer from synthesizer
	synthbuffer = Synth.GetOutput();

	printf ("Writing %s\n",(char *)outputfname.c_str());
	// for each window
	for(i = 0; i < processwindows; i++) {
	  		// get the synth to process it's data
	  /*if(i==10) {
			Synth.AddNote("chan1", 52, 100);
		}
		else if(i==20) {
			Synth.AddNote("chan1", 54, 100);
		}
		else if(i==30) {
			Synth.AddNote("chan1", 55, 100);
			//Synth.AddNote("chan1", 45, 100);
			//Synth.AddNote("chan1", 49, 100);
		}
		else if(i==40) {
			Synth.AddNote("chan1",57, 100);
			//Synth.AddNote("chan1", 44, 100);
			//Synth.AddNote("chan1", 47, 100);
		}
		else if(i==50) {
		  Synth.AddNote("chan1", 59, 100);
		}
		else if(i==60) {
		  Synth.AddNote("chan1", 61, 100);
		}
		else if(i==70) {
		  Synth.AddNote("chan1", 62, 100);
		}
		else if(i==80) {
		  Synth.AddNote("chan1", 64, 100);
		}
		else if(i==100) {
		  Synth.AddNote("chan1", 50, 100);
		  Synth.AddNote("chan1", 54, 100);
		  Synth.AddNote("chan1", 57, 100);
		  }*/
		Synth.Process();
		// convert floating point synth data to integer data
		// which we use in the real world, normalizing=)
		// if output channels > than Synth.GetChans()...duplicate sample
		for(j=0; j < buflen; j++) {
			outputbuffer[j]= (signed short)(((float)synthbuffer[j]/TH_MAX) *32767);
		}
		// send the window to the output device
		outputstream->Write(outputbuffer, buflen);
	}

	if (outputfname != "/dev/dsp")
	  delete (thWav*) outputstream;
	else
	  delete (thOSSAudio*) outputstream;

	//Synth.Process();

	//	Synth.PrintChan(0);
	free(outputbuffer);
	free(filename);
}
