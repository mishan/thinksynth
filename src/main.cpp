/* $Id: main.cpp,v 1.81 2003/05/10 20:56:19 ink Exp $ */

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
#include "thEndian.h"

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
	int buflen;
	float *mixedbuffer;

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

	Synth.AddChannel(strdup("chan1"), dspname, 40.0);
	Synth.AddNote("chan1", 50, 100);

	audiofmt.channels = Synth.GetChans();
	audiofmt.bits = 16;
	audiofmt.samples = TH_SAMPLE;

	try {
		outputwav = new thWav("test.wav", &audiofmt);
	}
	catch (thWavException e) {
	}

	buflen = Synth.GetWindowLen() * Synth.GetChans();
	outputbuffer = (signed short *)alloca(buflen * sizeof(signed short));

	mixedbuffer = Synth.GetOutput();
	for(i = 0; i < 150; i++) {  /* For testing... */
		if(i==10) {
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
		}

		Synth.Process();
		for(j=0; j < buflen; j++) {
			le16(outputbuffer[j], (signed short)(((float)mixedbuffer[j]/TH_MAX)
											 *32767));
		}
		outputwav->Write(outputbuffer, buflen);
	}

	delete outputwav;

	//Synth.Process();

	//	Synth.PrintChan(0);

	free(filename);
	free(dspname);
}
