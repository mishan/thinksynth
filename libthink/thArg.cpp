#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thArg.h"

thArg::thArg(char *name, float *value, int num)
{
	argName = strdup(name);

	argValues = new float[num];
	memcpy(argValues, value, num*sizeof(float));

	argNum = num;
}

thArg::thArg ()
{
	argName = NULL;
	argValues = NULL;
	argNum = 0;
}

thArg::~thArg()
{
	if(argName) {
		delete argName;
	}

	if(argValues) {
		delete argValues;
	}
}

void thArg::SetArg(char *name, float *value, int num)
{
	if(argName) {
		delete argName;
	}
	if(argValues) {
		delete argValues;
	}

	argName = strdup(name);
	
	argValues = new float[num];
	memcpy(argValues, value, num*sizeof(float));

	argNum = num;
}

const thArgValue *thArg::GetArg (void)
{
	thArgValue *value = new thArgValue;

	value->argName = argName;
	value->argValues = argValues;
	value->argNum = argNum;
}
