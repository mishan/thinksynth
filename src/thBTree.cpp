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

thBTree::thBTree (void)
{
	bRoot = NULL;
}

thBTree::~thBTree (void)
{
	DestroyTree(bRoot);
}

void thBTree::Insert(char *name, void *data)
{
	thBNode *node = new thBNode;

	node->name = strdup(name);
	node->data = data;

	node->left = NULL;
	node->right = NULL;

	if(!bRoot) {
		bRoot = node;
		return;
	}

	InsertHelper(bRoot, node);
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
	thBNode *parent = GetParent(bRoot, node);
	thBNode *left, *right;

	if(!parent) {
		fprintf(stderr, "thBree::Remove: No such node '%s'\n", node->name);
	}

	left = node->left;
	right = node->left;
	
	if(parent->left == node) {
		delete node->name;
		delete node;

		switch(StringCompare(left->name, right->name)) {
		case 0:
		case 1:
			parent->left = left;
			RemoveHelper(left, right);
			break;
		case -1:
			parent->left = right;
			RemoveHelper(right, left);
			break;
		}

	}
	else { /* parent->right == node */
		delete node->name;
		delete node;

		switch(StringCompare(left->name, right->name)) {
		case 0:
		case 1:
			parent->right = left;
			RemoveHelper(left, right);
			break;
		case -1:
			parent->right = right;
			RemoveHelper(right, left);
			break;
		}
	}
}

thBNode *thBTree::Find(char *name)
{
	if(!bRoot) {
		return NULL;
	}

	return FindHelper(bRoot, name);
}

void thBTree::PrintTree (void)
{
	PrintHelper(bRoot);
}

void thBTree::PrintHelper (thBNode *root)
{
	if(!root) {
		return;
	}

 	PrintHelper (root->left);
	printf("%s\n", root->name);
	PrintHelper (root->right);
}

void thBTree::RemoveHelper(thBNode *root, thBNode *node)
{
	if(!root) {
		fprintf(stderr, "thBTree::RemoveHelper: root is NULL\n");
		return;
	}

	switch(StringCompare(root->name, node->name)) {
	case -1:
		if(!root->right) {
			root->right = node;
		}
		else {
			switch(StringCompare(root->right->name, node->name)) {
			case -1:
			{
				thBNode *right = root->right;
				root->right = node;
				node = right;

				RemoveHelper(root->right, node);
			}
			break;
			case 0:
			case 1:
				RemoveHelper(root->right, node);
				break;
			}
		}
		break;
	case 0:
	case 1:
		if(!root->left) {
			root->left = node;
		}
		else {
			switch(StringCompare(root->left->name, node->name)) {
			case -1:
			{
				thBNode *left = root->left;
				root->left = node;
				node = left;
				
				RemoveHelper(root->left, node);
			}
			break;
			case 0:
			case 1:
				RemoveHelper(root->left, node);
				break;
			}
		}
		break;
	}
}

thBNode *thBTree::GetParent(thBNode *root, thBNode *node)
{
	thBNode *parent;

	if((root->left == node) || (root->right == node)) {
		return root;
	}

	if((parent = GetParent(root->left, node))) {
		return parent;
	}

	else if((parent = GetParent(root->right, node))) {
		return parent;
	}

	return NULL;
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

void thBTree::DestroyTree (thBNode *root)
{
	if(!root) {
		return;
	}

	DestroyTree(root->left);
	DestroyTree(root->right);

	delete root->name;
	delete root;
}
