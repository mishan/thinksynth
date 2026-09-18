/*
 * Copyright (C) 2004-2026 Metaphonic Labs
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

static const char desc[] = "Generates a square wave impulse";
thPlugin::State    mystate = thPlugin::PASSIVE;

void module_cleanup (thPlugin *plugin)
{
}

enum { IN_LEN,IN_WIDTH,IN_PW,IN_NUM,OUT_ARG };

int args[OUT_ARG + 1];

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    /* An impulse response for delay::fir, not a signal: the output is `len'
       samples long and is read whole, rather than one sample per window. All
       four args are read from sample 0 alone, so none of them modulates. */
    args[IN_LEN] = plugin->regArg("len", thPlugin::ARG_IN);
    /* `(int)(*in_len)[0]' -- the impulse is this many samples long. */
    plugin->setArgStep(args[IN_LEN], 1);
    plugin->setArgDesc(args[IN_LEN], "How long the whole response is");
    plugin->setArgUnits(args[IN_LEN], "samples");
    args[IN_WIDTH] = plugin->regArg("width", thPlugin::ARG_IN);
    /* Its own zero case: `if (pwidth == 0)' falls back to `pw' as a fraction
       of the pulse spacing. Writing both means this one wins. */
    plugin->setArgDesc(args[IN_WIDTH],
                       "How wide each pulse is; 0 uses pw instead");
    plugin->setArgUnits(args[IN_WIDTH], "samples");
    args[IN_PW] = plugin->regArg("pw", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_PW],
                       "Pulse width as a fraction of the spacing, when width "
                       "is 0");
    plugin->setArgRange(args[IN_PW], 0, 1);
    args[IN_NUM] = plugin->regArg("num", thPlugin::ARG_IN);
    /* `width = len/num' and `amp = 1/(pwidth*num)', so a num of 0 is a
       division by zero twice over and the response comes out non-finite.
       Said here rather than clamped: this is the count of pulses, an integer
       a caller chooses once, and a 0 is a graph that means nothing rather
       than a value drifting through a range. */
    plugin->setArgStep(args[IN_NUM], 1);
    plugin->setArgDesc(args[IN_NUM],
                       "How many pulses; must be at least 1");
    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG],
                       "The response, scaled so the pulses sum to 1");

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
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
    out = out_arg->allocate(len);

    for(i = 0; (i+width) < len; i += width)
    {
        for(j = 0; j < width; j++)
        {
            out[(int)i+j] = (j <= pwidth) ? amp : 0;
        }
    }
    
    return 0;
}
