/* $Id: main.cpp,v 1.53 2003/04/30 03:34:33 joshk Exp $ */

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

#include "parser.h"

char* plugin_path;

int main (int argc, char *argv[])
{
	int havearg;
	char *filename;
	char dspname[] = "test"; /* XXX for debugging */
	plugin_path = strdup(PLUGIN_PATH);
	unsigned int plugin_len = strlen(plugin_path);

	/* int i; XXX temporary hack to see more than 1 element of output */

	if (argc < 2) /* Not enough parameters */
	{
		printf ("error: not enough parameters\n");
syntax:
		printf("Usage: %s [options] dsp-file\n", argv[0]);
		exit(1);
	}
  
	while ((havearg = getopt (argc, argv, "hp:m:")) != -1) {
		switch (havearg) {
			case 'h':
				printf (PACKAGE " " VERSION " by Leif M. Ames, Misha Nasledov, Aaron Lehmann and Joshua Kwan\n");
				/* TODO: insert some helpful text here */
				printf("Usage: %s [options] dsp-file\n", argv[0]); /* i'd goto syntax but -h shouldn't exit 1 */
				exit(0);

				break;

			case 'p':
				if (optarg[strlen(optarg)-1] != '/') {
					plugin_path = (char*)(realloc(plugin_path, (plugin_len+1) * sizeof(char*)));
					strcat(plugin_path, "/");
				}

				strcpy(plugin_path, optarg);
				break;

			case 'm':
				strcpy(dspname, optarg);
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
		filename = argv[optind];
	}

	Synth.LoadMod(filename);
	Synth.ListMods();
  
	((thMod *)Synth.FindMod(dspname))->BuildSynthTree();

	Synth.AddChannel(strdup("chan1"), dspname, 80.0);
	Synth.AddNote("chan1", 20, 100);
	Synth.Process();
	Synth.Process();

	/*newmod = ((thMod *)Synth.FindMod(dspname))->Copy();
	  newmod->BuildSynthTree();
	  newmod->Process(1024);
	*/
	/*  printf("  = %f\n", ((thArgValue *)((thMod *)Synth.FindMod("static"))->GetArg("static", "out"))->argValues[0]); */
	//	for(i=0;i<1024;i++) { /* XXX temporary hack to view more than 1 element of data */
	/*	printf("  = %f\n", ((thArgValue *)newmod->GetArg("ionode", "out0"))->argValues[i]);
	}
*/
	free(filename);
}
