#ifndef TH_BSTREE_H
#define TH_BSTREE_H 1

struct thBNode {
	char *name;
	void *data;

	thBNode *left, *right;
};

class thBSTree {
public:
	thBSTree (void);
	~thBSTree (void);

	void Insert (char *name, void *data);
	void Remove (char *name);
	thBNode *Find (char *name);

	void PrintTree (void);
private:
	thBNode *bRoot;

	void PrintHelper (thBNode *root);
	void InsertHelper (thBNode *root, thBNode *node);
	thBNode *FindHelper (thBNode *root, char *name);
	thBNode *GetParent (thBNode *root, thBNode *node);
	void RemoveHelper (thBNode *root, thBNode *node);
	
	void DestroyTree (thBNode *root);
};

#endif /* TH_BSTREE_H */
