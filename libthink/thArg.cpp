/* $Id$ */
/*
 * Copyright (C) 2004 Metaphonic Labs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "config.h"

#include <string.h>

#include "think.h"

/* Ownership of value will transfer to us. Same goes for SetAllocatedArg. */

thArg::thArg(const string &name, float *value, const int num)
{
	name_ = name;
	values_ = value;
	len_ = num;

	type_ = ARG_VALUE;
	nodePtrId_ = -1;   /* so we know it has not been set yet */
	argPtrId_ = -1;   /* so we know it has not been set yet */

	widgetType_ = HIDE;
	min_ = 0;
	max_ = MIDIVALMAX;
}

thArg::thArg(const string &name, const string &node, const string &value)
{
	name_ = name;
	values_ = NULL;
	len_ = 0;

	nodePtrName_ = node;
	argPtrName_ = value;

	type_ = ARG_POINTER;
	nodePtrId_ = -1;   /* so we know it has not been set yet */
	argPtrId_ = -1;   /* so we know it has not been set yet */

	widgetType_ = HIDE;
	min_ = 0;
	max_ = MIDIVALMAX;
}

thArg::thArg(const string &name, const string &chanarg)
{
    name_ = name;
    values_ = NULL;
    len_ = 0;

    argPtrName_ = chanarg;

    type_ = ARG_CHANNEL;
    nodePtrId_ = -1;   /* so we know it has not been set yet */
    argPtrId_ = -1;   /* so we know it has not been set yet */
	argPtr_ = NULL;

	widgetType_ = HIDE;
	min_ = 0;
	max_ = MIDIVALMAX;
}


thArg::thArg (const thArg *copyArg)
{
	name_ = copyArg->name_;

	len_ = copyArg->len_;
	values_ = new float[len_];
	memcpy(values_, copyArg->values_, len_ * sizeof(float));
	
	comment_ = copyArg->comment_;
	label_ = copyArg->label_;
	units_ = copyArg->units_;
	min_ = copyArg->min_;
	max_ = copyArg->max_;
	widgetType_ = copyArg->widgetType_;

	argPtrName_ = copyArg->argPtrName_;
	type_ = copyArg->type_;
	nodePtrId_ = copyArg->nodePtrId_;
	argPtrId_ = copyArg->argPtrId_;
	argPtr_ = copyArg->argPtr_;
}

/* the equivalent of creating a thArg(NULL, NULL, 0) */
thArg::thArg (void)
{
	values_ = NULL;
	len_ = 0;
	widgetType_ = HIDE;
    nodePtrId_ = -1;   /* so we know it has not been set yet */
    argPtrId_ = -1;   /* so we know it has not been set yet */
	argPtr_ = NULL;
}

thArg::~thArg(void)
{
	if (values_) {
		delete[] values_;
	}
}

float *thArg::allocate (unsigned int elements)
{
	if(values_ == NULL) {
		values_ = new float[elements];
		len_ = elements;
	}
	else if(len_ != elements) {
		delete[] values_;
		values_ = new float[elements];
		len_ = elements;
	}

	return values_;
}

void thArg::setArg(const string &name, float *value, const int num)
{
	values_ = allocate(num);
	
	name_ = name;
	
	memcpy(values_, value, num * sizeof(float));

	len_ = num;
	type_ = ARG_VALUE;
}

void thArg::setAllocatedArg(const string &name, float *value, const int num)
{
	values_ = value;
	name_ = name;
	len_ = num;
	type_ = ARG_VALUE;
}

void thArg::setArg(const string &name, const string &node, const string &value)
{
	name_ = name;
	nodePtrName_ = node;
	argPtrName_ = value;
	
	type_ = ARG_POINTER;
}

void thArg::setArg(const string &name, const string &chanarg)
{
    name_ = name;
    argPtrName_ = chanarg;

    type_ = ARG_CHANNEL;
}

void thArg::getBuffer(float *buffer, unsigned int size)
{
	unsigned int i, j;	

	if(type_ == ARG_VALUE)
    {
		j = 0; /* depth into the arg float array (for the loop) */
		for(i = 0; i < size; i++)
		{
			buffer[i] = values_[j];
			if(++j >= len_)
			{
				j = 0;
			}
		}
	}
}

void thArg::setValue(float value)
{
	values_ = allocate(1);
	values_[0] = value;
	m_signal_arg_changed(this);
}

void thArg::setValue(float *values, int len)
{
	allocate(len);
	memcpy(values_, values, len*sizeof(float));
	m_signal_arg_changed(this);
}
