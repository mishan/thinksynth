/* $Id: thNode.h,v 1.40 2004/04/08 13:33:30 ink Exp $ */

#ifndef TH_NODE_H
#define TH_NODE_H 1

class thNode {
public:
	thNode (const string &name, thPlugin *thplug);
	~thNode (void);

	void SetName (const string &name) { nodename = name; };

	 string GetName (void) const { return nodename; }

	thArg *SetArg (const string &name, float *value, int num);
	thArg *SetArg (const string &name, const string &node, const string &value);

	int AddArgToIndex (thArg *arg);

	void SetArgCount (int argcnt) { argcounter = argcnt; };

	int GetArgCount (void) { return argcounter; };

	map<string,thArg*> GetArgTree (void) const { return args; }
	
	thArg *GetArg (const string &name) { return args[name]; };

	thArg *GetArg (int index) { return argindex[index]; };

	void PrintArgs (void);

	void SetID (int newid) { id = newid; }

	int GetID (void) const { return id; }

	bool GetRecalc(void) const { return recalc; }

	void SetRecalc(bool state) { recalc = state; }

	void AddChild(thNode *node) { children.push_back(node); }

	void AddParent(thNode *node) { parents.push_back(node); }

	list<thNode*> GetChildren (void) const { return children; }

	list<thNode*> GetParents(void) const { return parents; }

	void SetPlugin (thPlugin *plug) { plugin = plug; }

	thPlugin *GetPlugin() const { return plugin; }

	void CopyArgs (const map<string,thArg*> &args);

	void Process (void);

private:
	map<string, thArg*> args;

	thArg **argindex;
	int argcounter;
	int argsize;

	list<thNode*> parents, children;
	thPlugin *plugin;
	
	string nodename;
	bool recalc;

	int id;  /* id used as an index for thArg pointers */
};

#endif /* TH_NODE_H */
