/*
 * Copyright (C) 2004-2026 Metaphonic Labs
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

#ifndef TH_CHANEFFECT_H
#define TH_CHANEFFECT_H 1

#include "thExport.h"

/* A graph that runs on a channel's summed voices, once per window.
 *
 * The thing a delay throw needs and `delay::echo' cannot be. An echo inside a
 * voice's tree dies when the voice does: the note that fed it takes its tail
 * with it, because the tree the tail lives in is the note. Everything that
 * outlives a note -- a reverb, a chorus, a filter on the sum -- wants a graph
 * that is not anybody's note, and that is this: a second thSynthTree, owned by
 * the channel, fed the buffer the voice loop just filled.
 *
 * It is an ordinary .dsp. The io node has in0..in<N-1>, which the engine
 * writes the way it writes a voice's note and velocity, and out0..out<N-1>,
 * which it reads the way it reads a voice's. `play' means nothing here -- an
 * effect never ends -- and neither do note, velocity or trigger. What a file
 * does with the samples in between is a .dsp's business as usual, chanargs and
 * all.
 *
 * The outputs *replace* the channel's buffer rather than adding to it, so a
 * dry/wet mix is the graph's to write:
 *
 *     node wet delay::echo { in = ionode->in0; ... };
 *     node mix mixer::fade { in0 = ionode->in0; in1 = wet->out; fade = @mix; };
 *     node ionode { channels = 2; in0 = 0; out0 = mix->out; };
 *
 * which is one node more than a wet/dry control in the engine would be, and
 * it is a node the author can see and rewire.
 *
 * It runs every window, whether or not a voice sounds. That is the whole
 * point -- a tail has to keep coming out after the last note-off -- and it is
 * why thMidiChan keeps its buffer dirty while an effect is present, so the
 * effect is fed silence rather than skipped.
 *
 * Built on the GUI thread and installed by the audio thread, like the channel
 * itself: thSynth::loadEffect parses, builds and posts, thMidiChan::setEffect
 * swaps and retires. Nothing here allocates once the first window has run.
 */
class THINK_API thChanEffect {
public:
    /* Takes ownership of `tree' and destroys it.
     *
     * `channels' is the channel's, not the graph's: the two can disagree, and
     * what the engine can carry is the smaller of them. `windowlen' sizes the
     * in<N> buffers, which are allocated here so that the audio thread never
     * has to. */
    thChanEffect (thSynthTree *tree, int channels, int windowlen);
    ~thChanEffect ();

    /* Audio thread. `buf' is the channel's output -- `channels' channels of
     * `windowlen' samples, interleaved, as thMidiChan mixes it -- and it is
     * read, run through the graph, and written back.
     *
     * A channel this effect has no in<N> for is left alone rather than
     * silenced: a stereo channel through a mono effect keeps its right side
     * dry, which is a strange patch but not a broken one.
     *
     * Returns false if the graph produced a sample that is not a number, in
     * which case `buf' is untouched and what the voices mixed goes out dry.
     * An effect that diverges would otherwise take the channel with it for as
     * long as it is loaded, which is the failure the per-voice guard exists to
     * stop one note doing. */
    bool process (float *buf, int channels, int windowlen);

    /* The effect's own chanargs, kept apart from the instrument's so that an
       instrument's `@a' and an effect's cannot collide. Named `fx.<name>'
       wherever a chanarg is addressed from outside. */
    const thArgMap &args (void) const { return args_; }

    thArg *getArg (const string &name) const {
        const thArgMap::const_iterator i = args_.find(name);
        if (i != args_.end()) return i->second;
        return NULL;
    }

    thSynthTree *tree (void) { return tree_; }

    /* How many channels this effect actually carries: the smaller of what the
       channel has and what the graph declares in<N> for. */
    int channels (void) const { return channels_; }

private:
    void copyChanArgs (void);
    void assignChanArgPointers (void);
    void indexIOArgs (int windowlen);

    thSynthTree *tree_;
    thArgMap args_;

    /* Where the io node keeps in0..in<N-1> and out0..out<N-1>, as arg
       indices, for the reason thMidiChan gives for its own: a by-name lookup
       on the audio thread searches a std::map, and thSynthTree::getArg
       *creates* the arg when it does not find one. */
    int inindex_[TH_MAX_CHANNELS];
    int outindex_[TH_MAX_CHANNELS];

    int channels_;

    /* One window of de-interleaved samples, allocated once. */
    float *scratch_;
};

#endif /* TH_CHANEFFECT_H */
