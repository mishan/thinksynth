/* $Id$ */
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
	synthMutex_ = new pthread_mutex_t;
	pthread_mutex_init(synthMutex_, NULL);

	/* XXX: these should all be arguments and we should have corresponding
	   accessor/mutator methods for these arguments */
	windowlen_ = windowlen;

	channels_ = 2;  /* mono / stereo / etc */

	/* intialize default sample rate */
	sampleRate_ = samples;

	/* We should make a function to allocate this, so we can easily change
	   thChans and thWindowlen */
	output_ = new float[channels_*windowlen_];

	midiChannelCnt_ = CHANNELCHUNK;
	midiChannels_ = (thMidiChan **)calloc(midiChannelCnt_, sizeof(thMidiChan *));

	/* default path */
	pluginmanager_ = new thPluginManager(PLUGIN_PATH);

	controllerHandler_ = new thMidiController();

	if (instance_ == NULL)
		instance_ = this;
}

thSynth::thSynth (const string &plugin_path, int windowlen, int samples)
{
	synthMutex_ = new pthread_mutex_t;
	pthread_mutex_init(synthMutex_, NULL);

	/* XXX: these should all be arguments and we should have corresponding
	   accessor/mutator methods for these arguments */
	windowlen_ = windowlen;

	channels_ = 2;  /* mono / stereo / etc */

	/* intialize default sample rate */
	sampleRate_ = samples;

	/* We should make a function to allocate this, so we can easily change
	   thChans and thWindowlen */
	output_ = new float[channels_*windowlen_];

	midiChannelCnt_ = CHANNELCHUNK;
	midiChannels_ = (thMidiChan **)calloc(midiChannelCnt_, sizeof(thMidiChan *));

	pluginmanager_ = new thPluginManager(plugin_path);

	controllerHandler_ = new thMidiController();

	if (instance_ == NULL)
		instance_ = this;
}

thSynth::~thSynth (void)
{
	delete [] output_;

	DestroyMap(modlist_);
	free(midiChannels_);

	pthread_mutex_destroy(synthMutex_);
	delete synthMutex_;

	if (instance_ == this)
		instance_ = NULL;
}

void thSynth::removeChan (int channum)
{
	if (((channum >= 0) && (channum < midiChannelCnt_)) && midiChannels_[channum])
	{
/*		thMidiChan *chan = channels[channum];
		if (chan)
		delete chan; */

		midiChannels_[channum] = NULL;
		patchlist_[channum] = "";
		controllerHandler_->clearByDestChan(channum);
		m_sigChanDeleted_(channum);
	}
}

thMod * thSynth::loadMod (const string &filename)
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

	pthread_mutex_lock(synthMutex_);

	/* XXX: do we re-allocate these everytime we read a new input file?? */
     /* these are used by the parser */
	parsemod = new thMod("newmod", this);
	parsenode = new thNode("newnode", NULL);

	YYPARSE(this);

	fclose(yyin);

	delete parsenode;

	parsemod->buildArgMap(); /* build the index of args */
	parsemod->setPointers();
	parsemod->buildSynthTree();
	modlist_[parsemod->getName()] = parsemod;

	pthread_mutex_unlock(synthMutex_);

	return parsemod;
}

thMod * thSynth::loadMod (FILE *input)
{
	if (!input)
		return NULL;

	pthread_mutex_lock(synthMutex_);
	
	yyin = input;

	/* XXX: do we re-allocate these everytime we read a new input file?? */
	/* these are used by the parser */
	parsemod = new thMod("newmod", this);
	parsenode = new thNode("newnode", NULL);

	YYPARSE(this);

	delete parsenode;

	parsemod->buildArgMap(); /* build the index of args */
	parsemod->setPointers();
	parsemod->buildSynthTree();
	modlist_[parsemod->getName()] = parsemod;

	pthread_mutex_unlock(synthMutex_);

	return parsemod;
}

void thSynth::setChanArg (int channum, thArg *arg)
{
	if((channum < 0) || (channum >= midiChannelCnt_))
	{
		return;
	}

	thMidiChan *chan = midiChannels_[channum];
	
	if (!chan)
	{
		return;
	}

	pthread_mutex_lock(synthMutex_);

	chan->SetArg(arg);

	pthread_mutex_unlock(synthMutex_);
}

thArg *thSynth::getChanArg (int channum, const string &argname)
{
	if((channum < 0) || (channum >= midiChannelCnt_))
	{
		return NULL;
	}

	thMidiChan *chan = midiChannels_[channum];

	if(!chan)
	{
		return NULL;
	}

	return chan->GetArg(argname);
}

