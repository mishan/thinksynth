#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thList.h"
#include "thBTree.h"
#include "thArg.h"
#include "thPlugin.h"
#include "thNode.h"
#include "thMod.h"
#include "thMidiNote.h"

thMidiNote::thMidiNote (thMod *mod, float note, float velocity)
{
	args = new thList;

	SetArg("note", &note, 1);
	SetArg("velocity", &velocity, 1);
}

thMidiNote::thMidiNote (thMod *mod)
{
	args = new thList;
}

thMidiNote::~thMidiNote ()
{
	delete args;
}

void thMidiNote::SetArg (char *name, float *value, int num)
{
}

float *thMidiNote::GetArg (char *name)
{
	return NULL;
}

void thMidiNote::Process (void)
{
}
