/*
 * Copyright (C) 2004-2026 The thinksynth authors
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

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>

#include "think.h"
#include "thUnits.h"

#include "thcPlugin.h"
#include "thcScheduler.h"
#include "thcGenFile.h"
#include "JsonOut.h"

static_assert(THC_NOTE_SAY == TH_NOTE_SAY,
              "a note's say fits the voice's");

/* ---- thcParamStore ---------------------------------------------------- */

thcParamStore::thcParamStore (thcPlugin *plugin, unsigned seed)
    : plugin_(plugin), recording_(true), replaying_(false), instance_(NULL)
{
    int count = plugin->paramCount();

    values_.resize(count, 0.0);
    strings_.resize(count);
    beats_.resize(count, 0);
    knobs_.resize(count, (thArg *)NULL);
    nodes_.resize(count, (thArg *)NULL);
    lastNode_.resize(count, 0.0f);

    for (int i = 0; i < count; i++)
    {
        const thcPlugin::ParamInfo *p = plugin->paramInfo(i);

        values_[i] = p->def;
        strings_[i] = p->defString;
    }

    params_.ctx = this;
    params_.get = cbGet;
    params_.get_string = cbGetString;
    params_.seed = seed;

    /* No ear until the scheduler offers one, and never in a build
       that has none. Left unset, a module read a stray pointer here. */
    params_.audition = NULL;
}

double
thcParamStore::cbGet (void *ctx, int index)
{
    return static_cast<thcParamStore *>(ctx)->get(index);
}

const char *
thcParamStore::cbGetString (void *ctx, int index)
{
    return static_cast<thcParamStore *>(ctx)->getString(index);
}

double
thcParamStore::get (int index) const
{
    if (index < 0 || index >= (int)values_.size())
        return 0.0;

    /* A binding shadows the stored value entirely: the knob, or the
       embedded node's output, IS the value and there is nothing else to
       consult. Read here, at the moment the composer asks -- which is
       what makes `step = lfo->out' mean what it says rather than
       whatever the LFO happened to be when the piece loaded.
     *
       Binding one releases the other (see bindKnob), so the order
       these are tried in is not a precedence rule anybody has to
       remember: at most one of them is ever set. */
    if (held_ && nodes_[index] == NULL)
        return heldValues_[index];

    double v = values_[index];

    if (knobs_[index] != NULL)
        v = (double)(*knobs_[index])[0];
    else if (nodes_[index] != NULL)
        v = (double)(*nodes_[index])[0];

    /* Beats convert at read time, through whatever the tempo is at this
       moment -- that, and only that, is what makes `period = 4 beats'
       survive tempo automation mid-piece. */
    if (beats_[index] && tempo_)
    {
        double bpm = held_ ? heldTempo_ : tempo_();

        if (bpm > 0)
            v = v * 60.0 / bpm;
    }

    return v;
}

const char *
thcParamStore::getString (int index) const
{
    if (index < 0 || index >= (int)strings_.size())
        return "";

    if (held_)
        return heldStrings_[index].c_str();

    return strings_[index].c_str();
}

void
thcParamStore::set (int index, double v)
{
    if (index < 0 || index >= (int)values_.size())
        return;

    record(OP_SET, index, v);
    values_[index] = v;

    /* Forward to the module (a no-op when it exports no
       composer_param_changed), then wake it if it was sleeping: the
       contract on THC_NEVER is that a param change re-arms the tick. */
    announce(index);
}

void
thcParamStore::setString (int index, const std::string &v)
{
    if (index < 0 || index >= (int)strings_.size())
        return;

    record(OP_STRING, index, 0, v);
    strings_[index] = v;

    announce(index);
}

bool
thcParamStore::set (const std::string &name, double v)
{
    int index = plugin_->paramIndex(name);

    if (index < 0)
        return false;

    set(index, v);
    return true;
}

bool
thcParamStore::setString (const std::string &name, const std::string &v)
{
    int index = plugin_->paramIndex(name);

    if (index < 0)
        return false;

    setString(index, v);
    return true;
}

bool
thcParamStore::anyBeats (void) const
{
    for (size_t i = 0; i < beats_.size(); i++)
        if (beats_[i])
            return true;

    return false;
}

void
thcParamStore::setBeats (int index, bool beats)
{
    if (index >= 0 && index < (int)beats_.size())
    {
        record(OP_BEATS, index, 0, std::string(), NULL, beats);
        beats_[index] = beats ? 1 : 0;
    }
}

void
thcParamStore::bindKnob (int index, thArg *knob)
{
    if (index < 0 || index >= (int)knobs_.size())
        return;

    record(OP_KNOB, index, 0, std::string(), knob);
    knobs_[index] = knob;

    /* Exclusive by construction rather than by a precedence rule
       somebody has to remember: a param reads its stored value, or a
       knob, or a node, and binding one releases the other. */
    if (knob != NULL)
        nodes_[index] = NULL;
}

thArg *
thcParamStore::knobBinding (int index) const
{
    if (index < 0 || index >= (int)knobs_.size())
        return NULL;

    return knobs_[index];
}

void
thcParamStore::bindNode (int index, thArg *out)
{
    if (index < 0 || index >= (int)nodes_.size())
        return;

    record(OP_NODE, index, 0, std::string(), out);
    nodes_[index] = out;

    if (out != NULL)
        knobs_[index] = NULL;
}

thArg *
thcParamStore::nodeBinding (int index) const
{
    if (index < 0 || index >= (int)nodes_.size())
        return NULL;

    return nodes_[index];
}

void
thcParamStore::record (OpKind kind, int index, double value,
                       const std::string &text, thArg *arg, bool flag)
{
    /* A replay is not something that happened; it is something being
       done again. Recorded, it would double at every rewind. */
    if (replaying_)
        return;

    Op op = { kind, index, value, text, arg, flag };

    if (recording_)
    {
        /* A knob dragged before the first Play announces once per tick
           of the slider, and every one of those would be replayed into
           a module that may do real work on each. Only the last of a run
           on one index says anything the one before it did not. */
        if (kind == OP_NOTIFY && !history_.empty() &&
            history_.back().kind == OP_NOTIFY &&
            history_.back().index == index)
            return;

        history_.push_back(op);
        return;
    }

    /* The transport has run, so this is an edit rather than part of the
       load: the desktop poking the live store so a change to the work
       file is heard without a reload. A rewind is a load, and the file
       a load would read is the edited one, so it is kept and replayed
       after the load's own operations.
     *
       An announcement on its own is not kept. What makes them after the
       first start is a knob being moved, and a knob is not in the store:
       it holds its own position across a rewind, the param reads through
       to it, and the bind that made it readable is already in one of the
       two lists. Replaying a whole drag at every rewind would cost a
       param_changed each and change nothing. */
    if (kind != OP_NOTIFY)
        edits_.push_back(op);
}

void
thcParamStore::restoreDefaults (void)
{
    for (size_t i = 0; i < values_.size(); i++)
    {
        const thcPlugin::ParamInfo *p = plugin_->paramInfo((int)i);

        values_[i] = p->def;
        strings_[i] = p->defString;
        beats_[i] = 0;
        knobs_[i] = NULL;
        nodes_[i] = NULL;
        lastNode_[i] = 0.0f;
    }
}

void
thcParamStore::run (const std::vector<Op> &ops)
{
    for (size_t i = 0; i < ops.size(); i++)
    {
        const Op &op = ops[i];

        switch (op.kind)
        {
            case OP_SET:    set(op.index, op.value); break;
            case OP_STRING: setString(op.index, op.text); break;
            case OP_BEATS:  setBeats(op.index, op.flag); break;
            case OP_KNOB:   bindKnob(op.index, op.arg); break;
            case OP_NODE:   bindNode(op.index, op.arg); break;
            case OP_NOTIFY: announce(op.index); break;
        }
    }
}

void
thcParamStore::replay (void)
{
    /* Nothing done here is recorded a second time: a rewind before the
       first start would otherwise double the history, and the next one
       would do it all twice. */
    const bool was = replaying_;

    replaying_ = true;

    /* The load, to the instance that now serves the store. */
    run(history_);

    /* Then what has been done to the store since, in the order it was
       done: a rewind reads the work file as it stands, not as it was
       loaded. See the two lists in the header. */
    run(edits_);

    replaying_ = was;

    for (size_t i = 0; i < nodes_.size(); i++)
        lastNode_[i] = nodes_[i] != NULL ? (*nodes_[i])[0] : 0.0f;
}

void
thcParamStore::pollNodes (void)
{
    if (nodesMoved() && rearm_)
        rearm_();
}

bool
thcParamStore::nodesMoved (void)
{
    bool moved = false;

    for (size_t i = 0; i < nodes_.size(); i++)
    {
        if (nodes_[i] == NULL)
            continue;

        const float now = (*nodes_[i])[0];

        if (now == lastNode_[i])
            continue;

        lastNode_[i] = now;
        moved = true;
    }

    return moved;
}

void
thcParamStore::notifyChanged (int index)
{
    record(OP_NOTIFY, index);
    announce(index);
}

void
thcParamStore::announce (int index)
{
    if (held_)
        return;

    if (instance_ != NULL)
        plugin_->paramChanged(instance_, index);

    if (rearm_)
        rearm_();
}

void
thcParamStore::hold (void)
{
    if (held_)
        return;

    heldValues_.resize(values_.size());
    heldStrings_.assign(strings_.begin(), strings_.end());
    heldTempo_ = tempo_ ? tempo_() : 0;

    for (size_t i = 0; i < values_.size(); i++)
        heldValues_[i] = get((int)i);

    held_ = true;
}

void
thcParamStore::release (void)
{
    if (!held_)
        return;

    held_ = false;

    for (size_t i = 0; i < values_.size(); i++)
        if (nodes_[i] == NULL &&
            (get((int)i) != heldValues_[i] ||
             (i < strings_.size() && strings_[i] != heldStrings_[i])))
            announce((int)i);
}

/* ---- thcScheduler ----------------------------------------------------- */

thcScheduler::thcScheduler (thSynth *synth)
    /* In declaration order, which is what -Wreorder is about:
       controlSynth_ is declared up beside the instrument table it
       belongs to, which puts it ahead of the transport members here
       even though nothing about it is more fundamental. swapped_ is a
       container and needs no mention. */
    : synth_(synth), soloCount_(0), endAfter_(false), loopFrom_(0),
      cueKnob_(NULL), cueGrid_(0), cueKnobLast_(0), staging_(false),
      ownsControl_(true), ownsAuditioner_(true), started_(false),
      controlSynth_(NULL), auditioner_(NULL), running_(false),
      transportNow_(0), beat_(0), tempoAt_(0), beatAtTempo_(0), tempo_(120),
      lastMono_(thcMonotonicUs()),
      masterSeed_(std::random_device()()), pendingSeq_(0), heapSeq_(0),
      injectingLive_(false), deliveringChain_(-1), passingThrough_(false)
{
}

thcScheduler::~thcScheduler (void)
{
    flushNoteOffs();
    clearChains();

    if (ownsAuditioner_)
        delete auditioner_;
    auditioner_ = NULL;

    /* One last go, since after this there is nobody left to try. If the
       ring is still full the graph outlives us, which is the honest
       end of a synth whose audio thread stopped draining. */
    retireStranded();

    /* After clearChains, which is what destroys the node hosts that
       borrow it. */
    if (ownsControl_)
        delete controlSynth_;
    controlSynth_ = NULL;
}

size_t
thcScheduler::addChain (const std::string &name)
{
    chains_.push_back(thcChain());
    chains_.back().name = name;
    chains_.back().muted = false;
    chains_.back().soloed = false;
    chains_.back().lastHeard[0] = chains_.back().lastHeard[1] = -1;
    chains_.back().lastGated[0] = chains_.back().lastGated[1] = -1;
    chains_.back().inputMidi = false;
    chains_.back().start = 0;
    chains_.back().startBeats = false;
    chains_.back().seeded = false;
    chains_.back().seed = 0;

    return chains_.size() - 1;
}

void
thcScheduler::addSink (size_t chain, int channel, const std::string &chanarg)
{
    if (chain >= chains_.size())
        return;

    thcSink s;

    s.channel = channel;
    s.chanarg = chanarg;

    chains_[chain].sinks.push_back(s);
}

void
thcScheduler::addKnobSink (size_t chain, thArg *knob)
{
    if (chain >= chains_.size() || knob == NULL)
        return;

    thcSink s;

    s.channel = -1;
    s.knob = knob;

    chains_[chain].sinks.push_back(s);

    /* What the text declares, which in a staged edit is the stand-in's
       value and not the live knob's. */
    knobHome_.emplace(knob, (*knobMeta(knob->name()))[0]);
}

void
thcScheduler::setKnobHome (thArg *knob, float value)
{
    std::map<thArg *, float>::iterator h = knobHome_.find(knob);

    if (h != knobHome_.end())
        h->second = value;
}

void
thcScheduler::setChainInput (size_t chain, bool midi)
{
    if (chain < chains_.size())
        chains_[chain].inputMidi = midi;
}

void
thcScheduler::setChainSeed (size_t chain, unsigned seed)
{
    if (chain < chains_.size() && chains_[chain].stages.empty())
    {
        chains_[chain].seeded = true;
        chains_[chain].seed = seed;
    }
}

void
thcScheduler::setChainStart (size_t chain, double at, bool beats)
{
    if (chain < chains_.size())
    {
        chains_[chain].start = at;
        chains_[chain].startBeats = beats;

        /* The stages this chain already has: `start' may be written
           below them, and a wake armed at the old time has to move. */
        bool moved = false;

        for (size_t i = 0; i < wakeups_.size(); i++)
            if (wakeups_[i].chain == chain &&
                chains_[chain].stages[wakeups_[i].stage]->awaitingStart)
            {
                wakeups_[i].at = chainStartTime(chains_[chain]);
                moved = true;
            }

        if (moved)
            std::make_heap(wakeups_.begin(), wakeups_.end(), Later());
    }
}

double
thcScheduler::chainStartTime (const thcChain &chain) const
{
    if (!chain.startBeats)
        return std::max(transportNow_, chain.start);

    return transportNow_ + std::max(0.0, chain.start - beat_) *
        60.0 / tempo_;
}

/* Deterministic mixing of the master seed with the stage's position, so
 * the same master seed and the same construction order create the same
 * instances -- which is the whole replay story. The constants are
 * Knuth's multiplicative hash and the golden-ratio increment; anything
 * that separates neighboring (chain, stage) pairs would do. */
unsigned
thcScheduler::stageSeed (size_t chain, size_t stage) const
{
    const thcChain &c = chains_[chain];
    unsigned h = c.seeded ? c.seed : masterSeed_;

    /* A seeded chain is told apart by its name rather than its place, so
       its take does not change when chains are added or moved above it,
       and two chains given one seed do not play in lockstep. */
    if (c.seeded)
        for (char ch : c.name)
            h = (h ^ (unsigned char)ch) * 16777619u;
    else
        h ^= (unsigned)(chain + 1) * 2654435761u;
    h ^= (unsigned)(stage + 1) * 0x9e3779b9u + (h << 6) + (h >> 2);

    return h;
}

thcStage *
thcScheduler::addStage (size_t chain, thcPlugin *plugin)
{
    return addStage(chain, plugin,
                    plugin != NULL && plugin->hasTick());
}

thcStage *
thcScheduler::addStage (size_t chain, thcPlugin *plugin, bool asGenerator)
{
    if (chain >= chains_.size() || plugin == NULL)
        return NULL;

    thcChain &c = chains_[chain];
    size_t stage = c.stages.size();

    /* The role the placement declares, gated by what the module can
       actually do: an xform:: placement of a dual plugin must not tick,
       and asking for a generator out of a module with no tick is the
       loader's error to have caught. */
    bool wantTick = asGenerator && plugin->hasTick();

    c.stages.push_back(std::unique_ptr<thcStage>(
        new thcStage(plugin, stageSeed(chain, stage), wantTick)));

    thcStage *s = c.stages.back().get();

    /* The ear, offered before the instance exists so that a module may
       read it whenever it likes: an auditioner here, or the host's to ask
       (setEarRequest). Absent where there is neither -- a scheduler with
       no synth has nothing to render with. */
    ensureAuditioner();

    if (hasEar())
    {
        s->sched = this;
        s->chain = chain;
        s->ear.ctx = s;
        s->ear.hear = cbHear;
        s->ear.heard = cbHeard;
        s->ear.forget = cbForget;
        s->params.params_.audition = &s->ear;
    }

    s->state = plugin->create(s->params.params());

    /* A module may refuse an instance. Half a stage is worse than none
       -- ticks would hand a NULL state straight into the plugin -- so
       take it back out and say so. */
    if (s->state == NULL)
    {
        fprintf(stderr, "thcScheduler: %s refused to create an instance\n",
                plugin->name().c_str());
        c.stages.pop_back();
        return NULL;
    }

    /* Wire the store back to the instance it now serves. The rearm
       lambda captures indices, not pointers -- stages are never removed
       individually, so indices stay true; pointers into a vector that
       grows would not. Tempo comes through a closure so the store can
       convert beat-valued durations at read time without knowing what a
       scheduler is. */
    s->params.instance_ = s->state;
    s->params.rearm_ = [this, chain, stage] { rearmStage(chain, stage); };
    s->params.tempo_ = [this] { return tempo_; };

    if (s->ticks)
    {
        wakeups_.push_back({ chainStartTime(c), chain, stage, heapSeq_++ });
        std::push_heap(wakeups_.begin(), wakeups_.end(), Later());
    }

    return s;
}

void
thcScheduler::clearChains (void)
{
    /* Anything sounding came from these instances; silence it before
       taking them away. */
    flushNoteOffs();
    flushHeld();

    for (size_t ci = 0; ci < chains_.size(); ci++)
        for (size_t si = 0; si < chains_[ci].stages.size(); si++)
        {
            thcStage *s = chains_[ci].stages[si].get();

            s->plugin->destroy(s->state);
            s->state = NULL;
        }

    replays_.clear();
    chains_.clear();
    soloCount_ = 0;
    meter_ = 4;
    wakeups_.clear();
    pending_.clear();
    recompose_.clear();

    /* The arrangement names chains by the names this just took away. */
    sections_.clear();
    endAfter_ = false;
    loopFrom_ = 0;
    cues_.clear();
    cueKnob_ = NULL;

    /* Knob-to-param connections point into the stages just destroyed;
       the knobs themselves belong to the piece and go with it. */
    for (size_t i = 0; i < knobConns_.size(); i++)
        knobConns_[i].conn.disconnect();
    knobConns_.clear();
    holding_.clear();
    knobHome_.clear();

    /* A staged scheduler's knobs are partly the live one's (lent_), and
       those are not this one's to delete. */
    for (std::map<std::string, thArg *>::iterator i = knobs_.begin();
         i != knobs_.end(); ++i)
    {
        std::map<std::string, thArg *>::iterator l = lent_.find(i->first);

        if (l == lent_.end() || l->second != i->second)
            delete i->second;
    }
    knobs_.clear();

    for (size_t i = 0; i < retired_.size(); i++)
        delete retired_[i];
    retired_.clear();

    for (const auto &m : pendingMeta_)
        delete m.second;
    pendingMeta_.clear();
    started_ = false;

    /* Structure edits belong to the piece that made them. */
    nodeArgs_.clear();
    swapped_.clear();
    forgetGrowth(-1);

    /* The instrument table goes with the piece too. What is *loaded* on
       those channels does not: a patch outlives the file that asked for
       it, exactly as one loaded by hand outlives the window that loaded
       it, and deciding when a channel should be given back is the
       host's business rather than this table's. */
    instruments_.clear();

    /* A device is not a patch a person loaded: its route goes with the
       piece that named it. */
    for (int ch = 0; ch < 16; ch++)
        if (overMidi_[ch])
        {
            midiOut_->detach(ch);
            overMidi_[ch] = false;
        }

    for (int ch = 0; ch < 16; ch++)
        midiWhy_[ch].clear();

    /* The master effect goes with the piece for the same reason the table
       does. What is *loaded* on the mix is taken off by the next load
       rather than here, because this runs while a piece is being replaced
       and a gap of silence between two pieces' reverbs is not an
       improvement on one of them ending. */
    master_ = thcInstrument();
    master_.channel = -1;
}

thArg *
thcScheduler::addKnob (const std::string &name, float value)
{
    std::map<std::string, thArg *>::iterator i = knobs_.find(name);

    if (i != knobs_.end())
        return i->second;

    /* An edit's: the live knob of that name, which is what the stages the
       edit keeps are already bound to. Its value is the one it has unless
       the declaration changed, and then the new text's. */
    std::map<std::string, thArg *>::iterator l = lent_.find(name);

    if (l != lent_.end())
    {
        if (keepValues_.find(name) == keepValues_.end())
            pendingValues_[name] = value;

        thArg *meta = new thArg(name, value);

        meta->setWidgetType(thArg::CHANARG);
        pendingMeta_[name] = meta;

        knobs_[name] = l->second;
        return l->second;
    }

    thArg *arg = new thArg(name, value);

    arg->setWidgetType(thArg::CHANARG);
    knobs_[name] = arg;

    return arg;
}

thArg *
thcScheduler::knob (const std::string &name)
{
    std::map<std::string, thArg *>::iterator i = knobs_.find(name);

    return i == knobs_.end() ? NULL : i->second;
}

thArg *
thcScheduler::knobMeta (const std::string &name)
{
    std::map<std::string, thArg *>::iterator m = pendingMeta_.find(name);

    return m == pendingMeta_.end() ? knob(name) : m->second;
}

/* The live half of `prob = @density'. The store shadows its value with
 * the knob; the signal connection is what wakes a THC_NEVER sleeper and
 * forwards param_changed when the knob moves -- a composer that reads
 * its params inside tick() never needed either, but one that caches
 * (a NOTESET, a derived table) gets the same notification an edit of
 * the param itself would produce. */
void
thcScheduler::unbindParam (thcStage *stage, int paramIndex)
{
    if (stage == NULL || paramIndex < 0 ||
        paramIndex >= stage->plugin->paramCount())
        return;

    stage->params.bindKnob(paramIndex, NULL);
    stage->params.bindNode(paramIndex, NULL);

    /* Said, like any other change to what the param reads. */
    stage->params.notifyChanged(paramIndex);
}

