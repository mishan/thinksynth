#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thArg.h"
#include "thList.h"
#include "thBSTree.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"
#include "thSynth.h"

#include "parser.h"

//thNode *parsenode;
//thMod *parsemod;

thSynth::thSynth()
{
  windowlen = 1024;
}

thSynth::~thSynth()
{
}

void thSynth::LoadMod(char *filename)
{
	yyin = fopen(filename, "r");
	
	parsemod = new thMod("newmod");     /* these are used by the parser */
	parsenode = new thNode("newnode", NULL);
	
	yyparse();
	
	delete parsenode;
	
	modlist.Insert(parsemod->GetName(), parsemod);
}

thMod *thSynth::FindMod(char *modname)
{
  return (thMod *)modlist.GetData(modname);
}

/* Make these voids return something and add error checking everywhere! */

void thSynth::ListMods(void)
{
  modlist.PrintTree();
}

void thSynth::BuildSynthTree(char *modname)
{
  thMod *mod = FindMod(modname);
  thListNode *listnode;
  thArgValue *data;
  thNode *ionode = mod->GetIONode();

  ionode->SetRecalc(true);  /* We dont want to recalc the root if something
			       points here */

  if(ionode->GetArgList()) {
    for(listnode = ((thList *)ionode->GetArgList())->GetHead() ; listnode ; listnode = listnode->prev) {
      data = (thArgValue *)((thArg *)listnode->data)->GetArg();
      if(data->argType == ARG_POINTER) {
	BuildSynthTreeHelper(mod, ionode, data->argPointNode);
      }
    }
  }
}

int thSynth::BuildSynthTreeHelper(thMod *mod, thNode *parent, char *nodename)
{
  thListNode *listnode;
  thArgValue *data;
  thNode *currentnode = mod->FindNode(nodename);
  thNode *node;

  if(currentnode->GetRecalc() == true) {
    return(1);  /* This node has already been processed */
  }
  currentnode->SetRecalc(true);  /* This node has now been marked as processed */

  parent->AddChild(currentnode);
  currentnode->AddParent(parent);
  printf("Added Child %s to %s\n", currentnode->GetName(), parent->GetName());

  if(currentnode->GetArgList()) {
    for(listnode = ((thList *)currentnode->GetArgList())->GetHead(); listnode ; listnode = listnode->prev) {
      data = (thArgValue *)((thArg *)listnode->data)->GetArg();
      if(data->argType == ARG_POINTER) {
	node = mod->FindNode(data->argPointNode);
	if(node->GetRecalc() == false) {  /* Dont do the same node over and over */
	  BuildSynthTreeHelper(mod, currentnode, data->argPointNode);
	}
      }
    }
  }
  return(0);
}

const thPluginManager *thSynth::GetPluginManager(void)
{
  return &pluginmanager;
}

void thSynth::Process(char *modname)
{
  thMod *mod = FindMod(modname);
  mod->Process(mod, windowlen);
}



