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

#define COMPOSER_IFACE_VER 1

typedef struct _cairo cairo_t;  /* drawing is optional; no hard cairo dep */

/* ---- events ---------------------------------------------------------- */

typedef enum {
    THC_EV_NOTE = 0,     /* one note; the scheduler derives the note-off  */
    THC_EV_CHANARG       /* set a patch @chanarg -- generative timbre     */
} thcEventType;

typedef struct {
    thcEventType type;
    double       at;         /* absolute transport seconds                */
    int          channel;    /* MIDI channel 0-15                         */
    union {
        struct {
            int    note;       /* MIDI note number                        */
            int    velocity;   /* 1-127                                   */
            double duration;   /* seconds until note-off                  */
        } note;
        struct {
            const char *name;  /* @chanarg name; copied by the sink       */
            float       value;
        } chanarg;
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

    THC_PARAM_STRING     /* free text, e.g. an L-system axiom             */
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

    THINK_PLUGIN_API void composer_destroy (void *state);
}
#endif /* COMPOSER_PLUGIN_BUILD */

#endif /* THCOMPOSER_H */
