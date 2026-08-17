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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <filesystem>
#include <system_error>

#include <algorithm>

#include "think.h"
#include "parser.h"

thSynth *thSynth::instance_ = NULL;

/* `-l N' reaches windowlen through atoi(), which has no opinion about what a
   sensible window is: it will hand back 0, a negative, or two billion just as
   readily as 1024. All three of those size an allocation further down. */
static int clampWindowlen (int windowlen)
{
    if (windowlen >= 1 && windowlen <= TH_MAX_WINDOW_LENGTH)
        return windowlen;

    fprintf(stderr, "thSynth: window length %d out of range (1..%d), "
            "using %d\n", windowlen, TH_MAX_WINDOW_LENGTH,
            TH_DEFAULT_WINDOW_LENGTH);

    return TH_DEFAULT_WINDOW_LENGTH;
}

thSynth::thSynth (int windowlen, int samples)
{

    /* XXX: these should all be arguments and we should have corresponding
       accessor/mutator methods for these arguments */
    windowlen_ = clampWindowlen(windowlen);

    channels_ = 2;  /* mono / stereo / etc */

    /* intialize default sample rate */
    sampleRate_ = samples;

    /* We should make a function to allocate this, so we can easily change
       thChans and thWindowlen */
    output_ = new float[thOutputSamples(channels_, windowlen_)];

    /* Fixed capacity -- see TH_MIDI_CHANNELS. Never reallocated, so the audio
       thread can iterate it without racing a resize. */
    midiChannelCnt_ = TH_MIDI_CHANNELS;
    midiChannels_ = (thMidiChan **)calloc(midiChannelCnt_, sizeof(thMidiChan *));
    guiChannels_ = (thMidiChan **)calloc(midiChannelCnt_, sizeof(thMidiChan *));

    for (int i = 0; i < TH_MAX_PROBES; i++)
    {
        probes_[i] = NULL;
        guiProbes_[i] = NULL;
    }

    masterGain_ = TH_MASTER_GAIN_DEFAULT;

    /* default path */
    pluginmanager_ = new thPluginManager(PLUGIN_PATH);

    controllerHandler_ = new thMidiController();

    if (instance_ == NULL)
        instance_ = this;
}

thSynth::thSynth (const string &plugin_path, int windowlen, int samples)
{

    /* XXX: these should all be arguments and we should have corresponding
       accessor/mutator methods for these arguments */
    windowlen_ = clampWindowlen(windowlen);

    channels_ = 2;  /* mono / stereo / etc */

    /* intialize default sample rate */
    sampleRate_ = samples;

    /* We should make a function to allocate this, so we can easily change
       thChans and thWindowlen */
    output_ = new float[thOutputSamples(channels_, windowlen_)];

    /* Fixed capacity -- see TH_MIDI_CHANNELS. Never reallocated, so the audio
       thread can iterate it without racing a resize. */
    midiChannelCnt_ = TH_MIDI_CHANNELS;
    midiChannels_ = (thMidiChan **)calloc(midiChannelCnt_, sizeof(thMidiChan *));
    guiChannels_ = (thMidiChan **)calloc(midiChannelCnt_, sizeof(thMidiChan *));

    for (int i = 0; i < TH_MAX_PROBES; i++)
    {
        probes_[i] = NULL;
        guiProbes_[i] = NULL;
    }

    masterGain_ = TH_MASTER_GAIN_DEFAULT;

    pluginmanager_ = new thPluginManager(plugin_path);

    controllerHandler_ = new thMidiController();

    if (instance_ == NULL)
        instance_ = this;
}

thSynth::~thSynth (void)
{
    delete [] output_;

    /* The audio thread is expected to be stopped by now, so both queues can be
       emptied here without racing anything.
     *
     * The same thMidiChan can be reachable from up to three places at once: a
     * SET_CHANNEL command that was queued but never applied, guiChannels_, and
     * midiChannels_. Collect every candidate and delete each exactly once --
     * deleting per-slot double-freed any channel that was still in flight. */
    vector<thMidiChan *> doomed;

    /* Probes have the same three-places problem as channels, for the same
       reason: an unapplied SET_PROBE, guiProbes_ and probes_ can all name one
       object. */
    vector<thProbe *> doomedProbes;

    thSynthCommand cmd;

    while (commands_.pop(cmd))
    {
        delete cmd.note;
        delete cmd.arg;

        if (cmd.channel)
            doomed.push_back(cmd.channel);

        if (cmd.probe)
            doomedProbes.push_back(cmd.probe);
    }

    collectRetired();

    for (int i = 0; i < TH_MAX_PROBES; i++)
    {
        if (probes_[i])
            doomedProbes.push_back(probes_[i]);

        if (guiProbes_[i])
            doomedProbes.push_back(guiProbes_[i]);

        probes_[i] = NULL;
        guiProbes_[i] = NULL;
    }

    sort(doomedProbes.begin(), doomedProbes.end());
    doomedProbes.erase(unique(doomedProbes.begin(), doomedProbes.end()),
                       doomedProbes.end());

    for (vector<thProbe *>::iterator i = doomedProbes.begin();
         i != doomedProbes.end(); ++i)
    {
        delete *i;
    }

    /* The channels were never destroyed here at all -- each one leaked its
       args, its notes and (now) its tree. */
    for (int i = 0; i < midiChannelCnt_; i++)
    {
        if (midiChannels_[i])
            doomed.push_back(midiChannels_[i]);

        if (guiChannels_[i])
            doomed.push_back(guiChannels_[i]);

        midiChannels_[i] = NULL;
        guiChannels_[i] = NULL;
    }

    sort(doomed.begin(), doomed.end());
    doomed.erase(unique(doomed.begin(), doomed.end()), doomed.end());

    for (vector<thMidiChan *>::iterator i = doomed.begin();
         i != doomed.end(); ++i)
    {
        delete *i;
    }

    DestroyMap(treelist_);
    free(midiChannels_);
    free(guiChannels_);
    midiChannels_ = NULL;
    guiChannels_ = NULL;

    delete controllerHandler_;
    delete pluginmanager_;

    if (instance_ == this)
        instance_ = NULL;
}

