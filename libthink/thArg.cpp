#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thArg.h"

thArg::thArg(const char *name, const float *value, const int num)
{
	argName = strdup(name);

	argValues = new float[num];
	memcpy(argValues, value, num*sizeof(float));

	argNum = num;

	argPointNode = NULL;
	argPointName = NULL;

	argType = ARG_VALUE;
}

thArg::thArg(const char *name, const char *node, const char *value)
{
	argName = strdup(name);
	argPointNode = strdup(node);
	argPointName = strdup(value);

	argValues = NULL;
	argNum = 0;

	argType = ARG_POINTER;
}

thArg::thArg () /* the equivalent of creating a thArg(NULL, NULL, 0) */
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

void thArg::SetArg(const char *name, const float *value, const int num)
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
	argType = ARG_VALUE;
}

void thArg::SetArg(const char *name, const char *node, const char *value)
{
	if(argName) {
		delete argName;
	}

	if(argPointNode) {
		delete argPointNode;
	}

	if(argPointName) {
		delete argPointName;
	}

	argName = strdup(name);
	argPointNode = strdup(node);
	argPointName = strdup(value);

	argType = ARG_POINTER;
}

const char *thArg::GetArgName (void)
{
	return argName;
}

const thArgValue *thArg::GetArg (void)
{
	thArgValue *value;

	if(!argName || !argValues) {
		return NULL;
	}

	value = new thArgValue;

	value->argName = argName;
	value->argValues = argValues;
	value->argNum = argNum;

	return value;
}
