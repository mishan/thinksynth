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

/* The composer plugin interface.
 *
 * A composer is a shared library dropped into the composer plugin
 * directory, exactly as DSP nodes are. Two kinds share one interface:
 *
 *   generators   turn a clock into events (eno lines, euclid, markov, CA)
 *   transformers turn events into events (scale quantize, transpose,
 *                probability gate, humanize, echo)
 *
 * A generator exports composer_tick and ignores composer_receive; a pure
 * transformer exports composer_receive and may omit composer_tick. A
 * plugin may export both (an arpeggiator receives held notes and ticks
 * out the arpeggio).
 *
 * A separate ABI from thPlugin's for the same reason thVisual's is: a DSP
 * plugin is handed a node in a graph and expected to write an arg per
 * window. A composer has no node, writes no arg, and wants a clock and an
 * event sink instead. Overloading one onto the other would make every
 * composer pretend to be a graph node.
 *
 * THREADING. The entire composition layer lives on the GUI thread, on
 * the same side of the house as MIDI input. tick/receive/param access
 * and composer_draw never run concurrently, so instance state needs no
 * locking. The scheduler is what pushes resulting notes through the
 * existing GUI->audio command queue; a composer never touches the synth.
 *
 * TIME. All event times are absolute transport seconds. The scheduler
 * owns tempo; clocked composers convert via the transport's beat/tempo
 * fields, free-running ones just add seconds. Events may be emitted for
 * any time >= transport->now; the scheduler queues the future.
 *
 * DETERMINISM. Composers that use randomness must draw it from the seed
 * given at create time (e.g. seed a local PRNG), never from global
 * state, so a piece can be replayed exactly.
 */

#ifndef THCOMPOSER_H
#define THCOMPOSER_H

#include "thExport.h"          /* THINK_PLUGIN_API */

