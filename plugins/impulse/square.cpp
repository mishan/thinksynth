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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

char		*desc = "Generates a square wave impulse";
thPlugin::State	mystate = thPlugin::PASSIVE;

void module_cleanup (struct module *mod)
{
}

enum { IN_LEN,IN_WIDTH,IN_PW,IN_NUM,OUT_ARG };

int args[OUT_ARG + 1];

int module_init (thPlugin *plugin)
{
	plugin->setDesc (desc);
	plugin->setState (mystate);

	args[IN_LEN] = plugin->regArg("len");
	args[IN_WIDTH] = plugin->regArg("width");
	args[IN_PW] = plugin->regArg("pw");
	args[IN_NUM] = plugin->regArg("num");
	args[OUT_ARG] = plugin->regArg("out");

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen,
					 unsigned int samples)
{
	float *out;
	thArg *in_len, *in_pw, *in_num, *in_width;
	thArg *out_arg;
	float i, pw, num, amp, width, pwidth;
	unsigned int j, len;

	in_len = mod->getArg(node, args[IN_LEN]);
	in_width = mod->getArg(node, args[IN_WIDTH]);
	in_pw = mod->getArg(node, args[IN_PW]);
	in_num = mod->getArg(node, args[IN_NUM]);

	len = (int)(*in_len)[0];
	num = (*in_num)[0];
	width = len/num;
	pwidth = (*in_width)[0];
	if(pwidth == 0) {
		pw = (*in_pw)[0];
		pwidth = width * pw;
	}
	amp = 1/(pwidth*num);

	out_arg = mod->getArg(node, args[OUT_ARG]);
	out = out_arg->Allocate(len);

	for(i = 0; (i+width) < len; i += width)
	{
		for(j = 0; j < width; j++)
		{
			out[(int)i+j] = (j <= pwidth) ? amp : 0;
		}
	}
	
	return 0;
}
