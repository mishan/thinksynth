/* $Id: thMod.h,v 1.40 2004/08/16 09:34:48 misha Exp $ */
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
	thMod(const string &name, thSynth *argsynth);
	thMod(const thMod &oldmod);  /* Copy constructor */
	~thMod();

	thNode *FindNode(string name) const
	{
		const map<string,thNode*>::const_iterator i = modnodes.find(name);
		if (i != modnodes.end()) return i->second;
		return 0;
	};
	thArg *GetArg (const string &nodename, const string &argname);
	thArg *GetArg (thNode *node, const string &argname);
	thArg *GetArg (thNode *node, int index);
	thArg *GetArg (const string &argname) { return GetArg(ionode, argname); }

	void NewNode(thNode *node);
	void NewNode(thNode *node, int id);
	void SetIONode(const string &name);
	void PrintIONode(void);
	thNode *GetIONode(void) const { return ionode; }

	string GetName(void) const { return modname; }
	void SetName(const string &name) { modname = name; }

	string GetDesc(void) const { return moddesc; }
	void SetDesc(const string &desc) { moddesc = desc; }

	int GetNodeCount (void) const { return nodecount; }

	thArg *GetChanArg (string argName) { return chanargs[argName]; }
	void SetChanArg (thArg *arg);

	map<string,thNode*> GetNodeList (void) { return modnodes; }

	map<string, thArg*> GetChanArgs (void) { return chanargs; } 

	void Process (unsigned int windowlen);

	void SetActiveNodes(void);

	void BuildArgMap (void);

	void SetPointers (void);

	void BuildNodeIndex (void);

	void BuildSynthTree (void);

	void ListNodes(void);

protected:
	thSynth *synth;
private:
	void ProcessHelper (unsigned int windowlen, thNode *node);
	void SetActiveNodesHelper(thNode *node);

	void CopyHelper (thNode *parentnode);

	int BuildSynthTreeHelper(thNode *parent, int nodeid);
	void BuildSynthTreeHelper2(const map <string, thArg*> &argtree, thNode *currentnode);

	map<string,thNode*> modnodes;
	list<thNode*> activelist;
	thNode *ionode;

	map<string,thArg*> chanargs;  /* midi chan args */

	string modname, moddesc;

	int nodecount;  /* counter of thNodes in the thMod, used as the id
					   for the node index */
	thNode **nodeindex;  /* index of all the nodes */
};

#endif /* TH_MOD_H */
