/* $Id: thList.h,v 1.7 2003/04/26 04:26:28 misha Exp $ */

#ifndef TH_LIST_H
#define TH_LIST_H 1

class thList {
public:
	thList();
	thList(void *data, thList *next);
	thList(thList *prev, void *data);
	~thList();

	thList *Append(void *data);
	thList *Prepend(void *data);

	thList *GetNext (void);
	thList *GetPrev (void);

	void Remove(thList *node);
	void Remove(void *data);
private:
	void *lData;
	thList *lNext, *lPrev;
};

#endif /* TH_LIST_H */
