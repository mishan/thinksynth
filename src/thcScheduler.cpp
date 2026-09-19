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

#include <algorithm>

#include "think.h"
#include "thUnits.h"

#include "thcPlugin.h"
#include "thcScheduler.h"

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
        double bpm = tempo_();

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
    for (size_t i = 0; i < nodes_.size(); i++)
    {
        if (nodes_[i] == NULL)
            continue;

        const float now = (*nodes_[i])[0];

        if (now == lastNode_[i])
            continue;

        lastNode_[i] = now;

        if (rearm_)
            rearm_();
    }
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
    if (instance_ != NULL)
        plugin_->paramChanged(instance_, index);

    if (rearm_)
        rearm_();
}

/* ---- thcScheduler ----------------------------------------------------- */

/* One timer drives everything. 20ms is far finer than anything musical
 * happening here and coarser than anything GTK minds. It keeps running
 * while paused so composer_draw views stay live; only the musical clock
 * freezes. */
thcScheduler::thcScheduler (thSynth *synth)
    /* In declaration order, which is what -Wreorder is about:
       controlSynth_ is declared up beside the instrument table it
       belongs to, which puts it ahead of the transport members here
       even though nothing about it is more fundamental. swapped_ is a
       container and needs no mention. */
    : synth_(synth), endAfter_(false), controlSynth_(NULL),
      running_(false), transportNow_(0), beat_(0), tempo_(120),
      lastMono_(g_get_monotonic_time()),
      masterSeed_(g_random_int()), pendingSeq_(0), heapSeq_(0),
      injectingLive_(false)
{
    timer_ = Glib::signal_timeout().connect(
        sigc::mem_fun(*this, &thcScheduler::timerCallback), 20);
}

thcScheduler::~thcScheduler (void)
{
    timer_.disconnect();
    flushNoteOffs();
    clearChains();

    /* One last go, since after this there is nobody left to try. If the
       ring is still full the graph outlives us, which is the honest
       end of a synth whose audio thread stopped draining. */
    retireStranded();

    /* After clearChains, which is what destroys the node hosts that
       borrow it. */
    delete controlSynth_;
    controlSynth_ = NULL;
}

