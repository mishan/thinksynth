/* $Id: thSynth.cpp,v 1.113 2004/12/22 23:42:36 ink Exp $ */
/*
 * Copyright (C) 2004 Metaphonic Labs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "think.h"
#include "parser.h"

thSynth *thSynth::instance_ = NULL;

thSynth::thSynth (int windowlen, int samples)
{
	synthMutex = new pthread_mutex_t;
	pthread_mutex_init(synthMutex, NULL);

	/* XXX: these should all be arguments and we should have corresponding
	   accessor/mutator methods for these arguments */
	thWindowlen = windowlen;

	thChans = 2;  /* mono / stereo / etc */

	/* intialize default sample rate */
	thSamples = samples;

	/* We should make a function to allocate this, so we can easily change
	   thChans and thWindowlen */
	thOutput = new float[thChans*thWindowlen];

	channelcount = CHANNELCHUNK;
	channels = (thMidiChan **)calloc(channelcount, sizeof(thMidiChan *));

	/* default path */
	pluginmanager = new thPluginManager(PLUGIN_PATH);

	controllerHandler_ = new thMidiController();

	if (instance_ == NULL)
		instance_ = this;
}

thSynth::thSynth (const string &plugin_path, int windowlen, int samples)
{
	synthMutex = new pthread_mutex_t;
	pthread_mutex_init(synthMutex, NULL);

	/* XXX: these should all be arguments and we should have corresponding
	   accessor/mutator methods for these arguments */
	thWindowlen = windowlen;

	thChans = 2;  /* mono / stereo / etc */

	/* intialize default sample rate */
	thSamples = samples;

	/* We should make a function to allocate this, so we can easily change
	   thChans and thWindowlen */
	thOutput = new float[thChans*thWindowlen];

	channelcount = CHANNELCHUNK;
	channels = (thMidiChan **)calloc(channelcount, sizeof(thMidiChan *));

	pluginmanager = new thPluginManager(plugin_path);

	controllerHandler_ = new thMidiController();

	if (instance_ == NULL)
		instance_ = this;
}

thSynth::~thSynth (void)
{
	delete [] thOutput;

	DestroyMap(modlist);
	free(channels);

	pthread_mutex_destroy(synthMutex);
	delete synthMutex;

	if (instance_ == this)
		instance_ = NULL;
}

void thSynth::removeChan (int channum)
{
	if (((channum >= 0) && (channum < channelcount)) && channels[channum])
	{
/*		thMidiChan *chan = channels[channum];
		if (chan)
		delete chan; */

		channels[channum] = NULL;
		patchlist[channum] = "";
		controllerHandler_->clearByDestChan(channum);
		m_sigChanDeleted(channum);
	}
}

thMod * thSynth::LoadMod (const string &filename)
{
	struct stat dspinfo;

	if (stat(filename.c_str(), &dspinfo) < 0)
	{
		fprintf (stderr, "couldn't open %s: %s\n", filename.c_str(),
				 strerror(errno));
		return NULL;
	}
	else if (S_ISDIR(dspinfo.st_mode))
	{
		fprintf(stderr, "%s is a directory\n", filename.c_str());

#ifdef EISDIR
		errno = EISDIR; /* XXX */
#endif

		return NULL;
	}

	if ((yyin = fopen(filename.c_str(), "r")) == NULL) { /* ENOENT or smth */
		fprintf (stderr, "couldn't open %s: %s\n", filename.c_str(),
				 strerror(errno));
		return NULL;
	}

	pthread_mutex_lock(synthMutex);

	/* XXX: do we re-allocate these everytime we read a new input file?? */
     /* these are used by the parser */
	parsemod = new thMod("newmod", this);
	parsenode = new thNode("newnode", NULL);

	YYPARSE(this);

	fclose(yyin);

	delete parsenode;

	parsemod->BuildArgMap(); /* build the index of args */
	parsemod->SetPointers();
	parsemod->BuildSynthTree();
	modlist[parsemod->GetName()] = parsemod;

	pthread_mutex_unlock(synthMutex);

	return parsemod;
}

