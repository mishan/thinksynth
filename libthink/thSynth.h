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

	thMod *loadMod(const string &filename);
	thMod *loadMod(const string &filename, int channum, float amp);
	thMod *loadMod(FILE *input);
	thMod *findMod(const string &name) { return modlist_[name]; };

	void listMods(void);
	thPluginManager *getPluginManager (void) { return pluginmanager_; };

	thMidiNote *addNote(int channum, float note, float velocity);
	int delNote (int channum, float note);
	void clearAll (void);

	void process(void);
	void printChan(int chan);
	void removeChan (int channum);

	int audioChannelCount (void) const { return channels_; }

	thMidiChan::ArgMap getChanArgs (int chan) {
		if ((chan < 0) || (chan >= midiChannelCnt_) || 
			(midiChannels_[chan] == NULL))
			return thMidiChan::ArgMap();

		return midiChannels_[chan]->GetArgs();
	}

	int getWindowlen (void) const { return windowlen_; }
	void setWindowlen (int);

	float *getOutput (void) const;

	float *getChanBuffer (int chan);

	long getSampleRate (void) const { return sampleRate_; }
	void setSampleRate (long samples) { sampleRate_ = samples; }

	int midiChanCount (void) const { return midiChannelCnt_; }

	thArg *getChanArg (int channum, const string &argname);
	void setChanArg (int channum, thArg *arg);
	int setChanArgData (int channum, const string &argname, float *data,
						 int len);

	void handleMidiController (unsigned char channel, unsigned int param,
							   unsigned int value);

	void newMidiControllerConnection (unsigned char channel,
									  unsigned int param,
									  thMidiControllerConnection *connection);

	thMidiController::ConnectionMap *getMidiConnectionMap (void) { 
		return controllerHandler_->getConnectionMap();
	}

	thMidiControllerConnection *getMidiControllerConnection
	(unsigned char channel, unsigned int param) { 
		return controllerHandler_->getConnection(channel, param);
	}

	thMidiChan *getChannel (int chan) const
	{
		if ((chan < midiChannelCnt_) && (chan >= 0))
			return midiChannels_[chan];
		else
			return NULL;
	}

	type_signal_chan_changed signal_channel_changed (void) {
		return m_sigChanChanged_;
	}

	type_signal_chan_deleted signal_channel_deleted (void) {
		return m_sigChanDeleted_;
	}

private:
	type_signal_chan_changed m_sigChanChanged_;
	type_signal_chan_deleted m_sigChanDeleted_;

	map<string, thMod*> modlist_;
	map<int, string> patchlist_;
	thPluginManager *pluginmanager_;
	thMidiChan **midiChannels_; /* MIDI channels */
	int midiChannelCnt_;
	float *output_;
	int channels_;  /* Number of channels (mono/stereo/etc) */
	int windowlen_;
	long sampleRate_; /* the number of samples per second*/

	thMidiController *controllerHandler_;

	pthread_mutex_t *synthMutex_;

	static thSynth *instance_;
};

#endif /* TH_SYNTH_H */
