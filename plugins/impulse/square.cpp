/* $Id: square.cpp,v 1.2 2003/05/30 00:55:41 aaronl Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "think.h"

#include "thArg.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"
#include "thSynth.h"


char		*desc = "Generates a square wave impulse";
thPluginState	mystate = thPassive;

extern "C" int	module_init (thPlugin *plugin);
extern "C" int	module_callback (thNode *node, thMod *mod, unsigned int windowlen);
extern "C" void module_cleanup (struct module *mod);

void module_cleanup (struct module *mod)
{
	printf("Square plugin unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("Square plugin loaded\n");

	plugin->SetDesc (desc);
	plugin->SetState (mystate);

	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
	float *out;
	thArg *in_len, *in_pw, *in_num, *in_width;
	thArg *out_arg;
	float i, pw, num, amp, width, pwidth;
	unsigned int j, len;

	in_len = mod->GetArg(node, "len"); /* Length of output */
	in_width = mod->GetArg(node, "width"); /* Length of pulse */
	in_pw = mod->GetArg(node, "pw"); /* Width of the pulses (%) if no length given */
	in_num = mod->GetArg(node, "num"); /* Number of pulses */
	len = (int)(*in_len)[0];
	num = (*in_num)[0];
	width = len/num;
	pwidth = (*in_width)[0];
	if(pwidth == 0) {
		pw = (*in_pw)[0];
		pwidth = width * pw;
	}
	amp = 1/(pwidth*num);

	out_arg = mod->GetArg(node, "out");
	out = out_arg->allocate(len);
	for(i = 0; (i+width) < len; i += width) {
		for(j = 0; j < width; j++) {
			if(j <= pwidth) {
				out[(int)i+j] = amp;
			} else {
				out[(int)i+j] = 0;
			}
		}
	}
	
	return 0;
}

