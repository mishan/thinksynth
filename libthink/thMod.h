/* $Id: thMod.h,v 1.27 2003/04/28 21:48:26 ink Exp $ */

#ifndef TH_MOD_H
#define TH_MOD_H 1

class thMod {
public:
	thMod(const char *name);
	~thMod();
	
	thNode *FindNode(const char *name);
	const thArgValue *GetArg (const char *nodename, const char *argname);
	
	void NewNode(thNode *node);
	void SetIONode(const char *name);
	void PrintIONode(void);
	thNode *GetIONode(void) const { return ionode; };
	
	char *GetName(void) const { return modname; };
	void SetName(char *name);
	
	void Process (unsigned int windowlen);
	
	void SetActiveNodes(void);
	
	thMod *Copy (void);
	
	void BuildSynthTree (void);

private:
	void ProcessHelper (unsigned int windowlen, thNode *node);
	void SetActiveNodesHelper(thNode *node);
	
	void CopyHelper (thMod *mod, thNode *parentnode);
	
	int BuildSynthTreeHelper(thNode *parent, char *nodename);
	void BuildSynthTreeHelper2(thBSTree *argtree, thNode *currentnode);
	
	thBSTree *modnodes;
	thList activelist;
	thNode *ionode;
	
	char *modname;
};

#endif /* TH_MOD_H */
