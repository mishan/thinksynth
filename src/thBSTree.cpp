/* $Id: thBSTree.cpp,v 1.14 2003/04/26 02:32:10 misha Exp $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thBSTree.h"

thBSTree::thBSTree (void)
{
	bsLeft = NULL;
	bsRight = NULL;
}

thBSTree::~thBSTree (void)
{
	DestroyTree(bRoot);
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
	thBSNode *node;

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

bool thBSTree::Remove(void *id)
{
	/* this node isn't it, tell the children to remove this id,
	   but avoid calling extra children */
	if(bsCompare(id, bsId) != 0) {
		if(bsLeft) {
			if(!bsLeft->Remove(id) && bsRight) {
				return bsRight->Remove(id);
			}
			return true;
		}
		return (bsRight && bsRight->Remove(id));
	}

	/* XXX: can't free this here, because it would cause issues ... */
	/*	free(bsId); */

	bsId = NULL;
	bsData = NULL;

	if(bsLeft && bsRight) {
		thBSTree *newchild;

		switch(StringCompare(bsLeft->name, bsRight->name)) {
		case 0:
			fprintf(stderr, "thBSTree::Remove: Duplicate node should not exist.. Corrupt tree?\n");
			break;
		case 1:
			bsId = bsLeft->GetId();
			bsData = bsLeft->GetData();

			bsLeft->Remove(bsId);
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

			bsRight->Remove(bsId);
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

			bsLeft->Remove(bsId);
			if(bsLeft->IsEmpty()) { /* empty leaf node */
				delete bsLeft;
				bsLeft = NULL;
			}
		}
		else if(bsRight) {
			bsId = bsRight->GetId();
			bsData = bsRight->GetData();

			bsRight->Remove(bsId);
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