#ifdef __cplusplus
extern "C" {
#endif

/* 2: phase 4 put `patch' and `nodearg' in thcEvent's union, and
 * nodearg is two pointers and a float where the widest arm had been
 * sixteen bytes -- so thcEvent itself grew, and a .so built against
 * version 1 has a smaller one. The host reads what a sink is handed by
 * dereferencing the pointer at *its* size, which is a read off the end
 * of a v1 plugin's event. The gate in thcPlugin::moduleLoad turns that
 * into a refusal at load with both numbers printed, which is the whole
 * reason the byte is checked before anything is called.
 *
 * Note the difference from the additive changes below: an enum gaining
 * a value keeps every existing value's number and every existing
 * struct's layout, and those did not need a bump. A union arm that
 * changes sizeof does. */
#define COMPOSER_IFACE_VER 2

typedef struct _cairo cairo_t;  /* drawing is optional; no hard cairo dep */

/* ---- events ---------------------------------------------------------- */

typedef enum {
    THC_EV_NOTE = 0,     /* one note; the scheduler derives the note-off  */
    THC_EV_CHANARG,      /* set a patch @chanarg -- generative timbre     */

    /* Releases a held note -- the pair to a NOTE whose duration is <= 0,
     * which means "held until further notice". Composed material almost
     * never needs the pair (durations are the composed way to say when a
     * note ends); it exists because live MIDI has no idea how long a key
     * will stay down, and an arpeggiator has to know what is held NOW.
     * A NOTEOFF uses u.note.note; velocity and duration are ignored. */
    THC_EV_NOTEOFF,

    /* ---- structure edits ---------------------------------------------
     *
     * UNIFICATION.md phase 4: a composer reshaping the instrument rather
     * than playing it. The two below are the coarse end and the fine end
     * of the same idea, and both are *intents* -- a plugin says what it
     * wants to be true and the host does it. A composer cannot link
     * libthink and never touches a graph; that is the same bargain a
     * sink already expresses, and it is what keeps graph pointers out of
     * plugin code.
     *
     * Being events is the whole rate-limit. A structure edit is
     * scheduled, sparse, replayed with the seed, and drawn on the roll
     * like everything else, so a composer cannot thrash the graph faster
     * than the event stream flows.
     */

    /* This channel becomes that instrument, at that time.
     *
     * `u.patch.name' is one of the instruments the piece declares -- see
     * THC_PARAM_INSTRSET for how a plugin comes to know the names
     * without ever looking one up. The host rebuilds and swaps through
     * the ordinary patch-load path.
     *
     * WHAT A SWAP DOES TO A SOUNDING NOTE: it cuts it. That path
     * replaces the channel rather than retiring it gently -- anything
     * sounding on the old graph stops at the window the swap lands on,
     * with no release. What loadTree promises is that the outgoing
     * channel is not freed under the audio thread, which is a promise
     * about lifetimes and not the one a note is asking about; this said
     * otherwise for a while, on the strength of the two being confused.
     *
     * So a swap is a coarse edit and wants a clock measured in tens of
     * seconds, or a channel that is resting. THC_EV_NODEARG below is
     * the one that leaves sounding voices alone, and does it by
     * touching a tree the audio thread never reads rather than by
     * arranging anything. */
    THC_EV_PATCH,

    /* One constant inside that instrument's graph becomes this.
     *
     * `node' and `arg' name a node and an arg *in the .dsp* -- not a
     * chanarg. That is the entire point, and the line COMPOSITION_HANDOFF
     * §9 drew: a chanarg is the surface a patch chose to expose, and the
     * reach of every composer up to now. This is the other mechanism §9
     * promised rather than a widening of that one, and a piece using it
     * is reaching past what the instrument declared -- deliberately,
     * visibly, and on the piece's own say-so.
     *
     * The edit lands on the channel's prototype tree, which the audio
     * thread never reads, so the lifecycle promise above holds here too
     * without a swap: it is the *next* voice that is built differently. */
    THC_EV_NODEARG
} thcEventType;

typedef struct {
    thcEventType type;
    double       at;         /* absolute transport seconds                */
    int          channel;    /* MIDI channel 0-15                         */
    union {
        struct {
            int    note;       /* MIDI note number                        */
            int    velocity;   /* 1-127                                   */
            double duration;   /* seconds until note-off; <= 0: held
                                  until a matching THC_EV_NOTEOFF        */
        } note;
        struct {
            const char *name;  /* @chanarg name; copied by the sink       */
            float       value;
        } chanarg;
        struct {
            const char *name;  /* an instrument the piece declares;
                                  copied by the sink                      */
        } patch;
        struct {
            const char *node;  /* a node in the instrument's .dsp; copied */
            const char *arg;   /* one of that node's args; copied         */
            float       value;
        } nodearg;
    } u;
} thcEvent;

/* Host-provided. emit() copies the event; the composer keeps ownership
 * of nothing. Transformers re-emit modified copies of what they receive,
 * emit extra events (echo), or emit nothing (gate). */
typedef struct {
    void  *ctx;
    void (*emit)(void *ctx, const thcEvent *ev);
} thcEventSink;

/* ---- transport ------------------------------------------------------- */

typedef struct {
    double now;       /* seconds since transport start                    */
    double tempo;     /* beats per minute                                 */
    double beat;      /* now, expressed in beats                          */
    int    running;   /* 0 while paused; ticks still fire for UI updates  */
} thcTransport;

/* ---- parameters ------------------------------------------------------ */
/* Registered once in composer_init with full metadata -- this is what
 * the parameter panel and the .gen parser are driven by. register_param
 * returns the integer index used with thcParams at run time, mirroring
 * regArg in the DSP interface. */

typedef enum {
    THC_PARAM_FLOAT = 0,
    THC_PARAM_INT,
    THC_PARAM_NOTE,      /* a pitch; UI shows note names                  */

    /* A pitch pool. The string a plugin reads through get_string is a
     * comma-separated list of resolved MIDI note numbers ("53,56,60") --
     * the host parses note names and scale references once, at the file
     * boundary, and no plugin ever parses pitch text. */
    THC_PARAM_NOTESET,

    THC_PARAM_STRING,    /* free text, e.g. an L-system axiom             */

    /* A named chanarg vector -- a *preset*. The string a plugin reads
     * through get_string is the resolved vector, "res=0.8,fmin=0.06", in
     * the order the piece declared it. Same bargain as NOTESET: a .gen
     * file writes the preset's name, the host looks it up once at the
     * file boundary, and no plugin ever resolves a preset.
     *
     * The noun exists because a patch's declared chanargs are a vector of
     * floats and a vector of floats is a genome. A morph interpolating
     * between two of them, a GA breeding populations of them, a Markov
     * chain walking a set of them -- all of it needs something to refer
     * to, splice and save. This is that something, and it is the *only*
     * reach a composer has into an instrument: the args a patch chose to
     * declare, no deeper. See COMPOSITION_HANDOFF.md §9.
     *
     * Appended rather than slotted in beside NOTESET on purpose -- an
     * enum whose existing values keep their numbers is an additive
     * change, and needed no interface bump of its own. (The interface
     * is at 2 as of phase 4, for a reason that is about thcEvent's
     * size rather than about this enum -- see COMPOSER_IFACE_VER.) */
    THC_PARAM_PRESET,

    /* The instruments a piece declares, by name. The string a plugin
     * reads through get_string is the resolved list, "voice,bell,glass",
     * in the order the file wrote them.
     *
     * Same bargain as NOTESET and PRESET, one more time: a .gen names
     * the instruments, the host checks at the file boundary that each
     * one exists, and no plugin ever looks an instrument up. What a
     * composer does with them is pick one and put its name in a
     * THC_EV_PATCH -- which is as close as it comes to touching an
     * instrument, and closer than it can come to touching a graph. */
    THC_PARAM_INSTRSET
} thcParamType;

typedef struct {
    const char  *name;        /* key in .gen files and the UI label       */
    const char  *desc;
    thcParamType type;
    double       min, max, def;   /* numeric types only                   */
    const char  *def_string;      /* NOTESET/STRING default               */

    /* NULL for a unitless number. "s" declares a duration: the plugin
     * reads seconds, and a .gen file must write the value with a unit
     * (s, ms, beats) -- a bare number on a duration is a load error,
     * and a beat-valued duration is converted at read time through the
     * transport's tempo, which is what makes the same plugin clocked or
     * free-running depending on what the value says. */
    const char  *units;
} thcParamDef;

#define THC_GENERATOR   (1 << 0)
#define THC_TRANSFORMER (1 << 1)

/* Passed to composer_init. */
typedef struct {
    void *host;
    int  (*register_param)(void *host, const thcParamDef *def);
    void (*set_flags)     (void *host, int flags);
    void (*set_desc)      (void *host, const char *description);
} thcComposerInfo;

/* Passed to composer_create; live for the instance's lifetime. Values
 * reflect GUI edits immediately, so a composer that reads its params
 * inside tick() picks up changes with no extra machinery. */
typedef struct {
    void        *ctx;
    double      (*get)       (void *ctx, int index);
    const char *(*get_string)(void *ctx, int index);
    unsigned     seed;        /* per-instance; stable across a replay     */
} thcParams;

/* tick()'s "do not wake me again" -- sleep until composer_param_changed
   re-arms the composer. */
#define THC_NEVER (-1.0)

/* ---- input ------------------------------------------------------------ */

/* A gesture the host is passing on, for composers whose state is worth
 * touching directly.
 *
 * COMPOSITION_HANDOFF.md §7 argued for this twice and named the same
 * motivating case both times: `composer_draw' is draw-only, and the
 * things it draws -- a CA's grid, a Life board, a Euclid ring -- are
 * exactly the things a person wants to reach into. Interactive
 * evolution wants it too (the user as the fitness function), and a
 * param is a poor substitute: params are continuous knobs, and
 * selection is an event.
 *
 * The coordinates are the ones composer_draw was handed, in the same
 * space and the same units, so a plugin maps a click by inverting the
 * arithmetic it already wrote to draw with. `w' and `h' come along
 * because the draw's size is the host's business and can change between
 * one frame and the next -- an enlarged view is the same draw at a
 * different size, and a plugin that cached the last w it drew at would
 * be wrong exactly when someone was looking closely.
 *
 * THREADING is the GUI thread, like everything else in this layer, so
 * input may touch instance state directly and tick() will see it.
 *
 * DETERMINISM has the same boundary live MIDI has, and for the same
 * reason: a replay is exact only if the inputs are. A piece nobody
 * clicked replays from its seed; a piece somebody clicked replays given
 * the same clicks, which is what the gate feeds it. That is not a flaw,
 * it is what "replay the piece" honestly means once a person is part of
 * the piece. */
typedef enum {
    THC_IN_PRESS = 0,    /* button went down at (x, y)                  */
    THC_IN_DRAG,         /* still down, now at (x, y)                   */
    THC_IN_RELEASE
} thcInputType;

typedef struct {
    thcInputType type;
    double       x, y;      /* in composer_draw's coordinates           */
    double       w, h;      /* the size that draw was last given         */
    int          button;    /* 1 primary, 3 secondary                   */
} thcInputEvent;

#ifdef __cplusplus
}
#endif

