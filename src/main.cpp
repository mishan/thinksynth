/* $Id: main.cpp,v 1.68 2003/05/06 08:08:05 ink Exp $ */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "think.h"

#include "thArg.h"
#include "thList.h"
#include "thBSTree.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"
#include "thSynth.h"

#include "thException.h"
#include "thAudio.h"
#include "thWav.h"

#include "parser.h"

char* plugin_path;

int main (int argc, char *argv[])
{
	int havearg;
	char *filename;
	char *dspname = strdup("test"); /* XXX for debugging */
	unsigned int plugin_len;

	int i, j;
	thAudioFmt audiofmt;
	thWav *outputwav = NULL;
	signed short *outputbuffer;
	float *mixedbuffer;
	thWavException e;

	plugin_path = strdup(PLUGIN_PATH);
	plugin_len = strlen(plugin_path);

	if (argc < 2) /* Not enough parameters */
	{
		printf ("error: not enough parameters\n");
syntax:
		printf("Usage: %s [options] dsp-file\n", argv[0]);
		printf("Try %s -h for help\n", argv[0]);
		exit(1);
	}
  
	while ((havearg = getopt (argc, argv, "hp:m:")) != -1) {
		switch (havearg) {
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
				free (plugin_path);
				if (optarg[strlen(optarg)-1] != '/') {
					plugin_path = (char*)malloc(strlen(optarg)+2);
					sprintf (plugin_path, "%s/", optarg);
				}
				else {
					plugin_path = strdup(optarg);	
				}
				
				plugin_len = strlen (plugin_path);
				break;
				
			case 'm':
				free(dspname);
				dspname = strdup (optarg);
				
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

	Synth.LoadMod(filename);

	Synth.AddChannel(strdup("chan1"), dspname, 50.0);
	Synth.AddNote("chan1", 40, 100);

	audiofmt.channels = Synth.GetChans();
	audiofmt.bits = 16;
	audiofmt.samples = TH_SAMPLE;

	try {
		outputwav = new thWav("test.wav", &audiofmt);
	}
	catch (thWavException e) {
	}

	outputbuffer = (signed short *)alloca(Synth.GetWindowLen() * Synth.GetChans() * sizeof(signed short));

	mixedbuffer = Synth.GetOutput();
	for(i=0; i<100; i++) {  /* For testing... */

	  if(i==20) {
		Synth.AddNote("chan1", 42, 100);
	  }
	  if(i==40) {
		Synth.AddNote("chan1", 45, 100);
	  }
	  if(i==60) {
		Synth.AddNote("chan1", 50, 100);
		Synth.AddNote("chan1", 53, 100);
		Synth.AddNote("chan1", 55, 100);
	  }
	  if(i==75) {
		Synth.AddNote("chan1", 50, 100);
		Synth.AddNote("chan1", 52, 100);
		Synth.AddNote("chan1", 55, 100);
	  }

	  Synth.Process();
	  for(j=0; j < Synth.GetWindowLen() * Synth.GetChans(); j++) {
		outputbuffer[j] = (signed short)(((float)mixedbuffer[j]/TH_MAX)*32767);
	  }
	  outputwav->Write(outputbuffer, Synth.GetWindowLen() * Synth.GetChans());
	}

	delete outputwav;

	//Synth.Process();

	//	Synth.PrintChan(0);

	free(filename);
}
