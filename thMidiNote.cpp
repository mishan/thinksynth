#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thList.h"
#include "thPlugin.h"
#include "thNode.h"
#include "thMod.h"
#include "thMidiNote.h"

thMidiNote::thMidiNote ()
{
	/* create any objects */
}

thMidiNote::~thMidiNote ()
{
	/* free all data */
}

void thMidiNote::SetArg (char *name, float *value)
{
}

float *thMidiNote::GetArg (char *name)
{
	return NULL;
}

void thMidiNote::Process (void)
{
}
