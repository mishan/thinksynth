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
	SetArg("note", &note, 1);
	SetArg("velocity", &velocity, 1);
}

thMidiNote::thMidiNote (thMod *mod)
{
}

thMidiNote::~thMidiNote ()
{
}

void thMidiNote::SetArg (char *name, float *value, int num)
{
	thArg *arg = (thArg *)args.Find(name);
	if(!arg) {
		arg = new thArg(name, value, num);
		args.Insert(name, arg);
	} else {
		arg->SetArg(name, value, num);
	}
}

thArgValue *thMidiNote::GetArg (char *name)
{
	thArg *arg = (thArg *)args.Find(name);
	return (thArgValue *)arg->GetArg();
}

void thMidiNote::Process (void)
{
}