thMod * thSynth::LoadMod (FILE *input)
{
	if (!input)
		return NULL;

	pthread_mutex_lock(synthMutex);
	
	yyin = input;

	/* XXX: do we re-allocate these everytime we read a new input file?? */
	/* these are used by the parser */
	parsemod = new thMod("newmod", this);
	parsenode = new thNode("newnode", NULL);

	YYPARSE(this);

	delete parsenode;

	parsemod->BuildArgMap(); /* build the index of args */
	parsemod->SetPointers();
	parsemod->BuildSynthTree();
	modlist[parsemod->GetName()] = parsemod;

	pthread_mutex_unlock(synthMutex);

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

	pthread_mutex_lock(synthMutex);

	chan->SetArg(arg);

	pthread_mutex_unlock(synthMutex);
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

int thSynth::SetChanArgData (int channum, const string &argname, float *data, int len)
{
	float *buffer;
	thArg *argp;  /* pointer to the arg we search the chan for...  no need to
					 search more than once! */

	if((channum < 0) || (channum >= channelcount))
	{
		return -1;
	}

	thMidiChan *chan = channels[channum];

	if(!chan)
	{
		return -1;
	}

	argp = chan->GetArg(argname);
	buffer = argp->Allocate(len);
	memcpy(buffer, data, len * sizeof(float));
	argp->len_ = len;

	return 1;
}

void thSynth::handleMidiController (unsigned char channel, unsigned int param,
									unsigned int value)
{
	controllerHandler_->handleMidi(channel, param, value);
}

void thSynth::newMidiControllerConnection (unsigned char channel,
										unsigned int param,
										thMidiControllerConnection *connection)
{
	controllerHandler_->newConnection(channel, param, connection);
}

thMod * thSynth::LoadMod (const string &filename, int channum, float amp)
{
	struct stat dspinfo;

	if (stat(filename.c_str(), &dspinfo) < 0)
	{
		fprintf (stderr, "couldn't open %s: %s\n", filename.c_str(),
				 strerror(errno));
		return NULL;
	}
	else if (S_ISDIR(dspinfo.st_mode))
	{
		fprintf(stderr, "%s is a directory\n", filename.c_str());

#ifdef EISDIR
		errno = EISDIR; /* XXX */
#endif

		return NULL;
	}

 	if ((yyin = fopen(filename.c_str(), "r")) == NULL) { /* ENOENT or smth */
	 	fprintf (stderr, "couldn't open %s: %s\n", filename.c_str(),
		 		 strerror(errno));
 		return NULL;
	}

	pthread_mutex_lock(synthMutex);

	/* XXX: do we re-allocate these everytime we read a new input file?? */
	/* these are used by the parser */
	parsemod = new thMod("newmod", this);
	parsenode = new thNode("newnode", NULL);

	YYPARSE(this);

	delete parsenode;

	if (parsemod->GetIONode() == NULL)
	{
		fprintf(stderr, "%s: DSP does not have a valid IO node!\n",
			filename.c_str());
		pthread_mutex_unlock(synthMutex);
		return NULL;
	}
	
	parsemod->BuildArgMap(); /* build the index of args */
	parsemod->SetPointers();
	parsemod->BuildSynthTree();
	modlist[parsemod->GetName()] = parsemod;

	thMidiChan **newchans;
	int newchancount = channelcount;

	if (channum >= channelcount)
	{
		while(channum >= newchancount)
		{
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

	patchlist[channum] = filename;

	/* make sure there are no midi controllers set up for this channel */
	controllerHandler_->clearByDestChan(channum);

	pthread_mutex_unlock(synthMutex);

	m_sigChanChanged(filename, channum, amp);

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

	pthread_mutex_lock(synthMutex);

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
//	channels[channum]->CopyChanArgs(channels[channum]->GetMod());

	pthread_mutex_unlock(synthMutex);

	m_sigChanChanged(modname, channum, amp);
}

thMidiNote *thSynth::AddNote (int channum, float note,
							  float velocity)
{
	if((channum < 0) || (channum > channelcount))
	{
		debug("thSynth::AddNote: no such channel %d", channum);

		return NULL;
	}

	thMidiChan *chan = channels[channum];

	if (!chan)
	{
		debug("thSynth::AddNote: no such channel %d", channum);
		
		return NULL;
	}

 	pthread_mutex_lock(synthMutex);

	thMidiNote *newnote = chan->AddNote(note, velocity);

	pthread_mutex_unlock(synthMutex);

	return newnote;
}

int thSynth::DelNote (int channum, float note)
{
	if((channum < 0) || (channum > channelcount))
		return 1;

	thMidiChan *chan = channels[channum];

	if(!chan)
		return 1;

	int sustain = (int)(*(chan->GetSusPedalArg()))[0];

	float *pbuf = new float(0);
	if(sustain)
		*pbuf = 2; /* set it to sustain */

	pthread_mutex_lock(synthMutex);
	
	chan->SetNoteArg ((int)note, "trigger", pbuf, 1);
/* XXX IS THE OLD BUFFER BEING TAKEN CARE OF?  Possible memory leak */

	pthread_mutex_unlock(synthMutex);

	return 0;
}

int thSynth::SetNoteArg (int channum, int note, char *name,
						 float *value, int len)
{
	if((channum < 0) || (channum > channelcount))
	{
		debug("thSynth::SetNoteArg: no such channel %i", channum);

		return 1;
	}

	thMidiChan *chan = channels[channum];

	if(!chan)
	{
		debug("thSynth::SetNoteArg: no such channel %d", channum);

		return 1;
	}

	pthread_mutex_lock(synthMutex);
	
	chan->SetNoteArg (note, name, value, len);

	pthread_mutex_unlock(synthMutex);

	return 0;
}

void thSynth::Process (void)
{

	int mixchannels, notechannels;
	thMidiChan *chan;
	float *chanoutput;

	pthread_mutex_lock(synthMutex);

	memset(thOutput, 0, thChans * thWindowlen * sizeof(float));

	for (int i = 0; i < channelcount; i++)
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

			int bufferoffset = 0;
			
			for (int j = 0; j < mixchannels; j++)
			{
				for (int k = 0; k < thWindowlen; k++)
				{
					thOutput[bufferoffset + k] +=
						chanoutput[j + (k*mixchannels)];
				}

				bufferoffset += thWindowlen;
			}
		}
	}

	pthread_mutex_unlock(synthMutex);
}

void thSynth::PrintChan(int chan)
{
	for (int i = 0; i < thWindowlen; i++)
	{
		printf("-=- %f\n", thOutput[(i*thChans)+chan]);
	}
}

float *thSynth::GetOutput (void) const
{
	/* try locking the mutex (and block) to make sure it's not processing */
	pthread_mutex_lock(synthMutex);

//	float *output = new float[thChans*thWindowlen];
//	memcpy(output, thOutput, thChans*thWindowlen*sizeof(float));
	float *output = thOutput;

	pthread_mutex_unlock(synthMutex);

	return output;
}

float *thSynth::getChanBuffer (int chan)
{
	return &thOutput[chan * thWindowlen];
}

void thSynth::setWindowLen (int windowlen)
{
#if 0
	pthread_mutex_lock(synthMutex);
	thWindowlen = windowlen;
	delete [] thOutput;
	thOutput = new float[thChans*thWindowlen];
	pthread_mutex_unlock(synthMutex);
#endif
}
