#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thList.h" /* switch to BSTrees! */
#include "thBSTree.h"
#include "thArg.h"
#include "thPlugin.h"
#include "thNode.h"
#include "thMod.h"

thMod::thMod (const char *name)
{
	modname = strdup(name);
	ionode = NULL;

	/* create any other objects */
}

thMod::~thMod ()
{
	free(modname);
}

thNode *thMod::FindNode (const char *name)
{
	return (thNode *)modnodes.GetData(name);
}

const thArgValue *thMod::GetArg (const char *nodename, const char *argname)
  /* Follow pointers and return a thArgValue of a float string */
{
	thNode *node = (thNode *)modnodes.GetData(nodename);
	const thArgValue *args;
	
	if (node) {
		args = node->GetArg(argname);
	}

	while ((args->argType == ARG_POINTER) && node && args) { 
		/* Recurse through the list of pointers until we get a real value. */
		node = (thNode *)modnodes.GetData(args->argPointNode);
		if (node) {
			args = node->GetArg(args->argPointName);
		}
	}   /* Maybe also add some kind of infinite-loop checking thing? */

	return args;
}

void thMod::NewNode (thNode *node)
{
	modnodes.Insert((char *)node->GetName(), node);
}

/* We own this string. The caller may not free it. */
void thMod::SetName (char *name)
{
	free (modname);
	modname = name;
}

void thMod::SetIONode (const char *name)
{
	ionode = (thNode *)modnodes.GetData(name);
}

void thMod::PrintIONode (void)
{
	ionode->PrintArgs();
}

void thMod::Process (thMod *mod, unsigned int windowlen)
{
  thListNode *listnode;
  thNode *data;

  ionode->SetRecalc(false);

  for(listnode = ((thList *)ionode->GetChildren())->GetHead(); listnode; listnode = listnode->prev) {
    data = (thNode *)listnode->data;
    if(data->GetRecalc() == true) {
      ProcessHelper(mod, windowlen, data);
    }
  }
  printf("thMod::Process  %s\n", ionode->GetName());
  /* FIRE! */
  ((thPlugin *)ionode->GetPlugin())->Fire(ionode, mod, windowlen);
}

void thMod::ProcessHelper(thMod *mod, unsigned windowlen, thNode *node) {
  thListNode *listnode;
  thNode *data;

  node->SetRecalc(false);
  
  for(listnode = ((thList *)node->GetChildren())->GetHead(); listnode; listnode = listnode->prev) {
    data = (thNode *)listnode->data;
    if(data->GetRecalc() == true) {
      ProcessHelper(mod, windowlen, data);
    }
  }
  printf("thMod::ProcessHelper  %s\n", node->GetName());
  /* FIRE! */
  ((thPlugin *)node->GetPlugin())->Fire(node, mod, windowlen);
}

void thMod::SetActiveNodes(void) /* reset the recalc flag for nodes with active plugins */
{
  thListNode *listnode;
  thNode *data;

  for(listnode = activelist->GetHead(); listnode; listnode = listnode->prev) {
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

  for(listnode = ((thList *)node->GetParents())->GetHead(); listnode; listnode = listnode->prev) {
    data = (thNode *)listnode->data;
    if(data->GetRecalc() == false) {
      data->SetRecalc(true);
      SetActiveNodesHelper(data);
    }
  }
}
