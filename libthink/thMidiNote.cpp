/* $Id: thMidiNote.cpp,v 1.17 2003/04/27 02:33:05 misha Exp $ */

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

	modnode = mod->Copy();
	*notep = note, *velocityp = velocity;

	SetArg("note", notep, 1);
	SetArg("velocity", velocityp, 1);

	args = new thBSTree(StringCompare);
}

thMidiNote::thMidiNote (thMod *mod)
{
	modnode = mod->Copy();
	args = new thBSTree(StringCompare);
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

void thMidiNote::Process (void)
{
}