size_t
thcScheduler::addChain (const std::string &name)
{
    chains_.push_back(thcChain());
    chains_.back().name = name;
    chains_.back().muted = false;
    chains_.back().inputMidi = false;

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
thcScheduler::setChainInput (size_t chain, bool midi)
{
    if (chain < chains_.size())
        chains_[chain].inputMidi = midi;
}

/* Deterministic mixing of the master seed with the stage's position, so
 * the same master seed and the same construction order create the same
 * instances -- which is the whole replay story. The constants are
 * Knuth's multiplicative hash and the golden-ratio increment; anything
 * that separates neighboring (chain, stage) pairs would do. */
unsigned
thcScheduler::stageSeed (size_t chain, size_t stage) const
{
    unsigned h = masterSeed_;

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
        wakeups_.push_back({ transportNow_, chain, stage, heapSeq_++ });
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

    chains_.clear();
    wakeups_.clear();
    pending_.clear();

    /* The arrangement names chains by the names this just took away. */
    sections_.clear();
    endAfter_ = false;

    /* Knob-to-param connections point into the stages just destroyed;
       the knobs themselves belong to the piece and go with it. */
    for (size_t i = 0; i < knobConns_.size(); i++)
        knobConns_[i].conn.disconnect();
    knobConns_.clear();
    holding_.clear();

    for (std::map<std::string, thArg *>::iterator i = knobs_.begin();
         i != knobs_.end(); ++i)
        delete i->second;
    knobs_.clear();

    /* Structure edits belong to the piece that made them. */
    nodeArgs_.clear();
    swapped_.clear();

    /* The instrument table goes with the piece too. What is *loaded* on
       those channels does not: a patch outlives the file that asked for
       it, exactly as one loaded by hand outlives the window that loaded
       it, and deciding when a channel should be given back is the
       host's business rather than this table's. */
    instruments_.clear();

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
    kc.conn = knob->signal_arg_changed().connect(
        [store, paramIndex](thArg *) { store->notifyChanged(paramIndex); });

    knobConns_.push_back(kc);
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
        if (knobConns_[i].channel == channel)
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
           declares. A piece file has no such history, and an arg name
           the graph does not know is a typo every time -- so it is said
           rather than swallowed. The declared surface is the whole of
           what a piece may reach; see COMPOSITION_HANDOFF.md section 9. */
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

        const std::string declared = foldUnitOf(arg);

        /* A unit the arg is not folded in cannot be folded into it, and
           its absence is no better: `res = 50 ms' on a resonance that
           runs 0 to 1 would become two thousand-odd samples of nothing,
           and `a = 39690' on an envelope is a sample count nobody meant
           to write. Same rule as a duration param in a stage, for the
           same reason -- the unit decides what the number is, so its
           absence decides nothing. */
        if (a.units != declared)
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

        if (a.knob.empty())
        {
            arg->setValue((float)thFoldUnit(a.value, a.units,
                                            synth_->getSampleRate()));
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
                if (foldUnitOf(dest) != units)
                    return;

                dest->setValue((float)thFoldUnit((*from)[0], units,
                                                 synth_->getSampleRate()));
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
        kc.conn = k->signal_arg_changed().connect(push);

        knobConns_.push_back(kc);
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
                               const std::vector<thcInstrumentArg> &args)
{
    master_ = thcInstrument();
    master_.name = "the mix";
    master_.effect = dsp;
    master_.args = args;
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
    if (synth_ == NULL)
        return true;

    if (master_.effect.empty())
    {
        /* Nothing declared takes off whatever was there. A piece that says
           nothing about the mix means a dry mix, not "keep the last
           piece's reverb", which is what leaving it would mean in a
           session where pieces are opened one after another. */
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
                   std::to_string(channel + 1) + ")";

        return false;
    }

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
 * the whole of what every composer up to now could reach;
 * COMPOSITION_HANDOFF.md section 9 said the way past it would be a
 * different mechanism rather than a widening of that one, and this is
 * the different mechanism. A piece doing this is reaching into somebody
 * else's graph -- on its own say-so, in an event anybody can see on the
 * roll, and only as often as the event stream flows.
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
    if (inst.channel < 0)
        return true;                    /* never got there; nothing to do */

    if (unloadDsp_)
        return unloadDsp_(inst);

    if (synth_ != NULL)
        return synth_->removeChan(inst.channel);

    return true;
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
    const double total = sectionsLength();

    if (sections_.empty() || total <= 0)
        return -1;

    if (at < 0)
        at = 0;

    if (at >= total)
    {
        /* Past the last one: over, or round again. */
        if (endAfter_)
            return -1;

        at = fmod(at, total);
    }

    double edge = 0;

    for (size_t i = 0; i < sections_.size(); i++)
    {
        edge += sectionLength(sections_[i]);

        /* The nudge form makes at a bar line, and for the same reason:
           a note written on the edge belongs to the section it opens,
           not to the one it closes. A microsecond is far below anything
           the scheduler resolves -- a window is twenty-three
           milliseconds -- and far above the rounding of an edge summed
           from eight beat-valued lengths. */
        if (at < edge - 1e-6)
            return (int)i;
    }

    return (int)sections_.size() - 1;
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

/* Integrate time rather than derive it, so pause and tempo changes are
 * both trivially correct: seconds and beats just stop or change slope. */
bool
thcScheduler::timerCallback (void)
{
    gint64 mono = g_get_monotonic_time();

    if (running_)
        stepTransport((mono - lastMono_) / 1e6);
    else
        sendDueNoteOffs(transportNow_);   /* offs drain even when paused */

    /* Here as well as in stepTransport, because a channel that would not
       go is exactly as stuck on a paused transport as on a running one,
       and this timer keeps firing either way. */
    retireStranded();

    lastMono_ = mono;

    return true;
}

/* One transport step, however time got measured -- the real timer above,
 * or a harness's virtual clock. Everything the step does is keyed in
 * transport seconds, so the outcome depends on dt and nothing else. */
void
thcScheduler::stepTransport (double dt)
{
    /* Before the early return, so a harness -- which has no Glib timer
       and reaches the scheduler only through here -- still gets the
       retry it would otherwise never see. */
    retireStranded();

    if (!running_ || dt < 0)
        return;

    transportNow_ += dt;
    beat_ += dt * tempo_ / 60.0;

    runStep();
}

void
thcScheduler::stepTransportTo (double t)
{
    retireStranded();

    if (!running_ || t < transportNow_)
        return;

    beat_ += (t - transportNow_) * tempo_ / 60.0;
    transportNow_ = t;

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
    runDueTicks(transportNow_);

    for (size_t i = 0; i < chains_.size(); i++)
        if (chains_[i].nodes)
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
        transportNow_ >= sectionsLength())
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
 * `now' is where the transport got to; a wake at or before it runs with
 * the transport reading the wake's own time, not `now'. Composers
 * schedule from what they are handed -- thirteen of the sixteen in the
 * tree return `t->now + period' -- so handing them the end of the step
 * made every wake late by up to a step, and the next was scheduled from
 * the late one, so the lateness compounded. What a piece composed was
 * then a function of how often the host called this: the same file and
 * the same seed gave one piece at a 1024-frame step and another at 256,
 * and two machines with different sound cards could not agree on a piece
 * at all. docs/JAM.md section 3, and SCHEDULER_PLACEMENT.md for the
 * measurements.
 *
 * The chain's nodes move to the wake's time before the stage reads them,
 * for the same reason and to the same end: a param reading `lfo->out'
 * sees the lfo where the stage is rather than where the step ended.
 * stepTransport brings them up to the end of the step afterwards.
 */
void
thcScheduler::runDueTicks (double now)
{
    thcTransport t = { now, tempo_, beat_, running_ };
    const double beatAtNow = beat_;

    for (size_t i = 0; i < chains_.size(); i++)
        for (size_t si = 0; si < chains_[i].stages.size(); si++)
            chains_[i].stages[si]->stalled = 0;

    while (!wakeups_.empty() && wakeups_.front().at <= now)
    {
        std::pop_heap(wakeups_.begin(), wakeups_.end(), Later());
        Wakeup w = wakeups_.back();
        wakeups_.pop_back();

        thcChain &c = chains_[w.chain];
        thcStage *s = c.stages[w.stage].get();

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

void
thcScheduler::rearmStage (size_t chain, size_t stage)
{
    if (chain >= chains_.size() || stage >= chains_[chain].stages.size())
        return;

    thcStage *s = chains_[chain].stages[stage].get();

    if (!s->sleeping || !s->ticks)
        return;

    s->sleeping = false;
    wakeups_.push_back({ transportNow_, chain, stage, heapSeq_++ });
    std::push_heap(wakeups_.begin(), wakeups_.end(), Later());
}

/* Synchronous walk through the rest of the chain. Each transformer gets
 * a sink that continues from the stage after itself, so echoes and fans
 * flow strictly downstream -- no cycles are constructible. Whatever a
 * muted chain produces is dropped at the end, not suppressed at the
 * start: the algorithm keeps evolving silently, which is what you want
 * when un-muting mid-piece. */
void
thcScheduler::propagate (thcChain &c, size_t fromStage, const thcEvent &ev)
{
    if (fromStage >= c.stages.size())
    {
        if (c.muted)
            return;

        /* The arrangement, applied where the mute is (docs/GEN_FORMAT.md
         * §5c). The section is the one this event's own `at' falls in.
         *
         * A level of 0 mutes the chain for that section: its notes and
         * its chanargs are both dropped, so a walk or a gate driving a
         * knob stops pushing and the knob keeps the value it had.
         *
         * Two kinds of event go through whatever the level says, and
         * neither of them is sound. A NOTEOFF: a swallowed off hangs a
         * voice for the rest of the piece, while an off for a note
         * nobody holds is a no-op releaseHeld is already written to
         * cope with -- only one of the two is a bug. And a structure
         * edit: a swap or a node-arg edit is the piece rebuilding
         * itself, and one dropped leaves a channel holding a graph the
         * piece has moved on from, with no later event to catch it up.
         */
        const double level = sectionLevel(c, ev.at);
        thcEvent scaled = ev;
        const thcEvent *gated = &ev;

        if (level != 1.0 && ev.type != THC_EV_NOTEOFF &&
            !isStructureEdit(ev.type))
        {
            if (level <= 0)
                return;

            if (ev.type == THC_EV_NOTE)
            {
                /* Quieter, or louder, but never absent: a section that
                   scales a chain still plays it. */
                double v = ev.u.note.velocity * level + 0.5;

                if (v < 1)
                    v = 1;
                else if (v > 127)
                    v = 127;

                scaled.u.note.velocity = (int)v;
                gated = &scaled;
            }
        }

        /* No sinks: the programmatic-chain case; deliver as emitted. */
        if (c.sinks.empty())
        {
            queuePending(*gated, NULL);
            return;
        }

        /* Sinks route and type-filter: notes to note sinks, chanargs to
           chanarg sinks, each on the sink's channel. Multiple matches is
           fan-out. The event's own channel is overwritten -- routing
           belongs to the piece, not to the plugin, which is why eno_line
           no longer has a channel param. */
        for (size_t i = 0; i < c.sinks.size(); i++)
        {
            const thcSink &sink = c.sinks[i];

            /* The type filter, which is a rule about notes and chanargs
               and says nothing about a structure edit -- a swap is
               neither, and both kinds of sink name the channel it needs.
               So an edit goes to every sink, and fan-out means what
               fan-out means everywhere else: a chain with three sinks
               reshapes three channels. */
            if (!isStructureEdit(ev.type) &&
                (ev.type == THC_EV_CHANARG) != sink.isChanarg())
                continue;

            thcEvent routed = *gated;

            routed.channel = sink.channel;

            /* NULL means "keep what the event carries" -- which is what a
               note sink wants and also what a `*' chanarg sink wants, for
               opposite reasons: the note has no name to keep, and the
               vector's component has one worth keeping. */
            queuePending(routed,
                         (sink.isChanarg() && !sink.namesItsOwn())
                             ? &sink.chanarg : NULL);
        }
        return;
    }

    thcStage *s = c.stages[fromStage].get();

    if (!s->plugin->hasReceive())               /* pass-through          */
    {
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
                            const std::string *nameOverride)
{
    Pending p;

    p.at = ev.at;
    p.ev = ev;

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
    else if (ev.type == THC_EV_NODEARG)
    {
        p.text.reset(new std::string(ev.u.nodearg.node
                                     ? ev.u.nodearg.node : ""));
        p.text2.reset(new std::string(ev.u.nodearg.arg
                                      ? ev.u.nodearg.arg : ""));
        p.ev.u.nodearg.node = p.text->c_str();
        p.ev.u.nodearg.arg = p.text2->c_str();
    }

    /* Live input on a paused clock: pending_ is keyed in transport
       time and transport time is frozen, so anything due now would
       wait for Play. It should not -- the keys were pressed now. */
    if (injectingLive_ && p.at <= transportNow_)
    {
        deliver(p.ev);
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

        deliver(p.ev);
    }
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
    switch (ev.type)
    {
        case THC_EV_NOTE:
        {
            synth_->addNote(ev.channel, ev.u.note.note,
                            ev.u.note.velocity);

            /* A composed note carries its whole life in the duration;
               the off lands exactly there, keyed off the event's own
               time so a replay derives an identical off stream. A held
               note (duration <= 0, live input's spelling of "who
               knows") waits for its NOTEOFF instead. */
            if (ev.u.note.duration > 0)
            {
                noteOffs_.push_back({ ev.at + ev.u.note.duration,
                                      ev.channel, ev.u.note.note,
                                      heapSeq_++ });
                std::push_heap(noteOffs_.begin(), noteOffs_.end(),
                               Later());
            }
            else
                held_.push_back({ 0, ev.channel, ev.u.note.note });
            break;
        }
        case THC_EV_NOTEOFF:
        {
            releaseHeld(ev.channel, ev.u.note.note);
            break;
        }
        case THC_EV_CHANARG:
        {
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
        case THC_EV_NODEARG:
        {
            std::string why;

            if (ev.u.nodearg.node != NULL && ev.u.nodearg.arg != NULL &&
                !setNodeArg(ev.channel, ev.u.nodearg.node,
                            ev.u.nodearg.arg, ev.u.nodearg.value, why))
                fprintf(stderr, "thcScheduler: %s.%s on channel %d: %s\n",
                        ev.u.nodearg.node, ev.u.nodearg.arg,
                        ev.channel + 1, why.c_str());
            break;
        }
    }

    sigDelivered.emit(ev);                      /* piano roll, keyboard  */
}

void
thcScheduler::sendDueNoteOffs (double now)
{
    while (!noteOffs_.empty() && noteOffs_.front().at <= now)
    {
        std::pop_heap(noteOffs_.begin(), noteOffs_.end(), Later());
        NoteOff off = noteOffs_.back();
        noteOffs_.pop_back();

        synth_->delNote(off.channel, off.note);
    }
}

void
thcScheduler::flushNoteOffs (void)
{
    while (!noteOffs_.empty())
    {
        synth_->delNote(noteOffs_.back().channel, noteOffs_.back().note);
        noteOffs_.pop_back();
    }
}

void
thcScheduler::releaseHeld (int channel, int note)
{
    for (size_t i = 0; i < held_.size(); i++)
        if (held_[i].channel == channel && held_[i].note == note)
        {
            synth_->delNote(channel, note);
            held_.erase(held_.begin() + i);
            return;
        }

    /* An off for a note nobody holds: a release that raced a flush.
       delNote copes; do the same. */
    synth_->delNote(channel, note);
}

void
thcScheduler::flushHeld (void)
{
    while (!held_.empty())
    {
        synth_->delNote(held_.back().channel, held_.back().note);
        held_.pop_back();
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

    lastMono_ = g_get_monotonic_time();
    running_ = true;
}

/* stop() is a pause, but a pause must not hang notes: flush every
 * derived off immediately, and release every held key -- a note whose
 * end nobody knows still has to end when the music does. pending_ and
 * wakeups_ are keyed in transport time, which has stopped advancing, so
 * they keep on their own. */
void
thcScheduler::stop (void)
{
    running_ = false;
    flushNoteOffs();
    flushHeld();
}

/* reset() is what makes --seed style replays a first-class feature:
 * same chains, same master seed, same piece, every time. */
void
thcScheduler::reset (void)
{
    stop();

    transportNow_ = beat_ = 0;
    pending_.clear();
    wakeups_.clear();

    /* The embedded nodes rewind too, or a replay would start with an
       LFO wherever the last play left it -- which is the same
       divergence a composer instance carrying its old state would be,
       arriving through the other host. */
    for (size_t ci = 0; ci < chains_.size(); ci++)
        if (chains_[ci].nodes)
            chains_[ci].nodes->reset();

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
            holding_.clear();   /* every channel says what the file says */
        }
    }

    for (size_t ci = 0; ci < chains_.size(); ci++)
        for (size_t si = 0; si < chains_[ci].stages.size(); si++)
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

            /* Everything the load did, done again to the instance that
               now serves the store -- see replay(). After the swap,
               because the announcements go to whichever instance is
               current. */
            s->params.replay();

            if (s->ticks && s->state != NULL)
            {
                wakeups_.push_back({ 0.0, ci, si, heapSeq_++ });
                std::push_heap(wakeups_.begin(), wakeups_.end(), Later());
            }
        }

    sigReset.emit();
}

void
thcScheduler::setTempo (double bpm)
{
    if (bpm > 0)
        tempo_ = bpm;
}

bool
thcScheduler::usesBeats (void) const
{
    for (size_t ci = 0; ci < chains_.size(); ci++)
    {
        const thcChain &c = chains_[ci];

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

void
thcScheduler::injectMidiEvent (const thcEvent &ev)
{
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
        }
    }
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

    for (size_t i = 0; i < pending_.size(); i++)
        peekCache_.push_back(pending_[i].ev);

    return peekCache_;
}
