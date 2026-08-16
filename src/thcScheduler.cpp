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

#include "thcPlugin.h"
#include "thcScheduler.h"

/* ---- thcParamStore ---------------------------------------------------- */

thcParamStore::thcParamStore (thcPlugin *plugin, unsigned seed)
    : plugin_(plugin), instance_(NULL)
{
    int count = plugin->paramCount();

    values_.resize(count, 0.0);
    strings_.resize(count);
    beats_.resize(count, 0);
    knobs_.resize(count, (thArg *)NULL);

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

    /* A knob binding shadows the stored value entirely: dragging the
       knob is the edit, and there is nothing else to consult. */
    double v = knobs_[index] != NULL ? (double)(*knobs_[index])[0]
                                     : values_[index];

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

    values_[index] = v;

    /* Forward to the module (a no-op when it exports no
       composer_param_changed), then wake it if it was sleeping: the
       contract on THC_NEVER is that a param change re-arms the tick. */
    notifyChanged(index);
}

void
thcParamStore::setString (int index, const std::string &v)
{
    if (index < 0 || index >= (int)strings_.size())
        return;

    strings_[index] = v;

    notifyChanged(index);
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

void
thcParamStore::setBeats (int index, bool beats)
{
    if (index >= 0 && index < (int)beats_.size())
        beats_[index] = beats ? 1 : 0;
}

void
thcParamStore::bindKnob (int index, thArg *knob)
{
    if (index >= 0 && index < (int)knobs_.size())
        knobs_[index] = knob;
}

thArg *
thcParamStore::knobBinding (int index) const
{
    if (index < 0 || index >= (int)knobs_.size())
        return NULL;

    return knobs_[index];
}

void
thcParamStore::notifyChanged (int index)
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
    : synth_(synth), running_(false), transportNow_(0), beat_(0),
      tempo_(120), lastMono_(g_get_monotonic_time()),
      masterSeed_(g_random_int())
{
    timer_ = Glib::signal_timeout().connect(
        sigc::mem_fun(*this, &thcScheduler::timerCallback), 20);
}

thcScheduler::~thcScheduler (void)
{
    timer_.disconnect();
    flushNoteOffs();
    clearChains();
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
        wakeups_.push_back({ transportNow_, chain, stage });
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

    /* Knob-to-param connections point into the stages just destroyed;
       the knobs themselves belong to the piece and go with it. */
    for (size_t i = 0; i < knobConns_.size(); i++)
        knobConns_[i].disconnect();
    knobConns_.clear();

    for (std::map<std::string, thArg *>::iterator i = knobs_.begin();
         i != knobs_.end(); ++i)
        delete i->second;
    knobs_.clear();
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
thcScheduler::bindKnob (thcStage *stage, int paramIndex, thArg *knob)
{
    if (stage == NULL || knob == NULL)
        return;

    stage->params.bindKnob(paramIndex, knob);

    thcParamStore *store = &stage->params;

    knobConns_.push_back(knob->signal_arg_changed().connect(
        [store, paramIndex](thArg *) { store->notifyChanged(paramIndex); }));
}

void
thcScheduler::setMuted (size_t chain, bool muted)
{
    if (chain < chains_.size())
        chains_[chain].muted = muted;
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

    lastMono_ = mono;

    return true;
}

/* One transport step, however time got measured -- the real timer above,
 * or a harness's virtual clock. Everything the step does is keyed in
 * transport seconds, so the outcome depends on dt and nothing else. */
void
thcScheduler::stepTransport (double dt)
{
    if (!running_ || dt < 0)
        return;

    transportNow_ += dt;
    beat_ += dt * tempo_ / 60.0;

    runDueTicks(transportNow_);
    deliverDue(transportNow_);
    sendDueNoteOffs(transportNow_);
}

void
thcScheduler::runDueTicks (double now)
{
    thcTransport t = { now, tempo_, beat_, running_ };

    while (!wakeups_.empty() && wakeups_.front().at <= now)
    {
        std::pop_heap(wakeups_.begin(), wakeups_.end(), Later());
        Wakeup w = wakeups_.back();
        wakeups_.pop_back();

        thcChain &c = chains_[w.chain];
        thcStage *s = c.stages[w.stage].get();

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
            s->sleeping = true;
        else
        {
            wakeups_.push_back({ next > now ? next : now + 0.001,
                                 w.chain, w.stage });
            std::push_heap(wakeups_.begin(), wakeups_.end(), Later());
        }
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
    wakeups_.push_back({ transportNow_, chain, stage });
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

        /* No sinks: the programmatic-chain case; deliver as emitted. */
        if (c.sinks.empty())
        {
            queuePending(ev, NULL);
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

            if ((ev.type == THC_EV_CHANARG) != sink.isChanarg())
                continue;

            thcEvent routed = ev;

            routed.channel = sink.channel;
            queuePending(routed,
                         sink.isChanarg() ? &sink.chanarg : NULL);
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

    pending_.push_back(p);
    std::push_heap(pending_.begin(), pending_.end(), Later());
}

void
thcScheduler::deliverDue (double now)
{
    while (!pending_.empty() && pending_.front().at <= now)
    {
        std::pop_heap(pending_.begin(), pending_.end(), Later());
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

            /* Keyed off the event's own time, not the delivery tick's:
               the off lands exactly duration after the on was scheduled,
               so a replayed piece derives an identical off stream. */
            noteOffs_.push_back({ ev.at + ev.u.note.duration,
                                  ev.channel, ev.u.note.note });
            std::push_heap(noteOffs_.begin(), noteOffs_.end(), Later());
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
thcScheduler::start (void)
{
    lastMono_ = g_get_monotonic_time();
    running_ = true;
}

/* stop() is a pause, but a pause must not hang notes: flush every
 * derived off immediately. pending_ and wakeups_ are keyed in transport
 * time, which has stopped advancing, so they keep on their own. */
void
thcScheduler::stop (void)
{
    running_ = false;
    flushNoteOffs();
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

    for (size_t ci = 0; ci < chains_.size(); ci++)
        for (size_t si = 0; si < chains_[ci].stages.size(); si++)
        {
            thcStage *s = chains_[ci].stages[si].get();

            s->plugin->destroy(s->state);
            s->state = s->plugin->create(s->params.params());
            s->params.instance_ = s->state;
            s->sleeping = false;

            if (s->ticks && s->state != NULL)
            {
                wakeups_.push_back({ 0.0, ci, si });
                std::push_heap(wakeups_.begin(), wakeups_.end(), Later());
            }
        }
}

void
thcScheduler::setTempo (double bpm)
{
    if (bpm > 0)
        tempo_ = bpm;
}

void
thcScheduler::injectMidi (size_t chainIndex, const thcEvent &ev)
{
    if (chainIndex >= chains_.size())
        return;

    propagate(chains_[chainIndex], 0, ev);
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
            propagate(c, 0, ev);
    }
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
