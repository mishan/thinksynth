#ifndef TH_NODE_H
#define TH_NODE_H 1

class thNode {
public:
	thNode(char *name, thPlugin *thplug);
	~thNode();

	void SetArg(char *name, float *value);
	void SetArg(char *name, char *node, char *value);
	
	float *GetArg(char *name);

	void Process (void);
private:
	thBTree *args;
	thPlugin *plugin;

	char *nodename;
	bool recalc;
};

#endif /* TH_NODE_H */
