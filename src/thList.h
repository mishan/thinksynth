/* $Id: thList.h,v 1.4 2003/04/25 07:18:42 joshk Exp $ */

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
	thListNode *GetTail(void) { return tail; };
	thListNode *GetHead(void) { return head; };
private:
	thListNode *head, *tail;
};

#endif /* TH_LIST_H */