void
thcScheduler::bindKnob (thcStage *stage, int paramIndex, thArg *knob)
{
    if (stage == NULL)
        return;

    /* An index the plugin never registered must not get a binding OR a
       signal connection: notifyChanged would forward it into
       composer_param_changed, and what a module does with an index it
       never issued is nobody's guess to make. */
    if (paramIndex < 0 || paramIndex >= stage->plugin->paramCount())
        return;

    /* NULL unbinds: the param falls back to its stored value. The old
       signal connection stays parked harmlessly -- notifyChanged on an
       unbound index is a param_changed a live edit would have sent
       anyway -- and dies with the next clearChains. */
    if (knob == NULL)
    {
        stage->params.bindKnob(paramIndex, NULL);
        return;
    }

    if (chainWrites(stage->chain, knob))
        return;

    stage->params.bindKnob(paramIndex, knob);

    /* And say so once, now, the way setting the param would.
     *
       The connection below carries every *later* move of the knob. It
       cannot carry the binding itself, and a module that caches -- one
       that reparses a note set or sizes a board in param_changed --
       reads its params in composer_create and on param_changed and
       nowhere else. A stage is created before it is bound, so without
       this the module's idea of `width = @size' stayed at the
       registered default until somebody first touched the slider. */
    stage->params.notifyChanged(paramIndex);

    thcParamStore *store = &stage->params;

    KnobConn kc;

    kc.channel = -1;            /* drives a param, reaches no channel */
    kc.store = store;
    kc.conn = knob->signal_arg_changed().connect(
        [store, paramIndex](thArg *) { store->notifyChanged(paramIndex); });

    knobConns_.push_back(kc);
}

bool
thcScheduler::chainWrites (size_t chain, const thArg *knob) const
{
    if (chain >= chains_.size())
        return false;

    for (const thcSink &s : chains_[chain].sinks)
        if (s.knob == knob)
            return true;

    return false;
}

/* Every binding that pushes into this channel, gone.
 *
 * The param bindings (channel -1) are left alone: they are a property of
 * the piece's chains, not of whatever graph happens to be on a channel,
 * and they die with clearChains like they always did. */
void
thcScheduler::dropKnobConns (int channel)
{
    size_t w = 0;

    for (size_t i = 0; i < knobConns_.size(); i++)
    {
        /* A stage param's connection is channel -1 as well, and it is
           the master effect's channel too: which of the two a
           connection is, is the store, not the number. */
        if (knobConns_[i].channel == channel && knobConns_[i].store == NULL)
        {
            knobConns_[i].conn.disconnect();
            continue;
        }

        knobConns_[w++] = knobConns_[i];
    }

    knobConns_.resize(w);
}

thcNodeHost *
thcScheduler::newNodeHost (void)
{
    /* One control-rate synth for the whole piece, made on the first
       chain that wants nodes and kept until this scheduler dies.
     *
       Its plugin root is where the *application* is actually loading
       plugins from, resolved once by thPluginManager and asked for
       rather than searched for again: a second search can find a second
       answer -- an installed /usr/local beside a build tree is the
       ordinary case -- and two hosts running different builds of one
       module is precisely the drift the hazards section is about.

       Shared across chains rather than one each, because every plugin
       keeps its arg indices in a file-scope global and a second
       thPluginManager would dlopen the same .so and call module_init
       again against a second thPlugin. */
    if (controlSynth_ == NULL && synth_ != NULL)
        controlSynth_ = new thSynth(
            synth_->getPluginManager()->pluginPath(), 1,
            (int)controlRate());

    return new thcNodeHost(controlSynth_, controlRate());
}

/* ---- the ear ----------------------------------------------------------- */

void
thcScheduler::ensureAuditioner (void)
{
    if (auditioner_ == NULL && localEar_ && synth_ != NULL &&
        synth_->getPluginManager() != NULL)
    {
        auditioner_ = new thcAuditioner(synth_->getPluginManager()->pluginPath(),
                                        (double)synth_->getSampleRate());
        auditioner_->setTuner(tuner_);

#ifdef __EMSCRIPTEN__
        /* No threads in the browser: the worker this runs in is the
           thread renders take. */
        auditioner_->setSynchronous(true);
#endif
    }
}

void
thcScheduler::setTuner (const thcAuditioner::Tuner &fn)
{
    tuner_ = fn;

    if (auditioner_ != NULL)
        auditioner_->setTuner(fn);
}

void
thcScheduler::setAuditionSynchronous (bool on)
{
    auditionSync_ = on;
    ensureAuditioner();

#ifndef __EMSCRIPTEN__
    /* A synchronous render uses the synth the worker renders on, so the
       worker finishes first. */
    if (auditioner_ != NULL && on)
        auditioner_->drain();

    if (auditioner_ != NULL)
        auditioner_->setSynchronous(on);
#endif
}

/* ---- the ear, here or the host's ---------------------------------------- */

/* A graph's text, from the registry an inline or grown graph is in or the
   file a path names; empty for none. */
