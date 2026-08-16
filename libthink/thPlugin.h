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

#ifndef TH_PLUGIN_H
#define TH_PLUGIN_H 1

#include "thExport.h"

class thSynthTree;
class thNode;

/* Bumped from 4 when arg metadata grew a step and a set of value names.
 *
 * Direction did not need a bump: regArg() gained a defaulted parameter and
 * nothing about thPlugin's shape changed, so a plugin built against the older
 * header still loaded and simply reported every arg as an input. This one is
 * different. The three parallel arrays behind regArg() became one vector, which
 * moves every member after them -- and a plugin calls setDesc() and setState(),
 * which are inlined into the plugin from this header and reach their members by
 * offset.
 *
 * As it happens those two sit *before* what moved, so the mismatch would
 * probably go unnoticed. "Probably" is the problem: that is precisely the shape
 * of the stale-libthink bug ARCHITECTURE.md describes, where a binary linked
 * against the wrong library read every header-inlined accessor at the wrong
 * offset and returned garbage without a diagnostic. This check exists to make
 * that loud, and a layout change is what it is for. Stale plugin .so files left
 * in a source tree are a thing that actually happens here -- NodePalette's own
 * tooltip tells people to go and delete them.
 */
#define MODULE_IFACE_VER 5

/* We don't want this to exist unless we're using a plugin.
 *
 * These four are what the host looks up by name, so they are the plugin's
 * entire ABI and the only symbols it needs to export. Declaring them here
 * with THINK_PLUGIN_API means all 66 plugins get the export attribute from
 * the header rather than each having to say so -- on Windows the attribute
 * on a prior declaration is what counts, and every plugin includes think.h
 * before defining them.
 *
 * module_cleanup used to be declared `void module_cleanup(struct module *)'
 * -- and `struct module' is a type that exists nowhere in the tree; it was
 * being forward-declared into existence by that parameter list. Meanwhile
 * thPlugin.cpp casts the looked-up symbol to ModuleCleanup, which is
 * void (*)(thPlugin *), and calls it with `this'. So every plugin's cleanup
 * hook was called through a function pointer of the wrong type on every
 * unload. It survived only because all 66 bodies are empty. The declaration
 * now matches the typedef and the call.
 */
#ifdef PLUGIN_BUILD
THINK_PLUGIN_API unsigned char apiversion = MODULE_IFACE_VER;
class thPlugin;

/* Provide the prototypes */
extern "C" {
    THINK_PLUGIN_API int  module_init (thPlugin *plugin);
    THINK_PLUGIN_API int  module_callback (thNode *node, thSynthTree *mod,
                                           unsigned int windowlen,
                                           unsigned int samples);
    THINK_PLUGIN_API void module_cleanup (thPlugin *plugin);
}
#endif

class THINK_API thPlugin {
public:
    thPlugin (const string &path);
    ~thPlugin ();

    enum State { ACTIVE, PASSIVE, NOTLOADED };

    /* Which way an arg faces.
     *
     * Every plugin has always encoded this in the enum it indexes args[] with
     * -- IN_FREQ, OUT_ARG, INOUT_LAST -- but the information stopped at the
     * enum and never reached anything that could use it. regArg() took only a
     * name, so nothing outside a plugin could tell an input from an output,
     * which is what appeared to block building a node editor.
     *
     * STATE is the INOUT_ case: scratch a plugin keeps between windows --
     * delay rings, envelope positions, filter history. It is emphatically not
     * a port. No .dsp in the shipped corpus references one, and an editor must
     * not offer them for wiring.
     *
     * The default is ARG_IN so that a plugin built against the old header
     * still compiles; its args will all read as inputs, which is wrong but
     * harmless, and every in-tree plugin passes a direction explicitly.
     */
    enum ArgDir { ARG_IN = 0, ARG_OUT, ARG_STATE };

