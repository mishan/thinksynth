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
 * It may also hear a second channel. An effect whose io node declares
 * side0..side<N-1> is given another channel's output there every window --
 * the carrier a vocoder needs, the kick a compressor is keyed from -- and
 * which channel that is comes from the `.gen' effect clause's `side' rather
 * than from the file. The engine runs that channel first, so side<N> carries
 * the window being mixed and not the one before it, and refuses a cycle at
 * load. side<N> is read and never written back: what an effect returns is
 * its own channel's audio.
 *
 * It may also hear the machine. An effect whose io node declares
 * live0..live<N-1> is given what the host is capturing -- a microphone, a line
 * in -- there every window: the modulator a vocoder needs when the thing being
 * vocoded is a person rather than another channel. Unlike a side it names
 * nothing, because there is only one thing the machine is hearing, so a graph
 * asks for it by declaring it and no `.gen' clause is involved. Where no host
 * is feeding one it is silence, which is what a vocoder with nothing to vocode
 * should sound like -- and is what keeps every offline path reproducible. See
 * LIVEPREFIX in think.h and thSynth::feedCapture.
 *
 * Where no side was named, side<N> is *this* channel. A graph that reads it
 * therefore always has a signal there -- a compressor keyed from side0 is an
 * ordinary compressor until a piece names a kick for it -- which is the rule
 * dyn::compressor follows for an unwired `side' one level down, and the
 * reason no graph here carries a knob for "is there a side or not".
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
     * has to.
     *
     * `sideChan' is the channel whose audio this effect listens to besides
     * its own, or -1. It is carried here rather than on the channel because
     * it belongs to the effect -- a vocoder wants a carrier, an ordinary
     * echo does not -- and because thSynth reads it to decide what order to
     * run the channels in. See side<N> below. */
    thChanEffect (thSynthTree *tree, int channels, int windowlen,
                  int sideChan = -1);
    ~thChanEffect ();

    /* Audio thread. `buf' is the channel's output -- `channels' channels of
     * `windowlen' samples, interleaved, as thMidiChan mixes it -- and it is
     * read, run through the graph, and written back.
     *
     * `side' is the other channel's output, interleaved by `sidechannels',
     * or NULL where there is none: the audio a vocoder carries, the kick a
     * compressor is keyed from. It is read into side<N> and never written
     * back -- what this effect returns is this channel's.
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
    bool process (float *buf, int channels, int windowlen,
                  const float *side = NULL, int sidechannels = 0);

    /* The same, on a buffer laid out the other way round: `channels'
     * whole windows end to end, which is how thSynth keeps the mix.
     *
     * Two spellings of one loop rather than an interleave and a copy at
     * the call site. The master effect is this object on the summed mix
     * -- same graph, same chanargs, same guard -- and the only thing it
     * does not share with a channel's is where the samples sit. */
    bool processPlanar (float *buf, int channels, int windowlen);

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

    /* The channel this effect hears besides its own, or -1.
     *
     * Read by the audio thread every window -- it is what puts the side's
     * channel in front of this one in thSynth::process, so that side<N>
     * carries the window that is being mixed rather than the one before it
     * -- and written never: an effect is built with its side and replaced
     * to change it, the way it is built with its graph. */
    int sideChan (void) const { return sideChan_; }

    /* Audio thread. Bring the output down to nothing over `samples', and
     * then forget everything the graph remembers.
     *
     * An effect never ends -- that is what it is for -- so a transport stop
     * that only released the voices left every reverb and every echo ringing
     * out whatever it had been fed, for as long as its tail is. This ramps
     * what the graph writes back to zero and then zeroes every state arg in
     * it: the delay lines, the filter histories, the phases, the seeds. Zero
     * is exactly what a freshly built graph starts from (thArg::allocate), so
     * the effect is then the one that was loaded, with no tail left to come
     * back when the transport does. Zeroing is a memset of buffers that
     * already exist; nothing here allocates. */
    void fadeOut (int samples);

private:
    /* `step' is how far apart two samples of one channel are and `hop'
       how far apart two channels start: (channels, 1) is interleaved and
       (1, windowlen) is planar. */
    bool run (float *buf, int channels, int windowlen, int step, int hop,
              const float *side, int sidechannels);

    void copyChanArgs (void);
    void assignChanArgPointers (void);
    void indexIOArgs (int windowlen);

    /* Every state arg of every node, zeroed. See fadeOut. */
    void clearState (void);


    thSynthTree *tree_;
    thArgMap args_;

    /* Where the io node keeps in0..in<N-1> and out0..out<N-1>, as arg
       indices, for the reason thMidiChan gives for its own: a by-name lookup
       on the audio thread searches a std::map, and thSynthTree::getArg
       *creates* the arg when it does not find one. */
    int inindex_[TH_MAX_CHANNELS];
    int outindex_[TH_MAX_CHANNELS];

    /* And where side<N> lives, for the graphs that asked for one. -1 where
       the file declared none, which is every effect that is not listening to
       a second channel -- so an echo pays nothing for this and a vocoder
       pays two buffers. */
    int sideindex_[TH_MAX_CHANNELS];

    /* And live<N>, on the same terms and for the same reason: -1 where the
       file declared none, which is every effect that is not listening to the
       machine. */
    int liveindex_[TH_MAX_CHANNELS];

    int sideChan_;

    int channels_;

    /* One window of de-interleaved samples, allocated once. */
    float *scratch_;

    /* The fadeOut ramp: its length, and how much of it is left. Both zero on
       an effect that is not fading. Last, so that every member an inline
       accessor above reads keeps the offset it had. */
    int fadelen_, faderemaining_;
};

#endif /* TH_CHANEFFECT_H */