static std::string
graphTextOf (const std::string &name)
{
    std::string text;

    if (name.empty() || thUtil::findGraphText(name, text))
        return text;

    std::ifstream in(name.c_str(), std::ios::binary);

    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

/* Values as the ear's host carries them: a name and a number a line. */
static std::string
argLines (const std::vector<std::pair<std::string, float> > &args)
{
    std::string out;
    char value[32];

    for (size_t i = 0; i < args.size(); i++)
    {
        snprintf(value, sizeof value, "%.9g", (double)args[i].second);
        out += args[i].first + "\t" + value + "\n";
    }

    return out;
}

/* The name a graph goes by to the host's ear: its text's, so that one
   sent once is named after that and not sent again. A registry name's text
   never changes, so it is remembered; a file's may, and is read each time. */
std::string
thcScheduler::earGraph (const std::string &name, std::string &texts)
{
    if (name.empty())
        return name;

    const auto known = earIds_.find(name);

    if (known != earIds_.end())
        return known->second;

    std::string text;
    const bool registered = thUtil::findGraphText(name, text);

    if (!registered)
        text = graphTextOf(name);

    const std::string id = thcGenLoader::inlineGraphName("ear", text);

    if (registered)
        earIds_[name] = id;

    if (earSent_.insert(id).second)
    {
        texts += texts.empty() ? "" : ",";
        jsonString(texts, id);
        texts += ":";
        jsonString(texts, text);
    }

    return id;
}

int
thcScheduler::earHear (const thcAuditioner::Instrument &candidate,
                       const std::string &targetKey,
                       const thcAuditioner::Instrument *targetInstrument,
                       const std::string &targetFile, int tune,
                       const std::string &from)
{
    if (auditioner_ != NULL)
        return auditioner_->hear(candidate, targetKey, targetInstrument,
                                 targetFile, tune, from);

    if (!earRequest_)
        return -1;

    /* Graphs by the name their text gives them, each text carried the
       first time only: an inline or grown graph is in this realm's
       registry and nowhere else, and a population asks about the same one
       a generation through. */
    const int ticket = ++earTicket_;
    std::string texts;
    std::string ask = "{\"ticket\":" + std::to_string(ticket) +
                      ",\"tune\":" + std::to_string(tune);
    const auto field = [&ask] (const char *name, const std::string &value)
    {
        ask += std::string(",\"") + name + "\":";
        jsonString(ask, value);
    };

    field("dsp", earGraph(candidate.dsp, texts));
    field("effect", earGraph(candidate.effect, texts));
    field("args", argLines(candidate.chanargs));
    field("key", targetKey);

    if (!from.empty())
    {
        thUtil::GraphHold hold;
        const std::string name = thcGenLoader::inlineGraphName("from", from);

        hold = thUtil::addGraphText(name, from);
        field("from", earGraph(name, texts));
    }
    else
        field("from", "");

    if (targetInstrument != NULL)
    {
        field("tdsp", earGraph(targetInstrument->dsp, texts));
        field("teffect", earGraph(targetInstrument->effect, texts));
        field("targs", argLines(targetInstrument->chanargs));
    }
    else
        field("file", targetFile);

    ask += ",\"texts\":{" + texts + "}}";
    earAnswers_[ticket] = EarAnswer();
    earRequest_(ticket, ask);

    return ticket;
}

int
thcScheduler::earHeard (int ticket, double *distance, std::string *tuned)
{
    if (auditioner_ != NULL)
        return auditioner_->heard(ticket, distance, tuned);

    const auto a = earAnswers_.find(ticket);

    if (a == earAnswers_.end())
        return -1;

    if (!a->second.in)
        return 0;

    const EarAnswer answer = a->second;

    earAnswers_.erase(a);

    if (tuned)
        *tuned = answer.tuned;

    if (!answer.ok)
        return -1;

    if (distance)
        *distance = answer.distance;

    return 1;
}

void
thcScheduler::earForget (int ticket)
{
    if (auditioner_ != NULL)
        auditioner_->forget(ticket);
    else
        earAnswers_.erase(ticket);
}

void
thcScheduler::earAnswer (int ticket, bool ok, double distance,
                         const std::string &tuned)
{
    const auto a = earAnswers_.find(ticket);

    /* One forgotten while it was out is dropped here. */
    if (a == earAnswers_.end())
        return;

    a->second.in = true;
    a->second.ok = ok;
    a->second.distance = distance;
    a->second.tuned = tuned;
}

int
thcScheduler::cbHear (void *ctx, const char *target,
                      const char *const *names, const double *values, int n)
{
    thcStage *s = static_cast<thcStage *>(ctx);

    return s && s->sched ? s->sched->hear(s, target, names, values, n) : -1;
}

int
thcScheduler::cbHeard (void *ctx, int ticket, double *distance)
{
    thcStage *s = static_cast<thcStage *>(ctx);

    if (s == NULL || s->sched == NULL)
        return -1;

    return s->sched->earHeard(ticket, distance, NULL);
}

void
thcScheduler::cbForget (void *ctx, int ticket)
{
    thcStage *s = static_cast<thcStage *>(ctx);

    if (s != NULL && s->sched != NULL)
        s->sched->earForget(ticket);
}

bool
thcScheduler::auditionInstrument (const thcInstrument &inst,
                                  thcAuditioner::Instrument &out)
{
    if (inst.dsp.empty() || synth_ == NULL)
        return false;

    const std::string path =
        thUtil::findDataFile(inst.dsp, "dsp", "THINK_DSP_PATH", DSP_PATH);

    out.dsp = path.empty() ? inst.dsp : path;
    out.effect.clear();
    out.chanargs.clear();
    out.graphs = inst.graphs;

    /* Its side, if it names one, is another channel's sound, and the
       private synth has only this one: the effect is heard on its own. */
    if (!inst.effect.empty())
    {
        const std::string fx =
            thUtil::findDataFile(inst.effect, "dsp", "THINK_DSP_PATH", DSP_PATH);

        out.effect = fx.empty() ? inst.effect : fx;
    }

    for (size_t i = 0; i < inst.args.size(); i++)
    {
        const thcInstrumentArg &a = inst.args[i];
        double v = a.value;

        if (!a.knob.empty())
        {
            thArg *k = knob(a.knob);

            if (k == NULL)
                continue;

            v = (double)(*k)[0];
        }

        out.chanargs.push_back(std::make_pair(
            a.name, (float)thFoldUnit(v, a.units, synth_->getSampleRate(),
                                  synth_->tempo())));
    }

    return true;
}

/* The stage's chain sinks to an instrument; that, with the composer's
   values written over its own, is the candidate. The target is an
   instrument of the piece if the name is one, and a sound file
   otherwise -- as written, or found where samples are. */
int
thcScheduler::hear (thcStage *stage, const char *target,
                    const char *const *names, const double *values, int n)
{
    if (!hasEar() || target == NULL || *target == '\0' ||
        stage->chain >= chains_.size())
        return -1;

    const thcChain &c = chains_[stage->chain];
    const thcInstrument *mine = NULL;
    int channel = -1;

    for (size_t i = 0; i < c.sinks.size() && mine == NULL; i++)
        if (c.sinks[i].isChanarg())
        {
            channel = c.sinks[i].channel;
            mine = channelOf(channel);
        }

    thcAuditioner::Instrument candidate;

    if (mine == NULL || !auditionInstrument(*mine, candidate))
        return -1;

    stage->unwritten = true;

    if (catchingUp_)
        return -1;

    /* Where each value lands live: every chanarg sink on that channel
       takes it, under the sink's name unless the sink is `*' -- the
       rename propagate() does, in the order it does it. */
    for (int i = 0; i < n; i++)
        for (size_t j = 0; j < c.sinks.size(); j++)
        {
            const thcSink &sink = c.sinks[j];

            if (!sink.isChanarg() || sink.channel != channel)
                continue;

            const std::string name =
                sink.namesItsOwn() ? std::string(names[i]) : sink.chanarg;
            bool found = false;

            for (size_t k = 0; k < candidate.chanargs.size(); k++)
                if (candidate.chanargs[k].first == name)
                {
                    candidate.chanargs[k].second = (float)values[i];
                    found = true;
                }

            if (!found)
                candidate.chanargs.push_back(
                    std::make_pair(name, (float)values[i]));
        }

    return hearAgainst(candidate, target);
}

int
thcScheduler::hearAgainst (const thcAuditioner::Instrument &candidate,
                           const char *target, int tune,
                           const std::string &from)
{
    const thcInstrument *other = instrument(target);
    thcAuditioner::Instrument targetInstrument;

    if (other != NULL)
    {
        if (!auditionInstrument(*other, targetInstrument))
            return -1;

        return earHear(candidate, "instrument:" + other->name,
                       &targetInstrument, "", tune, from);
    }

    std::string file = target;

    if (!std::filesystem::exists(file))
    {
        const std::string found =
            thUtil::findDataFile(std::string("samples/") + target, "dsp",
                                 "THINK_DSP_PATH", DSP_PATH);

        if (found.empty())
            return -1;

        file = found;
    }

    return earHear(candidate, "file:" + file, NULL, file, tune, from);
}

std::vector<int>
thcScheduler::hearAll (const thcInstrument &inst,
                       const std::vector<std::string> &options,
                       const std::string &target, size_t tuned, int tune,
                       const std::string &from)
{
    std::vector<int> tickets(options.size(), -1);

    ensureAuditioner();

    for (size_t i = 0; hasEar() && i < options.size(); i++)
    {
        thcInstrument as = inst;
        thcAuditioner::Instrument candidate;

        as.dsp = thcGenLoader::inlineGraphName(inst.name, options[i]);
        as.graphs.push_back(thUtil::addGraphText(as.dsp, options[i]));

        if (auditionInstrument(as, candidate))
            tickets[i] = hearAgainst(candidate, target.c_str(),
                                     i < tuned ? tune : 0, from);
    }

    return tickets;
}

bool
thcScheduler::collect (Growth &g)
{
    bool all = true;

    /* A ticket is -1 once collected, or for an edit that could not be
       heard at all; either way its distance is already what it will be. */
    for (size_t i = 0; i < g.tickets.size(); i++)
    {
        double d;
        const int r = g.tickets[i] < 0 ? -2 :
                      earHeard(g.tickets[i], &d, &g.tuned[i]);

        if (r == 0)
            all = false;
        else if (r != -2)
        {
            g.distances[i] = r == 1 ? d : HUGE_VAL;
            g.tickets[i] = -1;
        }
    }

    return all;
}

void
thcScheduler::forgetHearing (Growth &g)
{
    for (size_t i = 0; i < g.tickets.size(); i++)
        if (g.tickets[i] >= 0)
            earForget(g.tickets[i]);

    doneHearing(g);
    g.queued.clear();
}

void
thcScheduler::doneHearing (Growth &g)
{
    g.options.clear();
    g.optionWhats.clear();
    g.tuned.clear();
    g.asked = 0;
    g.tickets.clear();
    g.distances.clear();
}

void
thcScheduler::runQueued (int channel)
{
    for (;;)
    {
        const auto g = grown_.find(channel);

        if (g == grown_.end() || g->second.queued.empty() ||
            g->second.asked != 0 || !g->second.options.empty())
            return;

        /* Set aside around the step: a step forgets what was queued for
           the one it replaces, and these are queued after it. */
        std::deque<Growth::Queued> rest;
        std::string why;

        rest.swap(g->second.queued);

        const Growth::Queued q = rest.front();

        rest.pop_front();

        if (q.back >= 0)
        {
            if (!growBackTo(channel, (size_t)q.back, why))
                fprintf(stderr, "thcScheduler: growth on channel %d: %s\n",
                        channel + 1, why.c_str());
        }
        else if (q.put)
            putGraph(channel, q.graph, q.over, q.strict, q.done);
        else if (q.nodeArg)
        {
            if (!setNodeArg(channel, q.node, q.arg, q.value, why))
                fprintf(stderr, "thcScheduler: %s.%s on channel %d: %s\n",
                        q.node.c_str(), q.arg.c_str(), channel + 1,
                        why.c_str());
        }
        else if (!stepGraph(channel, q.shrink, NULL, q.choice, q.kinds,
                            q.target, q.listen, q.tune, q.depth, why))
            fprintf(stderr, "thcScheduler: growing channel %d: %s\n",
                    channel + 1, why.c_str());

        const auto after = grown_.find(channel);

        if (after != grown_.end())
            after->second.queued.swap(rest);
    }
}

void
thcScheduler::forgetGrowth (int channel)
{
    for (auto &e : grown_)
        if (channel < 0 || e.first == channel)
            forgetHearing(e.second);

    if (channel < 0)
        grown_.clear();
    else
        grown_.erase(channel);

    growthSerial_++;
}

/* A growth being heard live goes on in the step its last answer comes in:
   the nearest of its edits, onto the graph it was grown from, which is
   still the top of the stack -- anything that moved the stack forgot it. */
void
thcScheduler::takeHeardGrowth (void)
{
    std::vector<int> ready;

    for (auto &e : grown_)
        if (!e.second.options.empty() && collect(e.second))
            ready.push_back(e.first);

    for (size_t i = 0; i < ready.size(); i++)
    {
        std::string why;

        if (!growthOf(ready[i], why) ||
            !takeHeard(ready[i], grown_[ready[i]], why))
            fprintf(stderr, "thcScheduler: growing channel %d: %s\n",
                    ready[i] + 1, why.c_str());

        runQueued(ready[i]);
    }
}

/* ---- instruments ------------------------------------------------------- */

/* What an arg is *folded* in, as opposed to what its author labelled it.
 * `@x.units = "Hz"' is a word for the panel to print and nothing
 * converts through it, so a bare number is the right and only way to
 * write one. */
static std::string
foldUnitOf (const thArg *arg)
{
    if (arg == NULL || !thUnitIsFolded(arg->units()))
        return std::string();

    return arg->units();
}

size_t
thcScheduler::addInstrument (const thcInstrument &inst)
{
    instruments_.push_back(inst);

    return instruments_.size() - 1;
}

const thcInstrument *
thcScheduler::instrument (const std::string &name) const
{
    for (size_t i = 0; i < instruments_.size(); i++)
        if (instruments_[i].name == name)
            return &instruments_[i];

    return NULL;
}

thcInstrument *
thcScheduler::instrument (size_t index)
{
    return index < instruments_.size() ? &instruments_[index] : NULL;
}

/* A chanarg by name, wherever it lives. */
thArg *
thcScheduler::findChanArg (int channel, const std::string &name)
{
    if (synth_ == NULL)
        return NULL;

    return channel < 0 ? synth_->getMasterArg(name)
                       : synth_->getChanArg(channel, name);
}

/* The values half of applyInstrument, on a channel whose graph is
 * already up. Split out so that every way of refusing one has a single
 * caller, and that caller can take the graph back down again. */
bool
thcScheduler::applyValues (const thcInstrument &inst, std::string &why)
{
    /* Whatever was bound into this channel last time, first.
     *
       An instrument is applied more than once now -- a swap applies one,
       a rewind applies them all again -- and a knob binding that was
       only ever added meant the instrument a swap replaced went on
       pushing into the channel it used to hold, alongside its
       replacement, for the rest of the session. Dropping first makes
       applying an instrument say the same thing however many times it
       is done, which is what both callers assume. */
    dropKnobConns(inst.channel);

    /* Where this instrument's own wiring starts, so a value refused
       halfway through can take back the bindings the values before it
       already made: the graph is about to come off the channel
       underneath them, and connections into a channel that is no longer
       there push into whatever gets loaded onto it next. Wrapping the
       body rather than unwinding at each of its half-dozen refusals,
       because the one that gets forgotten is the one that bites. */
    const size_t wired = knobConns_.size();

    if (writeValues(inst, why))
        return true;

    while (knobConns_.size() > wired)
    {
        knobConns_.back().conn.disconnect();
        knobConns_.pop_back();
    }

    return false;
}

bool
thcScheduler::writeValues (const thcInstrument &inst, std::string &why)
{
    for (size_t i = 0; i < inst.args.size(); i++)
    {
        const thcInstrumentArg &a = inst.args[i];
        thArg *arg = findChanArg(inst.channel, a.name);

        /* A .patch invents the arg instead, which it has to: patches
           predate arg metadata and half the corpus sets things no graph
           declares. A piece file has no such history, and an arg name the
           graph does not know is a typo every time -- so it is said rather
           than swallowed. The declared surface is the whole of what a
           piece may reach. */
        if (arg == NULL)
        {
            /* Which of the two graphs the name was aimed at: `fx.delay' is
               the effect's, and saying the instrument declares no `fx.delay'
               would send the reader to the wrong file. */
            const bool prefixed =
                a.name.compare(0, strlen(TH_EFFECT_PREFIX),
                               TH_EFFECT_PREFIX) == 0;

            /* And on the mix there is only ever one graph, so the names
               carry no prefix and the file to send the reader to is
               never in doubt. */
            const bool isEffect = prefixed || inst.channel < 0;

            why = "'" + (isEffect ? inst.effect : inst.dsp) +
                  "' declares no chanarg called '" +
                  (prefixed ? a.name.substr(strlen(TH_EFFECT_PREFIX))
                            : a.name) + "'";
            return false;
        }

        /* The channel's level, in dB of gain as well as in the engine's
           own terms: 0 dB is unity, which is MIDIVALMAX. */
        if (inst.channel >= 0 && a.name == "amp" && a.units == "dB" &&
            a.knob.empty())
        {
            arg->setValue((float)(MIDIVALMAX * thFoldUnit(a.value, a.units,
                                                          0)));
            continue;
        }

        const std::string declared = foldUnitOf(arg);

        /* A unit the arg is not folded in cannot be folded into it, and
           its absence is no better: `res = 50 ms' on a resonance that
           runs 0 to 1 would become two thousand-odd samples of nothing,
           and `a = 39690' on an envelope is a sample count nobody meant
           to write. Same rule as a duration param in a stage, for the
           same reason -- the unit decides what the number is, so its
           absence decides nothing. */
        if (thUnitDimension(a.units) != thUnitDimension(declared))
        {
            if (a.units.empty())
                /* Not "raw samples": that is what a bare number means on
                   a `ms' arg and not on a `%' one, where it is a raw
                   fraction of TH_MAX. The engine's own terms covers
                   both, and is what the two folds have in common. */
                why = "'" + a.name + "' is written in " + declared +
                      "; write the unit, or the number is in the "
                      "engine's own terms";
            else if (declared.empty())
                why = "'" + a.name + "' has no unit; '" + a.units +
                      "' means nothing to it";
            else
                why = "'" + a.name + "' is written in " + declared +
                      ", not " + a.units;

            return false;
        }

        /* What the piece wrote in `beats' follows the tempo, and what it
           wrote in ms or s does not, whatever the .dsp declared. On the mix
           the names carry no prefix. */
        {
            const bool fx = inst.channel >= 0 &&
                a.name.compare(0, strlen(TH_EFFECT_PREFIX),
                               TH_EFFECT_PREFIX) == 0;
            thChanEffect *e = inst.channel < 0 ? synth_->getMasterEffect()
                            : fx ? synth_->getEffect(inst.channel) : NULL;
            thMidiChan *c = (inst.channel >= 0 && !fx)
                ? synth_->getChannel(inst.channel) : NULL;
            thSynthTree *tree = e ? e->tree() : c ? c->modnode() : NULL;

            if (tree)
                tree->followTempo(fx ? a.name.substr(strlen(TH_EFFECT_PREFIX))
                                     : a.name, a.units == "beats");
        }

        if (a.knob.empty())
        {
            arg->setValue((float)thFoldUnit(a.value, a.units,
                                            synth_->getSampleRate(),
                                            synth_->tempo()));
            continue;
        }

        thArg *k = knob(a.knob);

        if (k == NULL)
        {
            /* The loader checks this when it reads the line, so getting
               here means the knob was declared and then went away --
               which nothing does. Said rather than dereferenced. */
            why = "'@" + a.knob + "' is not a declared knob";
            return false;
        }

        /* Look the chanarg up by name on every move rather than
           capturing the thArg the line above already has. A channel can
           be replaced from under this binding -- the Patch Selector will
           do it on request -- and the thArgs go with the thMidiChan that
           owned them, so a captured pointer is a use-after-free waiting
           for somebody to move a slider. A map lookup per knob move is
           nothing; the knob is a human hand. */
        const int         channel = inst.channel;
        const std::string name    = a.name;
        const std::string units   = a.units;

        std::function<void (thArg *)> push =
            [this, channel, name, units](thArg *from)
            {
                thArg *dest = findChanArg(channel, name);

                if (dest == NULL)
                    return;

                /* And it has to still be the arg this binding was
                   checked against. Re-looking the name up stops the
                   push from writing through a freed pointer; it does
                   not stop it from writing into a *different* arg that
                   happens to share the name, because a channel replaced
                   from under the piece -- which the Patch Selector will
                   do on request -- brings a whole new set of them.
                   `r' folded from ms on one graph and `r' running 0 to
                   1 on the next is a knob nudge writing 88200 into an
                   arg whose top is 1.
                 *
                   So the fold is re-checked, and a binding whose target
                   changed shape stops driving rather than driving
                   wrongly. Silent, because the alternative is a line of
                   stderr per pixel of a slider drag, and because the
                   piece is about to be reloaded by whoever did this. */
                if (thUnitDimension(foldUnitOf(dest)) !=
                    thUnitDimension(units))
                    return;

                dest->setValue((float)thFoldUnit((*from)[0], units,
                                                 synth_->getSampleRate(),
                                                 synth_->tempo()));
            };

        /* Where the knob is now, before anybody touches it: a piece must
           sound like its file the moment it loads, not one knob-move
           later. */
        push(k);

        /* knobConns_, so these die exactly when the knob-to-param
           connections do -- in clearChains, before the knobs
           themselves are deleted. */
        KnobConn kc;

        kc.channel = inst.channel;
        kc.store = NULL;
        kc.conn = k->signal_arg_changed().connect(push);

        knobConns_.push_back(kc);
    }

    /* The text args: onto the prototype the channel builds its voices
       from, as setNodeArg writes a number there. Only an arg the node's
       plugin declared and that is a text already -- a name written over a
       number would turn a wired or numeric arg into a string nothing
       reads -- and only on a graph, since a MIDI instrument has none. */
    if (!inst.texts.empty() && inst.channel >= 0 && !inst.dsp.empty())
    {
        thMidiChan *chan = synth_ ? synth_->getChannel(inst.channel) : NULL;
        thSynthTree *tree = chan != NULL ? chan->modnode() : NULL;

        for (size_t i = 0; i < inst.texts.size(); i++)
        {
            const thcInstrumentText &t = inst.texts[i];
            thNode *n = tree ? tree->findNode(t.node) : NULL;
            thArg *arg = n ? n->getArg(t.arg) : NULL;

            if (n == NULL)
            {
                why = "'" + inst.dsp + "' has no node called '" + t.node +
                      "'";
                return false;
            }

            /* An arg the graph leaves unset and one it sets to a number
               look the same from here -- nothing says which of a plugin's
               args take a name -- so one answer covers both. */
            if (arg == NULL || arg->type() != thArg::ARG_TEXT)
            {
                why = "'" + inst.dsp + "' gives '" + t.node + "." + t.arg +
                      "' no quoted name, and a piece can only replace one "
                      "the graph gives";
                return false;
            }

            arg->setText(t.value);
        }
    }

    return true;
}

/* The graph, then the values on top of it -- which is what a .patch is,
 * said in a language people write by hand.
 *
 * The split between the two halves is deliberate. Loading the graph is
 * the host's, because in the application it is also a patch tab and an
 * arg panel; setting the values is *not*, because what a value means --
 * which arg it lands on, what its unit folds to, what happens when the
 * patch has no such arg -- is a property of the .gen language and
 * belongs where the rest of the language's semantics are. One copy,
 * gated headlessly, whichever host is on the other end of the hook.
 *
 * All or nothing, though. A value can only be checked once its graph is
 * on the channel -- which arg it lands on is a question about that graph
 * -- so refusing an instrument for a chanarg its .dsp does not declare
 * happens with the .dsp already loaded. Rolling that back *here* is what
 * lets the caller's bookkeeping stay simple: this either applied or it
 * did not, and there is no third state for anybody else to track. The
 * caller that tried to track it got it wrong in both directions --
 * leaving the failed graph up, and later taking down a patch a failed
 * load had deliberately preserved.
 */
bool
thcScheduler::applyInstrument (size_t index, std::string &why)
{
    if (index >= instruments_.size())
    {
        why = "no such instrument";
        return false;
    }

    const thcInstrument &inst = instruments_[index];

    if (inst.channel < 0)
    {
        why = "no channel was allocated for it";
        return false;
    }

    /* Staged: recorded, and loaded by adopt() if it changed. */
    if (staging_)
        return true;

    /* Played on a device where a port answers; its dsp, if it has one,
       where none does. The reason is kept either way, for a host to show
       which of the two is happening. */
    midiWhy_[inst.channel].clear();

    /* What the channel holds now, which a swap may have made another
       instrument: its graph is the one to take off below. And from here
       it holds this one again, so the next swap away from it is a swap
       and not "already there". */
    const thcInstrument *held = instrument(holding(inst.channel));
    const auto takeHeldOff = [&]()
    {
        if (synth_ == NULL || synth_->getChannel(inst.channel) == NULL)
            return;

        thcInstrument old = held != NULL ? *held : inst;

        old.channel = inst.channel;
        takeGraphOff(old);
    };

    holding_.erase(inst.channel);

    /* Applied again over itself -- a rewind re-applies what was swapped
       -- starts from nothing on the device, so a refusal below leaves
       the channel a graph's and not both. */
    if (overMidi_[inst.channel])
    {
        midiOut_->detach(inst.channel);
        overMidi_[inst.channel] = false;
    }

    if (!inst.midi.empty())
    {
        std::string mwhy = "no MIDI output in this host";

        if (midiOut_ != NULL && midiOut_->attach(inst.channel, inst, mwhy))
        {
            /* And the graph an earlier apply or a swap put on, if it is
               still there, comes off: the device plays it now. Whichever
               instrument's it is -- after a swap, not this one's. */
            takeHeldOff();
            overMidi_[inst.channel] = true;
            return true;
        }

        midiWhy_[inst.channel] = mwhy;

        /* Silent until a port answers -- and so is the channel, whatever
           a swap had put on it. */
        if (inst.dsp.empty())
        {
            takeHeldOff();
            return true;
        }
    }

    if (loadDsp_)
    {
        /* Nothing was installed, so there is nothing to take back --
           and taking something back here would be worse than doing
           nothing: gthPatchManager::newPatch deliberately leaves the
           previous patch alone when a load fails, and an unload on this
           path would throw away the thing it just protected. */
        if (!loadDsp_(inst, why))
            return false;
    }
    else
    {
        /* No hook: the plain reading of what an instrument is. The name
           is searched for the way a .patch's `dsp' line is searched for,
           because a piece that only loaded from one directory would be a
           piece you could not send anybody. */
        const std::string path =
            thUtil::findDataFile(inst.dsp, "dsp", "THINK_DSP_PATH", DSP_PATH);

        if (synth_ == NULL ||
            synth_->loadTree((path.empty() ? inst.dsp : path).c_str(),
                             inst.channel, TH_DEFAULT_CHAN_AMP) == NULL)
        {
            why = "'" + inst.dsp + "' did not load";
            return false;
        }
    }

    /* The effect, after the instrument and before the values: loading an
     * instrument builds a new channel and an effect belongs to a channel, so
     * this order is the only one that leaves both up -- and the values
     * include the effect's, under `fx.', which cannot be written until it is
     * there.
     *
     * Through the host's hook where there is one, for the reason the
     * instrument goes through its own: a .patch carries an `effect' line and
     * the values under it, so an effect the host does not know about is a
     * patch page offering to choose one that is already there and a save
     * that writes values with no file to attach them to. Straight through
     * the synth without a hook, which is what a headless harness wants.
     *
     * Asked for on every apply, including the one where the instrument was
     * kept: the host is the only thing that can tell "the same effect is
     * already on this channel" from "this channel was rebuilt underneath it",
     * and gthPatchManager::setEffect does. */
    {
        bool got;

        if (loadEffect_)
            got = loadEffect_(inst.channel, inst.effect, inst.sideChannel,
                              why);
        else if (inst.effect.empty())
            got = true;
        else
        {
            const std::string path =
                thUtil::findDataFile(inst.effect, "dsp", "THINK_DSP_PATH",
                                     DSP_PATH);

            got = synth_ != NULL &&
                  synth_->loadEffect((path.empty() ? inst.effect
                                                   : path).c_str(),
                                     inst.channel, inst.sideChannel) != NULL;
        }

        if (!got)
        {
            if (why.empty())
                why = "'" + inst.effect + "' did not load as an effect";

            if (!unapplyInstrument(index))
                why += " (and its graph could not be taken off channel " +
                       std::to_string(inst.channel + 1) + ")";

            return false;
        }
    }

    /* applyValues takes its own wiring back when it refuses, so all
       that is left here is the graph. */
    if (!applyValues(inst, why))
    {
        /* The one way the promise above can fail to be kept: a full
           command ring means the audio thread cannot be told to drop
           the channel, so the graph stays up and sounding. Saying so is
           better than a message that leaves somebody hunting for why a
           refused instrument is audible -- and the host keeps the
           channel on its own books either way, so the next load tries
           again. */
        if (!unapplyInstrument(index))
            why += " (and its graph could not be taken off channel " +
                   std::to_string(inst.channel + 1) + ")";

        return false;
    }

    return true;
}

/* ---- the graph on the mix ---------------------------------------------- */

void
thcScheduler::setMasterEffect (const std::string &dsp,
                               const std::vector<thcInstrumentArg> &args,
                               const std::vector<std::shared_ptr<void>> &graphs)
{
    master_ = thcInstrument();
    master_.name = "the mix";
    master_.effect = dsp;
    master_.args = args;
    master_.graphs = graphs;
    master_.channel = -1;
}

/* Puts the piece's master effect on, or takes the last piece's off.
 *
 * applyInstrument's shape with the instrument left out: there is no graph
 * underneath to build first and no channel to allocate, so what is left is
 * the effect and the values on top of it. All or nothing, for the reason
 * applyInstrument is: a value refused after the graph is up takes the graph
 * back down, so a caller has two states to think about rather than three.
 */
bool
thcScheduler::applyMasterEffect (std::string &why)
{
    if (synth_ == NULL || staging_)
        return true;

    if (master_.effect.empty())
    {
        /* Nothing declared takes off whatever was there. A piece that says
           nothing about the mix means a dry mix, not "keep the last
           piece's reverb", which is what leaving it would mean in a
           session where pieces are opened one after another. */
        dropKnobConns(-1);

        if (!synth_->removeMasterEffect())
        {
            why = "the audio thread could not be told to drop the master "
                  "effect";
            return false;
        }

        return true;
    }

    const std::string path =
        thUtil::findDataFile(master_.effect, "dsp", "THINK_DSP_PATH",
                             DSP_PATH);

    if (synth_->loadMasterEffect((path.empty() ? master_.effect
                                               : path).c_str()) == NULL)
    {
        why = "'" + master_.effect + "' did not load as an effect";
        return false;
    }

    if (!applyValues(master_, why))
    {
        if (!unapplyMasterEffect())
            why += " (and it could not be taken off the mix)";

        return false;
    }

    return true;
}

bool
thcScheduler::unapplyMasterEffect (void)
{
    if (staging_)
        return true;

    dropKnobConns(-1);

    return synth_ == NULL || synth_->removeMasterEffect();
}

/* ---- structure edits --------------------------------------------------- */

/* This channel becomes that instrument.
 *
 * The same load hook and the same values a declared instrument gets, so
 * a swapped-in instrument is indistinguishable from one the file put
 * there: same patch tab, same arg panel, same dirty flag, everything the
 * application already knows how to show. What makes it a *structure*
 * edit rather than a chanarg is that the graph itself changes -- a
 * different .dsp, a different set of nodes.
 *
 * The voice lifecycle is the one loadTree has always promised and the
 * plan declined to re-decide: the replacement is built entirely off the
 * audio thread and published by a SET_CHANNEL at a window boundary, so
 * notes already sounding finish on the tree they started on and the next
 * note gets the new one.
 */
bool
thcScheduler::swapInstrument (int channel, const std::string &name,
                              std::string &why)
{
    const thcInstrument *want = instrument(name);

    if (want == NULL)
    {
        why = "no instrument called '" + name + "' is declared";
        return false;
    }

    /* Only onto a channel the piece brought with it.
     *
       An instrument's channel is either allocated by the loader, which
       asks the host whether somebody is already there, or written into a
       sink by hand. A swap has no such conversation: it fires from a
       running chain, so refusing it here is the only place the question
       can be asked. Without this, `sink { channel = 5; }' plus a
       gen::swap rebuilds channel 5 out from under whatever a person had
       loaded on it -- and channel 5 is in no instrument's declaration,
       so a rewind would not put their patch back either. The piece's own
       channels are the whole of what it may rebuild; reaching further is
       a chanarg's job, where the worst case is a number. */
    if (channelOf(channel) == NULL)
    {
        why = "channel " + std::to_string(channel + 1) + " is not one this "
              "piece declares an instrument for";
        return false;
    }

    /* Already this instrument? Then say so and touch nothing.
     *
       A gen::swap cannot know what its sink's channel is holding -- it
       has a list of names and a clock, and starts at the first name.
       Point it at a list whose first entry is what the sink already
       plays and the opening tick would otherwise rebuild the graph into
       a copy of itself: every sounding voice cut, for no change. Here is
       where that is knowable, so here is where it is answered. */
    if (holding(channel) == name)
        return true;

    /* A copy with the target channel written in. The declaration says
       which channel that instrument *lives* on; a swap is about where it
       is being put, which is the sink's business and not the
       declaration's. */
    thcInstrument put = *want;

    put.channel = channel;

    /* From here the channel no longer says what the file says, whether
       or not the rest of this succeeds -- the graph is going up before
       the values are checked, exactly as it does at load time. Recorded
       first so that a refusal below still leaves a rewind knowing there
       is something to put back. */
    swapped_.insert(channel);

    /* Off a device: what it holds there ends now, and the channel is a
       graph's again, or nothing's. */
    const thcInstrument *was = instrument(holding(channel));

    if (playsOverMidi(channel))
    {
        midiOut_->detach(channel);
        overMidi_[channel] = false;
    }

    midiWhy_[channel].clear();

    /* Onto a device, where one answers: the graph that was on the channel
       comes off -- the device plays it now -- and nothing below applies,
       since a device has no graph to load or values to set. Where none
       answers the swap goes on to the instrument's dsp, as applying it
       does, or leaves the channel silent. */
    if (!put.midi.empty())
    {
        std::string mwhy = "no MIDI output in this host";

        if (midiOut_ != NULL && midiOut_->attach(channel, put, mwhy))
        {
            /* The graph comes off, notes and all: its pending offs go to
               the device now, so a voice left on it would never be
               released. And what drove its args goes with it. */
            if (was != NULL && synth_ != NULL &&
                synth_->getChannel(channel) != NULL)
            {
                thcInstrument old = *was;

                old.channel = channel;
                takeGraphOff(old);
            }

            dropKnobConns(channel);

            overMidi_[channel] = true;
            holding_[channel] = name;
            forgetNodeArgs(channel);
            forgetGrowth(channel);
            return true;
        }

        midiWhy_[channel] = mwhy;

        if (put.dsp.empty())
        {
            if (was != NULL)
            {
                thcInstrument old = *was;

                old.channel = channel;
                takeGraphOff(old);
            }

            holding_[channel] = name;
            forgetNodeArgs(channel);
            forgetGrowth(channel);
            return true;
        }
    }

    if (!putOn(put, why))
        return false;

    /* Whatever grew on this channel grew on the graph that is gone. */
    forgetGrowth(channel);

    /* What the channel is holding now, so the next swap can tell whether
       it has anything to do and the host can tell that the channel no
       longer holds the graph its declaration names. */
    holding_[channel] = name;

    /* Any node-arg edits made to whatever was on this channel belonged
       to that graph, not this one. A `filt.cutoff' that meant something
       on the old .dsp names nothing on the new one -- or worse, names
       something else. */
    forgetNodeArgs(channel);

    return true;
}

bool
thcScheduler::putOn (const thcInstrument &put, std::string &why)
{
    if (loadDsp_)
    {
        if (!loadDsp_(put, why))
            return false;
    }
    else
    {
        const std::string path =
            thUtil::findDataFile(put.dsp, "dsp", "THINK_DSP_PATH", DSP_PATH);

        if (synth_ == NULL ||
            synth_->loadTree((path.empty() ? put.dsp : path).c_str(),
                             put.channel, TH_DEFAULT_CHAN_AMP) == NULL)
        {
            why = "'" + put.dsp + "' did not load";
            return false;
        }
    }

    /* The values, through the one implementation of what a value means.
       A swap that loaded the graph and left the numbers behind would be
       half an instrument.
     *
       All or nothing, the same promise applyInstrument makes and for the
       same reason: applyValues takes its own wiring back, and the graph
       comes off here, so nobody downstream has a third state to track.
       The first draft of this returned false with the new graph up and
       sounding. */
    if (!applyValues(put, why))
    {
        if (!unapply(put))
            why += " (and its graph could not be taken off channel " +
                   std::to_string(put.channel + 1) + ")";

        return false;
    }

    return true;
}

bool
thcScheduler::graphText (int channel, std::string &text) const
{
    if (grownText(channel, text))
        return true;

    const thcInstrument *inst = instrument(holding(channel));

    if (channelOf(channel) == NULL || inst == NULL || inst->dsp.empty())
        return false;

    /* The text the instrument declares, as its graph is loaded: an inline
       graph's from the registry, a file's from the disk. */
    if (!thUtil::findGraphText(inst->dsp, text))
    {
        const std::string path = thUtil::findDataFile(
            inst->dsp, "dsp", "THINK_DSP_PATH", DSP_PATH);
        std::ifstream in((path.empty() ? inst->dsp : path).c_str(),
                         std::ios::binary);

        text.assign(std::istreambuf_iterator<char>(in),
                    std::istreambuf_iterator<char>());
    }

    return !text.empty();
}

bool
thcScheduler::growInstrument (int channel, unsigned int choice, bool shrink,
                              const std::string &kinds,
                              const std::string &target, int listen, int tune,
                              std::string &why, int depth)
{
    return stepGraph(channel, shrink, NULL, choice, kinds, target, listen,
                     tune, depth, why);
}

bool
thcScheduler::tryGraph (int channel, const std::string &text,
                        std::string &why)
{
    return stepGraph(channel, false, &text, 0, "", "", 0, 0, 0, why);
}

thcScheduler::Growth *
thcScheduler::growthOf (int channel, std::string &why)
{
    const thcInstrument *inst = instrument(holding(channel));

    if (channelOf(channel) == NULL || inst == NULL)
    {
        why = "channel " + std::to_string(channel + 1) + " is not one this "
              "piece declares an instrument for";
        return NULL;
    }

    if (playsOverMidi(channel) || inst->dsp.empty())
    {
        why = "instrument '" + inst->name + "' has no graph here to grow";
        return NULL;
    }

    Growth &g = grown_[channel];

    if (g.texts.empty())
    {
        std::string text;

        if (!graphText(channel, text))
        {
            grown_.erase(channel);
            why = "'" + inst->dsp + "' could not be read";
            return NULL;
        }

        g.texts.push_back(text);
        g.holds.push_back(std::shared_ptr<void>());
        g.whats.push_back("");
        g.levels.push_back(std::nan(""));
    }

    return &g;
}

bool
thcScheduler::stepGraph (int channel, bool shrink, const std::string *given,
                         unsigned int choice, const std::string &kinds,
                         const std::string &target, int listen, int tune,
                         int depth, std::string &why)
{
    Growth *g = growthOf(channel, why);

    if (g == NULL)
        return false;

    /* A step of any kind supersedes a growth still being heard. */
    forgetHearing(*g);

    GrowthStep step;
    std::vector<std::string> options, whats;

    /* Held to the stack as it stands, not to the stage's own count: what a
       step back from a window, a failed step or a search did to it is not
       in that. */
    if (depth > 0 && given == NULL)
    {
        const size_t at = g->texts.size() - 1;

        shrink = at >= (size_t)depth || (at > 0 && shrink);
    }

    if (shrink)     /* at the declared graph there is nothing to go back on */
        return g->texts.size() < 2 ||
               placeStep(channel, g->texts.size() - 2, "", "", step, why);

    if (given != NULL)
        return placeStep(channel, std::string::npos, *given,
                         "what a search found", step, why);

    const size_t want = target.empty() ? 1 : (size_t)std::max(listen, 1);

    if (!grower_ && growRequest_)
    {
        g->asked = ++growTicket_;
        g->askTarget = target;
        g->askTune = tune;
        growRequest_(g->asked, g->texts.back(), g->texts[0], choice, kinds,
                     want);
        return true;
    }

    if (!grower_)
    {
        why = "this host cannot grow a graph";
        return false;
    }

    if (!grower_(g->texts.back(), g->texts[0], choice, kinds, want, options,
                 whats, why))
        return false;

    return chooseAmong(channel, *g, options, whats, target, tune, why);
}

bool
thcScheduler::chooseAmong (int channel, Growth &g,
                           const std::vector<std::string> &options,
                           const std::vector<std::string> &whats,
                           const std::string &target, int tune,
                           std::string &why)
{
    if (target.empty())
        return placeStep(channel, std::string::npos, options[0], whats[0],
                         GrowthStep(), why);

    /* The graph on the channel is heard too, last, so a host can show
       where the step started from as well as where it went. */
    std::vector<std::string> heard = options;

    heard.push_back(g.texts.back());

    g.options = options;
    g.optionWhats = whats;
    g.tuned.assign(heard.size(), std::string());
    g.tickets = hearAll(*instrument(holding(channel)), heard, target,
                        options.size(), tune, g.texts.back());
    g.distances.assign(heard.size(), HUGE_VAL);

    /* Answered later on a live host, so the step is taken then and the
       notes due meanwhile are not held up; answered by now on one that
       renders in the tick. */
    if (!auditionSync_ || !collect(g))
        return true;

    return takeHeard(channel, g, why);
}

bool
thcScheduler::takeHeard (int channel, Growth &g, std::string &why)
{
    const size_t n = g.options.size();
    GrowthStep step;

    for (size_t i = 0; i <= n; i++)
        step.heard.push_back(g.distances[i] == HUGE_VAL ? std::nan("")
                                                        : g.distances[i]);

    step.taken = (int)(std::min_element(g.distances.begin(),
                                        g.distances.begin() + n) -
                       g.distances.begin());
    step.from = step.heard[n];
    step.heard.pop_back();

    const std::string text = g.tuned[step.taken].empty()
                           ? g.options[step.taken] : g.tuned[step.taken];
    const std::string what = g.optionWhats[step.taken];

    doneHearing(g);

    return placeStep(channel, std::string::npos, text, what, step, why);
}

void
thcScheduler::growthAnswer (unsigned int ticket,
                            const std::vector<std::string> &grown,
                            const std::vector<std::string> &what,
                            const std::string &why)
{
    for (auto &e : grown_)
    {
        if (ticket == 0 || e.second.asked != ticket)
            continue;

        std::string failed = why;

        e.second.asked = 0;

        if (grown.empty() || what.size() != grown.size() ||
            !chooseAmong(e.first, e.second, grown, what, e.second.askTarget,
                         e.second.askTune, failed))
            fprintf(stderr, "thcScheduler: growing channel %d: %s\n",
                    e.first + 1, failed.c_str());

        runQueued(e.first);
        return;
    }
}

bool
thcScheduler::growBackTo (int channel, size_t depth, std::string &why)
{
    Growth *g = growthOf(channel, why);

    if (g == NULL)
        return false;

    if (g->asked != 0 || !g->options.empty())
    {
        Growth::Queued q;

        q.back = (int)depth;
        g->queued.push_back(q);
        return true;
    }

    if (depth + 1 >= g->texts.size())
    {
        why = "the channel is not that many edits on";
        return false;
    }

    forgetHearing(*g);

    return placeStep(channel, depth, "", "", GrowthStep(), why);
}

void
thcScheduler::putGraph (int channel, const std::string &graph,
                        const std::string &over, bool strict,
                        const PutDone &done)
{
    const auto g = grown_.find(channel);

    if (g != grown_.end() &&
        (g->second.asked != 0 || !g->second.options.empty()))
    {
        Growth::Queued q;

        q.put = true;
        q.graph = graph;
        q.over = over;
        q.strict = strict;
        q.done = done;
        g->second.queued.push_back(q);
        return;
    }

    std::string now, why;
    const bool onTop = !over.empty() && grownText(channel, now) &&
                       now == over;

    if (strict && !over.empty() && !onTop)
        why = "the channel has changed since the last Listen";
    else
    {
        if (onTop)
            growInstrument(channel, 0, true, "", "", 0, 0, why);

        if (!graph.empty())
            tryGraph(channel, graph, why);
        else if (!onTop)
            why = "it is no longer on the channel";
    }

    if (done)
        done(why);
}

bool
thcScheduler::placeStep (int channel, size_t backTo, const std::string &grown,
                         const std::string &what, GrowthStep step,
                         std::string &why)
{
    const thcInstrument *inst = instrument(holding(channel));
    Growth &g = grown_[channel];
    const bool back = backTo != std::string::npos;
    const std::string text = back ? g.texts[backTo] : grown;
    thcInstrument put = *inst;

    put.channel = channel;

    /* The declared graph goes back under its own name; a grown one under
       a name its text decides, held for as long as it is on the stack. */
    std::shared_ptr<void> hold;

    if (!back || backTo > 0)
    {
        put.dsp = thcGenLoader::inlineGraphName(inst->name, text);
        hold = thUtil::addGraphText(put.dsp, text);
        put.graphs.push_back(hold);
    }

    swapped_.insert(channel);

    if (!putOn(put, why))
        return false;

    if (std::isnan(g.levels.back()))
        g.levels.back() = step.from;

    if (back)
    {
        g.texts.resize(backTo + 1);
        g.holds.resize(backTo + 1);
        g.whats.resize(backTo + 1);
        g.levels.resize(backTo + 1);
    }
    else
    {
        g.texts.push_back(text);
        g.holds.push_back(hold);
        g.whats.push_back(what);
        g.levels.push_back(step.taken >= 0 ? step.heard[step.taken]
                                           : std::nan(""));
    }

    step.at = transportNow_;
    step.depth = g.texts.size() - 1;
    step.what = back ? "" : what;
    step.distance = g.levels.back();
    g.log.push_back(step);
    growthSerial_++;

    forgetNodeArgs(channel);

    return true;
}

bool
thcScheduler::growth (int channel, std::vector<std::string> &stack,
                      std::vector<GrowthStep> &log) const
{
    const auto g = grown_.find(channel);

    if (g == grown_.end() || g->second.texts.empty())
        return false;

    stack = g->second.whats;
    log = g->second.log;

    return true;
}

/* Which instrument this channel is holding: the last one swapped onto
 * it, or the one whose declaration owns it. Empty for a channel that is
 * none of the piece's business. */
std::string
thcScheduler::holding (int channel) const
{
    std::map<int, std::string>::const_iterator it = holding_.find(channel);

    if (it != holding_.end())
        return it->second;

    const thcInstrument *inst = channelOf(channel);

    return inst != NULL ? inst->name : std::string();
}

const thcInstrument *
thcScheduler::channelOf (int channel) const
{
    for (size_t i = 0; i < instruments_.size(); i++)
        if (instruments_[i].channel == channel)
            return &instruments_[i];

    return NULL;
}

/* One constant inside the graph on this channel.
 *
 * Not a chanarg. A chanarg is the surface a patch chose to expose, and
 * the whole of what every composer up to now could reach. The way past it
 * is a different mechanism rather than a widening of that one, and this
 * is that different mechanism. A piece doing this is
 * reaching into somebody else's graph -- on its own say-so, in an event
 * anybody can see on the roll, and only as often as the event stream
 * flows.
 *
 * It lands on the channel's *prototype* tree: the one thMidiChan builds
 * each new voice from, and the one thMidiChan.cpp says in as many words
 * the audio thread never reads. So there is no swap to make and no
 * command to queue -- notes already sounding are playing their own
 * copies and finish unchanged, and the next note is built from the
 * edited tree. The editor's promise, kept by the same mechanism rather
 * than restated.
 */
bool
thcScheduler::setNodeArg (int channel, const std::string &node,
                          const std::string &arg, float value,
                          std::string &why)
{
    if (synth_ == NULL)
    {
        why = "there is no synth to edit";
        return false;
    }

    /* Only onto a channel the piece brought with it -- the same refusal
       swapInstrument makes, for a reason that is stronger here rather
       than weaker.
     *
       A swap that reached an undeclared channel would rebuild somebody's
       hand-loaded patch out from under them, and a rewind could not put
       it back because the channel is in no declaration. A node-arg edit
       reaching one is that with the noise turned down: it rewrites a
       constant *inside* their graph, silently, and reset() restores by
       re-applying declarations, so there is nothing that will ever
       undo it. The piece's own channels are the whole of what it may
       reshape; that a sink may name any channel at all is exactly why
       the question has to be asked here. */
    if (channelOf(channel) == NULL)
    {
        why = "channel " + std::to_string(channel + 1) + " is not one this "
              "piece declares an instrument for";
        return false;
    }

    thMidiChan *chan = synth_->getChannel(channel);
    thSynthTree *tree = chan != NULL ? chan->modnode() : NULL;

    if (tree == NULL)
    {
        why = "nothing is loaded on that channel";
        return false;
    }

    thNode *n = tree->findNode(node);

    if (n == NULL)
    {
        why = "the graph on that channel has no node called '" + node + "'";
        return false;
    }

    /* Only an arg the node's plugin declared. thNode::setArg would
       otherwise invent one, which is a dead value nothing reads -- the
       same silence a mistyped node arg produced in the control-rate
       host, arriving by a different door. */
    thPlugin *p = n->plugin();
    bool known = false;

    for (int i = 0; p != NULL && i < p->argCount(); i++)
        if (p->getArgName(i) == arg)
        {
            if (p->getArgDir(i) == thPlugin::ARG_STATE)
            {
                why = "'" + node + "." + arg + "' is that module's own "
                      "scratch, not a constant a piece may set";
                return false;
            }

            known = true;
            break;
        }

    if (!known)
    {
        why = "node '" + node + "' has no arg called '" + arg + "'";
        return false;
    }

    /* And only an arg that is *already* a constant.
     *
       A whitelist, not a blacklist of the wired kinds, because thArg has
       four of them and the first draft of this only named ARG_POINTER --
       which let a piece write a number over an ARG_CHANNEL and unwire a
       chanarg. thNode::setArg rebuilds the arg as ARG_VALUE whatever it
       was, and thMidiChan::assignChanArgPointers only re-points args
       still typed ARG_CHANNEL, so `reshape { node = "fmap"; arg =
       "outmin"; }' against amb01.dsp would have killed that channel's
       @fmin for the rest of the session: slider dead, arg panel dead,
       any knob bound to it dead, and nothing said. NodeEditor's live
       edit asks the same question the same way, for the same reason.
     *
       So: an arg wired to anything -- another node's output, a chanarg,
       a note property -- is not a constant, and writing over it is an
       add/remove/rewire edit wearing a value edit's clothes. */
    thArg *existing = n->getArg(arg);

    if (existing == NULL || existing->type() != thArg::ARG_VALUE)
    {
        why = "'" + node + "." + arg + "' is wired, not a constant";
        return false;
    }

    /* setValue, not thNode::setArg: it is the single relaxed store the
       rest of the tree uses for exactly this, where setArg reassigns
       values_, len_, type_ and name_ one after another. Nothing on the
       audio thread reads a prototype tree's args today, but
       assignChanArgPointers walks them from applyCommand, and a
       four-field non-atomic rewrite is the wrong thing to be doing
       beside that even when the arithmetic happens to work out. */
    existing->setValue(value);

    /* Remembered so a swap on this channel can forget it, and so a
       reset knows there is something to put back. */
    for (size_t i = 0; i < nodeArgs_.size(); i++)
        if (nodeArgs_[i].channel == channel && nodeArgs_[i].node == node &&
            nodeArgs_[i].arg == arg)
            return true;

    NodeArgEdit e;

    e.channel = channel;
    e.node = node;
    e.arg = arg;

    nodeArgs_.push_back(e);

    return true;
}

void
thcScheduler::forgetNodeArgs (int channel)
{
    for (size_t i = nodeArgs_.size(); i > 0; i--)
        if (nodeArgs_[i - 1].channel == channel)
            nodeArgs_.erase(nodeArgs_.begin() + (i - 1));
}

/* The one way an instrument comes off a channel, so the first attempt
 * and every retry cannot drift apart. */
bool
thcScheduler::takeOff (const thcInstrument &inst)
{
    if (inst.channel < 0 || staging_)
        return true;                    /* never got there; nothing to do */

    /* This instrument's route, not whatever holds the channel now: a
       graph stranded on it and retried here must not take off the
       device another instrument has attached there since. */
    if (!inst.midi.empty() && playsOverMidi(inst.channel))
    {
        midiOut_->detach(inst.channel);
        overMidi_[inst.channel] = false;
        midiWhy_[inst.channel].clear();
        return true;
    }

    return takeGraphOff(inst);
}

bool
thcScheduler::takeGraphOff (const thcInstrument &inst)
{
    if (inst.dsp.empty())
        return true;                    /* MIDI with no graph: nothing on */

    if (unloadDsp_)
        return unloadDsp_(inst);

    if (synth_ != NULL)
        return synth_->removeChan(inst.channel);

    return true;
}

/* Every swapped channel holding an instrument that names `pattern' (or
   any MIDI instrument, where it is empty) swapped onto again: its place
   -- a device, its dsp, nothing -- decided afresh, as applying a declared
   instrument decides it. What a host calls when the ports, or a route,
   change. */
int
thcScheduler::reapplySwapped (const std::string &pattern)
{
    int n = 0;

    for (int ch : std::vector<int>(swapped_.begin(), swapped_.end()))
    {
        const std::string name = holding(ch);
        const thcInstrument *inst = instrument(name);

        if (inst == NULL || inst->midi.empty() || inst->channel == ch ||
            (!pattern.empty() && inst->midi != pattern))
            continue;

        std::string why;

        holding_.erase(ch);

        if (swapInstrument(ch, name, why))
            n++;
    }

    return n;
}

bool
thcScheduler::unapplyInstrument (size_t index)
{
    if (index >= instruments_.size())
        return true;

    return unapply(instruments_[index]);
}

bool
thcScheduler::unapply (const thcInstrument &what)
{
    const thcInstrument inst = what;     /* by value: stranded_ may grow */

    if (takeOff(inst))
        return true;

    /* It would not go, so the graph is still on that channel and still
     * sounding -- and the caller is about to throw the instrument table
     * away, because a load that needs a rollback is a load that failed.
     * Keeping a copy is the difference between a channel that gets
     * cleaned up on the next tick and one nothing in the program can
     * name.
     *
     * In the application the window keeps its own record too and would
     * eventually notice; headless there is no window, which is where
     * dropping this quietly would have cost most. */
    for (size_t i = 0; i < stranded_.size(); i++)
        if (stranded_[i].channel == inst.channel)
            return false;               /* already waiting; not twice     */

    stranded_.push_back(inst);

    return false;
}

/* ---- an edit while playing ------------------------------------------- */

void
thcScheduler::prepareEdit (thcScheduler &next,
                           const std::set<std::string> &keepKnobValues)
{
    next.staging_ = true;
    next.lent_ = knobs_;
    next.keepValues_ = keepKnobValues;
    next.taken_ = taken_;

    /* A pinned seed in the new text overrides this; an unpinned piece
       goes on composing from the seed the run was started with. */
    next.masterSeed_ = masterSeed_;

    /* So a new stage's first wake is computed against the transport as
       it is: now, or its chain's start if that is later. */
    next.transportNow_ = transportNow_;
    next.beat_ = beat_;
    next.tempoAt_ = tempoAt_;
    next.beatAtTempo_ = beatAtTempo_;
    next.tempo_ = tempo_;

    /* What a stage borrows from its scheduler for as long as it lives,
       lent rather than made again, so the stages adopt() takes need
       nothing from `next' once it is gone. */
    ensureAuditioner();

    next.auditioner_ = auditioner_;
    next.ownsAuditioner_ = auditioner_ == NULL;
    next.earRequest_ = earRequest_;

    if (controlSynth_ != NULL)
    {
        next.controlSynth_ = controlSynth_;
        next.ownsControl_ = false;
    }
}

bool
thcScheduler::adopt (thcScheduler &next, const EditPlan &plan,
                     std::string &why)
{
    typedef std::pair<size_t, size_t> Slot;

    /* The chains move under a replay, so none is left under way. */
    settleReplays();

    /* Which live stage each slot of the new piece keeps, by name. A
     * chain with embedded nodes is kept whole or not at all: a stage
     * bound to `lfo->out' holds a pointer into its chain's node host, so
     * the stage can only be kept alongside the host, and the host only
     * alongside every stage that reads it. */
    std::map<Slot, Slot> from;          /* new slot -> live slot */
    std::map<size_t, size_t> hostFrom;  /* new chain -> live chain */

    for (size_t j = 0; j < next.chains_.size(); j++)
    {
        const thcChain &nc = next.chains_[j];
        size_t i = 0;

        while (i < chains_.size() && chains_[i].name != nc.name)
            i++;

        if (i == chains_.size())
            continue;

        const thcChain &oc = chains_[i];
        const bool nodes = nc.nodes || oc.nodes;

        if (nodes && plan.keepChains.find(nc.name) == plan.keepChains.end())
            continue;

        std::map<Slot, Slot> here;
        std::set<size_t> used;

        for (size_t k = 0; k < nc.stages.size(); k++)
        {
            const thcStage *ns = nc.stages[k].get();

            if (ns->name.empty() ||
                plan.keep.find(std::make_pair(nc.name, ns->name)) ==
                    plan.keep.end())
                continue;

            for (size_t m = 0; m < oc.stages.size(); m++)
            {
                const thcStage *os = oc.stages[m].get();

                if (used.find(m) == used.end() && os->name == ns->name &&
                    os->plugin == ns->plugin && os->ticks == ns->ticks)
                {
                    here[Slot(j, k)] = Slot(i, m);
                    used.insert(m);
                    break;
                }
            }
        }

        /* A chain one of whose stages is built again is built again
           whole: what its generator composed went through the old
           stages, and only a generator made again composes it again. */
        if (here.size() != nc.stages.size() ||
            oc.stages.size() != nc.stages.size())
            continue;

        if (nodes)
            hostFrom[j] = i;

        from.insert(here.begin(), here.end());
    }

    std::map<Slot, Slot> to;            /* live slot -> new slot */

    for (std::map<Slot, Slot>::iterator f = from.begin(); f != from.end();
         ++f)
        to[f->second] = f->first;

    /* The stages that go: the new ones a kept one replaces, and the live
       ones nothing keeps. Their instances now, and their knob
       connections below, by the store each connection notifies. */
    std::set<const thcParamStore *> dead;

    for (std::map<Slot, Slot>::iterator f = from.begin(); f != from.end();
         ++f)
    {
        std::unique_ptr<thcStage> &fresh =
            next.chains_[f->first.first].stages[f->first.second];
        std::unique_ptr<thcStage> &kept =
            chains_[f->second.first].stages[f->second.second];

        /* The seed its place in the new text gives it, for a rewind,
           which creates it again from the text. Its running instance is
           the one it was created with. */
        kept->params.params_.seed = fresh->params.params_.seed;

        dead.insert(&fresh->params);
        fresh->plugin->destroy(fresh->state);
        fresh->state = NULL;
        fresh = std::move(kept);
    }

    std::set<int> lost;                 /* live chains that lose a stage */

    for (size_t i = 0; i < chains_.size(); i++)
        for (size_t m = 0; m < chains_[i].stages.size(); m++)
        {
            thcStage *s = chains_[i].stages[m].get();

            if (s == NULL)
                continue;

            lost.insert((int)i);
            dead.insert(&s->params);
            s->plugin->destroy(s->state);
            s->state = NULL;
        }

    for (std::map<size_t, size_t>::iterator h = hostFrom.begin();
         h != hostFrom.end(); ++h)
        next.chains_[h->first].nodes = std::move(chains_[h->second].nodes);

    /* The connections of what survives, the live ones first. */
    std::vector<KnobConn> conns;

    for (int side = 0; side < 2; side++)
    {
        std::vector<KnobConn> &list = side == 0 ? knobConns_
                                                : next.knobConns_;

        for (size_t c = 0; c < list.size(); c++)
            if (list[c].store != NULL &&
                dead.find(list[c].store) != dead.end())
                list[c].conn.disconnect();
            else
                conns.push_back(list[c]);
    }

    knobConns_ = conns;
    next.knobConns_.clear();

    /* The knobs are the new text's, and every one it kept is the live
       object already. The ones it stopped declaring are kept alive until
       the piece goes: something may still be holding one. */
    std::set<thArg *> still;

    for (std::map<std::string, thArg *>::iterator k = next.knobs_.begin();
         k != next.knobs_.end(); ++k)
        still.insert(k->second);

    for (std::map<std::string, thArg *>::iterator k = knobs_.begin();
         k != knobs_.end(); ++k)
        if (still.find(k->second) == still.end())
            retired_.push_back(k->second);

    knobs_ = next.knobs_;
    next.knobs_.clear();
    next.lent_.clear();

    knobHome_.swap(next.knobHome_);
    next.knobHome_.clear();

    /* The new text's sections may set the knobs otherwise: set again, as
       written now, at the next step. */
    knobSpanStart_ = -1;
    knobSpanSection_ = -1;

    /* A kept knob's metadata is the new text's, and what it no longer
       says goes back to a fresh knob's. */
    for (const auto &m : next.pendingMeta_)
    {
        thArg *k = m.second, *live = knob(m.first);

        live->setMin(k->min());
        live->setMax(k->max());
        live->setWidgetType(k->widgetType());
        live->setLabel(k->label());
        live->setUnits(k->units());
        live->setGroup(k->group());
        live->setValueNames(k->valueNames(), k->typedByFile());
        live->setStep(k->step(), k->typedByFile());
        delete k;
    }

    next.pendingMeta_.clear();


    /* The wakes: the kept stages' own, moved to their new places, and
       the new stages', in one deterministic order -- by time, the kept
       ones ahead of the new at one instant, each side in the order it
       was queued -- and numbered afresh from this scheduler's count. */
    struct Wake { Wakeup w; int side; };
    std::vector<Wake> wakes;

    for (size_t n = 0; n < wakeups_.size(); n++)
    {
        std::map<Slot, Slot>::iterator t =
            to.find(Slot(wakeups_[n].chain, wakeups_[n].stage));

        if (t == to.end())
            continue;

        Wake w = { wakeups_[n], 0 };
        const thcChain &oc = chains_[w.w.chain];
        const thcChain &nc = next.chains_[t->second.first];

        w.w.chain = t->second.first;
        w.w.stage = t->second.second;

        /* A stage still waiting for its chain's start waits for the new
           one. */
        if ((oc.start != nc.start || oc.startBeats != nc.startBeats) &&
            nc.stages[w.w.stage]->awaitingStart)
            w.w.at = chainStartTime(nc);

        wakes.push_back(w);
    }

    for (size_t n = 0; n < next.wakeups_.size(); n++)
        if (from.find(Slot(next.wakeups_[n].chain,
                           next.wakeups_[n].stage)) == from.end())
        {
            Wake w = { next.wakeups_[n], 1 };

            wakes.push_back(w);
        }

    std::sort(wakes.begin(), wakes.end(),
              [](const Wake &a, const Wake &b)
              {
                  if (a.w.at != b.w.at)
                      return a.w.at < b.w.at;

                  if (a.side != b.side)
                      return a.side < b.side;

                  return a.w.seq < b.w.seq;
              });

    wakeups_.clear();
    next.wakeups_.clear();

    for (size_t n = 0; n < wakes.size(); n++)
    {
        wakes[n].w.seq = heapSeq_++;
        wakeups_.push_back(wakes[n].w);
    }

    std::make_heap(wakeups_.begin(), wakeups_.end(), Later());

    /* A chain muted or soloed here stays so under its new text, and
       what it was heard to play stays its own. */
    std::vector<int> moved(chains_.size(), -1);

    soloCount_ = 0;

    for (size_t j = 0; j < next.chains_.size(); j++)
    {
        for (size_t i = 0; i < chains_.size(); i++)
            if (chains_[i].name == next.chains_[j].name)
            {
                thcChain &nc = next.chains_[j];

                nc.muted = chains_[i].muted;
                nc.soloed = chains_[i].soloed;
                std::copy(chains_[i].lastHeard, chains_[i].lastHeard + 2,
                          nc.lastHeard);
                std::copy(chains_[i].lastGated, chains_[i].lastGated + 2,
                          nc.lastGated);
                nc.played = std::move(chains_[i].played);
                moved[i] = (int)j;
            }

        if (next.chains_[j].soloed)
            soloCount_++;
    }

    chains_ = std::move(next.chains_);
    next.chains_.clear();

    /* The cues made so far go where they were going, by name: the
       piece keeps its place in the arrangement under its new text. One
       whose section is gone is dropped. */
    std::vector<std::pair<double, int> > cues;

    for (const auto &c : cues_)
        for (size_t k = 0; k < next.sections_.size(); k++)
            if ((size_t)c.second < sections_.size() &&
                next.sections_[k].name == sections_[c.second].name)
                cues.push_back(std::make_pair(c.first, (int)k));

    sections_ = next.sections_;
    endAfter_ = next.endAfter_;
    loopFrom_ = next.loopFrom_;
    cues_ = cues;
    meter_ = next.meter_;
    next.sections_.clear();

    cueKnob_ = next.cueKnob_;
    cueGrid_ = next.cueGrid_;

    /* A held note's release reaches its chain's stages as they are when
       it comes, and a new stage never saw the press: one that moved the
       note sends the release elsewhere, and the key would hang. */
    flushHeld(&lost);

    for (size_t n = 0; n < held_.size(); n++)
        if (held_[n].chain >= 0)
            held_[n].chain = moved[held_[n].chain];

    /* The chains built again, which are played up to now below -- not
       one played from a keyboard, whose keys a replay has not got. What
       one of them, or a chain taken away, had composed for after now is
       not played: the new chain composes its own, and a chain taken away
       is silent. */
    std::vector<bool> rebuilt(chains_.size(), true);
    std::vector<bool> replayed(chains_.size(), false);

    for (std::map<Slot, Slot>::iterator f = from.begin(); f != from.end();
         ++f)
        rebuilt[f->first.first] = false;

    for (size_t j = 0; j < chains_.size(); j++)
        replayed[j] = rebuilt[j] && started_ &&
                      !chains_[j].stages.empty() && !chains_[j].inputMidi;

    std::map<size_t, bool> marked;

    for (const auto &m : recompose_)
        if (moved[m.first] >= 0 && !replayed[moved[m.first]])
            marked[moved[m.first]] = m.second;

    recompose_.swap(marked);

    std::set<size_t> whole;

    for (size_t m : wholeMarked_)
        if (moved[m] >= 0)
            whole.insert(moved[m]);

    wholeMarked_.swap(whole);

    std::vector<Pending> left;

    for (Pending &p : pending_)
    {
        if (p.chain >= 0)
        {
            p.chain = moved[p.chain];

            if ((p.chain < 0 || rebuilt[p.chain]) &&
                (p.ev.type == THC_EV_NOTE || p.ev.type == THC_EV_CHANARG))
                continue;
        }

        left.push_back(std::move(p));
    }

    pending_.swap(left);
    std::make_heap(pending_.begin(), pending_.end(), LaterPending());

    /* Every stage answers to this scheduler and to its new place. */
    for (size_t j = 0; j < chains_.size(); j++)
        for (size_t k = 0; k < chains_[j].stages.size(); k++)
        {
            thcStage *st = chains_[j].stages[k].get();

            st->chain = j;

            if (st->ear.ctx != NULL)
                st->sched = this;

            st->params.rearm_ = [this, j, k] { rearmStage(j, k); };
            st->params.tempo_ = [this] { return tempo_; };

            /* What start() does to every stage, done to the new ones
               when the transport has already started: a rewind replays
               their load and not what came after it. */
            if (started_ && from.find(Slot(j, k)) == from.end())
                st->params.freeze();
        }

    /* A changed declaration's value, now that the text is the piece: set
       once the stages are in place and answer to this scheduler, so what
       it announces -- a param_changed, a re-arm -- reaches the stages
       that will hear it. */
    for (const auto &v : next.pendingValues_)
        if (thArg *k = knob(v.first))
        {
            k->setValue(v.second);
            knobMoved(k, false);
        }

    /* Those played up to now below hear the new value already. */
    for (size_t j = 0; j < chains_.size(); j++)
        if (replayed[j])
            recompose_.erase(j);

    next.pendingValues_.clear();

    /* What the text now declares is where the cue knob stands, not a move
       of it. */
    if (cueKnob_ != NULL)
        cueKnobLast_ = std::lround((*cueKnob_)[0]);

    if (plan.tempo > 0)
        setTempo(plan.tempo);

    /* What the new stages borrowed. */
    if (controlSynth_ == NULL && next.controlSynth_ != NULL &&
        next.ownsControl_)
    {
        controlSynth_ = next.controlSynth_;
        next.ownsControl_ = false;
    }

    if (auditioner_ == NULL && next.auditioner_ != NULL &&
        next.ownsAuditioner_)
    {
        auditioner_ = next.auditioner_;
        next.ownsAuditioner_ = false;
    }

    for (size_t j = 0; j < chains_.size(); j++)
        if (replayed[j])
            catchUp(j);

    /* The instruments. A channel no new instrument is on is taken back;
       an instrument that is new, or changed, or whose files changed under
       it, is loaded. One the same in both texts is left alone, sounding
       voices and all. */
    const std::vector<thcInstrument> was = instruments_;

    instruments_ = next.instruments_;
    next.instruments_.clear();

    bool ok = true;

    for (size_t o = 0; o < was.size(); o++)
    {
        bool reused = false;

        for (size_t n = 0; n < instruments_.size(); n++)
            if (instruments_[n].channel == was[o].channel)
                reused = true;

        if (reused)
            continue;

        /* What drove its args would push into whatever comes onto the
           channel next. */
        dropKnobConns(was[o].channel);

        if (!unapply(was[o]))
        {
            why += "instrument '" + was[o].name + "' would not come off; ";
            ok = false;
        }
    }

    for (size_t n = 0; n < instruments_.size(); n++)
    {
        const thcInstrument &ni = instruments_[n];
        bool same =
            plan.changedFiles.find(ni.dsp) == plan.changedFiles.end() &&
            (ni.effect.empty() ||
             plan.changedFiles.find(ni.effect) == plan.changedFiles.end()) &&
            swapped_.find(ni.channel) == swapped_.end();

        if (same)
        {
            same = false;

            for (size_t o = 0; o < was.size() && !same; o++)
                same = sameInstrument(was[o], ni);
        }

        std::string err;

        if (!same && !applyInstrument(n, err))
        {
            why += "instrument '" + ni.name + "': " + err + "; ";
            ok = false;
        }
    }

    /* The master effect by the same rule: a changed declaration, or its
       .dsp changed underneath it. */
    if (!sameInstrument(master_, next.master_) ||
        (!next.master_.effect.empty() &&
         plan.changedFiles.find(next.master_.effect) !=
             plan.changedFiles.end()))
    {
        master_ = next.master_;

        std::string err;

        if (!applyMasterEffect(err))
        {
            why += "the master effect: " + err + "; ";
            ok = false;
        }
    }

    next.master_ = thcInstrument();
    next.master_.channel = -1;

    return ok;
}

bool
thcScheduler::sameInstrument (const thcInstrument &a, const thcInstrument &b)
{
    if (a.name != b.name || a.dsp != b.dsp || a.effect != b.effect ||
        a.side != b.side || a.sideChannel != b.sideChannel ||
        a.channel != b.channel || a.args.size() != b.args.size() ||
        a.midi != b.midi || a.midiChannel != b.midiChannel ||
        a.midiProgram != b.midiProgram || a.ccs.size() != b.ccs.size() ||
        a.texts.size() != b.texts.size())
        return false;

    for (size_t i = 0; i < a.texts.size(); i++)
        if (a.texts[i].node != b.texts[i].node ||
            a.texts[i].arg != b.texts[i].arg ||
            a.texts[i].value != b.texts[i].value)
            return false;

    for (size_t i = 0; i < a.ccs.size(); i++)
        if (a.ccs[i].name != b.ccs[i].name || a.ccs[i].cc != b.ccs[i].cc ||
            a.ccs[i].min != b.ccs[i].min || a.ccs[i].max != b.ccs[i].max ||
            a.ccs[i].bend != b.ccs[i].bend)
            return false;

    for (size_t i = 0; i < a.args.size(); i++)
        if (a.args[i].name != b.args[i].name ||
            a.args[i].value != b.args[i].value ||
            a.args[i].units != b.args[i].units ||
            a.args[i].knob != b.args[i].knob)
            return false;

    return true;
}

void
thcScheduler::retireStranded (void)
{
    /* The ordinary case, and worth keeping free: nothing to do. */
    if (stranded_.empty())
        return;

    for (size_t i = stranded_.size(); i > 0; i--)
        if (takeOff(stranded_[i - 1]))
            stranded_.erase(stranded_.begin() + (i - 1));
}

void
thcScheduler::setMuted (size_t chain, bool muted)
{
    if (chain < chains_.size())
        chains_[chain].muted = muted;
}

void
thcScheduler::setSoloed (size_t chain, bool soloed)
{
    if (chain >= chains_.size() || chains_[chain].soloed == soloed)
        return;

    chains_[chain].soloed = soloed;

    if (soloed)
        soloCount_++;
    else
        soloCount_--;
}

/* ---- the arrangement --------------------------------------------------
 *
 * Three questions, asked in transport seconds: how long a section is,
 * how long the whole list is, and which one a time falls in. A length
 * written in beats is converted here, at the tempo the transport is
 * running now, for the reason the header gives.
 */

void
thcScheduler::addSection (const thcSection &s)
{
    sections_.push_back(s);
}

void
thcScheduler::applySectionKnobs (void)
{
    /* A knob reaches a node only when the node's next control window
       reads it, and the window a section's first tick reads is the last
       one at or before the section's start. So the section's knobs are
       written there, after every window and tick before it -- on every
       peer, wherever its steps happen to end. The slop takes up the
       rounding between a sum of section lengths and a sum of periods.
       A start that moved behind what has played -- a tempo change
       rescales them all, and a cue can land inside the window just
       fired -- leaves the nodes where they are. */
    const double slop = 1e-9;
    const double rate = (double)controlRate();
    SectionSpan span, next;

    if (!sectionSpanAt(transportNow_ + slop, span))
        return;

    if (sectionSpanAt(transportNow_ + 1 / rate, next) &&
        next.start > span.start &&
        std::floor(next.start * rate + slop) / rate <= transportNow_ + slop)
        span = next;

    if (span.start == knobSpanStart_ && span.section == knobSpanSection_)
        return;

    const double at = std::floor(span.start * rate + slop) / rate;

    if (at > 0)
    {
        runDueTicks(at - slop);

        for (size_t i = 0; i < chains_.size(); i++)
            if (chains_[i].nodes && !replaying(i))
                chains_[i].nodes->stepToward(at - slop);
    }

    knobSpanStart_ = span.start;
    knobSpanSection_ = span.section;

    for (const auto &k : sections_[(size_t)span.section].knobs)
    {
        thArg *a = knob(k.first);

        if (a == NULL || (*a)[0] == (float)k.second)
            continue;

        knobWriteAt_ = at;
        a->setValue((float)k.second);
        knobWriteAt_ = -1;
    }
}

void
thcScheduler::endAfterSections (bool end)
{
    endAfter_ = end;
}

double
thcScheduler::sectionLength (const thcSection &s) const
{
    if (!s.beats)
        return s.length;

    return tempo_ > 0 ? s.length * 60.0 / tempo_ : 0;
}

double
thcScheduler::sectionsLength (void) const
{
    double total = 0;

    for (size_t i = 0; i < sections_.size(); i++)
        total += sectionLength(sections_[i]);

    return total;
}

int
thcScheduler::sectionAt (double at) const
{
    SectionSpan span;

    return sectionSpanAt(at, span) ? span.section : -1;
}

/* Where the arrangement is at `at': from the last cue at or before it --
 * or from the top, where none is -- walked forward as written.
 *
 * The edges get the nudge form makes at a bar line, and for the same
 * reason: a note written on the edge belongs to the section it opens,
 * not to the one it closes. A microsecond is far below anything the
 * scheduler resolves -- a window is twenty-three milliseconds -- and far
 * above the rounding of an edge summed from eight beat-valued lengths.
 *
 * Once the walk has gone round to where the list loops from, the rest is
 * periodic and is skipped in one step rather than lap by lap: a game
 * leaves a piece running for an hour, and every note asks this. */
bool
thcScheduler::sectionSpanAt (double at, SectionSpan &span) const
{
    if (sections_.empty() || sectionsLength() <= 0)
        return false;

    if (at < 0)
        at = 0;

    double start = 0;
    size_t i = 0;

    for (const auto &c : cues_)
    {
        if (c.first > at + 1e-6)
            break;

        start = c.first;
        i = (size_t)c.second;
    }

    const size_t n = sections_.size();
    const size_t from = loopFrom_ < n ? loopFrom_ : 0;
    double lap = 0;
    bool holds = false;

    for (size_t k = from; k < n; k++)
    {
        lap += sectionLength(sections_[k]);
        holds = holds || sections_[k].hold;
    }

    /* Bounded because a lap is skipped in one step below; the walk is a
       few sections either side of it. */
    for (size_t guard = 0; guard < 4 * n + 4; guard++)
    {
        const double len = sectionLength(sections_[i]);

        /* A tempo of nothing makes a section no time long, and nothing
           can be found by walking past it. */
        if (len <= 0)
            return false;

        if (at < start + len - 1e-6)
        {
            span.start = start;
            span.length = len;
            span.section = (int)i;
            return true;
        }

        if (sections_[i].hold)
        {
            start += std::floor((at - start + 1e-6) / len) * len;
            continue;
        }

        start += len;

        if (i + 1 < n)
        {
            i++;
            continue;
        }

        /* Past the last one: over, or round again. */
        if (endAfter_)
            return false;

        i = from;

        /* A hold in the lap stops the walk before the lap is over. */
        if (!holds && lap > 0 && at >= start + lap)
            start += std::floor((at - start) / lap) * lap;
    }

    return false;
}

std::vector<thcScheduler::SectionSpan>
thcScheduler::sectionSpans (double until) const
{
    std::vector<SectionSpan> out;
    SectionSpan span;
    double at = 0;

    while (at < until && sectionSpanAt(at, span))
    {
        /* A cue cuts the span it lands in short. */
        double end = span.start + span.length;

        for (const auto &c : cues_)
            if (c.first > span.start + 1e-6 && c.first < end)
            {
                end = c.first;
                break;
            }

        span.length = end - span.start;
        out.push_back(span);

        if (end <= at)
            break;

        at = end;
    }

    return out;
}

double
thcScheduler::arrangementEnd (bool asWritten) const
{
    if (!endAfter_ || sections_.empty() || sectionsLength() <= 0)
        return INFINITY;

    double start = 0;
    size_t i = 0;

    if (!cues_.empty() && !asWritten)
    {
        start = cues_.back().first;
        i = (size_t)cues_.back().second;
    }

    for (; i < sections_.size(); i++)
    {
        if (sections_[i].hold)
            return INFINITY;

        start += sectionLength(sections_[i]);
    }

    return start;
}

void
thcScheduler::loopSectionsFrom (size_t section)
{
    loopFrom_ = section;
}

double
thcScheduler::cueTime (double gridBeats) const
{
    if (gridBeats > 0)
    {
        /* The next grid line after now, counted in beats from the top,
           so a bar line is where the piece's bars are whatever the
           tempo did on the way. Strictly after: what is due now has
           been heard. */
        const double next =
            (std::floor(beat_ / gridBeats + 1e-9) + 1) * gridBeats;

        return transportNow_ + (next - beat_) * 60.0 / tempo_;
    }

    /* The end of the section that will be playing when the last cue has
       landed, so a cue behind a pending one waits for the cued section
       to finish rather than cutting into it. */
    const double from = cues_.empty()
        ? transportNow_ : std::max(transportNow_, cues_.back().first);
    SectionSpan span;

    return sectionSpanAt(from, span) ? span.start + span.length : from;
}

double
thcScheduler::cueSection (size_t section, double gridBeats)
{
    if (section >= sections_.size() || tempo_ <= 0)
        return -1;

    const double at = cueTime(gridBeats);

    /* A later cue is the one that counts: anything already cued for
       after it is taken back. */
    while (!cues_.empty() && cues_.back().first >= at - 1e-9)
        cues_.pop_back();

    /* And one long behind is forgotten, so a game cueing for an hour does
       not make every note's lookup longer. What is asked about is an
       event's own time, which is now or after it; eight seconds back is
       room for anything late. */
    while (cues_.size() >= 2 && cues_[1].first <= transportNow_ - 8)
        cues_.erase(cues_.begin());

    cues_.push_back(std::make_pair(at, (int)section));

    return at;
}

void
thcScheduler::cueOnKnob (thArg *knob, double gridBeats)
{
    cueKnob_ = knob;
    cueGrid_ = gridBeats;
    cueKnobLast_ = knob != NULL ? std::lround((*knob)[0]) : 0;
}

void
thcScheduler::pollCueKnob (void)
{
    /* While seeking too: a chain that writes the knob cues on the way, as
       it did when the piece played straight through. Compared as the
       section it names, so a nudge that still names the playing section
       cues nothing. A glide names each section it passes, but a cue aimed
       at the grid line one is already aimed at replaces it, so only where
       the knob is when a line comes round is heard. */
    if (cueKnob_ == NULL)
        return;

    const long k = std::lround((*cueKnob_)[0]);

    if (k == cueKnobLast_)
        return;

    cueKnobLast_ = k;

    if (cuesPinned_)
        return;

    if (k < 1 || (size_t)k > sections_.size() || tempo_ <= 0)
        return;

    /* Naming the section the piece would be in at the cue anyway -- the
       one playing, or the one written next -- takes back the cues still
       to come rather than starting it again mid-phrase. */
    const auto cued = cues_;

    while (!cues_.empty() && cues_.back().first > transportNow_ + 1e-9)
        cues_.pop_back();

    SectionSpan span;

    if (sectionSpanAt(cueTime(cueGrid_), span) && span.section == k - 1)
        return;

    cues_ = cued;
    cueSection((size_t)k - 1, cueGrid_);
}

void
thcScheduler::setSectionLevel (size_t section, const std::string &chain,
                               double level)
{
    if (section >= sections_.size() || !(level >= 0))
        return;

    auto &levels = sections_[section].levels;

    for (size_t i = 0; i < levels.size(); i++)
        if (levels[i].first == chain)
        {
            if (level == 1)
                levels.erase(levels.begin() + i);
            else
                levels[i].second = level;

            return;
        }

    if (level != 1)
        levels.push_back(std::make_pair(chain, level));
}

double
thcScheduler::sectionLevelOf (size_t section, const std::string &chain) const
{
    if (section >= sections_.size())
        return 1.0;

    for (const auto &l : sections_[section].levels)
        if (l.first == chain)
            return l.second;

    return 1.0;
}

double
thcScheduler::sectionLevel (const thcChain &c, double at) const
{
    if (sections_.empty())
        return 1.0;

    const int i = sectionAt(at);

    /* Past the end of a piece that ends: nothing plays. The transport
       stops itself there, but a phrase emitted before the end can land
       after it, and that phrase is not part of the piece. */
    if (i < 0)
        return endAfter_ ? 0.0 : 1.0;

    const std::vector<std::pair<std::string, double> > &levels =
        sections_[i].levels;

    for (size_t k = 0; k < levels.size(); k++)
        if (levels[k].first == c.name)
            return levels[k].second;

    return 1.0;                       /* unnamed: as written           */
}

/* The arrangement's half of the gate; propagate has the mute's, and
 * says why offs and structure edits pass both. At delivery rather than
 * at emission because a cue can move the arrangement after a chain has
 * emitted ahead: a grammar that wrote eight bars before the cue still
 * goes quiet from the bar it lands on. A level of 0 drops notes and
 * chanargs; any other scales a note's voice gain and leaves its
 * velocity, so a hat under 0.7 is quieter and exactly as open. */
bool
thcScheduler::sectionGate (thcEvent &ev, int chain)
{
    if (sections_.empty() || chain < 0 || (size_t)chain >= chains_.size() ||
        (ev.type != THC_EV_NOTE && ev.type != THC_EV_CHANARG))
        return true;

    const double level = sectionLevel(chains_[chain], ev.at);

    if (level == 1.0)
        return true;

    if (level <= 0)
    {
        if (!seeking_ && !catchingUp_)
            chains_[chain].lastGated[ev.type == THC_EV_CHANARG] =
                std::max(transportNow_, ev.at);

        return false;
    }

    if (ev.type == THC_EV_NOTE)
        ev.u.note.level *= (float)level;

    return true;
}

void
thcScheduler::setMasterSeed (unsigned seed)
{
    /* See the header: instances already exist with seeds derived from
       the old value, and pretending otherwise would make "same seed,
       same piece" false. */
    if (!chains_.empty())
        return;

    masterSeed_ = seed;
}

/* The most transport one timer tick moves, against the 20 ms it is
   meant to. */
static const double MAX_TIMER_STEP_S = 0.25;

/* Integrate time rather than derive it, so pause and tempo changes are
 * both trivially correct: seconds and beats just stop or change slope. */
void
thcScheduler::tick (void)
{
    int64_t mono = thcMonotonicUs();

    /* A main loop that stalled -- a reload, a dialog, a sanitizer -- is
       waited out rather than made up for. Composing the seconds it missed
       in one step plays them all at once, and a piece whose steps cost
       more than the time they cover never catches up again. */
    if (running_)
        stepTransportAt(std::min((mono - lastMono_) / 1e6, MAX_TIMER_STEP_S),
                        mono);
    else
        sendDueNoteOffs(transportNow_);   /* offs drain even when paused */

    /* Here as well as in stepTransport, because a channel that would not
       go is exactly as stuck on a paused transport as on a running one,
       and this timer keeps firing either way. */
    retireStranded();

    lastMono_ = mono;
}

/* One transport step, however time got measured -- the real timer above,
 * or a harness's virtual clock. Everything the step does is keyed in
 * transport seconds, so the outcome depends on dt and nothing else. */
void
thcScheduler::stepTransport (double dt)
{
    /* Before the early return, so a harness -- which has no timer
       and reaches the scheduler only through here -- still gets the
       retry it would otherwise never see. */
    retireStranded();
    recomposeMarked();

    if (!running_ || dt < 0)
        return;

    transportNow_ += dt;
    beat_ = beatAtTempo_ + (transportNow_ - tempoAt_) * tempo_ / 60.0;

    clockTicks();
    runStep();
}

void
thcScheduler::stepTransportAt (double dt, int64_t mono)
{
    stepMono_ = mono;
    stepTransport(dt);
    stepMono_ = 0;
}

void
thcScheduler::stepTransportTo (double t)
{
    retireStranded();
    recomposeMarked();

    if (!running_ || t < transportNow_)
        return;

    transportNow_ = t;
    beat_ = beatAtTempo_ + (transportNow_ - tempoAt_) * tempo_ / 60.0;

    clockTicks();
    runStep();
}

void
thcScheduler::runStep (void)
{
    /* The stages first, each at the time it asked to be woken at, with
     * its chain's nodes moved to that time before it reads them --
     * runDueTicks does both, and says why.
     *
     * Then the nodes are brought up to the end of the step. On transport
     * time, because that is what makes a replay a replay: how far an LFO
     * has travelled is a function of where the transport is, not of how
     * many times this was called or how late a frame was. */
    takeHeardGrowth();

    /* Before the stages due from the section's start tick, so one woken
       at its first beat reads what the section says. */
    applySectionKnobs();
    runDueTicks(transportNow_);

    /* Not a chain being replayed, whose nodes are where its replay has
       got to. */
    for (size_t i = 0; i < chains_.size(); i++)
        if (chains_[i].nodes && !replaying(i))
        {
            chains_[i].nodes->stepTo(transportNow_);

            /* And wake anything that fell asleep behind one of them. A
               generator that returned THC_NEVER is waiting for a param
               to change, and a param reading a node has just changed if
               the node moved. Without this a gen::morph whose `mode' is
               node-driven slept through the whole of its own sweep. */
            for (size_t si = 0; si < chains_[i].stages.size(); si++)
                chains_[i].stages[si]->params.pollNodes();
        }

    /* Offs due at this instant before the ons due at it, then the offs
     * this step derived itself.
     *
     * A note-off falling due at exactly `now' belongs to a note that
     * started before it, so it has to reach the synth ahead of any on
     * at the same instant: thMidiChan keys its voices by pitch, and
     * releaseNote() silences whatever is sounding at that pitch *now*
     * rather than the voice the off was written for. A note split into
     * back-to-back halves puts an off and the next on at exactly the
     * same instant by construction -- xform::vary's `double', and
     * xform::ratchet's whole subdivision -- and with the ons first the
     * off released the half that had only just begun. What sounded was
     * one note and a release stub, on a tape that says two.
     *
     * The second call is for the offs deliverDue() has just pushed: a
     * note whose duration is shorter than the step is already over by
     * the end of it, and the first call could not have seen an off that
     * did not exist yet. */
    sendDueNoteOffs(transportNow_);
    deliverDue(transportNow_);
    sendDueNoteOffs(transportNow_);

    /* After the deliveries, so a chain that writes the cue knob in this
       step is heard cueing in it. */
    pollCueKnob();

    /* `section end;': the piece is over when its last section is, and
     * the transport says so itself rather than waiting to be paused.
     *
     * stop() is exactly what is wanted here -- it flushes the offs for
     * whatever is still sounding, so the releases ring out and a
     * renderer keeps rendering until they have. What it does not do is
     * rewind: the transport stays where the piece ended, and Play from
     * there plays nothing until a rewind, which is what "the piece is
     * over" means. */
    if (endAfter_ && !sections_.empty() &&
        transportNow_ >= arrangementEnd())
        stop();
}

/* How many times one stage may ask to be woken at a time that has already
   passed, inside a single step, before it is held to the step. A stage
   that asks for a future time -- every stage in the tree does -- never
   reaches this. It bounds the one that returns its own wake time for
   ever, which the 1 ms nudge below would otherwise spin at a thousand
   wakes per transport second. */
static const unsigned TH_MAX_STALLED_WAKES = 8;

/* The stages whose time has come, each ticked at the time it asked for.
 *
 * `now' is where the transport got to; a wake at or before it runs with the
 * transport reading the wake's own time, not `now'. Composers schedule from
 * what they are handed -- thirteen of the sixteen in the tree return
 * `t->now + period' -- so handing them the end of the step made every wake
 * late by up to a step, and the next was scheduled from the late one, so
 * the lateness compounded. What a piece composed was then a function of how
 * often the host called this: the same file and the same seed gave one
 * piece at a 1024-frame step and another at 256, and two machines with
 * different sound cards could not agree on a piece at all -- which is the
 * finding that had to be fixed before a room full of peers could compose
 * the same piece. docs/JAM.md has the design.
 *
 * The chain's nodes move to the wake's time before the stage reads them,
 * for the same reason and to the same end: a param reading `lfo->out'
 * sees the lfo where the stage is rather than where the step ended.
 * stepTransport brings them up to the end of the step afterwards.
 */
void
thcScheduler::runDueTicks (double now)
{
    thcTransport t = { now, tempo_, beat_, running_,
                       usesBeats() ? meter_ : 0 };
    const double beatAtNow = beat_;

    for (size_t i = 0; i < chains_.size(); i++)
        for (size_t si = 0; si < chains_[i].stages.size(); si++)
            chains_[i].stages[si]->stalled = 0;

    while (!wakeups_.empty() && wakeups_.front().at <= now)
    {
        std::pop_heap(wakeups_.begin(), wakeups_.end(), Later());
        Wakeup w = wakeups_.back();
        wakeups_.pop_back();

        /* What is due by this wake, delivered before it -- runStep's
         * three calls, at the wake's time rather than the step's end.
         *
         * A transformer can emit into the past: xform::humanize moves a
         * note up to its `time' earlier than the tick that made it. Such a
         * note is late whenever it is delivered, and with every delivery
         * held to the end of the step, when that was depended on where
         * the step ended -- before or after the notes of a chain whose
         * wake fell between the two, so the tape's order was a function
         * of the step size wherever two chains tick out of phase. Here a
         * note emitted into the past is delivered after the tick that
         * made it and before the next one, on every peer, however the
         * transport is stepped. */
        sendDueNoteOffs(w.at);
        deliverDue(w.at);
        sendDueNoteOffs(w.at);

        thcChain &c = chains_[w.chain];
        thcStage *s = c.stages[w.stage].get();
        s->awaitingStart = false;

        /* The transport as this stage sees it: its own wake, and the beat
           that time falls on. */
        t.now = w.at;
        t.beat = beatAtNow - (now - w.at) * tempo_ / 60.0;

        if (c.nodes)
            c.nodes->stepTo(w.at);

        /* The sink each stage emits into continues down its own chain. */
        struct Ctx { thcScheduler *self; size_t chain, stage; } ctx =
            { this, w.chain, w.stage };
        thcEventSink sink = { &ctx, [](void *p, const thcEvent *ev) {
            Ctx *c = static_cast<Ctx *>(p);
            c->self->propagate(c->self->chains_[c->chain],
                               c->stage + 1, *ev);
        }};

        double next = s->plugin->tick(s->state, &t, &sink);

        /* Contract enforcement: a composer returning the past would spin
           this loop forever; a THC_NEVER sleeper re-arms only through
           param_changed (the param store calls rearmStage for us). */
        if (next == THC_NEVER)
        {
            s->sleeping = true;
            continue;
        }

        double at = next;

        if (next > t.now)
            s->stalled = 0;
        else
        {
            /* Not in the future. Nudged from the stage's own clock rather
               than from the end of the step, so that a stage which does
               it once is still where the piece says it is -- and held to
               the step past the cap, so that one which does it every time
               costs a bounded amount of a bounded step instead of the
               loop. */
            at = t.now + 0.001;

            if (++s->stalled > TH_MAX_STALLED_WAKES)
            {
                if (s->stalled == TH_MAX_STALLED_WAKES + 1)
                    fprintf(stderr, "thcScheduler: chain %zu stage %zu keeps "
                            "asking to be woken in the past; holding it to "
                            "the step\n",
                            (size_t)(w.chain + 1), (size_t)(w.stage + 1));

                if (at <= now)
                    at = now + 0.001;
            }
        }

        wakeups_.push_back({ at, w.chain, w.stage, heapSeq_++ });
        std::push_heap(wakeups_.begin(), wakeups_.end(), Later());
    }
}

/* A chain built again while the piece plays, played from its start up to
 * now without a sound, so that from now on it plays what it would have had
 * it been the piece from the top: an arpeggio comes in on its beat and a
 * phrase in its bar, rather than wherever the edit landed. What it composes
 * for before now is dropped (queuePending), and what for after is queued as
 * any tick's is. Its stages' wakes are where a load put them.
 */
void
thcScheduler::catchUp (size_t chain)
{
    dropReplays(chain);

    thcChain &c = chains_[chain];
    Replay r;
    std::vector<Wakeup> rest;

    r.chain = chain;
    r.now0 = transportNow_;
    r.beat0 = beat_;
    r.tempo0 = tempo_;
    r.audible = audible(c);

    for (const Wakeup &w : wakeups_)
        (w.chain == chain ? r.due : rest).push_back(w);

    /* From the chain's start as a load computes it: in beats, on the
       beat grid the piece is on now, whatever tempo it got there at. */
    const double start = c.startBeats
        ? transportNow_ - (beat_ - c.start) * 60.0 / tempo_ : c.start;

    /* Its nodes from the top too, each stage reading them where they
       were at its wake: as they were kept, if they were, or fired again. */
    r.kept = c.nodes && c.nodes->recorded();

    if (c.nodes && !r.kept)
        c.nodes->reset();

    if (r.due.empty() || start >= transportNow_)
    {
        if (r.kept)
            c.nodes->showNow();

        if (c.nodes)
            c.nodes->stepTo(transportNow_, false);

        return;
    }

    wakeups_.swap(rest);
    std::make_heap(wakeups_.begin(), wakeups_.end(), Later());

    for (Wakeup &w : r.due)
        if (c.stages[w.stage]->awaitingStart)
            w.at = start;

    std::make_heap(r.due.begin(), r.due.end(), Later());

    const bool whole = wholeReplay(chain);

    if (!whole)
    {
        r.held = true;

        for (auto &st : c.stages)
            st->params.hold();

        if (c.nodes)
            c.nodes->holdKnobs();
    }

    replays_.push_back(std::move(r));

    if (whole)
    {
        unsigned all = ~0u;

        replayRun(replays_.size() - 1, all);
        landReplay(replays_.back());
        replays_.pop_back();
    }
}

/* The replays under way given up -- every one, or `chain's -- with
   their stores and nodes reading live again. */
void
thcScheduler::dropReplays (long chain)
{
    for (size_t i = replays_.size(); i-- > 0; )
    {
        Replay &r = replays_[i];

        if (chain >= 0 && r.chain != (size_t)chain)
            continue;

        if (r.held)
        {
            for (auto &st : chains_[r.chain].stages)
                st->params.release();

            if (chains_[r.chain].nodes)
                chains_[r.chain].nodes->releaseKnobs();
        }

        replays_.erase(replays_.begin() + (long)i);
    }
}

/* Played through in the call that asked for it. On a stopped transport
   too: a replay in pieces moves only while it runs, and would come in at
   each peer's own window once a room started. */
bool
thcScheduler::wholeReplay (size_t chain) const
{
    return !replayDeferred_ || replayWhole_ || !running_ ||
           wholeMarked_.count(chain) > 0;
}

bool
thcScheduler::replaying (size_t chain) const
{
    return std::any_of(replays_.begin(), replays_.end(),
                       [chain](const Replay &r) { return r.chain == chain; });
}

bool
thcScheduler::replayAdvance (unsigned ticks)
{
    bool caught = true;

    for (size_t i = 0; i < replays_.size(); i++)
        caught = replayRun(i, ticks) && caught;

    return caught;
}

void
thcScheduler::replayLand (void)
{
    for (size_t i = 0; i < replays_.size(); i++)
    {
        unsigned all = ~0u;

        replayRun(i, all);
        landReplay(replays_[i]);
    }

    replays_.clear();
}

bool
thcScheduler::replayRun (size_t i, unsigned &budget)
{
    Replay &r = replays_[i];
    const size_t chain = r.chain;
    thcChain &c = chains_[chain];
    std::vector<Wakeup> &due = r.due;
    const bool kept = r.kept;

    if (r.bailed)
        return true;

    catchingUp_ = true;
    replay_ = &r;

    while (budget > 0 && !due.empty() && due.front().at < transportNow_)
    {
        budget--;

        std::pop_heap(due.begin(), due.end(), Later());
        Wakeup w = due.back();
        due.pop_back();

        thcStage *s = c.stages[w.stage].get();

        /* The beat it falls on, on the tempo as it was when the replay
           began. */
        const thcTransport t = {
            w.at, r.tempo0, r.beat0 - (r.now0 - w.at) * r.tempo0 / 60.0,
            true, usesBeats() ? meter_ : 0 };

        s->awaitingStart = false;

        if (c.nodes)
        {
            if (kept)
                c.nodes->showAt(w.at);
            else
                c.nodes->stepTo(w.at, false);

            /* And a sleeper behind a node that moved woken, as runStep's
               poll wakes it. */
            for (size_t si = 0; si < c.stages.size(); si++)
            {
                thcStage *o = c.stages[si].get();

                if (o->params.nodesMoved() && o->sleeping && o->ticks &&
                    !o->awaitingStart)
                {
                    o->sleeping = false;
                    due.push_back({ w.at, chain, si, heapSeq_++ });
                    std::push_heap(due.begin(), due.end(), Later());
                }
            }
        }

        struct Ctx { thcScheduler *self; size_t chain, stage; } ctx =
            { this, chain, w.stage };
        thcEventSink sink = { &ctx, [](void *p, const thcEvent *ev) {
            Ctx *x = static_cast<Ctx *>(p);
            x->self->propagate(x->self->chains_[x->chain], x->stage + 1,
                               *ev);
        }};

        const double next = s->plugin->tick(s->state, &t, &sink);

        /* What it would have heard cannot be heard again: it starts from
           where it lands instead, as made, with nothing it composed
           here. */
        if (s->unwritten)
        {
            r.bailed = true;
            r.heard.clear();
            break;
        }

        if (next == THC_NEVER)
        {
            s->sleeping = true;
            continue;
        }

        /* Held to where it began past runDueTicks's cap, as that holds one
           to a step. */
        double at = next;

        if (next > w.at)
            s->stalled = 0;
        else
            at = ++s->stalled > TH_MAX_STALLED_WAKES
                 ? std::max(r.now0, w.at + 0.001) : w.at + 0.001;

        due.push_back({ at, chain, w.stage, heapSeq_++ });
        std::push_heap(due.begin(), due.end(), Later());
    }

    catchingUp_ = false;
    replay_ = NULL;

    return r.bailed || due.empty() || due.front().at >= transportNow_;
}

/* What a recomposed chain had queued, which played on while it replayed. */
void
thcScheduler::dropOldSounds (const Replay &r)
{
    if (!r.dropOld)
        return;

    const int chain = (int)r.chain;
    const unsigned long before = r.oldBefore;

    pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                       [chain, before](const Pending &p)
                       { return p.chain == chain && p.seq < before &&
                                (p.ev.type == THC_EV_NOTE ||
                                 p.ev.type == THC_EV_CHANARG); }),
                   pending_.end());
    std::make_heap(pending_.begin(), pending_.end(), LaterPending());
}

