/* $Id: thSynth.cpp,v 1.50 2003/05/06 18:17:15 misha Exp $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "thArg.h"
#include "thList.h"
#include "thBSTree.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"
#include "thMidiNote.h"
#include "thMidiChan.h"
#include "thSynth.h"

#include "parser.h"

thSynth::thSynth()
{
	thWindowlen = 1024;
	thChans = 2;  /* mono / stereo / etc */

	modlist = new thBSTree(StringCompare);
	channels = new thBSTree(StringCompare);

	thOutput = new float[thChans*thWindowlen];  /* We should make a function to
											 allocate this, so we can easily
											 change thChans and thWindowlen */
}

thSynth::~thSynth()
{
	delete modlist;
	delete channels;
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
	
	delete parsenode;

	parsemod->BuildSynthTree();
	modlist->Insert((void *)strdup(parsemod->GetName()), (void *)parsemod);
}

thMod *thSynth::FindMod(const char *modname)
{
	return (thMod *)modlist->GetData((void *)modname);
}

/* Make these voids return something and add error checking everywhere! */
void thSynth::ListMods(void)
{
	ListMods(modlist);
}

void thSynth::ListMods(thBSTree *node)
{
	if(!node) {
		return;
	}

	ListMods(node->GetLeft());
	printf("%s\n", (char *)node->GetId());
	ListMods(node->GetRight());
}

thPluginManager *thSynth::GetPluginManager(void)
{
	return &pluginmanager;
}

void thSynth::AddChannel(char *channame, char *modname, float amp)
{
	thMidiChan *newchan = new thMidiChan(FindMod(modname), amp, thWindowlen);
	channels->Insert((void *)channame, (void *)newchan);
}

thMidiNote *thSynth::AddNote(char *channame, float note, float velocity)
{
	thMidiChan *chan = channels->GetData((void *)channame);
	thMidiNote *newnote = chan->AddNote(note, velocity);

	return newnote;
}

void thSynth::Process()
{
	memset(thOutput, 0, thChans*thWindowlen*sizeof(float));
	ProcessHelper(channels);
}

void thSynth::ProcessHelper(thBSTree *chan)
{
	int i, j, mixchannels, notechannels;
	thMidiChan *data;
	float *chanoutput;

	if(!chan) {
		return;
	}

	ProcessHelper(chan->GetLeft());

	data = (thMidiChan *)chan->GetData();

	notechannels = data->GetChannels();
	mixchannels = notechannels;

	if(mixchannels > thChans) {
		mixchannels = thChans;
	}

	data->Process();
	chanoutput = data->GetOutput();

	for(i = 0; i < mixchannels; i++) {
		for(j = 0 ;j < thWindowlen; j++) {
			thOutput[i+(j*thChans)] += chanoutput[i+(j*notechannels)];
		}
	}

	ProcessHelper(chan->GetRight());
}

void thSynth::PrintChan(int chan)
{
	int i;

	for(i = 0; i < thWindowlen; i++) {
		printf("-=- %f\n", thOutput[(i*thChans)+chan]);
	}
}
