#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "thArg.h"
#include "thBSTree.h"
#include "thPlugin.h"
#include "thNode.h"
#include "thMod.h"

#include "thException.h"
#include "thAudio.h"
#include "thAudioBuffer.h"
#include "thWav.h"
#include "thOSSAudio.h"

#include "parser.h"

int main (int argc, char *argv[])
{
	parsemod = new thMod("newmod");     /* these are used by the parser */
	parsenode = new thNode("newnode", NULL);

	yyparse();
	parsemod->PrintIONode();
}
