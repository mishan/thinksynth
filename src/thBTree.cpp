#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thBTree.h"

static inline int StringCompare(char *str1, char *str2)
{
	if(!str1 || !str2) {
		return 0;
	}

	while(*str1 && *str2) {
		if(*str1 < *str2) {
			return -1;
		}
		else if(*str1 > *str2) {
			return 1;
		}

		str1++; str2++;
	}

	return 0;
}

thBTree::thBTree()
{
	broot = NULL;
}

thBTree::~thBTree()
{
	DestroyTree();
}

void thBTree::Insert(char *name, void *data)
{
	thBNode *node = new thBNode;

	node->name = strdup(name);
	node->data = data;

	node->left = NULL;
	node->right = NULL;

	if(!broot) {
		broot = node;
		return;
	}

	InsertHelper(broot, node);
}

void thBTree::InsertHelper(thBNode *root, thBNode *node)
{
	switch(StringCompare(node->name, root->name)) {
	case -1:
	case 0:
		if(root->left) {
			InsertHelper(root->left, node);
		}
		else {
			root->left = node;
		}
		break;
	case 1:
		if(root->right) {
			InsertHelper(root->right, node);
		}
		else {
			root->right = node;
		}
		break;
	}
}

void thBTree::Remove(thBNode *node)
{
}

thBNode *thBTree::Find(char *name)
{
	if(!broot) {
		return NULL;
	}

	return FindHelper(broot, name);
}

thBNode *thBTree::FindHelper(thBNode *root, char *name)
{
	if(!strcmp(root->name, name)) {
		return root;
	}

	switch(StringCompare(name, root->name)) {
	case -1:
	case 0:
		if(root->left) {
			return FindHelper(root->left, name);
		}
		else {
			return NULL;
		}
		break;
	case 1:
		if(root->right) {
			return FindHelper(root->right, name);
		}
		else {
			return NULL;
		}
		break;
	}

	return NULL;
}

void thBTree::DestroyTree()
{
}
