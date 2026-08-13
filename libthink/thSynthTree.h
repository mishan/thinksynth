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

#ifndef TH_SYNTH_TREE_H
#define TH_SYNTH_TREE_H 1

#include "thExport.h"

#include "thNode.h"

class thSynth;

class THINK_API thSynthTree {
public:
    thSynthTree(const string &name, thSynth *synth);
    thSynthTree(const thSynthTree &oldtree);  /* Copy constructor */
    ~thSynthTree();

    typedef map<string, thNode*> NodeMap;

    thNode *findNode(string name) const
    {
        const NodeMap::const_iterator i = nodes_.find(name);
        if (i != nodes_.end()) return i->second;
        return NULL;
    }

    thArg *getArg (const string &nodename, const string &argname);
    thArg *getArg (thNode *node, const string &argname);
    thArg *getArg (thNode *node, int index);
    thArg *getArg (const string &argname) { return getArg(ionode_, argname); }

    void newNode(thNode *node, bool set_id);

    void setIONode(const string &name);
    void printIONode(void);
    thNode *IONode(void) const { return ionode_; }

    /* Bounds-checked lookup into nodeindex_; NULL if id is out of range or the
       index has not been built yet. */
    thNode *nodeAt(int id) const;

    const string &name(void) const { return name_; }
    void setName(const string &name) { name_ = name; }

    const string &desc(void) const { return desc_; }
    void setDesc(const string &desc) { desc_ = desc; }

    int nodeCount (void) const { return nodecount_; }

    const thArgMap &chanArgs (void) const { return chanargs_; }

    /* NB: deliberately not chanargs_[argName] -- map::operator[] inserts a NULL
       entry on every miss, so a lookup for an undeclared chanarg both polluted
       the map and handed the caller a NULL it then dereferenced. */
    thArg *getChanArg (const string &argName) const {
        const thArgMap::const_iterator i = chanargs_.find(argName);
        if (i != chanargs_.end()) return i->second;
        return NULL;
    }
    void setChanArg (thArg *arg);

    const NodeMap &nodes (void) const { return nodes_; }

    void process (unsigned int windowlen);
    void setActiveNodes(void);
    void buildArgMap (void);
    void setPointers (void);
    void buildNodeIndex (void);
    void buildSynthTree (void);
    void listNodes(void);
protected:
    thSynth *synth (void) const { return synth_; }
private:
    void processHelper (unsigned int windowlen, thNode *node);
    void setActiveNodesHelper (thNode *node);
    void copyHelper (thNode *parentnode);
    int buildSynthTreeHelper (thNode *parent, int nodeid);
    void buildSynthTreeHelper2 (const thArgMap &argtree,
                                thNode *currentnode);

    thSynth *synth_;
    NodeMap nodes_;
    thNodeList activelist_;
    thNode *ionode_;
    thArgMap chanargs_;    /* midi chan args */
    string name_, desc_;
    int nodecount_;      /* counter of thNodes in the thSynthTree, used as the 
                            id for the node index */
    thNode **nodeindex_; /* index of all the nodes */
};

#endif /* TH_SYNTH_TREE_H */
