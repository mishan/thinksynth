/* $Id: thMod.h,v 1.30 2003/05/29 03:11:03 ink Exp $ */

#ifndef TH_MOD_H
#define TH_MOD_H 1

class thNode;

class thMod {
public:
	thMod(const char *name);
	thMod(thMod *oldmod);  /* Copy constructor */
	~thMod();
	
	thNode *FindNode(const char *name);
	const thArgValue *GetArg (const char *nodename, const char *argname);
	const thArgValue *GetArg (thNode *node, const char *argname);
	
	void NewNode(thNode *node);
	void SetIONode(const char *name);
	void PrintIONode(void);
	thNode *GetIONode(void) const { return ionode; };
	
	char *GetName(void) const { return modname; };
	void SetName(char *name);
	
	void Process (unsigned int windowlen);
	
	void SetActiveNodes(void);
	
	void BuildSynthTree (void);

	void ListNodes(void);

private:
	void ProcessHelper (unsigned int windowlen, thNode *node);
	void SetActiveNodesHelper(thNode *node);

	void CopyHelper (thNode *parentnode);

	int BuildSynthTreeHelper(thNode *parent, char *nodename);
	void BuildSynthTreeHelper2(thBSTree *argtree, thNode *currentnode);

	void ListNodes(thBSTree *node);

	thBSTree *modnodes;
	thList activelist;
	thNode *ionode;
	
	char *modname;
};

#endif /* TH_MOD_H */
