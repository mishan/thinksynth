/* $Id: thBSTree.h,v 1.25 2003/05/29 08:20:20 misha Exp $ */

#ifndef TH_BSTREE_H
#define TH_BSTREE_H 1

#include <string.h>

class thBSTree {
public:
	thBSTree (int (*fn)(void *, void *));
	thBSTree (int (*fn)(void *, void *), void *id, void *data);
	~thBSTree (void);

	void Insert (void *id, void *data);
	void Insert (thBSTree *node);

	void Remove (void *id);
	void Remove (thBSTree *node);

	void *GetData (void *id); /* get data from a specific node id */

	thBSTree *Find (void *id);

	void PrintTree (void);
	void PrintTree (thBSTree *node);

	inline void *GetData (void) const { /* get data from this node */
		return bsData;
	}

	inline void *GetId (void) const { /* get this node's id */
		return bsId;
	}

	inline thBSTree *GetLeft (void) const {
		return bsLeft;
	}
 
	inline thBSTree *GetRight (void) const {
		return bsRight;
	}

	inline bool IsEmpty (void) const {
		return !bsId;
	}

	inline bool IsLeaf (void) const {
		return !bsLeft && !bsRight;
	}
protected:
	bool _Remove (thBSTree *node, bool freemem);
	bool _Remove (void *id, bool freemem);
private:
	thBSTree *bsLeft, *bsRight;
	void *bsId, *bsData;

	int (*bsCompare)(void *, void *);
};

/* inline string comparison function */
inline int StringCompare (void *_str1, void *_str2) {
	char *str1 = (char *)_str1, *str2 = (char *)_str2;

	return strcmp(str1, str2);
}
/* inline integer comparison function, very straightforward */
inline int IntCompare(void *_int1, void *_int2) {
	int int1 = *((int *)_int1), int2 = *((int *)_int2);

	if(int1 < int2) {
		return -1;
	}
	else if(int1 == int2) {
		return 0;
	}

	/* else if(int1 > int2) */
	return 1;
}


#endif /* TH_BSTREE_H */
