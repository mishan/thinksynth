/*
 * Copyright (C) 2004-2014 Metaphonic Labs
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

#ifndef TH_NODE_H
#define TH_NODE_H 1

#include "thExport.h"

class THINK_API thNode {
public:
    thNode (const string &name, thPlugin *thplug);
    thNode (const thNode &copyNode);
    ~thNode (void);

    void setName (const string &name) { nodeName_ = name; };
    string name (void) const { return nodeName_; }

    thArg *setArg (const string &name, float value);
    thArg *setArg (const string &name, const float *value, int len);
    thArg *setArg (const string &name, const string &node,
                   const string &value);
    thArg *setArg (const string &name, const string &chanarg);

    int addArgToIndex (thArg *arg);

    void setArgCount (int argcnt) { argCount_ = argcnt; };
    int argCount (void) const { return argCount_; };

    /* Returned by reference: these are walked once per node per window on the
       audio thread, and copying the container there allocates. */
    const thArgMap &args (void) const { return args_; }

    /* NB: deliberately not args_[name] -- map::operator[] inserts a NULL entry
       on every miss, which both allocates on the audio thread and pollutes the
       map with NULLs that every iteration site then has to guard against. */
    thArg *getArg (const string &name) const {
        const thArgMap::const_iterator i = args_.find(name);
        if (i != args_.end()) return i->second;
        return NULL;
    };
    thArg *getArg (int index) const {
        if (index < 0 || index >= argCount_) return NULL;
        return argindex_[index];
    };

    void printArgs (void);

    void setId (int newid) { id_ = newid; }
    int id (void) const { return id_; }

    bool recalc (void) const { return recalc_; }
    void setRecalc (bool state) { recalc_ = state; }

    void addChild(thNode *node) { children_.push_back(node); }
    void addParent(thNode *node) { parents_.push_back(node); }

    const thNodeList &children (void) const { return children_; }
    const thNodeList &parents (void) const { return parents_; }

    void setPlugin (thPlugin *plug) { plugin_ = plug; }
    thPlugin *plugin (void) const { return plugin_; }

    void copyArgs (const thArgMap &args);
    void process (void);
private:
    thArgMap args_;

    thArg **argindex_;
    int argCount_;
    int argsize_;

    thNodeList parents_, children_;
    thPlugin *plugin_;

    string nodeName_;
    bool recalc_;

    int id_;  /* id used as an index for thArg pointers */
};

#endif /* TH_NODE_H */
