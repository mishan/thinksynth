/* $Id: thMidiNote.cpp,v 1.19 2003/04/27 10:38:52 ink Exp $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thList.h"
#include "thBSTree.h"
#include "thArg.h"
#include "thPlugin.h"
#include "thNode.h"
#include "thMod.h"
#include "thMidiNote.h"

thMidiNote::thMidiNote (thMod *mod, float note, float velocity)
{
	float *notep = new float, *velocityp = new float;

	args = new thBSTree(StringCompare);

	modnode = mod->Copy();
	*notep = note, *velocityp = velocity;

	SetArg("note", notep, 1);
	SetArg("velocity", velocityp, 1);


}

thMidiNote::thMidiNote (thMod *mod)
{
	args = new thBSTree(StringCompare);

	modnode = mod->Copy();
}

thMidiNote::~thMidiNote ()
{
	delete args;
}

void thMidiNote::SetArg (const char *name, float *value, int num)
{
	thArg *arg = (thArg *)args->GetData((void *)name);

	if(!arg) {
		arg = new thArg(name, value, num);
		args->Insert((void *)name, (void *)arg);
	} else {
		arg->SetArg(name, value, num);
	}
}

thArgValue *thMidiNote::GetArg (const char *name)
{
	thArg *arg = (thArg *)args->GetData((void *)name);
	return (thArgValue *)arg->GetArg();
}

void thMidiNote::Process (float *data, int length)
{
	int chan;

	for(chan=0; chan < modnode->GetArg(((thNode *)modnode->GetIONode())->GetName(), "channels"); chan++) {
		printf("Processing Note Channel Number %i\n", chan);
	}
}
