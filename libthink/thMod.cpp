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

thArgValue *thMod::GetArg (char *nodename, char *argname)

  /* Follow pointers and return a thArgValue of a float string */
{
  thNode *node = (thNode *)modnodes->GetData(nodename);
  thArgValue *args;

  if (node) { args = node->GetArg(argname); }
  printf("=-= %s %i  %p  %s\n", args->argName, args->argType, args, args->argPointNode);
  printf("--==-- %p\n", args);
 node->PrintArgs();
 printf("---\n");
 modnodes->PrintTree();
  while (args->argType == ARG_POINTER && node && args) {     /* Recurse through the 
                                     list of pointers until we get a real value. */
    printf("%s\n", args->argName);
    node = (thNode *)modnodes->GetData(args->argPointNode);
    if (node) { args = node->GetArg(args->argPointName); }
  }  /* Maybe also add some kind of infinite-loop checking thing? */
printf("-=- %s %i\n", args->argName, args->argType);
  return args;
}

void thMod::NewNode (thNode *node)
{
  modnodes->Insert(node->GetName(), node);
}

void thMod::SetIONode(char *name) {
  ionode = (thNode *)((thBSNode *)modnodes->Find(name))->data;
}

thNode *thMod::GetIONode(void) {
  return ionode;
}

void thMod::PrintIONode(void) {
  ionode->PrintArgs();
}

void thMod::Process (void)
{
}








