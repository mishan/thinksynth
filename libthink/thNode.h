/* $Id: thNode.h,v 1.32 2003/04/27 03:24:57 misha Exp $ */

#ifndef TH_NODE_H
#define TH_NODE_H 1

class thNode {
public:
	thNode (const char *name, thPlugin *thplug);
	~thNode (void);
	
	void SetName (char *name);

	inline char *GetName (void) const {
		return nodename; 
	}

	thArg *SetArg (const char *name, float *value, int num);
	thArg *SetArg (const char *name, const char *node, const char *value);
	
	const thArgValue *GetArg (const char *name);
	void PrintArgs (void);
	
	inline bool GetRecalc(void) {
		return recalc;
	}

	inline void SetRecalc(bool state) {
		recalc = state;
	}
	
	inline void AddChild(thNode *node) {
		children.Add(node);
	}

	inline void AddParent(thNode *node) {
		parents.Add(node);
	}
	
	inline thList *GetChildren (void) {
		return &children;
	}

	inline thList *GetParents(void) {
		return &parents;
	}
	
	inline void SetPlugin (thPlugin *plug) {
		plugin = plug;
	}
	
	inline thPlugin *GetPlugin() const {
		return plugin;
	}
	
	void CopyArgs (thBSTree *args);
	
	void Process (void);
	
private:
	thBSTree *args;
	thList parents, children;
	thPlugin *plugin;
	
	char *nodename;
	bool recalc;
};

#endif /* TH_NODE_H */
