#ifndef TH_BTREE_H
#define TH_BTREE_H 1

struct thBNode {
	char *name;
	void *data;

	thBNode *left, *right;
};

class thBTree {
public:
	thBTree();
	~thBTree();

	void Insert(char *name, void *data);
	void Remove(thBNode *bnode);
	thBNode *Find(char *name);
private:
	thBNode *bRoot;

	void InsertHelper(thBNode *root, thBNode *node);
	thBNode *FindHelper(thBNode *root, char *name);
	thBNode *GetParent(thBNode *root, thBNode *node);
	void RemoveHelper(thBNode *root, thBNode *node);
	
	void DestroyTree(void);
};

#endif /* TH_BTREE_H */
