/* $Id: thMod.cpp,v 1.56 2003/05/03 10:06:45 ink Exp $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thList.h" /* XXX switch to BSTrees! */
#include "thBSTree.h"
#include "thArg.h"
#include "thPlugin.h"
#include "thNode.h"
#include "thMod.h"

thMod::thMod (const char *name)
{
	ionode = NULL;
	modname = strdup(name);
	modnodes = new thBSTree(StringCompare);
}

thMod::~thMod ()
{
	free(modname);
	delete modnodes;
}

thNode *thMod::FindNode (const char *name)
{
	return (thNode *)modnodes->GetData((void *)name);
}

const thArgValue *thMod::GetArg (const char *nodename, const char *argname)
  /* Follow pointers and return a thArgValue of a float string */
{
	thNode *node = (thNode *)modnodes->GetData((void *)nodename);
	const thArgValue *args;
	float *tmp = new float[1];

	if (node) {
		args = node->GetArg(argname);
	}

	/* If the arg doesnt exist, make it a 0 */
	if(args == NULL) {
	  tmp[0] = 0;
	  args = ((thArg *)node->SetArg(argname, tmp, 1))->GetArg();
	}

	while (args && (args->argType == ARG_POINTER) && node) { 
		/* Recurse through the list of pointers until we get a real value. */
		node = (thNode *)modnodes->GetData((void *)args->argPointNode);
		if (node) {
			args = node->GetArg(args->argPointName);
			/* If the arg doesnt exist, make it a 0 */
			if(args == NULL) {
			  tmp[0] = 0;
			  args = ((thArg *)node->SetArg(argname, tmp, 1))->GetArg();
			}
		}
	}   /* Maybe also add some kind of infinite-loop checking thing? */

	return args;
}

void thMod::NewNode (thNode *node)
{
	modnodes->Insert(strdup(node->GetName()), node);
}

/* We own this string. The caller may not free it. */
void thMod::SetName (char *name)
{
	free (modname);
	modname = name;
}

void thMod::SetIONode (const char *name)
{
	ionode = (thNode *)modnodes->GetData((void *)name);

	if (ionode == NULL) {
		printf ("thMod::SetIONode: warning; ionode is NULL\n");
	}
}

void thMod::PrintIONode (void)
{
	ionode->PrintArgs();
}

void thMod::Process (unsigned int windowlen)
{
	thListNode *listnode;
	thNode *data;

	ionode->SetRecalc(false);

	for(listnode = ((thList *)ionode->GetChildren())->GetTail(); listnode; listnode = listnode->prev) {
		data = (thNode *)listnode->data;
		if(data->GetRecalc() == true) {
			ProcessHelper(windowlen, data);
		}
	}

	printf("thMod::Process  %s\n", ionode->GetName());

	/* FIRE! */
	((thPlugin *)ionode->GetPlugin())->Fire(ionode, this, windowlen);
}

void thMod::ProcessHelper(unsigned windowlen, thNode *node)
{
	thListNode *listnode;
	thNode *data;

	node->SetRecalc(false);
  
	for(listnode = ((thList *)node->GetChildren())->GetTail(); listnode; listnode = listnode->prev) {
		data = (thNode *)listnode->data;
		if(data->GetRecalc() == true) {
			ProcessHelper(windowlen, data);
		}
	}

	printf("thMod::ProcessHelper  %s\n", node->GetName());

	/* FIRE! */
	((thPlugin *)node->GetPlugin())->Fire(node, this, windowlen);
}

void thMod::SetActiveNodes(void) /* reset the recalc flag for nodes with active plugins */
{
	thListNode *listnode;
	thNode *data;

	for(listnode = activelist.GetTail(); listnode; listnode = listnode->prev) {
		data = (thNode *)listnode->data;
		if(data->GetRecalc() == false) {
			data->SetRecalc(true);
			SetActiveNodesHelper((thNode *)listnode->data);
		}
	}
}

void thMod::SetActiveNodesHelper(thNode *node)
{
	thListNode *listnode;
	thNode *data;

	for(listnode = ((thList *)node->GetParents())->GetTail(); listnode; listnode = listnode->prev) {
		data = (thNode *)listnode->data;
		if(data->GetRecalc() == false) {
			data->SetRecalc(true);
			SetActiveNodesHelper(data);
		}
	}
}

thMod *thMod::Copy (void)
{
	thMod *newmod = new thMod(modname);
	thNode *newnode = new thNode(ionode->GetName(), ionode->GetPlugin());

	newnode->CopyArgs(ionode->GetArgTree());

	newmod->NewNode(newnode);
	newmod->SetIONode((char *)newnode->GetName());

	CopyHelper(newmod, ionode);

	return newmod;
}

void thMod::CopyHelper (thMod *mod, thNode *parentnode)
{
	thNode *data, *newnode;
	thListNode *listnode;
	thBSTree *argtree;

	if(parentnode->GetChildren()) {
		for(listnode = ((thList* )parentnode->GetChildren())->GetTail(); listnode; listnode = listnode->prev) {
			data = (thNode *)listnode->data;
			if(!mod->FindNode((char *)data->GetName())) {
				newnode = new thNode((char *)data->GetName(), data->GetPlugin());

				argtree = data->GetArgTree();

				if(argtree) {
					newnode->CopyArgs(argtree);
				}

				mod->NewNode(newnode);
				CopyHelper(mod, data);
			}
		}
	}
}

void thMod::BuildSynthTree (void)
{
	thBSTree *argtree;

	/* We don't want to recalc the root if something points here */
	ionode->SetRecalc(true);  

	argtree = ionode->GetArgTree();
	BuildSynthTreeHelper2(argtree, ionode);
}

int thMod::BuildSynthTreeHelper(thNode *parent, char *nodename)
{
	thBSTree *argtree;
	thNode *currentnode = FindNode(nodename);

	if(currentnode->GetRecalc() == true) {
		return(1);  /* This node has already been processed */
	}

	currentnode->SetRecalc(true);  /* This node has now been marked as processed */

	if(((thPlugin *)currentnode->GetPlugin())->GetState() == thActive) {
	  activelist.Add(currentnode);
	}

	/*	parent->AddChild(currentnode);
		currentnode->AddParent(parent);*/
	printf("Added Child %s to %s\n", currentnode->GetName(), parent->GetName());

	argtree = currentnode->GetArgTree();

	BuildSynthTreeHelper2(argtree, currentnode);
	return(0);
}

void thMod::BuildSynthTreeHelper2(thBSTree *argtree, thNode *currentnode)
{
	thArgValue *data;
	thNode *node;

	if(argtree) {
		data = (thArgValue *)((thArg *)argtree->GetData())->GetArg();
		BuildSynthTreeHelper2(argtree->GetLeft(), currentnode);

		if(data->argType == ARG_POINTER) {
			node = FindNode(data->argPointNode);

			currentnode->AddChild(node);
			node->AddParent(currentnode);

			if(node->GetRecalc() == false) {  /* Don't do the same node over and over */
				BuildSynthTreeHelper(currentnode, data->argPointNode);
			}
		}

		BuildSynthTreeHelper2(argtree->GetRight(), currentnode);
	}
}