/* ---- plugin exports --------------------------------------------------
 *
 * The entry points thcPlugin looks up by name. Only composer_init,
 * composer_create and composer_destroy are mandatory; a plugin exports
 * composer_tick and/or composer_receive according to its role, and the
 * host checks which are present against the flags it declared.
 */
#ifdef COMPOSER_PLUGIN_BUILD

/* Same story as THINK_VISUAL_KEEP in thVisual.h: an inline variable is
 * emitted only if something odr-uses it, and nothing in a plugin ever
 * mentions its own version byte -- `used' is what says "emit this
 * anyway", on every GCC-family compiler, Windows included. */
#ifdef __GNUC__
#  define THINK_COMPOSER_KEEP __attribute__((used))
#else
#  define THINK_COMPOSER_KEEP
#endif

extern "C" {
    /* The version byte thcPlugin::moduleLoad checks before anything else. */
    THINK_PLUGIN_API THINK_COMPOSER_KEEP inline unsigned char
        composer_apiversion = COMPOSER_IFACE_VER;

    /* Register params, set flags/description. Once per dlopen.
       Return 0 on success; non-zero refuses the load. */
    THINK_PLUGIN_API int composer_init (thcComposerInfo *info);

    /* Allocate instance state. `params' outlives the instance. Multiple
       instances of one plugin may exist in a chain. */
    THINK_PLUGIN_API void *composer_create (const thcParams *params);

    /* Generators only. Called when t->now reaches the previously
       requested wakeup. Emit zero or more events (at >= t->now) and
       return the absolute time of the next wanted wakeup. Return
       THC_NEVER to sleep until a param change re-arms the composer.
       First call comes at transport start. */
    THINK_PLUGIN_API double composer_tick (void *state,
                                           const thcTransport *t,
                                           thcEventSink *out);

    /* Transformers only. Called once per event arriving from the
       upstream stage -- which may also be *live MIDI input*, so a
       Markov composer can train on playing, an arpeggiator can hold
       real keys. Emit whatever should continue downstream. */
    THINK_PLUGIN_API void composer_receive (void *state,
                                            const thcEvent *ev,
                                            thcEventSink *out);

    /* Optional. For composers that must rebuild on an edit (retrain,
       re-derive an L-system) rather than just read the new value.
       Also re-arms a THC_NEVER sleeper: its tick is called again. */
    THINK_PLUGIN_API void composer_param_changed (void *state, int index);

    /* Optional, the tier-two visualizer: the euclid ring, the CA grid,
       the transition graph. Same thread as tick/receive, so it may
       read instance state directly -- no snapshot copying. The tier-one
       piano roll is the host's job and needs nothing from the plugin. */
    THINK_PLUGIN_API void composer_draw (void *state, cairo_t *cr,
                                         double w, double h);

    /* Optional. A gesture on the area composer_draw painted, in the
       coordinates it painted them in. Exporting this is what makes a
       visualizer a control; a plugin without one is drawn and not
       touched, exactly as before. */
    THINK_PLUGIN_API void composer_input (void *state,
                                          const thcInputEvent *ev);

    /* Optional, and the other half of composer_input: what the plugin's
       touchable state currently is, as text a .gen file can carry.
     *
     * Deliberately not the opaque serialize/deserialize blob §7
     * sketched for a trained Markov table. This returns the value of one
     * of the plugin's *own params* -- `index' says which -- so a host
     * writes it back through the ordinary param path and the file stays
     * something a person can read and edit. It works because the state
     * worth clicking is usually small and already has a spelling: a Life
     * board is a pattern, and a pattern is a string.
     *
     * The returned pointer belongs to the plugin and must stay valid
     * until the next call. Return NULL for a param that has nothing to
     * capture. */
    THINK_PLUGIN_API const char *composer_capture (void *state, int index);

    THINK_PLUGIN_API void composer_destroy (void *state);
}
#endif /* COMPOSER_PLUGIN_BUILD */

#endif /* THCOMPOSER_H */
