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
 */

/* harmonize -- one note becomes a chord.
 *
 * Diatonic, not chromatic, and that is the whole design. A harmonizer
 * that adds a fixed number of semitones is a pitch shifter: it moves the
 * line sideways and every chord it makes is the same chord. This one
 * counts *scale degrees*, so a third above the first degree and a third
 * above the second are different intervals -- major here, minor there --
 * and the chord quality falls out of where in the scale the melody
 * happens to be. That is what makes a harmonized line sound like it was
 * written rather than processed.
 *
 * The root's own pitch is never retuned. A note the scale cannot spell
 * still gets a chord stacked on it, built from the scale tones above
 * where it sits; correcting the melody is `gen::quantize's job and a
 * piece that wants both says both, in the order it wants them. Two
 * plugins each doing one thing is the rule the tree keeps.
 *
 * `spread' turns the chord into a roll -- each voice a little later than
 * the one below -- which is the difference between a piano chord and a
 * harp. `taper' leans the velocity off the top so the added voices sit
 * under the melody instead of burying it. `below' stacks downward
 * instead, which turns a melody into the top of its own harmony.
 *
 * HELD NOTES. A THC_EV_NOTEOFF has to release exactly the pitches its
 * THC_EV_NOTE pressed, so the chord is remembered per sounding root
 * rather than recomputed at release time: `voices' or `scale' moving
 * while a note is held would otherwise send offs for pitches nothing
 * ever played and leave the real ones hanging forever. `gen::quantize'
 * makes the same argument about the same event and gets away with
 * recomputing because its answer does not depend on a param that a hand
 * on a slider can move mid-note. This one's does.
 *
 * Three ways that bookkeeping can go wrong, all of which it did:
 *
 * - A pitch pressed twice before either release. The second press used
 *   to overwrite the first, so the first release took the whole chord
 *   down and the second found nothing and forwarded a bare off for the
 *   root -- the added voices of one of the two presses hung forever.
 *   The second press now takes the first one down as it arrives, which
 *   keeps ons and offs one to one. `gen::arp' solved the same problem
 *   the same way.
 * - `spread'. Each voice sounds `spread' later than the one below, so
 *   each has to be *released* later by the same amount, or a key held
 *   for less than the roll's length sends the offs before their own
 *   ons and every voice above the first hangs. The offset is
 *   remembered beside the pitch rather than recomputed, for the same
 *   reason the pitch is.
 * - A press that emitted nothing -- `root = 0' with one voice, or a
 *   chord that ran off the top of the keyboard. There was no entry to
 *   find, so the release fell through to "an off with no on" and
 *   forwarded the root's pitch: an off for a note this stage
 *   deliberately did not play, which the scheduler passes to delNote
 *   and which can silence another stage's note at that pitch. An empty
 *   chord is now recorded as an empty chord, and releases nothing.
 *
 * DETERMINISM. No randomness at all: the chord is a function of the
 * pitch, the scale and the params. Replay is free.
 */

#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <vector>

#include "thcomposer.h"

enum { P_SCALE, P_VOICES, P_STEP, P_SPREAD, P_TAPER, P_ROOT, P_BELOW,
       P_COUNT };

static int paramIndex[P_COUNT];

extern "C" THINK_PLUGIN_API int
composer_init (thcComposerInfo *info)
{
    static const thcParamDef defs[P_COUNT] = {
        { "scale",  "the pitch set chords are spelled from",
          THC_PARAM_NOTESET, 0, 0, 0, "48,50,51,53,55,56,58", NULL },

        /* Including the root, so `voices = 1' is a passthrough and
           `voices = 3' is the triad everybody means by a triad. Counting
           only the *added* voices would make 3 a seventh chord and every
           reading of the file a subtraction. */
        { "voices", "notes in the chord, root included", THC_PARAM_INT,
          1, 6, 3, NULL, NULL },

        /* Two degrees is a third, which is why thirds stack into triads.
           1 gives clusters, 3 gives quartal voicings, 4 is a stack of
           fifths -- all of which are worth hearing, which is why this is
           a knob and not a hardcoded 2. */
        { "step",   "scale degrees between voices; 2 stacks thirds",
          THC_PARAM_INT, 1, 7, 2, NULL, NULL },
        { "spread", "time between voices; 0 is a block chord",
          THC_PARAM_FLOAT, 0, 4, 0, NULL, "s" },
        { "taper",  "velocity multiplier per voice away from the root",
          THC_PARAM_FLOAT, 0.1, 1, 0.85, NULL, NULL },
        { "root",   "1: the incoming note sounds too; 0: only the "
          "harmony does", THC_PARAM_INT, 0, 1, 1, NULL, NULL },
        { "below",  "1: stack downward, so the melody is the top voice",
          THC_PARAM_INT, 0, 1, 0, NULL, NULL },
    };

    for (int i = 0; i < P_COUNT; i++)
        paramIndex[i] = info->register_param(info->host, &defs[i]);

    info->set_flags(info->host, THC_TRANSFORMER);
    info->set_desc(info->host,
        "Turn each note into a chord built from a scale, not from "
        "fixed intervals.");

    return 0;
}

