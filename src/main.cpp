#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "thArg.h"
#include "thList.h"
#include "thBSTree.h"
#include "thPlugin.h"
#include "thNode.h"
#include "thMod.h"

#include "thException.h"
#include "thAudio.h"
#include "thAudioBuffer.h"
#include "thWav.h"
#include "thOSSAudio.h"

#include "thSynth.h"

//#include "parser.h"

int main (int argc, char *argv[])
{
/*	parsemod = new thMod("newmod");     /* these are used by the parser */
/*	parsenode = new thNode("newnode", NULL);

yyparse();
//	parsemod->PrintIONode();
printf("  = %f\n", *((thArgValue *)parsemod->GetArg("test1", "point"))->argValues);
*/

	if(argc != 2) {
		printf("Usage: %s <filename>\n", argv[0]);
		exit(1);
	}

	thSynth *Synth = new thSynth;
	Synth->LoadMod(argv[1]);
	Synth->ListMods();
	((thMod *)Synth->FindMod("test"))->PrintIONode();
	printf("  = %f\n", *((thArgValue *)((thMod *)Synth->FindMod("test"))->GetArg("test1", "point"))->argValues);
}