/* ------------------------------------------------------------------------
 * Command plumbing. See thSynthCommand.h for why this exists.
 * ------------------------------------------------------------------------ */

/* GUI thread. */
bool thSynth::postCommand (const thSynthCommand &cmd)
{
    if (commands_.push(cmd))
        return true;

    /* The audio thread is not draining -- either it is wedged or there is no
       audio backend running at all. Drop the command rather than block the GUI,
       and clean up whatever it was carrying so nothing leaks. */
    fprintf(stderr, "thSynth: command queue full, dropping command %d\n",
            (int)cmd.type);

    delete cmd.note;
    delete cmd.channel;
    delete cmd.arg;
    delete cmd.probe;

    return false;
}

/* GUI thread. */
void thSynth::collectRetired (void)
{
    thRetired item;

    while (retired_.pop(item))
    {
        switch (item.kind)
        {
            case thRetired::NOTE:    delete item.note;    break;
            case thRetired::CHANNEL: delete item.channel; break;
            case thRetired::ARG:     delete item.arg;     break;
            case thRetired::PROBE:   delete item.probe;   break;
        }
    }
}

/* Audio thread. Retires whatever is in the slot and installs `probe', which
   may be NULL. */
void thSynth::installProbe (int slot, thProbe *probe)
{
    if (slot < 0 || slot >= TH_MAX_PROBES)
    {
        /* Cannot install it and must not leak it. */
        if (probe)
        {
            thRetired item;

            item.kind = thRetired::PROBE;
            item.probe = probe;

            if (!retired_.push(item))
                delete probe;
        }

        return;
    }

    thProbe *old = probes_[slot];

    probes_[slot] = probe;

    /* Unreachable from probes_ now, so the GUI thread may destroy it. */
    if (old)
    {
        thRetired item;

        item.kind = thRetired::PROBE;
        item.probe = old;

        if (!retired_.push(item))
            delete old;
    }
}

/* Audio thread. */
void thSynth::applyCommand (const thSynthCommand &cmd)
{
    thRetired item;

    /* SET_PROBE addresses a probe slot rather than a channel, so it has to be
       handled before the channel bounds check below -- which would otherwise
       reject a disarm of a probe whose channel has already gone. */
    if (cmd.type == thSynthCommand::SET_PROBE)
    {
        installProbe(cmd.probeSlot, cmd.probe);
        return;
    }

    if (cmd.chan < 0 || cmd.chan >= midiChannelCnt_)
    {
        /* Should not happen -- the GUI side range-checks -- but the payload
           still has to go somewhere. */
        if (cmd.note)    { item.kind = thRetired::NOTE;    item.note = cmd.note;
                           retired_.push(item); }
        if (cmd.channel) { item.kind = thRetired::CHANNEL; item.channel = cmd.channel;
                           retired_.push(item); }
        if (cmd.arg)     { item.kind = thRetired::ARG;     item.arg = cmd.arg;
                           retired_.push(item); }
        return;
    }

    thMidiChan *chan = midiChannels_[cmd.chan];

    switch (cmd.type)
    {
        case thSynthCommand::NOTE_ON:
            if (chan)
            {
                chan->insertNote(cmd.note, &retired_);
            }
            else if (cmd.note)
            {
                item.kind = thRetired::NOTE;
                item.note = cmd.note;
                if (!retired_.push(item))
                    delete cmd.note;
            }
            break;

        case thSynthCommand::NOTE_OFF:
            if (chan)
                chan->releaseNote(cmd.noteId);
            break;

        case thSynthCommand::ALL_NOTES_OFF:
            for (int i = 0; i < midiChannelCnt_; i++)
            {
                if (midiChannels_[i])
                    midiChannels_[i]->clearAll(&retired_);
            }
            break;

        case thSynthCommand::SET_CHANNEL:
            midiChannels_[cmd.chan] = cmd.channel;

            /* Nothing to do about probes here. A probe on this channel was
               resolved against the outgoing thMidiChan's serial, which the new
               one does not share, so process() stops accumulating it of its
               own accord -- see the check there. The GUI disarms properly
               before queueing this (disarmProbesOn); the serial is the belt to
               those braces, and it fails towards silence rather than towards a
               display confidently drawing the wrong node.
             *
             * The old channel is now unreachable from this array, so it is
               safe for the GUI thread to destroy. */
            if (chan)
            {
                item.kind = thRetired::CHANNEL;
                item.channel = chan;
                if (!retired_.push(item))
                    delete chan;
            }
            break;

        case thSynthCommand::SET_PROBE:
            /* Handled above, before the channel bounds check. Listed so that
               adding a command type keeps failing to compile here until it is
               dealt with, which is how this switch has stayed honest. */
            break;

        case thSynthCommand::SET_CHAN_ARG:
            if (chan)
            {
                chan->setArg(cmd.arg, &retired_);
            }
            else if (cmd.arg)
            {
                item.kind = thRetired::ARG;
                item.arg = cmd.arg;
                if (!retired_.push(item))
                    delete cmd.arg;
            }
            break;
    }
}