/* Heard from now: what it composed queued as queuePending queues it, its
   wakes back among the rest, and its stores and nodes reading live. */
void
thcScheduler::landReplay (Replay &r)
{
    const size_t chain = r.chain;
    thcChain &c = chains_[chain];

    if (r.held)
    {
        for (auto &st : c.stages)
            st->params.release();

        if (c.nodes)
            c.nodes->releaseKnobs();
    }

    dropOldSounds(r);

    if (r.bailed)
    {
        for (size_t si = 0; si < c.stages.size(); si++)
            remake(chain, si);

        if (r.kept)
            c.nodes->showNow();

        if (c.nodes)
            c.nodes->stepTo(transportNow_, false);

        return;
    }

    if (r.kept)
        c.nodes->showNow();

    if (c.nodes)
        c.nodes->stepTo(transportNow_, false);

    /* A note before now would never have been heard, but a value or a
       structure edit is where the chain left its target, as a seek leaves
       it -- so the last for each target is delivered now. */
    std::map<std::string, Pending> caught;

    for (Pending &p : r.heard)
    {
        if (p.at >= transportNow_)
        {
            p.seq = pendingSeq_++;
            pending_.push_back(p);
            std::push_heap(pending_.begin(), pending_.end(), LaterPending());
            continue;
        }

        if (p.ev.type == THC_EV_NOTE || p.ev.type == THC_EV_NOTEOFF)
            continue;

        if (!sectionGate(p.ev, p.chain))
            continue;

        char key[64];

        snprintf(key, sizeof(key), "%d %d %p ", (int)p.ev.type, p.ev.channel,
                 (void *)p.knob);

        std::string k = key;

        if (p.chanargName)
            k += *p.chanargName;
        else if (p.ev.type == THC_EV_NODEARG)
            k += *p.text + "->" + *p.text2;
        else if (p.ev.type == THC_EV_GROW)
            k += std::to_string(pendingSeq_);

        p.seq = pendingSeq_++;
        caught[k] = p;
    }

    std::vector<Pending> last;

    for (auto &e : caught)
        last.push_back(e.second);

    std::sort(last.begin(), last.end(), [](const Pending &a,
                                           const Pending &b)
              { return a.seq < b.seq; });

    for (Pending &p : last)
    {
        p.at = p.ev.at = transportNow_;
        pending_.push_back(p);
        std::push_heap(pending_.begin(), pending_.end(), LaterPending());
    }

    /* Numbered afresh, in the order they are due: what they were given
       on the way depends on what else was woken meanwhile. */
    std::sort(r.due.begin(), r.due.end(), [](const Wakeup &a,
                                             const Wakeup &b)
              { return Later()(b, a); });

    for (Wakeup w : r.due)
    {
        w.seq = heapSeq_++;
        wakeups_.push_back(w);
        std::push_heap(wakeups_.begin(), wakeups_.end(), Later());
    }
}

