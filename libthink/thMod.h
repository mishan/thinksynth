#ifndef TH_MOD_H
#define TH_MOD_H 1

class thMod {
public:
	thMod(char *name);
	~thMod();

	thNode *FindNode(char *name); 
	void NewNode(thNode *node);
	void SetIONode(char *name);
	void PrintIONode(void);

	void Process (void);
private:
	thBSTree *modnodes, *actlist;
	thNode *ionode;

	char *modname;
};

#endif /* TH_MOD_H */