struct State {
    const thcParams *params;

    /* The scale as pitch *classes*. Octaves are the melody's business:
       a scale written across one octave should harmonize a line three
       octaves up, and writing the same seven notes out eight times over
       to say so would be a file nobody could edit. */
    bool pc[12];
    int  pcCount;

    /* One voice of a chord that is still down: which pitch went out,
       and how far after the root it went, so the release can be offset
       by the same amount. */
    struct Voice
    {
        int    note;
        double after;
    };

    /* What each sounding root actually pressed, so its release names the
       same pitches at the same spacing. Keyed by root pitch, which is
       what the off will arrive carrying. */
    std::map<int, std::vector<Voice> > sounding;

    /* Every root this instance has ever had down, which is how an off
       with nothing to release tells its two cases apart.
     *
       An off for a root that was never pressed here is an orphan --
       something was sounding before this instance existed, or a chain
       was rebuilt around a held key -- and forwarding it is the best
       guess available: one note left ringing is worse than one
       forwarded off. An off for a root that *was* pressed here and has
       already been released is not an orphan, it is a second off for
       one note, and forwarding it sends delNote after a pitch that may
       now belong to another stage on the same channel. A keyboard
       generates exactly that whenever a key is struck twice before it
       is let go, because the second press takes the first chord down
       and the two offs that follow have one chord between them. */
    std::set<int> seen;

    void reparse (void);
};

void
State::reparse (void)
{
    const char *s = params->get_string(params->ctx, paramIndex[P_SCALE]);

    for (int i = 0; i < 12; i++)
        pc[i] = false;

    pcCount = 0;

    while (s && *s)
    {
        const int n = atoi(s);

        if (n >= 0 && n <= 127 && !pc[n % 12])
        {
            pc[n % 12] = true;
            pcCount++;
        }

        if ((s = strchr(s, ',')))
            s++;
        else
            break;
    }

    /* A scale with nothing in it would make every chord a unison, which
       reads as "harmonize is broken" rather than as "the scale is
       empty". The chromatic scale is the honest fallback: every step is
       a semitone, which is what a set of no pitch classes means when you
       ask it for the note above. */
    if (pcCount == 0)
    {
        for (int i = 0; i < 12; i++)
            pc[i] = true;

        pcCount = 12;
    }
}

/* The nth scale tone strictly above (or below) `note'.
 *
 * Counted by walking semitones and stopping on the ones the scale
 * contains, rather than by indexing a degree table, because the root may
 * not be in the scale at all -- and "which degree is F-sharp in D minor"
 * has no answer, while "the second scale tone above F-sharp" has an
 * obvious one. The walk is at most 127 steps and happens once per voice
 * per note; the arithmetic is not where anybody's time goes.
 */
static int
walkTo (const State *st, int note, int n, bool down)
{
    int at = note;

    while (n > 0)
    {
        at += down ? -1 : 1;

        if (at < 0 || at > 127)
            return -1;

        if (st->pc[at % 12])
            n--;
    }

    return at;
}

extern "C" THINK_PLUGIN_API void *
composer_create (const thcParams *params)
{
    State *st = new State;

    st->params = params;
    st->reparse();

    return st;
}

extern "C" THINK_PLUGIN_API void
composer_destroy (void *state)
{
    delete static_cast<State *>(state);
}

extern "C" THINK_PLUGIN_API void
composer_param_changed (void *state, int index)
{
    if (index == paramIndex[P_SCALE])
        static_cast<State *>(state)->reparse();
}

/* Take a chord down: an off for every voice, each as far after `at' as
 * its on was after the press. Used by a release, and by a second press
 * of a pitch that is already down. */
