/* $Id: thSynth.h,v 1.61 2004/11/16 23:22:02 misha Exp $ */
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

#ifndef TH_SYNTH_H
#define TH_SYNTH_H

#include <sigc++/sigc++.h>

class thMidiNote;
class thMidiChan;

typedef SigC::Signal3<void, string, int, float> type_signal_chan_changed;
typedef SigC::Signal1<void, int> type_signal_chan_deleted;

class thSynth {
public:
	thSynth (int windowlen=TH_DEFAULT_WINDOW_LENGTH,
			 int samples=TH_DEFAULT_SAMPLES);
	thSynth (const string &plugin_path, int windowlen, int samples);
	~thSynth (void);

	static thSynth *instance (void) {
		return instance_;
	}

	thMod* LoadMod(const string &filename);
	thMod* LoadMod(const string &filename, int channum, float amp);
	thMod* LoadMod(FILE *input);
	thMod *FindMod(const string &name) { return modlist[name]; };
	void ListMods(void);
	void BuildSynthTree(const string &modname);
	thPluginManager *GetPluginManager (void) { return pluginmanager; };
	void AddChannel(int channum, const string &modname, float amp);

	thMidiNote *AddNote(int channum, float note, float velocity);
	int DelNote (int channum, float note);

	int SetNoteArg (int channum, int note, char *name, float *value, int len);
	void Process(void);
	void PrintChan(int chan);
	void removeChan (int channum);


	int GetChans(void) const { return thChans; }

	map<string, thArg*> GetChanArgs (int chan) {
		if ((chan < 0) || (chan >= channelcount) || (channels[chan] == NULL))
			return map<string, thArg*>();

		return channels[chan]->GetArgs();
	}

	int GetWindowLen(void) const { return thWindowlen; }
	void setWindowLen (int);

	float *GetOutput (void) const;

	float *getChanBuffer (int chan);

	long GetSamples(void) { return thSamples; }
	void setSamples(long samples) { thSamples = samples; }

	int GetChannelCount (void) const { return channelcount; }
	map<int, string> *GetPatchlist (void) { return &patchlist; }

	thArg *GetChanArg (int channum, const string &argname);
	void SetChanArg (int channum, thArg *arg);
	int SetChanArgData (int channum, const string &argname, float *data,
						 int len);

	void handleMidiController (unsigned char channel, unsigned int param,
							   unsigned int value);
	void newMidiControllerConnection (unsigned char channel,
									  unsigned int param,
									  thMidiControllerConnection *connection);
	map<unsigned int, thMidiControllerConnection *> *getMidiConnectionList
	(void)
		{ return controllerHandler_->getConnectionList(); }
	thMidiControllerConnection *getMidiControllerConnection
	(unsigned char channel, unsigned int param)
		{ return controllerHandler_->getConnection(channel, param); }

	inline thMidiChan * GetChannel (int chan) const
	{
		if ((chan < channelcount) && (chan >= 0))
			return channels[chan];
		else
			return NULL;
	}

	type_signal_chan_changed signal_channel_changed (void) {
		return m_sigChanChanged;
	}

	type_signal_chan_deleted signal_channel_deleted (void) {
		return m_sigChanDeleted;
	}

private:
	int BuildSynthTreeHelper(thMod *mod, thNode *parent, char *nodename);

	type_signal_chan_changed m_sigChanChanged;
	type_signal_chan_deleted m_sigChanDeleted;

	map<string, thMod*> modlist;
	map<int, string> patchlist;
	thPluginManager *pluginmanager;
	thMidiChan **channels; /* MIDI channels */
	int channelcount;
	float *thOutput;
	int thChans;  /* Number of channels (mono/stereo/etc) */
	int thWindowlen;
	long thSamples; /* the number of samples per second*/

	thMidiController *controllerHandler_;

	pthread_mutex_t *synthMutex;

	static thSynth *instance_;
};

#endif /* TH_SYNTH_H */