/* ------------------------------------------------------------------------
 * Probes. See thProbe.h.
 * ------------------------------------------------------------------------ */

/* GUI thread.
 *
 * Resolving here rather than on the audio thread is the whole point: this
 * walks maps, looks things up by name, and allocates the ring and the
 * accumulator. What crosses to the audio thread is a finished object addressed
 * by two integers.
 */
int thSynth::armProbe (int channum, const string &node, const string &arg,
                       string &why)
{
    std::lock_guard<std::mutex> lock(synthMutex_);
    collectRetired();

    if ((channum < 0) || (channum >= midiChannelCnt_))
    {
        why = "no such channel";
        return -1;
    }

    thMidiChan *chan = guiChannels_[channum];

    if (chan == NULL)
    {
        why = "nothing loaded on that channel";
        return -1;
    }

    thSynthTree *tree = chan->modnode();

    if (tree == NULL)
    {
        why = "that channel has no tree";
        return -1;
    }

    thNode *n = tree->findNode(node);

    if (n == NULL)
    {
        why = "no node called '" + node + "'";
        return -1;
    }

    thArg *a = n->getArg(arg);

    if (a == NULL)
    {
        why = "node '" + node + "' has no arg '" + arg + "'";
        return -1;
    }

    const int argIndex = a->index();

    if (argIndex < 0)
    {
        /* buildArgMap indexes every arg it sees, so this means the tree was
           never resolved -- not that the arg is unusual. */
        why = "'" + node + "." + arg + "' was never indexed";
        return -1;
    }

    /* Plugin-internal state is not a signal. delay::echo's ring buffer and
       every filter's `last' are allocated and read across windows by the
       plugin and referenced by no .dsp in the corpus; drawing one would be
       drawing an implementation detail, and NodeGraph already refuses to wire
       them for the same reason. The io node has no plugin and so declares no
       directions, which is why this only rejects what a plugin positively
       says is state. */
    thPlugin *plug = n->plugin();

    if (plug && argIndex < plug->argCount() &&
        plug->getArgDir(argIndex) == thPlugin::ARG_STATE)
    {
        why = "'" + node + "." + arg + "' is plugin state, not a signal";
        return -1;
    }

    /* Retargeting an existing probe reuses its slot; otherwise take a free
       one. Matching on node and arg means arming the same point twice is
       idempotent rather than burning two of the eight. */
    int slot = -1;

    for (int i = 0; i < TH_MAX_PROBES; i++)
    {
        if (guiProbes_[i] && guiProbes_[i]->chan() == channum &&
            guiProbes_[i]->nodeName() == node && guiProbes_[i]->argName() == arg)
        {
            slot = i;
            break;
        }
    }

    if (slot < 0)
    {
        for (int i = 0; i < TH_MAX_PROBES; i++)
        {
            if (guiProbes_[i] == NULL)
            {
                slot = i;
                break;
            }
        }
    }

    if (slot < 0)
    {
        why = "all probe slots are in use";
        return -1;
    }

    thProbe *probe = new thProbe(channum, chan->serial(), n->id(), argIndex,
                                 node, arg, (unsigned int)windowlen_,
                                 (unsigned int)windowlen_ *
                                     TH_PROBE_RING_WINDOWS + 1);

    thSynthCommand cmd;

    cmd.type = thSynthCommand::SET_PROBE;
    cmd.chan = channum;
    cmd.probeSlot = slot;
    cmd.probe = probe;

    if (!postCommand(cmd))
    {
        /* postCommand has already destroyed the probe. Leave guiProbes_ alone:
           if this was a retarget, the old probe is still the one installed. */
        why = "the command queue is full";
        return -1;
    }

    /* Whatever was here is now the audio thread's to hand back. */
    guiProbes_[slot] = probe;

    return slot;
}

