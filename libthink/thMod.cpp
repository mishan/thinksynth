#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thList.h" /* switch to BTrees! */
#include "thBTree.h"
#include "thArg.h"
#include "thPlugin.h"
#include "thNode.h"
#include "thMod.h"

thMod::thMod (char *name)
{
	modname = strdup(name);

	/* create any other objects */
}

thMod::~thMod ()
{
}

thNode *thMod::FindNode (char *name)
{
	return NULL;
}

void thMod::NewNode (thNode *node)
{
}

void thMod::Process (void)
{
}
