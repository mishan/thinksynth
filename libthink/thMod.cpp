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

void thMod::NewNode (thNode *node)
{
  modnodes->Insert(node->GetName(), node);
}

void thMod::SetIONode(char *name) {
  ionode = (thNode *)((thBSNode *)modnodes->Find(name))->data;
}

void thMod::PrintIONode(void) {
  ionode->PrintArgs();
}

void thMod::Process (void)
{
}








