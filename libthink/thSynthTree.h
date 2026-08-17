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

#include <vector>

#include "thExport.h"

#include "thNode.h"

class thSynth;

/* A value the file wrote with a unit, parked until the fold can be done
 * honestly.
 *
 * `5 ms' used to become 220.5 inside the grammar action that read it,
 * using the compile-time TH_SAMPLE. That is wrong the moment the synth
 * runs at any other rate: `thinksynth -r 48000' opens the device at 48k
 * and then plays every envelope in every patch 8.8% short, because the
 * durations were converted by a parser with no idea what rate it was
 * parsing for. It is wrong in principle even at 44100 -- a parse should
 * say what the file says, and folding engine semantics into it is the
 * parser answering a question that belongs to the synth.
 *
 * So the grammar records the literal and its unit and moves on, and
 * thSynthTree::foldUnits does the arithmetic once, at load, with the rate
 * the synth was actually built with. One record per *value site* rather
 * than one per arg, because a file may write `@decay = 500 ms' and then
 * `@decay.max = 88200': the same arg, one site in milliseconds and one
 * already in samples, and only the site knows which it is. */
struct thUnitFold
{
    enum Field { VALUE, MIN, MAX, STEP };

    thArg  *arg;
    Field   field;
    float   literal;
    string  units;
};

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

    /* Parked by the grammar, applied by foldUnits. See thUnitFold. */
    void deferUnitFold (thArg *arg, thUnitFold::Field field,
                        float literal, const string &units);

    /* Turns every `5 ms' and `90%' the file wrote into what the engine
       works in, at `sampleRate' samples per second, and forgets them --
       so calling it twice cannot fold twice. Run once, from
       thSynth::finishParse, before anything reads a value. */
    void foldUnits (long sampleRate);

    void process (unsigned int windowlen);
    void setActiveNodes(void);
    void buildArgMap (void);
    void setPointers (void);
    void buildNodeIndex (void);

    /* Carries what a plugin says about one of its args -- that it means a whole
       number, and what its values are called -- to the control that drives it.
       See the definition for why a control can only be typed by agreement. */
    void typeChanArgs (void);

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

    /* Empty except between the parse and foldUnits(). Not copied by the
       copy constructor for the same reason: a tree is only ever copied
       after it has been finished, and a copy that carried these would
       fold a second time if anyone ever called foldUnits on it. */
    std::vector<thUnitFold> unitFolds_;

    string name_, desc_;
    int nodecount_;      /* counter of thNodes in the thSynthTree, used as the 
                            id for the node index */
    thNode **nodeindex_; /* index of all the nodes */
};

#endif /* TH_SYNTH_TREE_H */
