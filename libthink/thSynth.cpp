/* $Id: thSynth.cpp,v 1.76 2004/03/26 09:28:35 joshk Exp $ */

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
	thWindowlen = TH_WINDOW_LENGTH;

	thChans = 2;  /* mono / stereo / etc */

	/* intialize default sample rate */
	thSamples = TH_SAMPLE;

	/* We should make a function to allocate this, so we can easily change
	   thChans and thWindowlen */
	thOutput = new float[thChans*thWindowlen];

	channelcount = CHANNELCHUNK;
	channels = (thMidiChan **)calloc(channelcount, sizeof(thMidiChan*));
}

thSynth::thSynth(thSynth *copySynth)
{
	thWindowlen = copySynth->GetWindowLen();
	thChans     = copySynth->GetChans();
	thSamples   = copySynth->GetSamples();
	
	thOutput = new float[thChans*thWindowlen];

	channelcount = CHANNELCHUNK;
	channels = (thMidiChan **)calloc(channelcount, sizeof(thMidiChan *));

	/* XXX: unfinished */
}

thSynth::~thSynth (void)
{
	delete [] thOutput;

	DestroyMap(modlist);
	free(channels);
}

thMod * thSynth::LoadMod (const string &filename)
{
	if ((yyin = fopen(filename.c_str(), "r")) == NULL) { /* ENOENT or smth */
		fprintf (stderr, "couldn't open %s: %s\n", filename.c_str(),
				 strerror(errno));
		return NULL;
	}

	/* XXX: do we re-allocate these everytime we read a new input file?? */
     /* these are used by the parser */
	parsemod = new thMod("newmod");
	parsenode = new thNode("newnode", NULL);

	yyparse();

	fclose(yyin);

	delete parsenode;

	printf("IO NODE ID: %i\n\n", parsemod->GetIONode()->GetID());
	parsemod->SetPointers();
	parsemod->BuildSynthTree();
	modlist[parsemod->GetName()] = parsemod;

	return parsemod;
}

thMod * thSynth::LoadMod (FILE *input)
{
	if (!input)
	  return NULL;
	
	yyin = input;

	/* XXX: do we re-allocate these everytime we read a new input file?? */
	/* these are used by the parser */
	parsemod = new thMod("newmod");
	parsenode = new thNode("newnode", NULL);

	yyparse();

	delete parsenode;

	parsemod->SetPointers();
	parsemod->BuildSynthTree();
	modlist[parsemod->GetName()] = parsemod;

	return parsemod;
}

void thSynth::SetChanArg (int channum, thArg *arg)
{
	if((channum < 0) || (channum >= channelcount))
	{
		return;
	}

	thMidiChan *chan = channels[channum];
	
	if (!chan)
	{
		return;
	}

	chan->SetArg(arg);
}

thArg *thSynth::GetChanArg (int channum, const string &argname)
{
	if((channum < 0) || (channum >= channelcount))
	{
		return NULL;
	}

	thMidiChan *chan = channels[channum];

	if(!chan)
	{
		return NULL;
	}

	return chan->GetArg(argname);

}

thMod * thSynth::LoadMod (const string &filename, int channum, float amp)
{
	if ((yyin = fopen(filename.c_str(), "r")) == NULL) { /* ENOENT or smth */
		fprintf (stderr, "couldn't open %s: %s\n", filename.c_str(),
				 strerror(errno));
		return NULL;
	}

	/* XXX: do we re-allocate these everytime we read a new input file?? */
	/* these are used by the parser */
	parsemod = new thMod("newmod");
	parsenode = new thNode("newnode", NULL);

	yyparse();

	delete parsenode;

	parsemod->SetPointers();
	parsemod->BuildSynthTree();
	modlist[parsemod->GetName()] = parsemod;

	thMidiChan **newchans;
	int newchancount = channelcount;

	if (channum > channelcount)
	{
		while(channum > newchancount) {
			newchancount = ((newchancount / CHANNELCHUNK) + 1) * CHANNELCHUNK;
			/* add one more chunk to the channel pointer array */
		}
		newchans = (thMidiChan **)calloc(newchancount, sizeof(thMidiChan*));

        /* copy pointers over */
		memcpy(newchans, channels, channelcount * sizeof(thMidiChan*));
		free(channels);
		channelcount = newchancount;
		channels = newchans;
	}

	if(channels[channum] != NULL)
	{
		delete channels[channum];
	}

	channels[channum] = new thMidiChan(parsemod, amp, thWindowlen);
/* Shouldn't be used until most DSPs have valid descriptions. */
#if 0
	patchlist[channum] = parsemod->GetDesc();
#else
	patchlist[channum] = filename;
#endif

	return parsemod;
}


/* Make these voids return something and add error checking everywhere! */
void thSynth::ListMods (void)
{
	for (map<string, thMod*>::const_iterator im = modlist.begin(); 
		 im != modlist.end(); ++im) {
		printf("%s\n", im->first.c_str());
	}
}

void thSynth::AddChannel (int channum, const string &modname, float amp)
{
	thMidiChan **newchans;
	int newchancount = channelcount;

	if (channum > channelcount)
	{
		while(channum > newchancount) {
			newchancount = ((newchancount / CHANNELCHUNK) + 1) * CHANNELCHUNK;
			/* add one more chunk to the channel pointer array */
		}
		newchans = (thMidiChan **)calloc(newchancount, sizeof(thMidiChan*));

        /* copy pointers over */
		memcpy(newchans, channels, channelcount * sizeof(thMidiChan*));
		free(channels);
		channelcount = newchancount;
		channels = newchans;
	}

	if(channels[channum] != NULL)
	{
		delete channels[channum];
	}

	channels[channum] = new thMidiChan(FindMod(modname), amp, thWindowlen);
}

thMidiNote *thSynth::AddNote (int channum, float note,
							  float velocity)
{
	thMidiChan *chan = channels[channum];

	if (!chan || channum > channelcount)
	{
		debug("thSynth::AddNote: no such channel %i", channum);

		return NULL;
	}

	thMidiNote *newnote = chan->AddNote(note, velocity);

	return newnote;
}

int thSynth::SetNoteArg (int channum, int note, char *name,
						 float *value, int len)
{
	thMidiChan *chan = channels[channum];

	if (!chan || channum > channelcount)
	{
		debug("thSynth::SetNoteArg: no such channel %i", channum);

		return 1;
	}
	
	chan->SetNoteArg (note, name, value, len);

	return 0;
}

void thSynth::Process (void)
{
	memset(thOutput, 0, thChans * thWindowlen * sizeof(float));

	int mixchannels, notechannels;
	thMidiChan *chan;
	float *chanoutput;
	int i;

	for (i = 0; i < channelcount; i++)
	{
		chan = channels[i];

		if (chan)
		{
			notechannels = chan->GetChannels();
			mixchannels = notechannels;
			
			if (mixchannels > thChans) {
				mixchannels = thChans;
			}
			
			chan->Process();
			chanoutput = chan->GetOutput();
			
			for (int i = 0; i < mixchannels; i++)
			{
				for (int j = 0 ;j < thWindowlen; j++)
				{
					thOutput[i+(j*thChans)] += chanoutput[i+(j*notechannels)];
				}
			}
		}
	}
}

void thSynth::PrintChan(int chan)
{
	for (int i = 0; i < thWindowlen; i++)
	{
		printf("-=- %f\n", thOutput[(i*thChans)+chan]);
	}
}