/* GUI thread. */
void thSynth::disarmProbe (int slot)
{
    std::lock_guard<std::mutex> lock(synthMutex_);
    collectRetired();

    if ((slot < 0) || (slot >= TH_MAX_PROBES) || (guiProbes_[slot] == NULL))
        return;

    thSynthCommand cmd;

    cmd.type = thSynthCommand::SET_PROBE;
    cmd.chan = guiProbes_[slot]->chan();
    cmd.probeSlot = slot;
    cmd.probe = NULL;

    /* Only forget it if the audio thread has actually been told to drop it --
       the same reasoning as removeChan. Clearing guiProbes_ against a full
       queue would leave the callback publishing into a probe nothing would
       ever free. */
    if (postCommand(cmd))
        guiProbes_[slot] = NULL;
}

/* GUI thread, with synthMutex_ held. */
void thSynth::disarmProbesOn (int channum)
{
    for (int i = 0; i < TH_MAX_PROBES; i++)
    {
        if (guiProbes_[i] == NULL || guiProbes_[i]->chan() != channum)
            continue;

        thSynthCommand cmd;

        cmd.type = thSynthCommand::SET_PROBE;
        cmd.chan = channum;
        cmd.probeSlot = i;
        cmd.probe = NULL;

        if (postCommand(cmd))
            guiProbes_[i] = NULL;
    }
}

/* Audio thread, at the top of process(). */
void thSynth::drainCommands (void)
{
    thSynthCommand cmd;

    while (commands_.pop(cmd))
        applyCommand(cmd);
}

void thSynth::removeChan (int channum)
{
    if ((channum < 0) || (channum >= midiChannelCnt_))
        return;

    std::lock_guard<std::mutex> lock(synthMutex_);
    collectRetired();

    if (guiChannels_[channum] != NULL)
    {
        thSynthCommand cmd;

        /* Queue the removal instead of deleting here: the callback may be
           inside this very channel. The audio thread hands it back through the
           retire queue once it is unreachable, and collectRetired() frees it.
           The delete used to be commented out entirely, so every removed
           channel leaked its notes, args and tree. */
        /* Before the channel goes, not after: the queue is FIFO, so a disarm
           posted first is applied first, and the probe never sees the tree it
           was resolved against being torn down. */
        disarmProbesOn(channum);

        cmd.type = thSynthCommand::SET_CHANNEL;
        cmd.chan = channum;
        cmd.channel = NULL;

        /* Only forget the channel if the audio thread has actually been told
           to drop it. Clearing guiChannels_ first meant that a full queue left
           the GUI believing the channel was gone while the callback carried on
           playing it -- and with the GUI's only reference dropped, nothing was
           ever going to free it. The bookkeeping below has to go the same way,
           or the patch list and controller map disagree with reality. */
        if (postCommand(cmd))
        {
            guiChannels_[channum] = NULL;
            patchlist_[channum] = "";
            controllerHandler_->clearByDestChan(channum);
        }
    }

}

/* Common tail for the loadTree() overloads.
 *
 * Historically the return value of YYPARSE() was thrown away and the tree was
 * resolved regardless. A .dsp with a syntax error, a missing `io' declaration,
 * or a node referencing a plugin that failed to load would then produce a tree
 * with a NULL ionode and an unbuilt node index, which the audio thread would
 * walk straight off the end of. Callers already handle a NULL return.
 *
 * `registerTree' decides ownership. The per-channel overload passes false and
 * hands the tree to the thMidiChan, which owns it outright; the two whole-synth
 * overloads pass true and the tree goes into treelist_, owned by thSynth.
 *
 * Trees used to *always* go into treelist_, keyed by the DSP's `name'
 * statement. Since every .dsp without a `name' parses as "newmod", they all
 * collided -- and thMidiChan::assignChanArgPointers caches raw thArg pointers
 * into the tree it is handed, so two channels sharing one tree silently
 * overwrote each other's chanarg pointers, and destroying either left the other
 * pointing at freed memory.
 *
 * When registerTree is true this touches treelist_, so those callers
 * hold synthMutex_; `tree' is consumed either way.
 */
