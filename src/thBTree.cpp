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
	thBNode *node;

	if(Find(name)) {
		fprintf(stderr, "thBTree::Insert: Duplicate node '%s'\n", name);
		return;
	}

	node = new thBNode;

	node->name = strdup(name);
	node->data = data;

	node->left = NULL;
	node->right = NULL;

	if(!bRoot) {
		bRoot = node;
		return;
	}

	/* find a place to put this node in */
	InsertHelper(bRoot, node);
}

void thBTree::InsertHelper(thBNode *root, thBNode *node)
{
	switch(StringCompare(node->name, root->name)) {
		/* node is equal to current node, cannot have duplicate nodes */
	case 0:
		fprintf(stderr, "thBTree::InsertHelper: Duplicate node should not exist\n");
		break;
		/* node is less than current node */
	case -1:
		if(root->left) {
			InsertHelper(root->left, node);
		}
		else {
			root->left = node;
		}
		break;
		/* node is greater than current node */
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

void thBTree::Remove(char *name)
{
	thBNode *node = Find(name);
	thBNode *parent; /* can be NULL */
	thBNode *left, *right; /* can also be NULL */
	thBNode *newroot, *newchild; /* both of these are also potentially NULL */

	if(!node) {
		fprintf(stderr, "thBTree::Remove: No such node '%s'\n", name);
		return;
	}

	parent = GetParent(bRoot, node);

	left = node->left;
	right = node->right;

	delete node->name;
	delete node;

	if(left && right) {
		switch(StringCompare(left->name, right->name)) {
		case 0:
			fprintf(stderr, "thBTree::InsertHelper: Duplicate node should not exist\n");
			break;
		case 1:
			newroot = left;
			newchild = right;
			break;
		case -1:
			newroot = right;
			newchild = left;
			break;
		}
	}
	else {
		newroot = left ? left : right;
		newchild = NULL;
	}

	if(parent) {
		if(parent->left == node) {
			parent->left = newroot;
		}
		else { /* parent->right == node */
			parent->right = newroot;
		}
	}
	else {
		/* if this node doesn't have a parent, that means it's the root. Make
		   newroot the new bRoot */
 		bRoot = newroot;
	}

	/* if we have a left-over node, we must rebuild the whole tree downwards,
	   this is what the RemoveHelper method accomplishes */
	if(newchild) {
		RemoveHelper(newroot, newchild);	
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
				fprintf(stderr, "thBTree::InsertHelper: Duplicate node should not exist\n");
				break;
			case 1:
				RemoveHelper(root->right, node);
				break;
			}
		}
		break;
	case 0:
		fprintf(stderr, "thBTree::InsertHelper: Duplicate node should not exist\n");
		break;
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
				fprintf(stderr, "thBTree::InsertHelper: Duplicate node should not exist\n");
				break;
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

	if(!root) {
		return NULL;
	}

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