static void
release (State *st, int root, const thcEvent &like, double at,
         thcEventSink *out)
{
    std::map<int, std::vector<State::Voice> >::iterator it =
        st->sounding.find(root);

    if (it == st->sounding.end())
        return;

    for (size_t i = 0; i < it->second.size(); i++)
    {
        thcEvent off = like;

        off.type = THC_EV_NOTEOFF;
        off.at = at + it->second[i].after;
        off.u.note.note = it->second[i].note;

        out->emit(out->ctx, &off);
    }

    st->sounding.erase(it);
}

extern "C" THINK_PLUGIN_API void
composer_receive (void *state, const thcEvent *ev, thcEventSink *out)
{
    State *st = static_cast<State *>(state);
    const thcParams *p = st->params;

    /* Pitch is the only thing this has an opinion about. */
    if (ev->type != THC_EV_NOTE && ev->type != THC_EV_NOTEOFF)
    {
        out->emit(out->ctx, ev);
        return;
    }

    const bool on = ev->type == THC_EV_NOTE;
    const int  root = ev->u.note.note;

    /* A release replays the press, note for note and offset for offset. */
    if (!on)
    {
        if (st->sounding.find(root) == st->sounding.end())
        {
            /* Nothing to release. Forward it only if this stage has
               never had that root down -- see `seen' above for why the
               two cases want opposite answers. A press this stage
               swallowed on purpose is not the orphan case either: it
               recorded an empty chord, so it never reaches here. */
            if (!st->seen.count(root))
                out->emit(out->ctx, ev);

            return;
        }

        release(st, root, *ev, ev->at, out);
        return;
    }

    /* Pressed again without being released. Take the first one down
       before the second goes up, so every on has exactly one off. */
    release(st, root, *ev, ev->at, out);

    int voices = (int)p->get(p->ctx, paramIndex[P_VOICES]);
    int step   = (int)p->get(p->ctx, paramIndex[P_STEP]);

    if (voices < 1)
        voices = 1;

    if (step < 1)
        step = 1;

    const double spread = p->get(p->ctx, paramIndex[P_SPREAD]);
    const double taper  = p->get(p->ctx, paramIndex[P_TAPER]);
    const bool   keep   = p->get(p->ctx, paramIndex[P_ROOT]) != 0;
    const bool   down   = p->get(p->ctx, paramIndex[P_BELOW]) != 0;

    std::vector<State::Voice> pressed;
    double vel = ev->u.note.velocity;

    for (int v = 0; v < voices; v++)
    {
        const int note = v == 0 ? root : walkTo(st, root, v * step, down);

        /* Off the end of the keyboard. Stop rather than clamp: a stack
           of thirds that piles three voices onto note 127 is a chord
           with a unison in it, which sounds like a bug and is one. */
        if (note < 0)
            break;

        /* `root = 0' drops the melody and keeps its harmony -- but the
           root still had to be walked from, and its velocity still sets
           the taper, so it is skipped here rather than earlier. */
        if (v == 0 && !keep)
        {
            vel *= taper;
            continue;
        }

        thcEvent copy = *ev;

        copy.u.note.note = note;
        copy.u.note.velocity = (int)(vel + 0.5) < 1 ? 1 : (int)(vel + 0.5);

        if (copy.u.note.velocity > 127)
            copy.u.note.velocity = 127;

        /* A roll, if asked for. Later voices arrive later; the durations
           are untouched, so a spread chord ends ragged the way a rolled
           chord on a real instrument does. */
        copy.at = ev->at + spread * v;

        out->emit(out->ctx, &copy);

        State::Voice held;

        held.note = note;
        held.after = spread * v;

        pressed.push_back(held);

        vel *= taper;
    }

    /* Only a note the host will ask us to release needs remembering. A
       note carrying its own duration gets its off derived downstream
       from the pitch that was delivered, at the time it was delivered,
       which is already right for every voice.
     *
       Recorded even when it is empty. `pressed' is empty when this
       stage decided to play nothing -- `root = 0' with a single voice,
       or a chord that walked off the top of the keyboard -- and the
       release still has to find something, or it takes the "off with no
       on" branch and forwards an off for a pitch this stage never
       played. */
    if (ev->u.note.duration <= 0)
    {
        st->sounding[root] = pressed;
        st->seen.insert(root);
    }
}
