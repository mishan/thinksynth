#ifndef TH_BSTREE_H
#define TH_BSTREE_H 1

struct thBSNode {
	char *name;
	void *data;

	thBSNode *left, *right;
};

class thBSTree {
public:
	thBSTree (void);
	~thBSTree (void);

	void Insert (char *name, void *data);
	void Remove (char *name);
	thBSNode *Find (char *name);

	void PrintTree (void);
	void *GetData ( char *name );
private:
	thBSNode *bRoot;

	void PrintHelper (thBSNode *root);
	void InsertHelper (thBSNode *root, thBSNode *node);
	thBSNode *FindHelper (thBSNode *root, char *name);
	thBSNode *GetParent (thBSNode *root, thBSNode *node);
	void RemoveHelper (thBSNode *root, thBSNode *node);
	
	void DestroyTree (thBSNode *root);

	/* inline string comparison function */
	int StringCompare(char *str1, char *str2) {
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

};

#endif /* TH_BSTREE_H */