void
thcScheduler::rearmStage (size_t chain, size_t stage)
{
    if (chain >= chains_.size() || stage >= chains_[chain].stages.size())
        return;

    thcStage *s = chains_[chain].stages[stage].get();

    if (!s->sleeping || !s->ticks || s->awaitingStart)
        return;

    s->sleeping = false;
    wakeups_.push_back({ knobWriteAt_ >= 0 ? knobWriteAt_ : transportNow_,
                         chain, stage, heapSeq_++ });
    std::push_heap(wakeups_.begin(), wakeups_.end(), Later());
}

/* Synchronous walk through the rest of the chain. Each transformer gets
 * a sink that continues from the stage after itself, so echoes and fans
 * flow strictly downstream -- no cycles are constructible. Whatever a
 * muted chain produces is dropped at the end, not suppressed at the
 * start: the algorithm keeps evolving silently, which is what you want
 * when un-muting mid-piece. */
void
thcScheduler::propagate (thcChain &c, size_t fromStage, const thcEvent &in)
{
    /* A note's level is a field every emitter has to fill, and not every
     * emitter does: a plugin built against the header before the field
     * existed, a `thcEvent ev = {}' in a generator nobody revisited, a
     * key pressed on a page. All of those mean "as emitted", and a zero
     * that reached a voice would be a note composed and not heard, which
     * is the failure the browser gate caught. So zero is read as one at
     * the one door every event comes through. Nothing legitimate emits a
     * zero: a section at 0 drops the note above, and xform::level at
     * gain 0 drops it too, for exactly this reason. */
    thcEvent lifted;
    const thcEvent *at = &in;

    if (in.type == THC_EV_NOTE && !(in.u.note.level > 0))
    {
        lifted = in;
        lifted.u.note.level = 1;
        at = &lifted;
    }

    const thcEvent &ev = *at;

    /* What the composer view flashes: the stage this came out of, and
       the one it is going into. Not a stage the event only passed
       through -- a generator placed after another has no receive, and
       what goes past it is not its doing. */
    const bool passed = passingThrough_;

    passingThrough_ = false;

    if (ev.type != THC_EV_NOTEOFF && !seeking_ && !catchingUp_)
    {
        if (fromStage > 0 && fromStage <= c.stages.size() && !passed)
            c.stages[fromStage - 1]->lastOut = transportNow_;

        if (fromStage < c.stages.size() &&
            c.stages[fromStage]->plugin->hasReceive())
            c.stages[fromStage]->lastIn = transportNow_;
    }

    /* And what a probe on an arrow draws: every note that went along it,
       whoever made it -- one that only passed through the stage before
       it included -- and every note that went into a stage that takes
       them, eight seconds of each. A held note's release gives it its
       length. */
    if (!seeking_ && (ev.type == THC_EV_NOTE || ev.type == THC_EV_NOTEOFF) &&
        !(catchingUp_ && ev.at < transportNow_))
    {
        auto keep = [&](std::deque<thcPlayed> &d)
        {
            if (ev.type == THC_EV_NOTEOFF)
            {
                for (size_t i = d.size(); i-- > 0; )
                    if (d[i].note == ev.u.note.note && d[i].duration <= 0)
                    {
                        d[i].duration = std::max(ev.at - d[i].at, 0.0);
                        break;
                    }

                return;
            }

            d.push_back({ ev.at, ev.u.note.duration, ev.u.note.note,
                          ev.u.note.velocity });

            while (!d.empty() && (d.front().at < transportNow_ - 8 ||
                                  d.size() > 512))
                d.pop_front();
        };

        if (fromStage > 0 && fromStage <= c.stages.size())
            keep(c.stages[fromStage - 1]->out);

        if (fromStage < c.stages.size() &&
            c.stages[fromStage]->plugin->hasReceive())
            keep(c.stages[fromStage]->in);
    }

    const int chainIndex = (int)(&c - &chains_[0]);

    if (fromStage >= c.stages.size())
    {
        /* The live mute and solo. A chain they silence is silent
         * whatever the section says; the section itself is applied when
         * the event is delivered (sectionGate), because a cue can move
         * the arrangement after a chain has emitted ahead.
         *
         * A muted chain's notes and its chanargs are both dropped, so a
         * walk or a gate driving a knob stops pushing and the knob keeps
         * the value it had.
         *
         * Two kinds of event go through whatever the level says, and
         * neither of them is sound. A NOTEOFF: a swallowed off hangs a
         * voice for the rest of the piece, while an off for a note
         * nobody holds is a no-op releaseHeld is already written to
         * cope with -- only one of the two is a bug. A key held on an
         * `input midi' chain that is muted before it is let go is the
         * live case. And a structure edit: a swap or a node-arg edit is
         * the piece rebuilding itself, and one dropped leaves a channel
         * holding a graph the piece has moved on from, with no later
         * event to catch it up.
         */
        const thcEvent *gated = &ev;

        /* A replay hears the chain as it was heard when it began. */
        const bool heard = replay_ != NULL && &c == &chains_[replay_->chain]
                           ? replay_->audible : audible(c);

        if (!heard && ev.type != THC_EV_NOTEOFF &&
            !isStructureEdit(ev.type))
        {
            /* At the event's own time, which is when it would have been
               heard: a chain that emits ahead is dropped ahead too, and
               would light before the notes it still plays. */
            if ((ev.type == THC_EV_NOTE || ev.type == THC_EV_CHANARG) &&
                !seeking_ && !catchingUp_)
                c.lastGated[ev.type == THC_EV_CHANARG] =
                    std::max(transportNow_, ev.at);

            return;
        }

        /* No sinks: the programmatic-chain case; deliver as emitted. */
        if (c.sinks.empty())
        {
            queuePending(*gated, NULL, chainIndex);
            return;
        }

        /* A value that names its knob needs no sink to name one, so an
           instrument's own sink takes it beside the notes: one sink for
           a stage that plays and rides a knob at once. */
        const bool namesItself = ev.type == THC_EV_CHANARG &&
                                 ev.u.chanarg.name && *ev.u.chanarg.name;

        /* Sinks route and type-filter: notes to note sinks, chanargs to
           chanarg sinks, each on the sink's channel. Multiple matches is
           fan-out. The event's own channel is overwritten -- routing
           belongs to the piece, not to the plugin, which is why eno_line
           no longer has a channel param. */
        for (size_t i = 0; i < c.sinks.size(); i++)
        {
            const thcSink &sink = c.sinks[i];

            /* A knob sink takes values and nothing else: a note has no
               number to set it to, and a structure edit has no channel
               here to reshape. */
            if (sink.isKnob())
            {
                if (ev.type == THC_EV_CHANARG)
                    queuePending(*gated, NULL, chainIndex, sink.knob);
                continue;
            }

            /* The type filter, which is a rule about notes and chanargs
               and says nothing about a structure edit -- a swap is
               neither, and both kinds of sink name the channel it needs.
               So an edit goes to every sink, and fan-out means what
               fan-out means everywhere else: a chain with three sinks
               reshapes three channels. */
            if (!isStructureEdit(ev.type) &&
                (ev.type == THC_EV_CHANARG) != sink.isChanarg() &&
                !(namesItself && !sink.isChanarg()))
                continue;

            thcEvent routed = *gated;

            routed.channel = sink.channel;

            /* NULL means "keep what the event carries" -- which is what a
               note sink wants and also what a `*' chanarg sink wants, for
               opposite reasons: the note has no name to keep, and the
               vector's component has one worth keeping. */
            queuePending(routed,
                         (sink.isChanarg() && !sink.namesItsOwn())
                             ? &sink.chanarg : NULL,
                         chainIndex);
        }
        return;
    }

    thcStage *s = c.stages[fromStage].get();

    if (!s->plugin->hasReceive())               /* pass-through          */
    {
        passingThrough_ = true;
        propagate(c, fromStage + 1, ev);
        return;
    }

    struct Ctx { thcScheduler *self; thcChain *chain; size_t stage; } ctx =
        { this, &c, fromStage };
    thcEventSink sink = { &ctx, [](void *p, const thcEvent *e) {
        Ctx *c = static_cast<Ctx *>(p);
        c->self->propagate(*c->chain, c->stage + 1, *e);
    }};

    s->plugin->receive(s->state, &ev, &sink);
}

