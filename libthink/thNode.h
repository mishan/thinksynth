#ifndef TH_NODE_H
#define TH_NODE_H 1

class thNode {
public:
	thNode (char *name, thPlugin *thplug);
	~thNode (void);

	void SetName (const char *name);
	const char *GetName (void);
	void SetArg (const char *name, const float *value, int num);
	void SetArg (const char *name, const char *node, const char *value);
	
	const thArgValue *GetArg (char *name);
	void PrintArgs (void);

	void Process (void);
private:
	thBSTree *args;
	thPlugin *plugin;

	char *nodename;
	bool recalc;
};

#endif /* TH_NODE_H */
