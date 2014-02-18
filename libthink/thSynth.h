/*
 * Copyright (C) 2004-2014 Metaphonic Labs
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
    thSynth (int windowlen=TH_DEFAULT_WINDOW_LENGTH,
             int samples=TH_DEFAULT_SAMPLES);
    thSynth (const string &plugin_path, int windowlen, int samples);
    ~thSynth (void);

    static thSynth *instance (void) {
        return instance_;
    }

    thSynthTree *loadTree(const string &filename);
    thSynthTree *loadTree(const string &filename, int channum, float amp);
    thSynthTree *loadTree(FILE *input);

    void listTrees (void);
    thPluginManager *getPluginManager (void) { return pluginmanager_; };

    thMidiNote *addNote(int channum, float note, float velocity);
    int delNote (int channum, float note);
    void clearAll (void);

    void process(void);
    void printChan(int chan);
    void removeChan (int channum);

    int audioChannelCount (void) const { return channels_; }

    thArgMap getChanArgs (int chan) {
        if ((chan < 0) || (chan >= midiChannelCnt_) || 
            (midiChannels_[chan] == NULL))
            return thArgMap();

        return midiChannels_[chan]->args();
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

    void handleMidiController (unsigned char channel, unsigned int param,
                               unsigned int value);

    void newMidiControllerConnection (unsigned char channel,
                                      unsigned int param,
                                      thMidiControllerConnection *connection);

    thMidiController::ConnectionMap *getMidiConnectionMap (void) { 
        return controllerHandler_->connectionMap();
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

private:
    map<string, thSynthTree*> treelist_;
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
