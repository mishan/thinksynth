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
  modnode = mod->Copy();
  float *notep=new float, *velocityp=new float;
  *notep = note, *velocityp = velocity;
  SetArg("note", notep, 1);
  SetArg("velocity", velocityp, 1);
}

thMidiNote::thMidiNote (thMod *mod)
{
}

thMidiNote::~thMidiNote ()
{
}

void thMidiNote::SetArg (const char *name, float *value, int num)
{
	thArg *arg = (thArg *)args.GetData(name);
	if(!arg) {
		arg = new thArg(name, value, num);
		args.Insert(name, arg);
	} else {
		arg->SetArg(name, value, num);
	}
}

thArgValue *thMidiNote::GetArg (const char *name)
{
	thArg *arg = (thArg *)args.GetData(name);
	return (thArgValue *)arg->GetArg();
}

void thMidiNote::Process (void)
{
}
