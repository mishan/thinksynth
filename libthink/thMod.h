/* $Id */

#ifndef TH_MOD_H
#define TH_MOD_H 1

class thMod {
public:
	thMod(const char *name);
	~thMod();

	thNode *FindNode(const char *name);
	const thArgValue *GetArg (const char *nodename, const char *argname);
	void NewNode(thNode *node);
	const char *GetName(void) { return modname; };
	void SetName(char *name);
	void SetIONode(const char *name);
	thNode *GetIONode(void) { return ionode; };
	void PrintIONode(void);

	void Process (thMod *mod, unsigned int windowlen);

	void SetActiveNodes(void);

	thMod *Copy (void);

	void BuildSynthTree (void);
	int BuildSynthTreeHelper(thNode *parent, char *nodename);

private:
	void ProcessHelper (thMod *mod, unsigned int windowlen, thNode *node);
	void SetActiveNodesHelper(thNode *node);

	void CopyHelper (thMod *mod, thNode *parentnode);

	thBSTree modnodes;
	thList *activelist;
	thNode *ionode;

	char *modname;
};

#endif /* TH_MOD_H */
