/* $Id: thList.cpp,v 1.3 2003/05/29 08:08:53 aaronl Exp $ */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include "thList.h"

thList::thList(void)
{
	head = NULL;
	tail = NULL;
}

thList::~thList(void)
{
	thListNode *node, *prev;

	for(node = tail; node; node = prev) {
		prev = node->prev;
		delete node;
	}
}

void thList::Add(void *data) 
{
	thListNode *node = new thListNode;

	node->data = data;

	node->prev = tail;
	node->next = NULL;
	
	if(tail) {
		tail->next = node;
	}

	if(!head) {
		head = node;
	}

	tail = node;
}

void thList::Remove (thListNode *node)
{
	if(node->next)
		node->next->prev = node->prev;
	if(node->prev)
		node->prev->next = node->next;
	if(node == tail) {
		if(node->prev) {
			tail = node->prev;
		}
		else { /* it must also be a tail then */
			head = NULL;
			tail = NULL;
		}
	}
	
	delete node;
}

thListNode *thList::GetNth(int n)
{
	int i = 1;
	thListNode *node;

	if(!head) {
		return NULL;
	}

	for(node = tail; node; node = node->next, i++) {
		if(n == i) {
			return node;
		}
	}

	return NULL;
}
