/* $Id: thList.h,v 1.5 2003/04/26 00:19:23 joshk Exp $ */

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
	thListNode *GetTail(void) const { return tail; };
	thListNode *GetHead(void) const { return head; };
private:
	thListNode *head, *tail;
};

#endif /* TH_LIST_H */