int thSynth::setChanArgData (int channum, const string &argname, float *data, int len)
{
	float *buffer;
	thArg *argp;  /* pointer to the arg we search the chan for...  no need to
					 search more than once! */

	if((channum < 0) || (channum >= midiChannelCnt_))
	{
		return -1;
	}

	thMidiChan *chan = midiChannels_[channum];

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

thMod * thSynth::loadMod (const string &filename, int channum, float amp)
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

	pthread_mutex_lock(synthMutex_);

	/* XXX: do we re-allocate these everytime we read a new input file?? */
	/* these are used by the parser */
	parsemod = new thMod("newmod", this);
	parsenode = new thNode("newnode", NULL);

	YYPARSE(this);

	delete parsenode;

	if (parsemod->getIONode() == NULL)
	{
		fprintf(stderr, "%s: DSP does not have a valid IO node!\n",
			filename.c_str());
		pthread_mutex_unlock(synthMutex_);
		return NULL;
	}
	
	parsemod->buildArgMap(); /* build the index of args */
	parsemod->setPointers();
	parsemod->buildSynthTree();
	modlist_[parsemod->getName()] = parsemod;

	thMidiChan **newchans;
	int newchancount = midiChannelCnt_;

	if (channum >= midiChannelCnt_)
	{
		while(channum >= newchancount)
		{
			newchancount = ((newchancount / CHANNELCHUNK) + 1) * CHANNELCHUNK;
			/* add one more chunk to the channel pointer array */
		}
		newchans = (thMidiChan **)calloc(newchancount, sizeof(thMidiChan*));

        /* copy pointers over */
		memcpy(newchans, midiChannels_, midiChannelCnt_ * sizeof(thMidiChan*));
		free(midiChannels_);
		midiChannelCnt_ = newchancount;
		midiChannels_ = newchans;
	}

	if(midiChannels_[channum] != NULL)
	{
		delete midiChannels_[channum];
	}

	midiChannels_[channum] = new thMidiChan(parsemod, amp, windowlen_);

	patchlist_[channum] = filename;

	/* make sure there are no midi controllers set up for this channel */
	controllerHandler_->clearByDestChan(channum);

	pthread_mutex_unlock(synthMutex_);

	m_sigChanChanged_(filename, channum, amp);

	return parsemod;
}


/* Make these voids return something and add error checking everywhere! */
void thSynth::listMods (void)
{
	for (map<string, thMod*>::const_iterator im = modlist_.begin(); 
		 im != modlist_.end(); ++im) {
		printf("%s\n", im->first.c_str());
	}
}

thMidiNote *thSynth::addNote (int channum, float note,
							  float velocity)
{
	if((channum < 0) || (channum > midiChannelCnt_))
	{
		debug("thSynth::addNote: no such channel %d", channum);

		return NULL;
	}

	thMidiChan *chan = midiChannels_[channum];

	if (!chan)
	{
		debug("thSynth::addNote: no such channel %d", channum);
		
		return NULL;
	}

 	pthread_mutex_lock(synthMutex_);

	thMidiNote *newnote = chan->AddNote(note, velocity);

	pthread_mutex_unlock(synthMutex_);

	return newnote;
}

int thSynth::delNote (int channum, float note)
{
	if((channum < 0) || (channum > midiChannelCnt_))
		return 1;

	thMidiChan *chan = midiChannels_[channum];

	if(!chan)
		return 1;

	int sustain = (int)(*(chan->GetSusPedalArg()))[0];

	float *pbuf = new float(0);
	if(sustain)
		*pbuf = 2; /* set it to sustain */

	pthread_mutex_lock(synthMutex_);
	
	chan->SetNoteArg ((int)note, "trigger", pbuf, 1);
/* XXX IS THE OLD BUFFER BEING TAKEN CARE OF?  Possible memory leak */

	pthread_mutex_unlock(synthMutex_);

	return 0;
}

void thSynth::clearAll (void)
{
	/* XXX: this code is horrible. fuck you joshk */
	thMidiChan **c = midiChannels_;

	while (*c)
		(*c++)->ClearAll();
}

void thSynth::process (void)
{
	int mixchannels, notechannels;
	thMidiChan *chan;
	float *chanoutput;

	pthread_mutex_lock(synthMutex_);

	memset(output_, 0, channels_ * windowlen_ * sizeof(float));

	for (int i = 0; i < midiChannelCnt_; i++)
	{
		chan = midiChannels_[i];

		if (chan)
		{
			notechannels = chan->GetChannels();
			mixchannels = notechannels;
			
			if (mixchannels > channels_) {
				mixchannels = channels_;
			}
			
			chan->Process();
			chanoutput = chan->GetOutput();

			int bufferoffset = 0;
			
			for (int j = 0; j < mixchannels; j++)
			{
				for (int k = 0; k < windowlen_; k++)
				{
					output_[bufferoffset + k] +=
						chanoutput[j + (k*mixchannels)];
				}

				bufferoffset += windowlen_;
			}
		}
	}

	pthread_mutex_unlock(synthMutex_);
}

void thSynth::printChan(int chan)
{
	for (int i = 0; i < windowlen_; i++)
	{
		printf("-=- %f\n", output_[(i*channels_)+chan]);
	}
}

float *thSynth::getOutput (void) const
{
	/* try locking the mutex (and block) to make sure it's not processing */
	pthread_mutex_lock(synthMutex_);

//	float *output = new float[thChans*thWindowlen];
//	memcpy(output, output_, thChans*thWindowlen*sizeof(float));
	float *output = output_;

	pthread_mutex_unlock(synthMutex_);

	return output;
}

float *thSynth::getChanBuffer (int chan)
{
	return &output_[chan * windowlen_];
}

void thSynth::setWindowlen (int windowlen)
{
#if 0
	pthread_mutex_lock(synthMutex_);
	windowlen_ = windowlen;
	delete [] output_;
	output_ = new float[channels_*windowlen_];
	pthread_mutex_unlock(synthMutex_);
#endif
}
