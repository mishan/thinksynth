/* $Id$ */
/*
 * Copyright (C) 2004 Metaphonic Labs
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

#ifndef TH_MOD_H
#define TH_MOD_H 1

#include "thNode.h"

class thSynth;

class thMod {
public:
	thMod(const string &name, thSynth *synth);
	thMod(const thMod &oldmod);  /* Copy constructor */
	~thMod();

	typedef map<string, thNode*> NodeMap;
	typedef list<thNode *> NodeList;
	typedef map<string, thArg *> ArgMap;

	thNode *findNode(string name) const
	{
		const NodeMap::const_iterator i = modnodes_.find(name);
		if (i != modnodes_.end()) return i->second;
		return NULL;
	}

	thArg *getArg (const string &nodename, const string &argname);
	thArg *getArg (thNode *node, const string &argname);
	thArg *getArg (thNode *node, int index);
	thArg *getArg (const string &argname) { return getArg(ionode_, argname); }

	void newNode(thNode *node);
	void newNode(thNode *node, int id);
	void setIONode(const string &name);
	void printIONode(void);
	thNode *getIONode(void) const { return ionode_; }

	string getName(void) const { return modname_; }
	void setName(const string &name) { modname_ = name; }

	string getDesc(void) const { return moddesc_; }
	void setDesc(const string &desc) { moddesc_ = desc; }

	int getNodeCount (void) const { return nodecount_; }

	thArg *getChanArg (string argName) { return chanargs_[argName]; }
	void setChanArg (thArg *arg);

	NodeMap getNodeList (void) { return modnodes_; }

	ArgMap getChanArgs (void) { return chanargs_; } 

	void process (unsigned int windowlen);
	void setActiveNodes(void);
	void buildArgMap (void);
	void setPointers (void);
	void buildNodeIndex (void);
	void buildSynthTree (void);
	void listNodes(void);
protected:
	thSynth *getSynth (void) const { return synth_; }
private:
	void processHelper (unsigned int windowlen, thNode *node);
	void setActiveNodesHelper(thNode *node);

	void copyHelper (thNode *parentnode);

	int buildSynthTreeHelper(thNode *parent, int nodeid);
	void buildSynthTreeHelper2(const ArgMap &argtree,
							   thNode *currentnode);

	thSynth *synth_;

	NodeMap modnodes_;
	NodeList activelist_;
	thNode *ionode_;

	ArgMap chanargs_;    /* midi chan args */

	string modname_, moddesc_;

	int nodecount_;      /* counter of thNodes in the thMod, used as the id
							for the node index */
	thNode **nodeindex_; /* index of all the nodes */
};

#endif /* TH_MOD_H */