thSynthTree *thSynth::finishParse (const string &what, thSynthTree *tree,
                                   int parseResult, bool registerTree)
{
    if (tree == NULL)
    {
        return NULL;
    }

    if (parseResult != 0)
    {
        fprintf(stderr, "%s: parse failed, discarding\n", what.c_str());
        delete tree;
        return NULL;
    }

    if (tree->IONode() == NULL)
    {
        fprintf(stderr, "%s: DSP does not have a valid IO node!\n",
                what.c_str());
        delete tree;
        return NULL;
    }

    tree->buildArgMap(); /* build the index of args */
    tree->setPointers();

    /* The three args every note writes into its io node, declared here so that
       the tree owns them whether or not the .dsp mentioned them.
     *
     * thMidiNote's constructor does setArg("note"/"velocity"/"trigger") on its
     * copy of the tree. Where the .dsp never referenced them -- 5 of the 92
     * shipped files -- that *created* the arg in each copy, taking a fresh
     * index out of addArgToIndex. The indices happened to agree between copies
     * because every copy starts from the same argCount_, which is a
     * coincidence rather than a guarantee; and nothing outside a copy could
     * name them at all, so a probe on `ionode.note' resolved against the
     * prototype and found nothing while the node editor cheerfully drew the
     * port. This is the same thing buildArgMap already does for a plugin arg a
     * .dsp omitted: the engine knows the arg exists, so the tree should say so.
     *
     * After setPointers(), not before. buildArgMap() begins by resetting
     * argCount_ to zero and then only indexes args whose index is still
     * negative, so the counter is not settled until setPointers() has finished
     * creating args for the references it resolves -- and a .dsp with a typo in
     * one of those references creates one. `harpsi0.dsp' reads
     * `ionode->bandlo' where the io node declares `bandlow'; done any earlier,
     * that typo and `note' ended up sharing index 21, and a probe on
     * `ionode.bandlo' read back the note numbers. */
    if (tree->IONode())
    {
        thNode *io = tree->IONode();

        if (io->getArg("note") == NULL)
            io->setArg("note", 0);

        if (io->getArg("velocity") == NULL)
            io->setArg("velocity", 0);

        if (io->getArg("trigger") == NULL)
            io->setArg("trigger", 0);
    }

    /* After setPointers(), because a control's consumers are exactly its
       ARG_CHANNEL references and buildArgMap() is what settles the arg indices
       this reads the plugin's metadata by. Before anything is handed out, so
       that a channel's copy of the chanargs and the node editor's own parse
       agree about what a control is. */
    tree->typeChanArgs();

    tree->buildSynthTree();

    if (registerTree)
    {
        /* Still name-keyed, so still collision-prone -- but nothing owns these
           except thSynth itself, so a collision only leaks. */
        map<string, thSynthTree*>::iterator existing =
            treelist_.find(tree->name());

        if (existing != treelist_.end() && existing->second != tree)
        {
            delete existing->second;
        }

        treelist_[tree->name()] = tree;
    }

    return tree;
}

thSynthTree * thSynth::loadTree (const string &filename)
{
    std::error_code ec;

    if (!std::filesystem::exists(filename, ec))
    {
        /* ec is only set when the query itself failed -- a permission problem
           on a parent directory, say. A file that is simply not there is not
           an error to fs::exists, so it reports nothing and the message has
           to supply its own wording rather than reach for errno, which
           nothing here has set. */
        fprintf (stderr, "couldn't open %s: %s\n", filename.c_str(),
                 ec ? ec.message().c_str() : "no such file or directory");
        return NULL;
    }
    else if (std::filesystem::is_directory(filename, ec))
    {
        fprintf(stderr, "%s is a directory\n", filename.c_str());

#ifdef EISDIR
        errno = EISDIR; /* XXX */
#endif

        return NULL;
    }

    /* "rb", not "r". The tokens carry byte offsets into the file as it sits
       on disk -- that is what thcGenEdit splices by and what NodeEdit would
       -- and text mode on Windows eats a byte per CRLF, so every span past
       the first line would name the wrong bytes. See thLexStream. */
    FILE *input = fopen(filename.c_str(), "rb");

    if (input == NULL) { /* ENOENT or smth */
        fprintf (stderr, "couldn't open %s: %s\n", filename.c_str(),
                 strerror(errno));
        return NULL;
    }

    /* The parser is pure now; the mutex is for treelist_, which
       finishParse(registerTree = true) mutates. */
    std::lock_guard<std::mutex> lock(synthMutex_);

    thSynthTree *raw = NULL;
    int parseResult = thParseDsp(this, input, &raw);

    fclose(input);

    /* No channel involved, so thSynth keeps this one. */
    thSynthTree *tree = finishParse(filename, raw, parseResult, true);

    return tree;
}

thSynthTree * thSynth::parseTree (const string &filename)
{
    std::error_code ec;

    if (!std::filesystem::exists(filename, ec) ||
        std::filesystem::is_directory(filename, ec))
        return NULL;

    /* "rb", not "r". The tokens carry byte offsets into the file as it sits
       on disk -- that is what thcGenEdit splices by and what NodeEdit would
       -- and text mode on Windows eats a byte per CRLF, so every span past
       the first line would name the wrong bytes. See thLexStream. */
    FILE *input = fopen(filename.c_str(), "rb");

    if (input == NULL)
        return NULL;

    /* No mutex, deliberately: the parser is pure -- a scanner and a
       context per call -- and with registerTree false, finishParse
       touches nothing shared either. This function used to open with a
       paragraph about the window between assigning the global yyin and
       taking the lock; the fix for that class of bug was not a wider
       lock but the absence of the global.

       "Touches nothing shared" wants one qualification, because there is
       exactly one thing left: a parse resolves its nodes' plugins through
       thPluginManager, which is the synth's and not the parse's.
       thPluginManager locks its own map and resolves a name in one call,
       which is where that belongs -- a lock here would serialize whole
       parses to protect one map lookup, and would still leave the manager
       unsafe for the node editor and the composer window, which reach it
       from outside any parse at all. */
    thSynthTree *raw = NULL;
    int parseResult = thParseDsp(this, input, &raw);

    fclose(input);

    thSynthTree *tree = finishParse(filename, raw, parseResult, false);

    return tree;
}

