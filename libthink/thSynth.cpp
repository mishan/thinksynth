/* $Id: thSynth.cpp,v 1.63 2004/01/25 23:59:33 ink Exp $ */

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
	/* XXX: these should all be arguments and we should have corresponding
	   accessor/mutator methods for these arguments */

	/* XXX: whoever commented this class: you found reason to comment on the
	   obvious thSamples line but could offer no elucidation for this?? */
	thWindowlen = 1024;

	thChans = 2;  /* mono / stereo / etc */

	/* intialize default sample rate */
	thSamples = TH_SAMPLE;

	/* We should make a function to allocate this, so we can easily change
	   thChans and thWindowlen */
	thOutput = new float[thChans*thWindowlen];
}

thSynth::~thSynth (void)
{
	delete [] thOutput;
	DestroyMap(modlist);
	DestroyMap(channels);
}

void thSynth::LoadMod(const string &filename)
{
	if ((yyin = fopen(filename.c_str(), "r")) == NULL) { /* ENOENT or smth */
		fprintf (stderr, "couldn't open %s: %s\n", filename.c_str(),
				 strerror(errno));
		exit(1);
	}

	/* XXX: do we re-allocate these everytime we read a new input file?? */
     /* these are used by the parser */
	parsemod = new thMod("newmod");
	parsenode = new thNode("newnode", NULL);

	yyparse();

	fclose(yyin);

	delete parsenode;

	parsemod->BuildSynthTree();
	modlist[parsemod->GetName()] = parsemod;
}

void thSynth::LoadMod(FILE *input)
{
	yyin = input;

	/* XXX: do we re-allocate these everytime we read a new input file?? */
	/* these are used by the parser */
	parsemod = new thMod("newmod");
	parsenode = new thNode("newnode", NULL);

	yyparse();

	delete parsenode;

	parsemod->BuildSynthTree();
	modlist[parsemod->GetName()] = parsemod;
}


/* Make these voids return something and add error checking everywhere! */
void thSynth::ListMods(void)
{
	for (map<string, thMod*>::const_iterator im = modlist.begin(); 
		 im != modlist.end(); ++im) {
		printf("%s\n", im->first.c_str());
	}
}

void thSynth::AddChannel(const string &channame, const string &modname,
						 float amp)
{
	channels[channame] = new thMidiChan(FindMod(modname), amp, thWindowlen);
}

thMidiNote *thSynth::AddNote(const string &channame, float note,
							 float velocity)
{
	thMidiChan *chan = channels[channame];
	thMidiNote *newnote = chan->AddNote(note, velocity);

	return newnote;
}

int thSynth::SetNoteArg (const string &channame, int note, char *name, float *value, int len)
{
	thMidiChan *chan = channels[channame];

	chan->SetNoteArg (note, name, value, len);
}

void thSynth::Process()
{
	memset(thOutput, 0, thChans*thWindowlen*sizeof(float));

	int i, j, mixchannels, notechannels;
	thMidiChan *chan;
	float *chanoutput;

	for (map<string, thMidiChan*>::const_iterator im = channels.begin();
		 im != channels.end(); ++im) {
		chan = im->second;

		notechannels = chan->GetChannels();
		mixchannels = notechannels;

		if (mixchannels > thChans) {
			mixchannels = thChans;
		}

		chan->Process();
		chanoutput = chan->GetOutput();

		for (i = 0; i < mixchannels; i++) {
			for (j = 0 ;j < thWindowlen; j++) {
				thOutput[i+(j*thChans)] += chanoutput[i+(j*notechannels)];
			}
		}
	}
}

void thSynth::PrintChan(int chan)
{
	int i;

	for (i = 0; i < thWindowlen; i++) {
		printf("-=- %f\n", thOutput[(i*thChans)+chan]);
	}
}