/* The sink's copy of a chanarg name, promised by the ABI: the composer
 * may rewrite its own string on the very next tick, and a chanarg sink
 * names the target itself (the plugin that emitted the value does not
 * know or care which patch knob it lands on). */
void
thcScheduler::queuePending (const thcEvent &ev,
                            const std::string *nameOverride, int chain,
                            thArg *knob)
{
    Pending p;

    p.at = ev.at;
    p.ev = ev;
    p.chain = chain;
    p.knob = knob;

    if (ev.type == THC_EV_CHANARG)
    {
        const char *from = nameOverride ? nameOverride->c_str()
                                        : ev.u.chanarg.name;

        p.chanargName.reset(new std::string(from ? from : ""));
        p.ev.u.chanarg.name = p.chanargName->c_str();
    }
    else if (ev.type == THC_EV_PATCH)
    {
        /* Copied for the reason the chanarg name is: the ABI says a
           sink copies what it keeps, and the composer that emitted this
           is entitled to reuse its buffer the moment emit() returns.
           An event sitting in pending_ for thirty seconds holding the
           plugin's pointer is a dangling read waiting for a param
           change. */
        p.text.reset(new std::string(ev.u.patch.name ? ev.u.patch.name
                                                     : ""));
        p.ev.u.patch.name = p.text->c_str();
    }
    else if (ev.type == THC_EV_GROW)
    {
        p.text.reset(new std::string(ev.u.grow.kinds ? ev.u.grow.kinds
                                                     : ""));
        p.ev.u.grow.kinds = p.text->c_str();
        p.text2.reset(new std::string(ev.u.grow.target ? ev.u.grow.target
                                                       : ""));
        p.ev.u.grow.target = p.text2->c_str();
    }
    else if (ev.type == THC_EV_NODEARG)
    {
        p.text.reset(new std::string(ev.u.nodearg.node
                                     ? ev.u.nodearg.node : ""));
        p.text2.reset(new std::string(ev.u.nodearg.arg
                                      ? ev.u.nodearg.arg : ""));
        p.ev.u.nodearg.node = p.text->c_str();
        p.ev.u.nodearg.arg = p.text2->c_str();
    }

    /* A chain being played up to now: held until it has caught up, and
       queued then (replayRun). */
    if (replay_ != NULL)
    {
        replay_->heard.push_back(p);
        return;
    }

    /* Live input on a paused clock: pending_ is keyed in transport
       time and transport time is frozen, so anything due now would
       wait for Play. It should not -- the keys were pressed now. */
    if (injectingLive_ && p.at <= transportNow_)
    {
        if (!sectionGate(p.ev, chain))
            return;

        if (knob != NULL)
            setKnobFrom(knob, p.ev, chain);
        else
            deliverFrom(p.ev, chain);
        return;
    }

    p.seq = pendingSeq_++;

    pending_.push_back(p);
    std::push_heap(pending_.begin(), pending_.end(), LaterPending());
}

