/* $Id: thNode.h,v 1.27 2003/04/25 22:22:20 ink Exp $ */

#ifndef TH_NODE_H
#define TH_NODE_H 1

class thNode {
public:
	thNode (const char *name, thPlugin *thplug);
	~thNode (void);

	void SetName (char *name);
	const char *GetName (void) { return nodename; };
	thArg *SetArg (const char *name, float *value, int num);
	thArg *SetArg (const char *name, const char *node, const char *value);

	const thArgValue *GetArg (const char *name);
	void PrintArgs (void);

	const thList *GetArgList (void) { return args.GetList(); };

	bool GetRecalc(void) { return recalc; };
	void SetRecalc(bool state) { recalc = state; };

	void AddChild(thNode *node) { children.Add(node); };
	void AddParent(thNode *node) { parents.Add(node); };

	thList *GetChildren() { return &children; };
	thList *GetParents() { return &parents; };

	void SetPlugin (thPlugin *plug);
	thPlugin *GetPlugin() { return plugin; };

	void CopyArgs (thList *args);

	void Process (void);
private:
	thBSTree args;
	thList parents, children;
	thPlugin *plugin;

	char *nodename;
	bool recalc;
};

#endif /* TH_NODE_H */
