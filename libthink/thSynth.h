/* $Id: thSynth.h,v 1.52 2004/08/16 09:34:48 misha Exp $ */
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

class thMidiNote;
class thMidiChan;

class thSynth {
public:
	thSynth (int windowlen, int samples);
	thSynth (const string &plugin_path, int windowlen, int samples);
	~thSynth (void);

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


	int GetChans(void) const { return thChans; }

	map<string, thArg*> GetChanArgs (int chan) { 
			return channels[chan]->GetArgs();
	}

	int GetWindowLen(void) const { return thWindowlen; }
//	float *GetOutput(void) const { return thOutput; }
	float *GetOutput (void) const;

	float *GetChanBuffer (int chan) { 
//		pthread_mutex_lock(synthMutex);
//		pthread_mutex_unlock(synthMutex);
		return &thOutput[chan * thWindowlen]; };

	/* note that as of 9/15/03 these don't do anything. corresponding changes
	   elsewhere in the implementation must be made (thSamples is intialized to
	   TH_SAMPLE in the the constructor though, so calls to GetSamples should
	   work ok) (brandon) */
	long GetSamples(void) { return thSamples; }
	void SetSamples(long) { return; }

	int GetChannelCount (void) const { return channelcount; }
	map<int, string> *GetPatchlist (void) { return &patchlist; }

	thArg *GetChanArg (int channum, const string &argname);
	void SetChanArg (int channum, thArg *arg);
	int SetChanArgData (int channum, const string &argname, float *data,
						 int len);

private:
	int BuildSynthTreeHelper(thMod *mod, thNode *parent, char *nodename);

	map<string, thMod*> modlist;
	map<int, string> patchlist;
	thPluginManager *pluginmanager;
	thMidiChan **channels; /* MIDI channels */
	int channelcount;
	float *thOutput;
	int thChans;  /* Number of channels (mono/stereo/etc) */
	int thWindowlen;
	long thSamples; /* the number of samples per second*/

	pthread_mutex_t *synthMutex;
};

#endif /* TH_SYNTH_H */
