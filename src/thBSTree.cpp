/* $Id: thBSTree.cpp,v 1.27 2003/05/09 02:40:12 aaronl Exp $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thBSTree.h"

thBSTree::thBSTree (int (*fn)(void *, void *))
{
	bsLeft = NULL;
	bsRight = NULL;
	bsId = NULL;
	bsData = NULL;
	bsCompare = fn;
}

thBSTree::thBSTree (int (*fn)(void *, void *), void *id, void *data)
{
	bsLeft = NULL;
	bsRight = NULL;
	bsCompare = fn;

	bsId = id;
	bsData = data;
}

thBSTree::~thBSTree (void)
{
	free(bsId);
}

void thBSTree::Insert(thBSTree *tree)
{
	if(!tree) {
		fprintf(stderr, "thBSTree::Insert: Cannot insert NULL tree\n");
		return;
	}

	if(tree->IsEmpty()) {
		fprintf(stderr, "thBSTree::Insert: Cannot insert empty tree\n");
		return;
	}

	Insert(tree->GetId(), tree->GetData());

	if(tree->GetRight()) {
		Insert(tree->GetRight());
	}
	if(tree->GetLeft()) {
		Insert(tree->GetLeft());
	}
}

void thBSTree::Insert(void *id, void *data)
{
	if(!id) {
		fprintf(stderr, "thBSTree::Insert: Cannot insert node with NULL id\n");
		return;
	}

	if(!data) {
		fprintf(stderr, "thBSTree::Insert: Cannot insert node with NULL data\n");
		return;
	}

	if(IsEmpty()) {
		bsId = id;
		bsData = data;
	}

	switch(bsCompare(data, bsData)) {
	case -1:
		if(bsLeft) {
			bsLeft->Insert(id, data);
		}
		else {
			bsLeft = new thBSTree(bsCompare, id, data);
		}
		break;
	case 0:
		/* if the bsIds are equal, just update the data ptr */
		bsData = data;
		return;
		break;
	case 1:
		if(bsRight) {
			bsRight->Insert(id, data);
		}
		else {
			bsRight = new thBSTree(bsCompare, id, data);
		}
		break;
	}
}

/* XXX: remove very last empty child node or all is lost */
void thBSTree::Remove(void *id)
{
	_Remove(id, true);
}

bool thBSTree::_Remove(void *id, bool freemem)
{
	if (!bsId) return false;
	/* this node isn't it, tell the children to remove this id,
	   but avoid calling extra children */
	if(bsCompare(id, bsId) != 0) {
		if(bsLeft) {
			if(!bsLeft->_Remove(id, true) && bsRight) {
				return bsRight->_Remove(id, true);
			}
			return true;
		}
		return (bsRight && bsRight->_Remove(id, true));
	}

	if(freemem) {
		free(bsId);
	}

	bsId = NULL;
	bsData = NULL;

	if(bsLeft && bsRight) {
		thBSTree *newchild;

		switch(bsCompare(bsLeft->GetId(), bsRight->GetId())) {
		case 0:
			fprintf(stderr, "thBSTree::Remove: Duplicate node should not exist.. Corrupt tree?\n");
			break;
		case 1:
			bsId = bsLeft->GetId();
			bsData = bsLeft->GetData();

			bsLeft->_Remove(bsId, false);
			if(bsLeft->IsEmpty()) { /* empty leaf node */
				delete bsLeft;
				bsLeft = NULL;
			}

			newchild = bsRight;
			bsRight = NULL;

			Insert(newchild);
			break;
		case -1:
			bsId = bsRight->GetId();
			bsData = bsRight->GetData();

			bsRight->_Remove(bsId, false);
			if(bsRight->IsEmpty()) { /* empty leaf node */
				delete bsRight;
				bsRight = NULL;
			}

			newchild = bsLeft;
			bsLeft = NULL;

			Insert(newchild);
			break;
		}
	}
	else {
		if(bsLeft) {
			bsId = bsLeft->GetId();
			bsData = bsLeft->GetData();

			bsLeft->_Remove(bsId, false);
			if(bsLeft->IsEmpty()) { /* empty leaf node */
				delete bsLeft;
				bsLeft = NULL;
			}
		}
		else if(bsRight) {
			bsId = bsRight->GetId();
			bsData = bsRight->GetData();

			bsRight->_Remove(bsId, false);
			if(bsRight->IsEmpty()) { /* empty leaf node */
				delete bsRight;
				bsRight = NULL;
			}
		}
	}

	return true;
}

thBSTree *thBSTree::Find(void *id) 
{
	thBSTree *node = NULL;

	if(!IsEmpty() && (bsCompare(bsId, id) == 0)) {
		return this;
	}

	if(IsLeaf()) {
		return NULL;
	}

	if(bsLeft && (node = bsLeft->Find(id))) {
		return node;
	}
	else if(bsRight) {
		return bsRight->Find(id);
	}

	return NULL;
}

void *thBSTree::GetData (void *id) 
{
	thBSTree *node = Find(id);

	if(node) {
		return node->GetData();
	}

	return NULL;
}