void
thcScheduler::deliverDue (double now)
{
    while (!pending_.empty() && pending_.front().at <= now)
    {
        std::pop_heap(pending_.begin(), pending_.end(), LaterPending());
        Pending p = pending_.back();
        pending_.pop_back();

        if (!sectionGate(p.ev, p.chain))
            continue;

        if (p.knob != NULL)
            setKnobFrom(p.knob, p.ev, p.chain);
        else
            deliverFrom(p.ev, p.chain);
    }
}

/* As written: a knob's .min and .max are its slider's travel, not a bound
 * on the piece. A seek sets it too: where a knob stands is where the piece
 * is. Nothing holds a hand off, so a drag stays until the chain's next
 * value, as a chanarg under gen::pump does. */
void
thcScheduler::setKnobFrom (thArg *knob, const thcEvent &ev, int chain)
{
    const float v = ev.u.chanarg.value;

    if (!seeking_ && chain >= 0 && (size_t)chain < chains_.size())
        chains_[chain].lastHeard[1] = transportNow_;

    if ((*knob)[0] != v)
    {
        knobWriteAt_ = ev.at;
        knob->setValue(v);
        knobWriteAt_ = -1;
    }
}

void
thcScheduler::deliverFrom (const thcEvent &ev, int chain)
{
    if (seeking_)
    {
        deliver(ev);
        return;
    }

    if (chain >= 0 && (size_t)chain < chains_.size() &&
        (ev.type == THC_EV_NOTE || ev.type == THC_EV_CHANARG))
        chains_[chain].lastHeard[ev.type == THC_EV_CHANARG] = transportNow_;

    /* And what it played, for a freeze: a minute or so of it, which is
       more bars than a pattern holds at any tempo anybody plays. */
    if (chain >= 0 && (size_t)chain < chains_.size() &&
        ev.type == THC_EV_NOTE)
    {
        static const double PLAYED_KEEP = 90;
        std::deque<thcPlayed> &played = chains_[chain].played;

        played.push_back({ ev.at, ev.u.note.duration, ev.u.note.note,
                           ev.u.note.velocity });

        while (!played.empty() &&
               (played.front().at < ev.at - PLAYED_KEEP ||
                played.size() > 4096))
            played.pop_front();
    }

    /* A held note's release, which is where its length is known. */
    if (chain >= 0 && (size_t)chain < chains_.size() &&
        ev.type == THC_EV_NOTEOFF)
    {
        std::deque<thcPlayed> &played = chains_[chain].played;

        for (size_t i = played.size(); i-- > 0; )
            if (played[i].note == ev.u.note.note && played[i].duration <= 0)
            {
                played[i].duration = std::max(ev.at - played[i].at, 0.0);
                break;
            }
    }

    deliveringChain_ = chain;
    deliver(ev);
    deliveringChain_ = -1;
}

/* The only place the framework touches the synth, and it touches it
 * exactly the way the on-screen keyboard does: build on the GUI thread,
 * enqueue, let process() apply it. Note-offs are derived here, so no
 * composer ever tracks a hanging note.
 *
 * Velocity goes through raw, 1-127: that is what dispatchmidi passes
 * from the wire and what the Keyboard widget passes from its rows, so it
 * is what addNote means. */
void
thcScheduler::deliver (const thcEvent &ev)
{
    /* A seek plays the piece up to where it is going without a sound:
       the notes are not heard, but what an event leaves behind -- a
       chanarg's value, a swapped instrument -- is where the piece is. */
    if (seeking_ && (ev.type == THC_EV_NOTE || ev.type == THC_EV_NOTEOFF))
        return;

    switch (ev.type)
    {
        case THC_EV_NOTE:
        {
            bool sounded = true;

            if (playsOverMidi(ev.channel))
                midiOut_->noteOn(ev.channel, ev.u.note.note,
                                 ev.u.note.velocity, ev.u.note.level,
                                 stampAt(ev.at));
            else
                sounded = synth_->addNote(ev.channel, ev.u.note.note,
                                          ev.u.note.velocity,
                                          ev.u.note.level, ev.u.note.aux,
                                          ev.u.note.say);

            /* A channel keys its voices by note number, so this note has
               just put the one sounding on its key into release
               (thMidiChan::insertNote), and an off still pending for
               that key would find this note under it instead: the off
               for a note 0-3 s retriggered at 1 s ended the retrigger at
               3 s rather than 4. Those offs have nothing left to end.
               Kept where addNote failed, since the old voice is then
               still keyed and still needs its off. */
            const double doubled =
                sounded ? dropNoteOffs(ev.channel, ev.u.note.note, ev.at)
                        : -HUGE_VAL;

            /* A composed note carries its whole life in the duration;
               the off lands exactly there, keyed off the event's own
               time so a replay derives an identical off stream. A held
               note (duration <= 0, live input's spelling of "who
               knows") waits for its NOTEOFF instead.

               Two notes struck on one key at one instant -- two chains
               doubling a hit -- are one voice however they arrive, and
               it lasts as long as the longer of them, not as long as
               whichever chain happened to deliver second. */
            if (ev.u.note.duration > 0)
            {
                noteOffs_.push_back({ std::max(ev.at + ev.u.note.duration,
                                               doubled),
                                      ev.channel, ev.u.note.note,
                                      heapSeq_++, ev.at });
                std::push_heap(noteOffs_.begin(), noteOffs_.end(),
                               Later());
            }
            else
                held_.push_back({ 0, ev.channel, ev.u.note.note, 0, 0,
                                  deliveringChain_ });
            break;
        }
        case THC_EV_NOTEOFF:
        {
            releaseHeld(ev.channel, ev.u.note.note, ev.at);
            break;
        }
        case THC_EV_CHANARG:
        {
            if (playsOverMidi(ev.channel))
            {
                /* A seek plays minutes of a piece in an instant; the
                   device hears where each controller ended up, once,
                   when it is over -- not every value on the way. */
                if (ev.u.chanarg.name == NULL)
                    ;
                else if (seeking_)
                    seekControls_[std::make_pair(ev.channel,
                                                 std::string(
                                                     ev.u.chanarg.name))] =
                        ev.u.chanarg.value;
                else
                    midiOut_->control(ev.channel, ev.u.chanarg.name,
                                      ev.u.chanarg.value, stampAt(ev.at));
                break;
            }

            /* The route the sliders use: a single-float setValue is safe
               from the GUI thread, and a chanarg the patch does not
               declare simply is not there to set. */
            thArg *arg = synth_->getChanArg(ev.channel, ev.u.chanarg.name);

            if (arg != NULL)
                arg->setValue(ev.u.chanarg.value);
            break;
        }
        case THC_EV_PATCH:
        {
            std::string why;

            if (ev.u.patch.name != NULL &&
                !swapInstrument(ev.channel, ev.u.patch.name, why))
                fprintf(stderr, "thcScheduler: swap to '%s' on channel %d: "
                        "%s\n", ev.u.patch.name, ev.channel + 1,
                        why.c_str());
            break;
        }
        case THC_EV_GROW:
        {
            std::string why;
            const auto g = grown_.find(ev.channel);

            /* A composer's step waits for one the ear or the mirror has
               yet to answer, so every host takes the same steps in the
               same order and the stage's count of them stays true. */
            if (g != grown_.end() &&
                (g->second.asked != 0 || !g->second.options.empty()))
            {
                Growth::Queued q;

                q.shrink = ev.u.grow.shrink != 0;
                q.choice = ev.u.grow.choice;
                q.kinds = ev.u.grow.kinds ? ev.u.grow.kinds : "";
                q.target = ev.u.grow.target ? ev.u.grow.target : "";
                q.listen = ev.u.grow.listen;
                q.tune = ev.u.grow.tune;
                q.depth = ev.u.grow.depth;
                g->second.queued.push_back(q);
                break;
            }

            if (!growInstrument(ev.channel, ev.u.grow.choice,
                                ev.u.grow.shrink != 0,
                                ev.u.grow.kinds ? ev.u.grow.kinds : "",
                                ev.u.grow.target ? ev.u.grow.target : "",
                                ev.u.grow.listen, ev.u.grow.tune, why,
                                ev.u.grow.depth))
                fprintf(stderr, "thcScheduler: growing channel %d: %s\n",
                        ev.channel + 1, why.c_str());
            break;
        }
        case THC_EV_NODEARG:
        {
            std::string why;
            const auto g = grown_.find(ev.channel);

            /* Behind a growth still out, for the graph it will make: a
               host that grew at once would set it on that one. */
            if (g != grown_.end() && ev.u.nodearg.node != NULL &&
                ev.u.nodearg.arg != NULL &&
                (g->second.asked != 0 || !g->second.options.empty()))
            {
                Growth::Queued q;

                q.nodeArg = true;
                q.node = ev.u.nodearg.node;
                q.arg = ev.u.nodearg.arg;
                q.value = ev.u.nodearg.value;
                g->second.queued.push_back(q);
                break;
            }

            if (ev.u.nodearg.node != NULL && ev.u.nodearg.arg != NULL &&
                !setNodeArg(ev.channel, ev.u.nodearg.node,
                            ev.u.nodearg.arg, ev.u.nodearg.value, why))
                fprintf(stderr, "thcScheduler: %s.%s on channel %d: %s\n",
                        ev.u.nodearg.node, ev.u.nodearg.arg,
                        ev.channel + 1, why.c_str());
            break;
        }
    }

    if (!seeking_)
        sigDelivered.emit(ev);                  /* piano roll, keyboard  */
}

/* A note's end, to wherever it sounds: the synth, or the device for an
   instrument played over MIDI, stamped with the time it is due. */
void
thcScheduler::endNote (int channel, int note, double at)
{
    if (playsOverMidi(channel))
        midiOut_->noteOff(channel, note, stampAt(at));
    else
        synth_->delNote(channel, note);
}

/* Transport time `at' on the monotonic clock: the step's own moment,
   moved by how far `at' is from where the step brought the transport.
   Anything outside a running step -- a paused transport, a seek, a
   harness stepping without a clock -- is now. */
int64_t
thcScheduler::stampAt (double at) const
{
    if (!running_ || seeking_)
        return midiNow_ ? midiNow_() : thcMonotonicUs();

    if (midiAt_)
        return midiAt_(at);

    if (stepMono_ == 0)
        return thcMonotonicUs();

    return stepMono_ + (int64_t)llround((at - transportNow_) * 1e6);
}

void
thcScheduler::sendDueNoteOffs (double now)
{
    while (!noteOffs_.empty() && noteOffs_.front().at <= now)
    {
        std::pop_heap(noteOffs_.begin(), noteOffs_.end(), Later());
        NoteOff off = noteOffs_.back();
        noteOffs_.pop_back();

        endNote(off.channel, off.note, off.at);
    }
}

void
thcScheduler::flushNoteOffs (void)
{
    while (!noteOffs_.empty())
    {
        endNote(noteOffs_.back().channel, noteOffs_.back().note,
                transportNow_);
        noteOffs_.pop_back();
    }
}

/* The derived offs pending for one key, removed: see deliver(). The
   latest of those whose note was struck at `struck' -- within a
   microsecond, since two chains reach one beat through different
   arithmetic -- or -HUGE_VAL where none was.

   A held note on the key is let go of too. Its voice is in release like
   any other, and the key coming up later would otherwise release the
   note that replaced it. */
double
thcScheduler::dropNoteOffs (int channel, int note, double struck)
{
    double doubled = -HUGE_VAL;
    size_t kept = 0;

    for (size_t i = 0; i < noteOffs_.size(); i++)
    {
        const NoteOff &off = noteOffs_[i];

        if (off.channel != channel || off.note != note)
        {
            noteOffs_[kept++] = off;
            continue;
        }

        if (fabs(off.from - struck) < 1e-6)
            doubled = std::max(doubled, off.at);
    }

    if (kept != noteOffs_.size())
    {
        noteOffs_.resize(kept);
        std::make_heap(noteOffs_.begin(), noteOffs_.end(), Later());
    }

    held_.erase(std::remove_if(held_.begin(), held_.end(),
                               [&](const NoteOff &h)
                               {
                                   return h.channel == channel &&
                                          h.note == note;
                               }),
                held_.end());

    return doubled;
}

void
thcScheduler::releaseHeld (int channel, int note, double at)
{
    for (size_t i = 0; i < held_.size(); i++)
        if (held_[i].channel == channel && held_[i].note == note)
        {
            endNote(channel, note, at);
            held_.erase(held_.begin() + i);
            return;
        }

    /* An off for a note nobody holds: a release that raced a flush,
       whose voice the flush already ended, or a key a later note on it
       took over (dropNoteOffs). Released here, it would end that later
       note instead. */
}

