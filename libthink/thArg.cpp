#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thArg.h"

thArg::thArg(const char *name, const float *value, const int num)
{
  argValue = new thArgValue;
	argValue->argName = strdup(name);

	argValue->argValues = new float[num];
	memcpy(argValue->argValues, value, num*sizeof(float));

	argValue->argNum = num;

	argValue->argPointNode = NULL;
	argValue->argPointName = NULL;

	argValue->argType = ARG_VALUE;
}

thArg::thArg(const char *name, const char *node, const char *value)
{
  argValue = new thArgValue;
	argValue->argName = strdup(name);
	argValue->argPointNode = strdup(node);
	argValue->argPointName = strdup(value);

	argValue->argValues = NULL;
	argValue->argNum = 0;

	argValue->argType = ARG_POINTER;
}

thArg::thArg () /* the equivalent of creating a thArg(NULL, NULL, 0) */
{
	argValue = new thArgValue;
}

thArg::~thArg()
{
	if(argValue) {
		delete argValue;
	}
}

void thArg::SetArg(const char *name, const float *value, const int num)
{
	if(argValue->argName) {
		delete argValue->argName;
	}

	if(argValue->argValues) {
		delete argValue->argValues;
	}

	argValue->argName = strdup(name);
	
	argValue->argValues = new float[num];
	memcpy(argValue->argValues, value, num*sizeof(float));

	argValue->argNum = num;
	argValue->argType = ARG_VALUE;
}

void thArg::SetArg(const char *name, const char *node, const char *value)
{
	if(argValue->argName) {
		delete argValue->argName;
	}

	if(argValue->argPointNode) {
		delete argValue->argPointNode;
	}

	if(argValue->argPointName) {
		delete argValue->argPointName;
	}

	argValue->argName = strdup(name);
	argValue->argPointNode = strdup(node);
	argValue->argPointName = strdup(value);

	argValue->argType = ARG_POINTER;
}

const char *thArg::GetArgName (void)
{
	return argValue->argName;
}

const thArgValue *thArg::GetArg (void)
{
  //	thArgValue *value;

	if(!argValue) {
		return NULL;
	}
	/*
	value = new thArgValue;

	value->argName = argName;
	value->argValues = argValues;
	value->argNum = argNum;
	value->argType = argType;
	value->argPointNode = argPointNode;
	value->argPointName = argPointName;
	*/

	return argValue;
}




















