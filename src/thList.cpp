/* $Id */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

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

	for(node = head; node; node = prev) {
		prev = node->prev;
		delete node;
	}
}

void thList::Add(void *data) 
{
	thListNode *node = new thListNode;
	node->data = data;
	node->prev = head;
	node->next = NULL;
	
	if(head) {
		head->next = node;
	}

	if(!tail) {
		tail = node;
	}

	head = node;
}

void thList::Remove (thListNode *node)
{
	if(node->next)
		node->next->prev = node->prev;
	if(node->prev)
		node->prev->next = node->next;
	if(node == head) {
		if(node->prev) {
			head = node->prev;
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
