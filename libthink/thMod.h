/* $Id: thMod.h,v 1.32 2003/06/03 23:05:06 aaronl Exp $ */

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
	thArg *GetArg (const string &argname) { return GetArg(ionode, argname); };

	void NewNode(thNode *node);
	void SetIONode(const string &name);
	void PrintIONode(void);
	thNode *GetIONode(void) const { return ionode; };

	string GetName(void) const { return modname; };
	void SetName(const string &name) { modname = name; };

	void Process (unsigned int windowlen);

	void SetActiveNodes(void);

	void BuildSynthTree (void);

	void ListNodes(void);

private:
	void ProcessHelper (unsigned int windowlen, thNode *node);
	void SetActiveNodesHelper(thNode *node);

	void CopyHelper (thNode *parentnode);

	int BuildSynthTreeHelper(thNode *parent, const string &nodename);
	void BuildSynthTreeHelper2(const map <string, thArg*> &argtree, thNode *currentnode);

	map<string,thNode*> modnodes;
	list<thNode*> activelist;
	thNode *ionode;

	string modname;
};

#endif /* TH_MOD_H */
