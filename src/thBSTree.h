/* $Id: thBSTree.h,v 1.19 2003/04/29 19:14:34 misha Exp $ */

#ifndef TH_BSTREE_H
#define TH_BSTREE_H 1

class thBSTree {
public:
	thBSTree (int (*fn)(void *, void *));
	thBSTree (int (*fn)(void *, void *), void *id, void *data);
	~thBSTree (void);

	void Insert (void *id, void *data);
	void Insert (thBSTree *node);

	void Remove (void *id);
	bool _Remove (void *id, bool freemem);
	void Remove (thBSTree *node);
	bool _Remove (thBSTree *node, bool freemem);

	void *GetData (void *id); /* get data from a specific node id */

	thBSTree *Find (void *id);

	void PrintTree (void);
	void PrintTree (thBSTree *node);

	inline void *GetData (void) { /* get data from this node */
		return bsData;
	}

	inline void *GetId (void) { /* get this node's id */
		return bsId;
	}

	inline thBSTree *GetLeft (void) {
		return bsLeft;
	}
 
	inline thBSTree *GetRight (void) {
		return bsRight;
	}

	inline bool IsEmpty (void) const {
		return !bsId;
	}

	inline bool IsLeaf (void) const {
		return !bsLeft && !bsRight;
	}
private:
	thBSTree *bsLeft, *bsRight;
	void *bsId, *bsData;

	int (*bsCompare)(void *, void *);
};

/* inline string comparison function */
inline int StringCompare(void *_str1, void *_str2) {
	char *str1 = (char *)_str1, *str2 = (char *)_str2;

	/* error */
	if(!str1 || !str2) {
		return -2;
	}
	
	while(*str1 && *str2) {
		if(*str1 < *str2) {
			/* str1 is less than str2 */
			return -1;
		}
		else if(*str1 > *str2) {
			/* str1 is greater than str2 */
			return 1;
		}
		
		str1++; str2++;
	}
	
	/* these strings might be equal up to a point, but one might be longer,
	   so we must handle this case */
	int len1, len2;
	
	if((len1 = strlen(str1)) == (len2 = strlen(str2))) {
		/* str1 == str2 */
		return 0;
	}
	else if (len1 < len2) {
		/* str1 < str2 */
		return -1;
	}
	
	/* str1 > str2 */
	return 1;
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
	return -1;
}



#endif /* TH_BSTREE_H */
