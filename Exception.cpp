#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Exception.h"

Exception::Exception(void)
{
	message = NULL;
}

Exception::Exception(const char *msg)
{
	printf("construcor called with %s\n", msg);
	message = strdup(msg);
}

Exception::~Exception (void)
{
	if(message) {
		delete message;
	}
}

const char *Exception::string (void)
{
	return message;
}
