/* $Id: thMod.h,v 1.33 2004/02/18 23:41:16 ink Exp $ */

#ifndef TH_MOD_H
#define TH_MOD_H 1

#include "thNode.h"

class thMod {
public:
	thMod(const string &name);
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
	thArg *GetArg (const string &argname) { return GetArg(ionode, argname); }

	void NewNode(thNode *node);
	void NewNode(thNode *node, int id);
	void SetIONode(const string &name);
	void PrintIONode(void);
	thNode *GetIONode(void) const { return ionode; }

	string GetName(void) const { return modname; }
	void SetName(const string &name) { modname = name; }

	int GetNodeCount (void) const { return nodecount; }

	void Process (unsigned int windowlen);

	void SetActiveNodes(void);

	void SetPointers (void);

	void BuildNodeIndex (void);

	void BuildSynthTree (void);

	void ListNodes(void);

private:
	void ProcessHelper (unsigned int windowlen, thNode *node);
	void SetActiveNodesHelper(thNode *node);

	void CopyHelper (thNode *parentnode);

	int BuildSynthTreeHelper(thNode *parent, int nodeid);
	void BuildSynthTreeHelper2(const map <string, thArg*> &argtree, thNode *currentnode);

	map<string,thNode*> modnodes;
	list<thNode*> activelist;
	thNode *ionode;

	string modname;

	int nodecount;  /* counter of thNodes in the thMod, used as the id
					   for the node index */
	thNode **nodeindex;  /* index of all the nodes */
};

#endif /* TH_MOD_H */
