#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>

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
