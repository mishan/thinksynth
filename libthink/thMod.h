#ifndef TH_MOD_H
#define TH_MOD_H 1

class thMod {
public:
	thMod(char *name);
	~thMod();

	thNode *FindNode(char *name); 
	void NewNode(thNode *node); 

	void Process (void);
private:
	thList *nodes, *actlist;
	thNode *comnode;

	char *modname;
};

#endif /* TH_MOD_H */
