/* $Id: thSynth.cpp,v 1.61 2003/09/15 23:17:06 brandon Exp $ */

#include "config.h"
#include "think.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "thArg.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"
#include "thMidiNote.h"
#include "thMidiChan.h"
#include "thSynth.h"

#include "parser.h"

thSynth::thSynth (void)
{
	thWindowlen = 1024;
	thChans = 2;  /* mono / stereo / etc */
	/* added by brandon on 9/15/03 */
	// intialize default sample rate
	thSamples = TH_SAMPLE;

	/* We should make a function to allocate this,
	so we can easily change thChans and thWindowlen */
	thOutput = new float[thChans*thWindowlen];
}

thSynth::~thSynth (void)
{
	delete [] thOutput;
	DestroyMap(modlist);
	DestroyMap(channels);
}

void thSynth::LoadMod(const char *filename)
{
	if ((yyin = fopen(filename, "r")) == NULL) { /* ENOENT or smth */
		fprintf (stderr, "couldn't open %s: %s\n", filename, strerror(errno));
		exit(1);
	}

	parsemod = new thMod("newmod");     /* these are used by the parser */
	parsenode = new thNode("newnode", NULL);

	yyparse();

	fclose(yyin);

	delete parsenode;

	parsemod->BuildSynthTree();
	modlist[parsemod->GetName()] = parsemod;
}

/* Make these voids return something and add error checking everywhere! */
void thSynth::ListMods(void)
{
	for (map<string, thMod*>::const_iterator im = modlist.begin(); im != modlist.end(); ++im)
		printf("%s\n", im->first.c_str());
}

void thSynth::AddChannel(const string &channame, const string &modname, float amp)
{
	channels[channame] = new thMidiChan(FindMod(modname), amp, thWindowlen);
}

thMidiNote *thSynth::AddNote(const string &channame, float note, float velocity)
{
	thMidiChan *chan = channels[channame];
	thMidiNote *newnote = chan->AddNote(note, velocity);

	return newnote;
}

void thSynth::Process()
{
	memset(thOutput, 0, thChans*thWindowlen*sizeof(float));

	int i, j, mixchannels, notechannels;
	thMidiChan *chan;
	float *chanoutput;

	for (map<string, thMidiChan*>::const_iterator im = channels.begin(); im != channels.end(); ++im)
	{
		chan = im->second;

		notechannels = chan->GetChannels();
		mixchannels = notechannels;

		if(mixchannels > thChans) {
			mixchannels = thChans;
		}

		chan->Process();
		chanoutput = chan->GetOutput();

		for(i = 0; i < mixchannels; i++) {
			for(j = 0 ;j < thWindowlen; j++) {
				thOutput[i+(j*thChans)] += chanoutput[i+(j*notechannels)];
			}
		}
	}
}

void thSynth::PrintChan(int chan)
{
	int i;

	for(i = 0; i < thWindowlen; i++) {
		printf("-=- %f\n", thOutput[(i*thChans)+chan]);
	}
}
