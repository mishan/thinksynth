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
#include "thNode.h"
#include "thMod.h"
#include "thSynth.h"

#include "parser.h"

//thNode *parsenode;
//thMod *parsemod;

thSynth::thSynth()
{
	modlist = new thBSTree();
}

thSynth::~thSynth()
{
	delete modlist;
}

void thSynth::LoadMod(char *filename)
{
	yyin = fopen(filename, "r");
	
	parsemod = new thMod("newmod");     /* these are used by the parser */
	parsenode = new thNode("newnode", NULL);
	
	yyparse();
	
	delete parsenode;
	
	modlist->Insert(parsemod->GetName(), parsemod);
}

thMod *thSynth::FindMod(char *modname)
{
  return (thMod *)modlist->GetData(modname);
}

/* Make these voids return something and add error checking everywhere! */

void thSynth::ListMods(void)
{
  modlist->PrintTree();
}

void thSynth::BuildSynthTree(char *modname)
{
  thMod *mod = FindMod(modname);
  thListNode *listnode;
  thArgValue *data;
  thNode *ionode = mod->GetIONode();

  for(listnode = ((thList *)ionode->GetArgList())->GetHead() ; listnode ; listnode = listnode->prev) {
    data = (thArgValue *)((thArg *)listnode->data)->GetArg();
    printf("-=- %s %i\n", data->argName, data->argType);
  }
}
      
      



