thSynthTree * thSynth::loadTree (FILE *input)
{
    if (!input)
        return NULL;

    /* The mutex is for treelist_, as in the by-name overload. */
    std::lock_guard<std::mutex> lock(synthMutex_);

    thSynthTree *raw = NULL;
    int parseResult = thParseDsp(this, input, &raw);

    thSynthTree *tree = finishParse("<stream>", raw, parseResult, true);

    return tree;
}

/* Carry a parameter's description -- widget type, range, label, units, group
 * -- from the arg being updated onto the one replacing it.
 *
 * A description belongs to whoever declared the parameter: the .dsp for a
 * `@chanarg', thMidiChan for the channel's own `amp'. Everything that reaches
 * setChanArg from outside libthink is expressing a *value*: the patch parser
 * reading `cutoff 8.809662', the preferences restoring a saved channel
 * amplitude, the Patch Selector's amplitude spinner. They all build a bare
 * `new thArg(name, value)', which by construction carries the defaults --
 * widget type HIDE, range 0..MIDIVALMAX, no label.
 *
 * So the incoming description was never a description. setChanArg used to
 * copy it across anyway, which meant the channel amplitude -- the one arg
 * libthink itself describes -- was declared a slider by thMidiChan and then
 * quietly reset to HIDE by the first thing to set its value. Restoring a
 * saved session does exactly that for every channel, so the control vanished
 * from the panel for precisely the patches you had loaded last time.
 *
 * None of this is read by the audio thread. */
static void describeLike (thArg *arg, const thArg *like)
{
    arg->setWidgetType(like->widgetType());
    arg->setMin(like->min());
    arg->setMax(like->max());
    arg->setLabel(like->label());
    arg->setUnits(like->units());
    arg->setGroup(like->group());
}

/* GUI thread. Takes ownership of `arg'. */
void thSynth::setChanArg (int channum, thArg *arg)
{
    if ((channum < 0) || (channum >= midiChannelCnt_) || arg == NULL)
    {
        delete arg;
        return;
    }

    std::lock_guard<std::mutex> lock(synthMutex_);
    collectRetired();

    if (!guiChannels_[channum])
    {
        delete arg;
        return;
    }

    thArg *existing = guiChannels_[channum]->getArg(arg->name());

    /* Fast path: changing the value of an arg that is already a single float.
     *
     * thArg::setValue does not reallocate in that case, so it is one relaxed
     * atomic store into a buffer the audio thread is only reading -- safe to
     * do right here, and it has to be done right here, because callers read
     * their own writes straight back. gthPatchManager::newPatch saves the
     * amplitude, reloads the DSP, restores it with setChanArg and then
     * repopulates the slider table from the channel; queueing that made the
     * GUI show the pre-restore value. Patch loading does the same for every
     * `name value' line it parses.
     *
     * The description is not touched. See describeLike() below. */
    if (existing != NULL
        && existing->type() == thArg::ARG_VALUE && existing->len() == 1
        && arg->type() == thArg::ARG_VALUE && arg->len() == 1)
    {
        existing->setValue((*arg)[0]);

        delete arg;
        return;
    }

    /* Anything else really is a replacement -- a new arg, or one changing
       length -- and installing it deletes the arg it displaces while live note
       trees still point at that one until the channel re-resolves them. That
       has to happen on the audio thread.

       The replacement inherits the description first, on this thread, while
       the arg it is displacing is still ours to read. */
    if (existing != NULL)
        describeLike(arg, existing);

    thSynthCommand cmd;

    cmd.type = thSynthCommand::SET_CHAN_ARG;
    cmd.chan = channum;
    cmd.arg = arg;

    postCommand(cmd);

}

/* GUI thread.
 *
 * The returned thArg is shared with the audio thread, which reads it every
 * window. Writing a single float through setValue() is safe (see thArg.cpp);
 * anything that reallocates has to go through setChanArg() instead.
 */