    /* Everything a plugin can say about one of its args.
     *
     * Direction was the first field and for a long time the only one. This is
     * the rest of the proposal ARCHITECTURE.md deferred, and the two new fields
     * answer the same question at different resolutions.
     *
     * `step' is 0 for an ordinary continuous parameter and 1 for one that means
     * a whole number. That is not a display preference: `waveform' is read
     * `switch ((int)buf_waveform[i])', so 3.4 *is* 3, and a control that can
     * produce 3.4 is a control most of whose travel does nothing. The eight
     * shipped patches that drive a waveform all declare `.max = 5.1' or `5.5'
     * rather than 5, which is an author working around exactly that -- padding
     * the top of a continuous slider so it can still truncate to the last
     * waveform.
     *
     * `values' names the whole numbers when they mean something sayable:
     * "Sine", "Sawtooth", "Square". It implies a step of 1 and a range of
     * its first named entry to its last, because a name for every value is a
     * stronger statement than either.
     *
     * The length is a statement in its own right: it says how many indices the
     * arg has, which is not always how many are worth offering. osc::window's
     * switch spans six and implements three, so its list is six long with three
     * holes -- shortening it to four would quietly redefine what a 5 means.
     *
     * Both are advice to whatever draws the control. Nothing in the audio path
     * reads them, and a plugin that says nothing gets what it always got.
     */
    struct ArgInfo {
        string name;
        ArgDir dir;
        float step;
        vector<string> values;

        ArgInfo (const string &n, ArgDir d) : name(n), dir(d), step(0) {}
    };

    typedef int (*Callback)(thNode *,thSynthTree *,unsigned int, unsigned int);
    typedef int (*ModuleInit)(thPlugin *);
    typedef void (*ModuleCleanup)(thPlugin *);

    const string &path (void) const { return path_; };
    const string &desc (void) const { return desc_; };
    State state (void) const { return state_; };
    
    void setDesc(const string &desc) { desc_ = desc; }
    void setState(State state) { state_ = state; };
    
    int regArg (const string &argname, ArgDir dir = ARG_IN);

    /* Says the arg means a whole number. Separate from regArg() rather than
       another defaulted parameter on it, because the overwhelming majority of
       args are continuous and a call site that says nothing should look like
       one that has nothing to say. */
    void setArgStep (int index, float step);

    /* Names the arg's values, from 0 upwards. Implies setArgStep(index, 1).
       `names' is copied. */
    void setArgValues (int index, const char *const *names, int count);

    int argCount (void) const { return (int)args_.size(); };
    string getArgName (int index) const {
        if (index >= 0 && index < (int)args_.size())
            return args_[index].name;
        return "";
    }

    ArgDir getArgDir (int index) const {
        if (index >= 0 && index < (int)args_.size())
            return args_[index].dir;
        return ARG_IN;
    }

    float getArgStep (int index) const {
        if (index >= 0 && index < (int)args_.size())
            return args_[index].step;
        return 0;
    }

    /* Empty when the arg's values have no names, which is almost all of them.
       By reference: the editor asks per redraw. */
    const vector<string> &getArgValues (int index) const {
        if (index >= 0 && index < (int)args_.size())
            return args_[index].values;
        return noValues_;
    }

    /* Convenience for the editor: a port is anything a .dsp may legitimately
       wire, i.e. everything except plugin-internal state. */
    bool argIsPort (int index) const { return getArgDir(index) != ARG_STATE; }

    void fire (thNode *node, thSynthTree *mod, unsigned int windowlen,
               unsigned int samples);
private:
    int moduleLoad (void);
    void moduleUnload (void);

    string path_;
    string desc_;
    State state_;
    void *handle_;

    /* Was three parallel arrays -- `string **', `ArgDir *', a count and a
       capacity -- grown by hand in ARGCHUNK steps with calloc and memcpy, and
       the grow check was off by one for years. A fourth and a fifth array for
       the step and the value names is not a thing worth writing; a vector of
       one struct is the same data with the bookkeeping deleted. */
    vector<ArgInfo> args_;

    /* What getArgValues() hands back for an index that has none, so it can
       return by reference without every caller checking. */
    static const vector<string> noValues_;

    Callback callback_;
};

#endif /* TH_PLUGIN_H */
