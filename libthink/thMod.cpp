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

thMod::thMod (char *name)
{
  modname = strdup(name);
  modnodes = new thBSTree;
  ionode = NULL;
  
  /* create any other objects */
}

thMod::~thMod ()
{
}

thNode *thMod::FindNode (char *name)
{
	return (thNode *)((thBSNode *)modnodes->Find(name))->data;
}

const thArgValue *thMod::GetArg (char *nodename, char *argname)
  /* Follow pointers and return a thArgValue of a float string */
{
	thNode *node = (thNode *)modnodes->GetData(nodename);
	const thArgValue *args;
	
	if (node) {
		args = node->GetArg(argname);
	}

	while ((args->argType == ARG_POINTER) && node && args) { 
		/* Recurse through the list of pointers until we get a real value. */
		node = (thNode *)modnodes->GetData(args->argPointNode);
		if (node) {
			args = node->GetArg(args->argPointName);
		}
	}   /* Maybe also add some kind of infinite-loop checking thing? */

	return args;
}

void thMod::NewNode (thNode *node)
{
	modnodes->Insert((char *)node->GetName(), node);
}

const char *thMod::GetName (void)
{
	return(modname);
}

void thMod::SetName (char *name)
{
	if(modname) {
		delete modname;
	}
	modname = strdup(name);
}

void thMod::SetIONode (char *name)
{
	ionode = (thNode *)((thBSNode *)modnodes->Find(name))->data;
}

thNode *thMod::GetIONode (void)
{
	return ionode;
}

void thMod::PrintIONode (void)
{
	ionode->PrintArgs();
}

void thMod::Process (void)
{
  thListNode *listnode;
  thNode *data;

  ionode->SetRecalc(false);

  for(listnode = ((thList *)ionode->GetChildren())->GetHead(); listnode; listnode = listnode->prev) {
    data = (thNode *)listnode->data;
    if(data->GetRecalc() == true) {
      ProcessHelper(data);
    }
  }
  printf("thMod::Process  %s\n", ionode->GetName());
  /* XXX This is where we will fire the IO Node's plugin */
}

void thMod::ProcessHelper(thNode *node) {
  thListNode *listnode;
  thNode *data;

  node->SetRecalc(false);
  
  for(listnode = ((thList *)node->GetChildren())->GetHead(); listnode; listnode = listnode->prev) {
    data = (thNode *)listnode->data;
    if(data->GetRecalc() == true) {
      ProcessHelper(data);
    }
  }
  printf("thMod::ProcessHelper  %s\n", node->GetName());
  /* XXX This is where we will fire the IO Node's plugin */
}









