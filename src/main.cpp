/* $Id: main.cpp,v 1.35 2003/04/26 02:55:12 ink Exp $ */

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

#include "thMidiNote.h"
#include "thMidiChan.h"

#include "thException.h"
#include "thAudio.h"
#include "thAudioBuffer.h"
#include "thWav.h"
#include "thOSSAudio.h"

#include "thSynth.h"

#include "parser.h"

int main (int argc, char *argv[])
{
	thMod *newmod;
	int havearg;
	char *filename;

	int i; /* XXX temporary hack to see more than 1 element of output */

	if (argc < 2) /* Not enough parameters */
	{
		printf ("error: not enough parameters\n");
syntax:
		printf("Usage: %s [options] dsp-file\n", argv[0]);
		exit(1);
	}
  
	while ((havearg = getopt (argc, argv, "h")) != -1) {
		switch (havearg) {
			case 'h':
				printf (PACKAGE " " VERSION " by Misha Nasledov, Leif M. Ames, Aaron Lehmann and Joshua Kwan\n");
				/* TODO: insert some helpful text here */
				printf("Usage: %s [options] dsp-file\n", argv[0]); /* i'd goto syntax but -h shouldn't exit 1 */
				exit(0);

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
		filename = strdup (argv[optind]);
	}

	Synth.LoadMod(filename);
	Synth.ListMods();
  
	((thMod *)Synth.FindMod("test"))->BuildSynthTree();
	Synth.AddChannel("chan1", "test");
	Synth.AddNote("chan1", 20, 100);

	newmod = ((thMod *)Synth.FindMod("test"))->Copy();
	newmod->BuildSynthTree();
	newmod->Process(newmod, 1024);

	/*  printf("  = %f\n", ((thArgValue *)((thMod *)Synth.FindMod("static"))->GetArg("static", "out"))->argValues[0]); */
	for(i=0;i<1024;i++) { /* XXX temporary hack to view more than 1 element of data */
		printf("  = %f\n", ((thArgValue *)newmod->GetArg("ionode", "out"))->argValues[i]);
	}
}
