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
  //	parsemod->PrintIONode();
  printf("  = %f\n", *((thArgValue *)parsemod->GetArg("test1", "point"))->argValues);
  delete parsenode;
}