thArg *thSynth::getChanArg (int channum, const string &argname)
{
    if ((channum < 0) || (channum >= midiChannelCnt_))
    {
        return NULL;
    }

    thMidiChan *chan = guiChannels_[channum];

    if (!chan)
    {
        return NULL;
    }

    return chan->getArg(argname);
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

thSynthTree * thSynth::loadTree (const string &filename, int channum, float amp)
{
    if (channum < 0)
    {
        fprintf(stderr, "thSynth::loadTree: negative channel %d\n", channum);
        return NULL;
    }

    std::error_code ec;

    if (!std::filesystem::exists(filename, ec))
    {
        /* ec is only set when the query itself failed -- a permission problem
           on a parent directory, say. A file that is simply not there is not
           an error to fs::exists, so it reports nothing and the message has
           to supply its own wording rather than reach for errno, which
           nothing here has set. */
        fprintf (stderr, "couldn't open %s: %s\n", filename.c_str(),
                 ec ? ec.message().c_str() : "no such file or directory");
        return NULL;
    }
    else if (std::filesystem::is_directory(filename, ec))
    {
        fprintf(stderr, "%s is a directory\n", filename.c_str());

#ifdef EISDIR
        errno = EISDIR; /* XXX */
#endif

        return NULL;
    }

    /* "rb", not "r". The tokens carry byte offsets into the file as it sits
       on disk -- that is what thcGenEdit splices by and what NodeEdit would
       -- and text mode on Windows eats a byte per CRLF, so every span past
       the first line would name the wrong bytes. See thLexStream. */
    FILE *input = fopen(filename.c_str(), "rb");

    if (input == NULL) { /* ENOENT or smth */
        fprintf (stderr, "couldn't open %s: %s\n", filename.c_str(),
                 strerror(errno));
        return NULL;
    }

    /* The parser is pure; the mutex is for the channel bookkeeping and
       retire queue below, as it always really was. */
    std::lock_guard<std::mutex> lock(synthMutex_);
    collectRetired();

    thSynthTree *raw = NULL;
    int parseResult = thParseDsp(this, input, &raw);

    fclose(input);

    /* registerTree false: the thMidiChan below takes ownership. */
    thSynthTree *tree = finishParse(filename, raw, parseResult, false);

    if (tree == NULL)
    {
        return NULL;
    }

    /* The array is a fixed TH_MIDI_CHANNELS slots and is never resized, so
       there is nothing to grow here any more. */
    if (channum >= midiChannelCnt_)
    {
        fprintf(stderr, "thSynth::loadTree: channel %d is beyond the %d "
                "available channels\n", channum, midiChannelCnt_);
        delete tree;
        return NULL;
    }

    /* Build the replacement fully before publishing it, then queue the swap.
       Deleting the old channel here would free it under a callback that may be
       inside it; the audio thread hands it back once it is unreachable. */
    thMidiChan *newchan = new thMidiChan(tree, amp, windowlen_);

    /* Before the swap, not after. A probe's node id was measured against the
       tree that is about to be replaced, and ids are assigned in parse order,
       so the same id in the new tree is a different node. Re-arming by name is
       the caller's job -- only it knows whether the node still exists. */
    disarmProbesOn(channum);

    thSynthCommand cmd;

    cmd.type = thSynthCommand::SET_CHANNEL;
    cmd.chan = channum;
    cmd.channel = newchan;

    /* Publish only once the swap is queued. Assigning first and then resetting
       to NULL on failure would drop the GUI's reference to whatever channel was
       already there, leaving it playing with nothing able to reach it. */
    if (!postCommand(cmd))
    {
        /* postCommand deleted newchan, which owns the tree; the previous
           channel is untouched and still current. */
        return NULL;
    }

    guiChannels_[channum] = newchan;

    patchlist_[channum] = filename;

    /* make sure there are no midi controllers set up for this channel */
    controllerHandler_->clearByDestChan(channum);

    return tree;
}

/* Make these voids return something and add error checking everywhere! */
void thSynth::listTrees (void)
{
    for (map<string, thSynthTree*>::const_iterator im = treelist_.begin(); 
         im != treelist_.end(); ++im) {
        printf("%s\n", im->first.c_str());
    }
}

/* GUI thread.
 *
 * Building the note is the expensive half -- it copy-constructs the channel's
 * whole synth tree -- and it stays here. Only the container insert crosses over
 * to the audio thread. Doing both here, while the callback walked notes_, is
 * what produced the static when two notes sounded together.
 */
bool thSynth::addNote (int channum, float note, float velocity)
{
    /* was `> midiChannelCnt_' -- midiChannels_[midiChannelCnt_] is one past
       the end of the array. */
    if ((channum < 0) || (channum >= midiChannelCnt_))
    {
        debug("thSynth::addNote: no such channel %d", channum);

        return false;
    }

    std::lock_guard<std::mutex> lock(synthMutex_);
    collectRetired();

    thMidiChan *chan = guiChannels_[channum];

    if (!chan)
    {
        debug("thSynth::addNote: no such channel %d", channum);
        return false;
    }

    thMidiNote *newnote = chan->buildNote(note, velocity);

    if (newnote == NULL)
    {
        return false;
    }

    thSynthCommand cmd;

    cmd.type = thSynthCommand::NOTE_ON;
    cmd.chan = channum;
    cmd.note = newnote;

    bool ok = postCommand(cmd);

    return ok;
}

int thSynth::delNote (int channum, float note)
{
    /* was `> midiChannelCnt_' -- one past the end of the array. */
    if ((channum < 0) || (channum >= midiChannelCnt_))
        return 1;

    std::lock_guard<std::mutex> lock(synthMutex_);
    collectRetired();

    if (!guiChannels_[channum])
    {
        return 1;
    }

    /* The sustain-pedal test moved to thMidiChan::releaseNote, on the audio
       thread: reading the pedal and poking the note's `trigger' arg from here
       meant writing into a note the callback was mixing. */
    thSynthCommand cmd;

    cmd.type = thSynthCommand::NOTE_OFF;
    cmd.chan = channum;
    cmd.noteId = (int)note;

    postCommand(cmd);

    return 0;
}

void thSynth::clearAll (void)
{
    std::lock_guard<std::mutex> lock(synthMutex_);
    collectRetired();

    /* This used to walk `while (*c) (*c++)->clearAll()', relying on a NULL
       terminator that midiChannels_ does not have -- with every slot occupied
       it ran straight off the end of the array. */
    thSynthCommand cmd;

    cmd.type = thSynthCommand::ALL_NOTES_OFF;
    cmd.chan = 0;

    postCommand(cmd);

}

/* Audio thread. */
void thSynth::process (void)
{
    int mixchannels, notechannels;
    thMidiChan *chan;
    float *chanoutput;

    /* Apply anything the GUI thread has queued. This is the only place
       midiChannels_ and everything below it is mutated, which is what makes
       the mutex the old code had commented out here unnecessary. */
    drainCommands();

    memset(output_, 0,
           thOutputSamples(channels_, windowlen_) * sizeof(float));

    /* Probes are zeroed before any channel runs and published after all of
       them, rather than around the channel they belong to, so that a probe on
       a channel with nothing loaded -- or on one whose process() bails out
       early because it has no output buffer -- still publishes a window of
       silence. A display that froze on its last content instead of going quiet
       would be lying about the signal. */
    int probeCount = 0;

    for (int p = 0; p < TH_MAX_PROBES; p++)
    {
        if (probes_[p] == NULL)
            continue;

        probes_[p]->beginWindow();
        probeCount++;
    }

    for (int i = 0; i < midiChannelCnt_; i++)
    {
        /* Gathered per channel, so thMidiChan is handed only the probes that
           concern it and its note loops carry no test beyond the count. Eight
           slots, so this is a fixed eight-iteration scan and not worth
           caching. */
        thProbe *taps[TH_MAX_PROBES];
        int ntaps = 0;

        chan = midiChannels_[i];

        if (probeCount && chan)
        {
            for (int p = 0; p < TH_MAX_PROBES; p++)
            {
                /* The serial is what makes a probe stop rather than start
                   reading a different node when the patch on this channel is
                   replaced. See thMidiChan::serial(). */
                if (probes_[p] && probes_[p]->chan() == i &&
                    probes_[p]->chanSerial() == chan->serial())
                {
                    taps[ntaps++] = probes_[p];
                }
            }
        }

        if (chan)
        {
            notechannels = chan->numChannels();
            mixchannels = notechannels;
            
            if (mixchannels > channels_) {
                mixchannels = channels_;
            }
            
            chan->process(&retired_, taps, ntaps);
            chanoutput = chan->output();

            if (chanoutput == NULL || mixchannels <= 0) {
                continue;
            }

            int bufferoffset = 0;
            int inneroffset, chanoffset;

            for (int j = 0; j < mixchannels; j++)
            {
                inneroffset = bufferoffset;
                chanoffset = j;
                for (int k = 0; k < windowlen_; k++)
                {
                    output_[inneroffset] += chanoutput[chanoffset];
                    inneroffset++;
                    chanoffset += mixchannels;
                }

                bufferoffset += windowlen_;
            }
        }
    }

    /* Master gain, then the limiter.
     *
     * Voices are summed above with no headroom management and they sum
     * coherently -- every envelope peaks together on the attack -- so the mix
     * routinely runs past full scale once more than one note sounds. Doing
     * this here rather than in each audio backend means every output path gets
     * it, and getOutput() hands back the signal that will actually be heard.
     * The clamps in the ALSA and JACK paths stay as a backstop for anything
     * the limiter cannot tame (a NaN out of a diverging DSP, say). */
    const float gain = masterGain();
    const size_t samples = thOutputSamples(channels_, windowlen_);

    for (size_t i = 0; i < samples; i++)
        output_[i] = thSoftLimit(output_[i] * gain);

    /* Deliberately after the mix and deliberately untouched by the gain or the
       limiter: a probe reports the signal at a point inside the graph, and
       bending it by a master-stage decision made downstream would make the
       display disagree with the patch it is describing. */
    for (int p = 0; p < TH_MAX_PROBES; p++)
    {
        if (probes_[p])
            probes_[p]->publish();
    }
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
    /* output_ is allocated once in the constructor and never moved, so there is
       nothing to lock here -- the commented-out mutex was pointless. */
    return output_;
}

float *thSynth::getChanBuffer (int chan)
{
    return &output_[chan * windowlen_];
}

void thSynth::setWindowlen (int windowlen)
{
    /* XXX: fixme */
#if 0
    std::lock_guard<std::mutex> lock(synthMutex_);
    windowlen_ = clampWindowlen(windowlen);
    delete [] output_;
    output_ = new float[thOutputSamples(channels_, windowlen_)];
#endif
}
