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

#define SQR(x) (x*x)

enum {IN_ARG, IN_CUTOFF, IN_RES, OUT_ARG, OUT_AOUT, INOUT_LAST};
int args[INOUT_LAST + 1];

static const char desc[] = "`INK Filter`  Gravity-based low pass";
thPlugin::State    mystate = thPlugin::ACTIVE;

/* The stable region; see the callback. The determinant of the state matrix is
   res exactly, and the trace condition is cutoff*cutoff < 2(1+res)/res. CCEIL
   caps the cutoff when res is zero and there is no bound to derive; CMARGIN is
   the slack against float rounding. */
#define RMAX     0.999f
#define CCEIL    64.0f
#define CMARGIN  0.98f

void module_cleanup (thPlugin *plugin)
{
}

int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[IN_ARG] = plugin->regArg("in", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_ARG], "Signal in");
    plugin->setArgRange(args[IN_ARG], TH_MIN, TH_MAX);
    plugin->setArgUnits(args[IN_ARG], "full scale");
    args[IN_CUTOFF] = plugin->regArg("cutoff", thPlugin::ARG_IN);
    /* 0 to 1 is the part that does not move: above 1 what is stable depends
       on res, which a single range cannot say. */
    plugin->setArgDesc(args[IN_CUTOFF],
                       "Cutoff, 0 to 1 -- a spring constant, not hertz. What "
                       "is stable above 1 depends on res");
    plugin->setArgRange(args[IN_CUTOFF], 0, 1);
    plugin->setArgUnits(args[IN_CUTOFF], "spring constant");
    args[IN_RES] = plugin->regArg("res", thPlugin::ARG_IN);
    plugin->setArgDesc(args[IN_RES],
                       "Resonance, 0 to 1; 1 is the edge of the stable "
                       "region and is clamped short");
    plugin->setArgRange(args[IN_RES], 0, RMAX);

    args[OUT_ARG] = plugin->regArg("out", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_ARG], "Filtered signal");
    plugin->setArgUnits(args[OUT_ARG], "full scale");
    args[OUT_AOUT] = plugin->regArg("aout", thPlugin::ARG_OUT);
    plugin->setArgDesc(args[OUT_AOUT], "The filter's velocity, band-pass-ish");
    plugin->setArgUnits(args[OUT_AOUT], "full scale");

    args[INOUT_LAST] = plugin->regArg("last", thPlugin::ARG_STATE);

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int sample)
{
    float *out;
    float *aout;
    float *out_last;
//    float *out_last_accel = new float[1];
    thArg *in_arg, *in_cutoff, *in_res;
    thArg *out_arg, *out_accel;
    thArg *inout_last;
    float buf_in[windowlen], buf_cut[windowlen], buf_res[windowlen];
    unsigned int i;
    float in, last, diff, accel;
    float val_arg, val_cutoff, val_res;

    out_arg = mod->getArg(node, args[OUT_ARG]);
    out_accel = mod->getArg(node, args[OUT_AOUT]);
    inout_last = mod->getArg(node, args[INOUT_LAST]);

    last = (*inout_last)[0];
    accel = (*inout_last)[1];
    out_last = inout_last->allocate(2);

    out = out_arg->allocate(windowlen);
    aout = out_accel->allocate(windowlen);

    in_arg = mod->getArg(node, args[IN_ARG]);
    in_cutoff = mod->getArg(node, args[IN_CUTOFF]);
    in_res = mod->getArg(node, args[IN_RES]);
    
    in_arg->getBuffer(buf_in, windowlen);
    in_cutoff->getBuffer(buf_cut, windowlen);
    in_res->getBuffer(buf_res, windowlen);

    /* Feedback state: one non-finite input is read back for ever after, so
       start over rather than stay dead for the life of the note. */
    if (!thIsFinite(last) || !thIsFinite(accel))
    {
        last = 0;
        accel = 0;
    }

    for(i = 0; i < windowlen; i++) {
        val_arg = buf_in[i];

        /* The state is (accel, last), stepped as accel' = res*(accel +
         * (in - last)*cutoff*cutoff), last' = last + accel'. That matrix has
         * determinant res and trace res + 1 - res*cutoff*cutoff, so both
         * eigenvalues are inside the unit circle exactly when 0 <= res < 1 and
         * cutoff*cutoff < 2(1 + res)/res. The cutoff bound moves with res, so
         * it cannot be written on the knob.
         *
         * The run-away reset below stays -- it catches a signal loud enough to
         * push the filter out on its own -- but it could not catch a NaN,
         * since every comparison with one is false. */
        val_res = thClampArg(buf_res[i], 0.0f, RMAX);

        {
            float cmax = (val_res > 0)
                ? sqrtf(CMARGIN * 2.0f * (1.0f + val_res) / val_res)
                : CCEIL;

            if (cmax > CCEIL)
                cmax = CCEIL;

            val_cutoff = thClampMag(buf_cut[i], cmax);
        }

        in = val_arg;
        diff = in - last;
        //printf("diff: %f \tperc: %f \tlast: %f \t%f\n\nres: %f \tcut: %f\n\n", diff, diff/TH_RANGE, last, accel, (*in_res)[i], (*in_cutoff)[i]);
        if(fabs(diff) > TH_RANGE) { /* damn unstable filters */
            diff = TH_RANGE * (diff > 0 ? 1 : -1);
        }
        diff *= 1-SQR((diff/(TH_RANGE+1))); /* My special blend of herbs and 
                                               spices */
        accel += diff*SQR(val_cutoff);
        accel *= val_res;
        
        /* was abs((int)accel): casting an already-diverged float to int is
           itself undefined, and abs(INT_MIN) has no result. Stay in float.

           The finiteness test is first: fabs(NaN) > TH_RANGE is false, so a
           NaN walked straight through this and out of the filter. */
        if(!thIsFinite(accel) || fabs(accel) > TH_RANGE) {
            accel = 0;
            last = 0;
        }

        last += accel;

        aout[i] = accel;

        out[i] = last;
    }

    out_last[0] = last;
    out_last[1] = accel;

    return 0;
}

