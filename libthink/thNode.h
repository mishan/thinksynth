#ifndef TH_NODE_H
#define TH_NODE_H 1

class thNode {
public:
	thNode(char *name, thPlugin *thplug);
	~thNode();

	void SetName(char *name);
	char *GetName();
	void SetArg(char *name, float *value, int num);
	void SetArg(char *name, char *node, char *value);
	
	thArgValue *GetArg(char *name);

	void Process (void);
private:
	thBSTree *args;
	thPlugin *plugin;

	char *nodename;
	bool recalc;
};

#endif /* TH_NODE_H */
