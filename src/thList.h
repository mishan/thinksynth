/* $Id: thList.h,v 1.11 2003/05/29 08:20:20 misha Exp $ */

#ifndef TH_LIST_H
#define TH_LIST_H 1

struct thListNode {
	void *data;
	thListNode *next, *prev;
};

class thList {
public:
	thList();
	~thList();

	void Add(void *data);
	void Remove(thListNode *node);

	thListNode *GetNth(int n);
	inline thListNode *GetTail(void) const {
		return tail;
	}
	inline thListNode *GetHead(void) const {
		return head;
	}

private:
	thListNode *head, *tail;
};

#endif /* TH_LIST_H */
