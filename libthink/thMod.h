/* $Id: thMod.h,v 1.26 2003/04/27 04:28:16 ink Exp $ */

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
	
	void Process (thMod *mod, unsigned int windowlen);
	
	void SetActiveNodes(void);
	
	thMod *Copy (void);
	
	void BuildSynthTree (void);

private:
	void ProcessHelper (thMod *mod, unsigned int windowlen, thNode *node);
	void SetActiveNodesHelper(thNode *node);
	
	void CopyHelper (thMod *mod, thNode *parentnode);
	
	int BuildSynthTreeHelper(thNode *parent, char *nodename);
	void BuildSynthTreeHelper2(thBSTree *argtree, thNode *currentnode);
	
	thBSTree *modnodes;
	thList *activelist;
	thNode *ionode;
	
	char *modname;
};

#endif /* TH_MOD_H */
