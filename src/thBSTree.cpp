#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thList.h"
#include "thBSTree.h"

thBSTree::thBSTree (void)
{
	bRoot = NULL;
}

thBSTree::~thBSTree (void)
{
	DestroyTree(bRoot);
}

void thBSTree::Insert(const char *name, void *data)
{
	thBSNode *node;

	if(!name) {
		fprintf(stderr, "thBSTree::Insert: Cannot insert node with NULL name\n");
		return;
	}

 	if(Find(name)) {
		fprintf(stderr, "thBSTree::Insert: Duplicate node '%s'\n", name);
	 	return;
	}

	node = new thBSNode;
 
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

void thBSTree::InsertHelper(thBSNode *root, thBSNode *node)
{
	switch(StringCompare(node->name, root->name)) {
		/* node is equal to current node, cannot have duplicate nodes */
	case 0:
		fprintf(stderr, "thBSTree::InsertHelper: Duplicate node should not exist\n");
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

void thBSTree::Remove(const char *name)
{
	thBSNode *node = Find(name);
	thBSNode *parent; /* can be NULL */
	thBSNode *left, *right; /* can also be NULL */
	thBSNode *newroot, *newchild; /* both of these are also potentially NULL */

	if(!node) {
		fprintf(stderr, "thBSTree::Remove: No such node '%s'\n", name);
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
			fprintf(stderr, "thBSTree::InsertHelper: Duplicate node should not exist\n");
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

thBSNode *thBSTree::Find(const char *name)
{
	if(!bRoot) {
		return NULL;
	}

	return FindHelper(bRoot, name);
}

void thBSTree::PrintTree (void)
{
	PrintHelper(bRoot);
}

void thBSTree::PrintHelper (thBSNode *root)
{
	if(!root) {
		return;
	}

 	PrintHelper (root->left);
	printf("%s\n", root->name);
	PrintHelper (root->right);
}

thList *thBSTree::GetList (void)
{
	thList *tlist;

	if(!bRoot) {
		return NULL;
	}
	
	tlist = new thList;
	
	GetListHelper(bRoot, tlist);

	return tlist;
}

void thBSTree::GetListHelper(thBSNode *root, thList *tlist)
{
	if(!root) {
		return;
	}

	tlist->Add(root->data);

	GetListHelper(root->left, tlist);
	GetListHelper(root->right, tlist);
}

void thBSTree::RemoveHelper(thBSNode *root, thBSNode *node)
{
	if(!root) {
		fprintf(stderr, "thBSTree::RemoveHelper: root is NULL\n");
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
				thBSNode *right = root->right;
				root->right = node;
				node = right;

				RemoveHelper(root->right, node);
			}
			break;
			case 0:
				fprintf(stderr, "thBSTree::InsertHelper: Duplicate node should not exist\n");
				break;
			case 1:
				RemoveHelper(root->right, node);
				break;
			}
		}
		break;
	case 0:
		fprintf(stderr, "thBSTree::InsertHelper: Duplicate node should not exist\n");
		break;
	case 1:
		if(!root->left) {
			root->left = node;
		}
		else {
			switch(StringCompare(root->left->name, node->name)) {
			case -1:
			{
				thBSNode *left = root->left;
				root->left = node;
				node = left;
				
				RemoveHelper(root->left, node);
			}
			break;
			case 0:
				fprintf(stderr, "thBSTree::InsertHelper: Duplicate node should not exist\n");
				break;
			case 1:
				RemoveHelper(root->left, node);
				break;
			}
		}
		break;
	}
}

thBSNode *thBSTree::GetParent(thBSNode *root, thBSNode *node)
{
	thBSNode *parent;

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

thBSNode *thBSTree::FindHelper(thBSNode *root, const char *name)
{
	if(!name) {
		return NULL;
	}

	if(root->name && (!strcmp(root->name, name))) {
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

void thBSTree::DestroyTree (thBSNode *root)
{
	if(!root) {
		return;
	}

	DestroyTree(root->left);
	DestroyTree(root->right);

	delete root->name;
	delete root;
}

void *thBSTree::GetData ( char *name )
{
	thBSNode *node = Find(name);

	if(node) {
		return node->data;
	}

	return NULL;
}
