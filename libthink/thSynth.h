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

    /* ---- GUI thread ----
     *
     * These no longer touch the live graph. They allocate whatever is needed
     * and queue the change; the audio thread applies it on its next window,
     * within one window's latency. See thSynthCommand.h.
     *
     * addNote returned a thMidiNote* that no caller used, and handing back a
     * pointer to an object the audio thread now owns would be a trap, so it
     * returns success instead. */
    bool addNote(int channum, float note, float velocity);
    int delNote (int channum, float note);
    void clearAll (void);

    /* ---- audio thread ---- */
    void process(void);

    void printChan(int chan);
    void removeChan (int channum);

    int audioChannelCount (void) const { return channels_; }

    /* GUI thread: reads the GUI's own view of the channels, never the audio
       thread's array. */
    thArgMap getChanArgs (int chan) {
        if ((chan < 0) || (chan >= midiChannelCnt_) ||
            (guiChannels_[chan] == NULL))
            return thArgMap();

        return guiChannels_[chan]->args();
    }

    int getWindowlen (void) const { return windowlen_; }
    void setWindowlen (int);

    float *getOutput (void) const;

    float *getChanBuffer (int chan);

    long getSampleRate (void) const { return sampleRate_; }
    void setSampleRate (long samples) { sampleRate_ = samples; }

    /* Master gain, applied to the summed mix before the output limiter.
     *
     * Written from the GUI thread and read by the audio thread every window,
     * so it goes through a relaxed atomic rather than the command queue -- the
     * same reasoning as thArg::setValue for a slider. A torn read of a gain
     * would be audible; last-writer-wins is exactly what a fader wants. */
    void setMasterGain (float gain)
    {
        if (gain < 0.0f)
            gain = 0.0f;
        else if (gain > TH_MASTER_GAIN_MAX)
            gain = TH_MASTER_GAIN_MAX;

        __atomic_store(&masterGain_, &gain, __ATOMIC_RELAXED);
    }

    float masterGain (void) const
    {
        float gain;

        __atomic_load(&masterGain_, &gain, __ATOMIC_RELAXED);

        return gain;
    }

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

    /* GUI thread. Deliberately the GUI's view: the audio thread's array is
       written by the audio thread and must not be read from here. */
    thMidiChan *getChannel (int chan) const
    {
        if ((chan < midiChannelCnt_) && (chan >= 0))
            return guiChannels_[chan];
        else
            return NULL;
    }

    /* GUI thread: frees whatever the audio thread has handed back. Called at
       the top of every GUI-side entry point, so an idle GUI is the only way
       for retired objects to sit around. */
    void collectRetired (void);

private:
    /* Shared tail of the three loadTree() overloads: checks the parse result,
       validates the tree, and resolves it. Returns NULL (having discarded the
       half-built tree) if the .dsp did not parse into something usable.

       registerTree: true puts the tree in treelist_ under thSynth's ownership;
       false gives the caller an unowned tree to hand to a thMidiChan. */
    thSynthTree *finishParse (const string &what, int parseResult,
                              bool registerTree);

    /* Applies one queued command. Audio thread. */
    void applyCommand (const thSynthCommand &cmd);

    /* Drains the command queue. Audio thread, at the top of process(). */
    void drainCommands (void);

    /* GUI thread: queue a command, cleaning up the payload if the ring is
       full. Returns false if it was dropped. */
    bool postCommand (const thSynthCommand &cmd);

    map<string, thSynthTree*> treelist_;
    map<int, string> patchlist_;
    thPluginManager *pluginmanager_;

    /* Two views of the same channel objects.
     *
     * midiChannels_ is the audio thread's, written only by applyCommand().
     * guiChannels_ is the GUI thread's, written only by the GUI. They agree
     * except in the window between the GUI queueing a SET_CHANNEL and the
     * audio thread applying it, during which both objects are alive and each
     * thread is looking at one of them. Neither thread ever reads the other's
     * array, which is what removes the race. */
    thMidiChan **midiChannels_; /* MIDI channels -- audio thread */
    thMidiChan **guiChannels_;  /* the same channels -- GUI thread */

    int midiChannelCnt_;
    float *output_;
    int channels_;  /* Number of channels (mono/stereo/etc) */
    int windowlen_;
    float masterGain_;  /* see setMasterGain(); accessed atomically */
    long sampleRate_; /* the number of samples per second*/

    thMidiController *controllerHandler_;

    thRing<thSynthCommand, TH_COMMAND_QUEUE_SIZE> commands_;  /* GUI -> audio */
    thRing<thRetired, TH_RETIRE_QUEUE_SIZE> retired_;         /* audio -> GUI */

    /* Serialises GUI-thread callers against each other (the parser globals are
       not reentrant either). The audio thread does not take it -- that is the
       whole point of the queues. */
    pthread_mutex_t *synthMutex_;

    static thSynth *instance_;
};

#endif /* TH_SYNTH_H */
