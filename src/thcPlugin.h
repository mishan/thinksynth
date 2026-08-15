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

#ifndef TH_CPLUGIN_H
#define TH_CPLUGIN_H 1

#include <string>
#include <vector>

#include "libthink/thcomposer.h"

using std::string;
using std::vector;

/* The host side of one loaded composer module.
 *
 * Deliberately shaped like thVisual -- a path, a dlopen handle, a set of
 * looked-up entry points -- so there is one story in this tree about what
 * a plugin is. Like thVisual it lives in src/ rather than libthink/:
 * composers are loaded by the GUI, never by the parser, and the engine
 * never sees one.
 *
 * The difference from thVisual is that a composer registers *parameters*
 * at init time, with metadata (thcParamDef), and the host has to own
 * copies of everything the module handed it -- the module's strings live
 * in its .rodata and the defs outlive any particular call.
 */
class thcPlugin {
public:
    explicit thcPlugin (const string &path);
    ~thcPlugin (void);

    enum State { LOADED, NOTLOADED };

    /* An owned copy of one registered thcParamDef. */
    struct ParamInfo {
        string       name;
        string       desc;
        thcParamType type;
        double       min, max, def;
        string       defString;      /* NOTESET/STRING default            */
        string       units;          /* "" unitless; "s" is a duration    */

        bool isDuration (void) const { return units == "s"; }
    };

    State state (void) const { return state_; }

    const string &path (void) const { return path_; }

    /* The short name -- "eno_line" -- taken from the filename, which is
       also what a .gen file's `gen::eno_line' will resolve against. */
    const string &name (void) const { return name_; }
    const string &desc (void) const { return desc_; }

    /* THC_GENERATOR / THC_TRANSFORMER, as declared in composer_init.
       Checked against the exported entry points at load: a self-declared
       generator with no composer_tick is refused, loudly, because the
       alternative is a stage that silently never runs. */
    int  flags (void) const { return flags_; }
    bool isGenerator (void) const { return (flags_ & THC_GENERATOR) != 0; }
    bool isTransformer (void) const { return (flags_ & THC_TRANSFORMER) != 0; }

    bool hasTick (void) const { return tick_ != NULL; }
    bool hasReceive (void) const { return receive_ != NULL; }
    bool hasParamChanged (void) const { return paramChanged_ != NULL; }
    bool hasDraw (void) const { return draw_ != NULL; }

    int paramCount (void) const { return (int)params_.size(); }

    /* NULL for an index out of range. */
    const ParamInfo *paramInfo (int index) const
    {
        if (index < 0 || index >= (int)params_.size())
            return NULL;
        return &params_[index];
    }

    /* -1 when no param has that name. */
    int paramIndex (const string &name) const;

    /* ---- instances ---- */

    /* NULL if the module did not load or refuses. `params' must outlive
       the instance -- the module is entitled to keep the pointer. */
    void *create (const thcParams *params);
    void  destroy (void *state);

    double tick (void *state, const thcTransport *t, thcEventSink *out);
    void   receive (void *state, const thcEvent *ev, thcEventSink *out);
    void   paramChanged (void *state, int index);
    void   draw (void *state, cairo_t *cr, double w, double h);

private:
    /* Copying would give two owners of one dlopen handle. */
    thcPlugin (const thcPlugin &);
    thcPlugin &operator= (const thcPlugin &);

    int  moduleLoad (void);
    void moduleUnload (void);

    /* The three host callbacks composer_init reaches us through, with
       `host' pointing back at this. */
    static int  hostRegisterParam (void *host, const thcParamDef *def);
    static void hostSetFlags (void *host, int flags);
    static void hostSetDesc (void *host, const char *description);

    typedef int    (*ComposerInit)    (thcComposerInfo *);
    typedef void  *(*ComposerCreate)  (const thcParams *);
    typedef double (*ComposerTick)    (void *, const thcTransport *,
                                       thcEventSink *);
    typedef void   (*ComposerReceive) (void *, const thcEvent *,
                                       thcEventSink *);
    typedef void   (*ComposerParamChanged) (void *, int);
    typedef void   (*ComposerDraw)    (void *, cairo_t *, double, double);
    typedef void   (*ComposerDestroy) (void *);

    string path_;
    string name_;
    string desc_;

    State state_;
    void *handle_;

    int flags_;
    vector<ParamInfo> params_;

    ComposerCreate       create_;
    ComposerTick         tick_;
    ComposerReceive      receive_;
    ComposerParamChanged paramChanged_;
    ComposerDraw         draw_;
    ComposerDestroy      destroy_;
};

#endif /* TH_CPLUGIN_H */
