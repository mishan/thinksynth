/* $Id: main.cpp,v 1.93 2003/06/14 23:41:11 aaronl Exp $ */

#include "think.h"
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>


#include "thArg.h"
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

string plugin_path;

#if defined(__i386__) && defined(__GNUC__)
int threednow, threednowext;
#endif

int main (int argc, char *argv[])
{
	int havearg;
	char *filename;
	string dspname = "test"; /* XXX for debugging */

	int i, j;
	thAudioFmt audiofmt;
	thWav *outputwav = NULL;
	signed short *outputbuffer;
	int buflen;
	float *mixedbuffer;
	int notetoplay = 69;  /* XXX Remove when sequencing is external */
	int processwindows = 100;  /* How many windows do we process? */

	plugin_path = PLUGIN_PATH;

	/* check for 3dnow */
#if defined (__i386__) && defined (__GNUC__)
	asm volatile ("\
xorl %%edx, %%edx\n\t\
xorl %%ecx, %%ecx\n\t\
xorl %%eax,%%eax         # CPUID function: Vendor ID\n\t\
cpuid                    # Invoke cpuid function\n\t\
testl %%eax,%%eax\n\t\
jz  1f\n\t\
movl $0x80000000, %%eax  # CPUID function: Largest extended value\n\t\
cpuid\n\t\
cmpl $0x80000001, %%eax  # We can execute feature #1, right?\n\t\
jl  1f                   # If not, we're done here.\n\t\
movl $0x80000001, %%eax  # CPUID function: Signature + features\n\t\
cpuid\n\t\
movl %%edx, %%ecx\n\t\
andl $0x80000000, %%edx\n\t\
shrl $0x0000001F, %%edx\n\t\
andl $0x40000000, %%ecx\n\t\
shrl $0x0000001E, %%ecx\n\t\
1:\n\t"
		: "=d" (threednow), "=c" (threednowext) : : "eax", "ebx");
	printf ("3dnow support: %i, 3dnowext support: %i\n", threednow, threednowext);
#endif

	if (argc < 2) /* Not enough parameters */
	{
		printf ("error: not enough parameters\n");
syntax:
		printf("Usage: %s [options] dsp-file\n", argv[0]);
		printf("Try %s -h for help\n", argv[0]);
		exit(1);
	}

	while ((havearg = getopt (argc, argv, "hp:m:n:l:")) != -1) {
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
	printf ("Writing test.wav\n");

	mixedbuffer = Synth.GetOutput();
	for(i = 0; i < processwindows; i++) {  /* For testing... */
/*		if(i==10) {
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
*/
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
}