/* A flush ends every held note, and an ending is not only the synth's
 * business. delNote silences the voice and tells nobody: the off a held
 * note was promised never reaches sigDelivered, so anything keeping the
 * note -- the roll's open bar, which grows to the now-line until a
 * release names its end -- keeps it forever, and the roll's history can
 * never prune past it. So the promise is kept here, at the transport
 * time the flush happens, which is the honest end of a note nobody
 * released.
 *
 * Popped before the emission, not after: a handler is entitled to ask
 * the scheduler what it is holding, and by then the answer is already
 * "not this one". */
void
thcScheduler::flushHeld (const std::set<int> *chains)
{
    for (size_t i = held_.size(); i-- > 0; )
    {
        if (chains != NULL && chains->find(held_[i].chain) == chains->end())
            continue;

        const NoteOff h = held_[i];
        thcEvent off = {};

        held_.erase(held_.begin() + i);
        endNote(h.channel, h.note, transportNow_);

        off.type            = THC_EV_NOTEOFF;
        off.at              = transportNow_;
        off.channel         = h.channel;
        off.u.note.note     = h.note;
        off.u.note.velocity = 0;
        off.u.note.duration = 0;

        sigDelivered.emit(off);
    }
}

void
thcScheduler::start (void)
{
    /* What a rewind will repeat is now settled: everything announced to
       a stage so far was part of loading it, and everything from here
       on is playing it (thcParamStore::rebind). */
    for (size_t ci = 0; ci < chains_.size(); ci++)
        for (size_t si = 0; si < chains_[ci].stages.size(); si++)
            chains_[ci].stages[si]->params.freeze();

    /* The DSP's random nodes re-roll with the piece's seed too. */
    synth_->setSeed(masterSeed_);

    lastMono_ = thcMonotonicUs();
    running_ = true;
    started_ = true;
    clockStart();
}

bool
thcScheduler::clocking (void) const
{
    return running_ && !seeking_ && midiOut_ != NULL &&
           midiOut_->wantsClock();
}

/* Start from the top, or Song Position and Continue from anywhere else.
 * Song Position counts sixteenths, six ticks each, so a resume between two
 * of them goes on from the next one -- where the position says it is --
 * and the clock lands on the beat grid the device counts. */
void
thcScheduler::clockStart (void)
{
    if (!clocking())
        return;

    const int64_t now = stampAt(transportNow_);

    if (beat_ < 1e-9)
    {
        clockTick_ = 0;
        midiOut_->clock(thcMidiOut::CLOCK_START, 0, now);
        return;
    }

    const double sixteenth = std::ceil(beat_ * 4 - 1e-9);

    clockTick_ = sixteenth * 6;
    midiOut_->clock(thcMidiOut::CLOCK_POSITION, (int)sixteenth, now);
    midiOut_->clock(thcMidiOut::CLOCK_CONTINUE, 0, now);
}

/* The ticks the step just passed, each stamped at its own beat: tick k is
   at beat k/24, which was (beat_ - k/24) beats before where the step
   brought the transport. */
void
thcScheduler::clockTicks (void)
{
    if (!clocking())
        return;

    while (clockTick_ <= beat_ * 24 + 1e-9)
    {
        const double at =
            transportNow_ - (beat_ - clockTick_ / 24) * 60.0 / tempo_;

        midiOut_->clock(thcMidiOut::CLOCK_TICK, 0, stampAt(at));
        clockTick_ += 1;
    }
}

/* stop() is a pause, but a pause must not hang notes: flush every
 * derived off immediately, and release every held key -- a note whose
 * end nobody knows still has to end when the music does. pending_ and
 * wakeups_ are keyed in transport time, which has stopped advancing, so
 * they keep on their own. */
void
thcScheduler::stop (void)
{
    if (clocking())
        midiOut_->clock(thcMidiOut::CLOCK_STOP, 0, stampAt(transportNow_));

    running_ = false;
    flushNoteOffs();
    flushHeld();

    /* The offs above were stamped now; this also drops whatever was
       queued ahead of them for later, so a pause is silent on the device
       at once. */
    if (midiOut_ != NULL)
        for (int ch = 0; ch < 16; ch++)
            if (overMidi_[ch])
                midiOut_->flush(ch);
}

void
thcScheduler::seek (double t)
{
    seekWith(t, std::vector<std::pair<double, int> >());
}

void
thcScheduler::seekSection (size_t section)
{
    if (section >= sections_.size())
        return;

    std::vector<std::pair<double, int> > keep;
    const double at = sectionPlace(section, keep);

    seekWith(at, keep);
}

/* Its place in the first time through, and a cue at the end of each hold
   on the way, so the walk does not wait in one. */
double
thcScheduler::sectionPlace (size_t section,
                            std::vector<std::pair<double, int> > &keep) const
{
    double at = 0;

    for (size_t k = 0; k < section && k < sections_.size(); k++)
    {
        at += sectionLength(sections_[k]);

        if (sections_[k].hold)
            keep.push_back(std::make_pair(at, (int)k + 1));
    }

    return at;
}

void
thcScheduler::seekWith (double t,
                        const std::vector<std::pair<double, int> > &keep)
{
    seekWithBegin(t, keep);

    while (!seekStep(1 << 20))
        ;

    seekEnd();
}

void
thcScheduler::seekBegin (double t)
{
    seekWithBegin(t, std::vector<std::pair<double, int> >());
}

void
thcScheduler::seekSectionBegin (size_t section)
{
    std::vector<std::pair<double, int> > keep;
    const double at = section < sections_.size()
        ? sectionPlace(section, keep) : transportNow_;

    seekWithBegin(at, keep);
}

void
thcScheduler::seekWithBegin (double t,
                             const std::vector<std::pair<double, int> > &keep)
{
    seekWas_ = running_;

    halt();
    reset();
    cues_ = keep;
    cuesPinned_ = !keep.empty();

    /* No further than a piece that ends goes, and no further than an
       hour: a seek is the whole piece up to there, and a target nobody
       could mean is a host stalled for nothing. */
    if (std::isfinite(arrangementEnd()))
        t = std::min(t, arrangementEnd());

    t = std::min(t, 3600.0);

    /* Up to just short of `t', silently -- short of it, so what is due
       at `t' itself is heard: a section starts on a bar line, and so
       does its downbeat. Steps of 20 ms, the desktop's own: a stage asks
       for its wakes and gets them whatever the step, but one woken by a
       node moving is woken at the end of the step it moved in. An ear a
       composer listens through answers at once, as a harness's does, or
       a breeding population would not breed on the way. */
    seekTo_ = t - 1e-6;
    seekWasSync_ = auditionSync_;

    if (seekTo_ > 0)
    {
        if (!seekWasSync_)
            setAuditionSynchronous(true);

        seekControls_.clear();
        seeking_ = true;
        start();
    }
}

bool
thcScheduler::seekStep (int steps)
{
    for (int n = 0; n < steps; n++)
    {
        if (!seeking_ || !running_ || transportNow_ >= seekTo_ - 1e-9)
            return true;

        stepTransportTo(std::min(seekTo_, transportNow_ + 0.02));
    }

    return !seeking_ || !running_ || transportNow_ >= seekTo_ - 1e-9;
}

void
thcScheduler::seekEnd (void)
{
    if (seeking_)
    {
        seeking_ = false;

        for (const auto &c : seekControls_)
            if (playsOverMidi(c.first.first))
                midiOut_->control(c.first.first, c.first.second, c.second,
                                  midiNow_ ? midiNow_()
                                           : thcMonotonicUs());

        seekControls_.clear();

        if (!seekWasSync_)
            setAuditionSynchronous(false);
    }

    cuesPinned_ = false;

    /* Playing on from there if it was playing -- start() takes the wall
       clock the timer steps by from now, or its first tick would step
       the transport by however long the seek took; otherwise waiting
       there for Play. A piece that ends before `t' has stopped itself. */
    if (seekWas_ && !(endAfter_ && transportNow_ >= arrangementEnd()))
        start();
    else
        running_ = false;
}

void
thcScheduler::halt (void)
{
    stop();

    if (synth_)
        synth_->silence();
}

/* reset() is what makes --seed style replays a first-class feature:
 * same chains, same master seed, same piece, every time. */
void
thcScheduler::reset (void)
{
    stop();

    transportNow_ = beat_ = 0;
    tempoAt_ = beatAtTempo_ = 0;

    /* Before the stages are rebuilt below, so they are created hearing
       it, and through setValue, so an instrument bound to it is pushed.
       And before the wakes are cleared: a sleeper bound to it is re-armed
       by the move, and the loop below arms every stage again. */
    /* A knob a section sets is put back where the text declares it, and
       the first section then sets it again. Here and not as the section
       is read: the knob may be declared below it. */
    for (const auto &s : sections_)
        for (const auto &k : s.knobs)
            if (thArg *a = knob(k.first))
                knobHome_.emplace(a, (*knobMeta(k.first))[0]);

    for (const auto &h : knobHome_)
        if ((*h.first)[0] != h.second)
            h.first->setValue(h.second);

    /* And the first section's, which is what the top of the piece is. */
    knobSpanStart_ = -1;
    knobSpanSection_ = -1;
    applySectionKnobs();

    /* The arrangement as written, from the top: a cue was a hand on the
       piece that played, and a replay is not that play. The knob is
       back home with the others, which is not a move. */
    cues_.clear();

    if (cueKnob_ != NULL)
        cueKnobLast_ = std::lround((*cueKnob_)[0]);

    pending_.clear();
    wakeups_.clear();
    recompose_.clear();
    dropReplays();
    wholeMarked_.clear();
    started_ = false;

    /* A growth still being heard was asked for by a play that is over. */
    for (auto &e : grown_)
        forgetHearing(e.second);

    /* The embedded nodes rewind too, or a replay would start with an
       LFO wherever the last play left it -- which is the same
       divergence a composer instance carrying its old state would be,
       arriving through the other host. */
    for (size_t ci = 0; ci < chains_.size(); ci++)
        if (chains_[ci].nodes)
            chains_[ci].nodes->reset();

    /* And what the lights remember: times from the last run would light
       again when the replay reached them, whatever the replay did. */
    for (thcChain &c : chains_)
    {
        c.lastHeard[0] = c.lastHeard[1] = -1;
        c.lastGated[0] = c.lastGated[1] = -1;
        c.played.clear();

        for (auto &s : c.stages)
        {
            s->lastIn = s->lastOut = -1;
            s->in.clear();
            s->out.clear();
        }
    }

    /* And the instruments, if a structure edit has been anywhere near
       them.
     *
     * A replay starts from the file, and after a swap or a node-arg
     * edit the channels no longer say what the file says: one of them
     * is playing a different .dsp, another has a constant nobody
     * declared. Re-applying every instrument is what "as the piece was
     * written" means, and it is the same call the loader makes -- so
     * there is one answer to what a declaration means rather than a
     * second one kept in step by hand.
     *
     * Only when something moved. An ordinary rewind of an ordinary
     * piece reloads nothing, which matters because reloading a graph
     * is the most expensive thing in here. */
    if (!nodeArgs_.empty() || !swapped_.empty())
    {
        /* The channels an edit actually reached. See swapped_ for why
           this is a set of channels rather than a flag. */
        std::set<int> touched = swapped_;

        for (size_t i = 0; i < nodeArgs_.size(); i++)
            touched.insert(nodeArgs_[i].channel);

        bool restored = true;

        for (size_t i = 0; i < instruments_.size(); i++)
        {
            if (touched.find(instruments_[i].channel) == touched.end())
                continue;

            std::string why;

            if (!applyInstrument(i, why))
            {
                fprintf(stderr, "thcScheduler: rewinding instrument '%s': "
                        "%s\n", instruments_[i].name.c_str(), why.c_str());
                restored = false;
            }
        }

        /* Forgotten only if it was actually put back.
         *
           applyInstrument takes the graph back off the channel when it
           refuses, so a failed restore leaves that channel silent --
           and clearing the bookkeeping regardless then told every later
           rewind there was nothing to put back, so the channel stayed
           silent for the rest of the session even after whatever caused
           the refusal had been fixed. Keeping it means the next rewind
           tries again, which is the only thing that can help. */
        if (restored)
        {
            nodeArgs_.clear();
            swapped_.clear();
            forgetGrowth(-1);
            holding_.clear();   /* every channel says what the file says */
        }
    }

    /* Each stage is what its text says again, painted on or not. */
    for (size_t ci = 0; ci < chains_.size(); ci++)
        for (size_t si = 0; si < chains_[ci].stages.size(); si++)
        {
            chains_[ci].stages[si]->unwritten = false;
            remake(ci, si);
        }

    sigReset.emit();
}

/* A stage's instance made again over its store, as the load made it, and
 * woken at its chain's start. */
void
thcScheduler::remake (size_t ci, size_t si)
{
    thcStage *s = chains_[ci].stages[si].get();

    /* Over the defaults, as the load created it -- what the
       store was before the loader touched it -- and the load's
       work done again afterwards (thcParamStore::replay). */
    s->params.restoreDefaults();

    /* The replacement is made before the old instance goes,
       so a module refusing the second create leaves the stage
       with its old state rather than with a NULL that the next
       tick or receive would hand straight back to it. A stale
       instance mid-replay is wrong; a crash is more wrong. */
    void *fresh = s->plugin->create(s->params.params());

    if (fresh == NULL)
    {
        /* The replay below still runs, over the instance that
           survived: restoreDefaults() has already emptied the
           store, and leaving it on the plugin's defaults would
           make every param of this stage read something the
           file never said. A stage that did not rewind is the
           lesser of the two. */
        fprintf(stderr, "thcScheduler: %s refused to recreate; "
                "keeping the old instance\n",
                s->plugin->name().c_str());
    }
    else
    {
        s->plugin->destroy(s->state);
        s->state = fresh;
        s->params.instance_ = s->state;
    }

    /* Awake before anything is announced to it, as a stage is
       on a load. An announcement re-arms a sleeper, and a stage
       that had gone to sleep by the end of the run would be
       armed by the first announcement and then again below: two
       wakes at zero, two ticks, and a replay that was not one. */
    s->sleeping = false;
    s->awaitingStart = s->ticks;

    /* Everything the load did, done again to the instance that
       now serves the store -- see replay(). After the swap,
       because the announcements go to whichever instance is
       current. */
    s->params.replay();

    if (s->ticks && s->state != NULL)
    {
        wakeups_.push_back({ chainStartTime(chains_[ci]), ci, si,
                             heapSeq_++ });
        std::push_heap(wakeups_.begin(), wakeups_.end(), Later());
    }
}

void
thcScheduler::input (thcStage *s, const thcInputEvent &ev)
{
    s->plugin->input(s->state, &ev);
    s->unwritten |= !s->plugin->inputEdits();
}

void
thcScheduler::recompose (size_t chain, bool passing)
{
    if (chain >= chains_.size())
        return;

    const auto m = recompose_.find(chain);

    recompose_[chain] = passing && (m == recompose_.end() || m->second);

    if (replayWhole_)
        wholeMarked_.insert(chain);
}

void
thcScheduler::knobMoved (const thArg *knob, bool passing)
{
    for (size_t j = 0; j < chains_.size(); j++)
    {
        if (std::any_of(chains_[j].sinks.begin(), chains_[j].sinks.end(),
                        [knob](const thcSink &s) { return s.knob == knob; }))
            continue;

        bool reads = chains_[j].nodes && chains_[j].nodes->reads(knob);

        for (const auto &s : chains_[j].stages)
            for (int i = 0; !reads && i < s->plugin->paramCount(); i++)
                reads = s->params.knobBinding(i) == knob;

        if (reads)
            recompose(j, passing);
    }
}

/* How often a chain may be played again from its start for values a drag
   passes through: the replay's cost grows with how long the piece has
   played, and a drag sends a value a frame. Those in between wait, and the
   one let go at is not held. */
static const double PASSING_EVERY_SEC = 0.1;

void
thcScheduler::recomposeMarked (void)
{
    const bool due = transportNow_ < passedAt_ ||
                     transportNow_ >= passedAt_ + PASSING_EVERY_SEC;
    std::map<size_t, bool> marked, later;

    for (const auto &m : recompose_)
        (m.second && !due ? later : marked)[m.first] = m.second;

    recompose_.swap(later);

    for (const auto &m : marked)
    {
        if (m.second)
            passedAt_ = transportNow_;

        composeAgain(m.first, m.second);
    }
}

void
thcScheduler::composeAgain (size_t chain, bool passing)
{
    if (chain >= chains_.size() || !started_ || chains_[chain].inputMidi ||
        std::any_of(chains_[chain].stages.begin(),
                    chains_[chain].stages.end(),
                    [](const std::unique_ptr<thcStage> &s)
                    { return s->unwritten; }))
        return;

    const unsigned long before = pendingSeq_;
    auto sound = [chain, before](const Pending &p)
    {
        return p.chain == (int)chain && p.seq < before &&
               (p.ev.type == THC_EV_NOTE || p.ev.type == THC_EV_CHANARG);
    };

    const double beat = transportNow_ + 60.0 / tempo_;

    if (passing && std::none_of(pending_.begin(), pending_.end(),
                     [&](const Pending &p) { return sound(p) &&
                                                    p.at > beat; }))
        return;

    auto dropSound = [this, sound]
    {
        pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                      sound),
                       pending_.end());
        std::make_heap(pending_.begin(), pending_.end(), LaterPending());
    };

    /* In pieces, what it had queued plays on until the replay lands. */
    const bool whole = wholeReplay(chain);

    if (whole)
        dropSound();

    wakeups_.erase(std::remove_if(wakeups_.begin(), wakeups_.end(),
                                  [chain](const Wakeup &w)
                                  { return w.chain == chain; }),
                   wakeups_.end());
    std::make_heap(wakeups_.begin(), wakeups_.end(), Later());

    /* One still under way is begun again, over stages made again. */
    dropReplays((long)chain);

    /* A key held through it would be released into stages that never
       saw it pressed, as in adopt(). */
    const std::set<int> one = { (int)chain };

    flushHeld(&one);

    /* Made reading the nodes as a load does, before they have run. */
    if (chains_[chain].nodes && chains_[chain].nodes->recorded())
        chains_[chain].nodes->showLoad();
    else if (chains_[chain].nodes)
        chains_[chain].nodes->reset();

    for (size_t si = 0; si < chains_[chain].stages.size(); si++)
        remake(chain, si);

    catchUp(chain);
    wholeMarked_.erase(chain);

    if (whole)
        return;

    auto r = std::find_if(replays_.begin(), replays_.end(),
                          [chain](const Replay &x) { return x.chain == chain; });

    if (r == replays_.end())
        dropSound();
    else
    {
        r->dropOld = true;
        r->oldBefore = before;
    }
}

void
thcScheduler::setTempo (double bpm)
{
    /* A staged edit's `tempo' line waits for adopt(), which applies it
       where the line changed: an unchanged line is no tempo change, and
       the tempo may since have been moved by a control or a room. */
    if (staging_)
        return;

    if (bpm > 0 && bpm <= MAX_TEMPO_BPM)
    {
        /* The line beat_ is read off turns here. */
        beatAtTempo_ = beat_;
        tempoAt_ = transportNow_;
        tempo_ = bpm;

        if (synth_ != NULL)
            synth_->setTempo((float)bpm);

        /* A beat-valued chain start follows the clock until its first
           tick. Once the generator has begun, its own schedule owns it. */
        bool moved = false;

        for (size_t i = 0; i < wakeups_.size(); i++)
        {
            Wakeup &w = wakeups_[i];
            const thcChain &c = chains_[w.chain];

            if (c.startBeats && c.stages[w.stage]->awaitingStart)
            {
                w.at = chainStartTime(c);
                moved = true;
            }
        }

        if (moved)
            std::make_heap(wakeups_.begin(), wakeups_.end(), Later());
    }
}

bool
thcScheduler::usesBeats (void) const
{
    for (size_t ci = 0; ci < chains_.size(); ci++)
    {
        const thcChain &c = chains_[ci];

        if (c.startBeats)
            return true;

        for (size_t si = 0; si < c.stages.size(); si++)
            if (c.stages[si] && c.stages[si]->params.anyBeats())
                return true;
    }

    /* And an arrangement written in bars or beats, which is every
       arrangement worth writing: the tempo decides how long each
       section lasts, so a piece with one is a piece the tempo reaches
       even if no stage of it counts beats. */
    for (size_t i = 0; i < sections_.size(); i++)
        if (sections_[i].beats)
            return true;

    return false;
}

void
thcScheduler::injectMidi (size_t chainIndex, const thcEvent &ev)
{
    if (chainIndex >= chains_.size())
        return;

    injectingLive_ = !running_;
    propagate(chains_[chainIndex], 0, ev);
    injectingLive_ = false;
}

bool
thcScheduler::injectMidiEvent (const thcEvent &ev)
{
    bool taken = false;

    for (size_t ci = 0; ci < chains_.size(); ci++)
    {
        thcChain &c = chains_[ci];

        if (!c.inputMidi)
            continue;

        /* "Arriving on the sink channel": the sink is where the chain
           says which channel it lives on, for input as for output. */
        bool match = false;

        for (size_t si = 0; si < c.sinks.size(); si++)
            if (c.sinks[si].channel == ev.channel)
            {
                match = true;
                break;
            }

        if (match || c.sinks.empty())
        {
            injectingLive_ = !running_;
            propagate(c, 0, ev);
            injectingLive_ = false;
            taken = true;
        }
    }

    return taken;
}

bool
thcScheduler::chanArgExists (int channel, const std::string &name) const
{
    return synth_ != NULL && synth_->getChanArg(channel, name) != NULL;
}

bool
thcScheduler::chanArgRange (int channel, const char *name,
                            float &lo, float &hi) const
{
    thArg *arg = synth_->getChanArg(channel, name != NULL ? name : "");

    if (arg == NULL || arg->max() <= arg->min())
        return false;

    lo = arg->min();
    hi = arg->max();

    return true;
}

const std::vector<thcEvent> &
thcScheduler::peekPending (void) const
{
    peekCache_.clear();
    peekCache_.reserve(pending_.size());
    peekChains_.clear();
    peekChains_.reserve(pending_.size());

    for (size_t i = 0; i < pending_.size(); i++)
    {
        peekCache_.push_back(pending_[i].ev);
        peekChains_.push_back(pending_[i].chain);
    }

    return peekCache_;
}
