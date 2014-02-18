/*
 * Copyright (C) 2004-2014 Metaphonic Labs
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
#include <time.h>
#include <math.h>

#include "think.h"

#include "thArg.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thSynthTree.h"
#include "thSynth.h"

static inline float SQR (float x)
{
    return x*x;
}

enum {IN_FREQ, IN_AMP, IN_PW, IN_WAVEFORM, IN_FM, IN_FMAMT, IN_RESET, IN_MUL,
    OUT_ARG, OUT_SYNC, INOUT_LAST};

int args[INOUT_LAST + 1];

char        *desc = "Complex Oscillator";
thPlugin::State    mystate = thPlugin::ACTIVE;

const float SQRT2_2 = 2*sqrt(2);

void module_cleanup (struct module *mod)
{
}

/* ModuleLoad() invokes this function with a pointer to the plugin
 * instance. */
int module_init (thPlugin *plugin)
{
    plugin->setDesc (desc);
    plugin->setState (mystate);

    args[IN_FREQ] = plugin->regArg("freq");
    args[IN_AMP] = plugin->regArg("amp");
    args[IN_PW] = plugin->regArg("pw");
    args[IN_WAVEFORM] = plugin->regArg("waveform");
    args[IN_FM] = plugin->regArg("fm");
    args[IN_FMAMT] = plugin->regArg("fmamt");
    args[IN_RESET] = plugin->regArg("reset");
    args[IN_MUL] = plugin->regArg("mul");

    args[OUT_ARG] = plugin->regArg("out");
    args[OUT_SYNC] = plugin->regArg("sync");

    args[INOUT_LAST] = plugin->regArg("last");

    return 0;
}

int module_callback (thNode *node, thSynthTree *mod, unsigned int windowlen,
                     unsigned int samples)
{
    int i;
    float *out;
    float *out_last, *sync;
    float halfwave, ratio;
    float position, fmpos;
    double wavelength, freq;
    float amp_max, amp_min, amp_range;
    float mul;
    float pw; /* Make pw cooler! */
//    float fmamt;
    thArg *in_freq, *in_amp, *in_pw, *in_waveform, *in_fm, *in_fmamt, *in_reset, *in_mul;
    thArg *out_arg, *out_sync;
    thArg *inout_last;
    float buf_freq[windowlen], buf_amp[windowlen], buf_pw[windowlen], buf_waveform[windowlen], buf_fm[windowlen], buf_fmamt[windowlen], buf_reset[windowlen], buf_mul[windowlen];

    out_arg = mod->getArg(node, args[OUT_ARG]);
    out_sync = mod->getArg(node, args[OUT_SYNC]); /* Output a 1 when the wave begins 
                                             its cycle */
    inout_last = mod->getArg(node, args[INOUT_LAST]);
    position = (*inout_last)[0];
    out_last = inout_last->allocate(1);
    sync = out_sync->allocate(windowlen);

    out = out_arg->allocate(windowlen);

    in_freq = mod->getArg(node, args[IN_FREQ]);
    in_amp = mod->getArg(node, args[IN_AMP]);
    in_pw = mod->getArg(node, args[IN_PW]);
    in_waveform = mod->getArg(node, args[IN_WAVEFORM]);
    in_fm = mod->getArg(node, args[IN_FM]); /* FM Input */
    in_fmamt = mod->getArg(node, args[IN_FMAMT]); /* Modulation amount */
    in_reset = mod->getArg(node, args[IN_RESET]); /* Reset position to 0 when this 
                                              goes to 1 */
    in_mul = mod->getArg(node, args[IN_MUL]);  /* Multiply the wavelength by this */

    in_freq->getBuffer(buf_freq, windowlen);
    in_amp->getBuffer(buf_amp, windowlen);
    in_pw->getBuffer(buf_pw, windowlen);
    in_waveform->getBuffer(buf_waveform, windowlen);
    in_fm->getBuffer(buf_fm, windowlen);
    in_fmamt->getBuffer(buf_fmamt, windowlen);
    in_reset->getBuffer(buf_reset, windowlen);
    in_mul->getBuffer(buf_mul, windowlen);

    for(i=0; i < (int)windowlen; i++) {
        //wavelength = TH_SAMPLE/(*in_freq)[i];
        freq = buf_freq[i];

        mul = buf_mul[i];  /* optional frequency multiplier */
        if(mul) {
            freq *= mul;
        }

        amp_max = buf_amp[i];
        if(amp_max == 0) {
          amp_max = TH_MAX;
        }
        amp_min = -amp_max;
        amp_range = amp_max-amp_min;

        wavelength = samples/freq;

        position += 1 + ((buf_fm[i] / TH_MAX) * buf_fmamt[i]);
        //printf("%f\n", position);
        /* worked FM into the position counter */

        if(buf_reset[i] == 1) {
            position = 0;
            sync[i] = 1;
        }

        if(position > wavelength) {
            position -= wavelength;
            sync[i] = 1;
        } else if (position < 0) {
            position += wavelength;
            sync[i] = 1;
        } else {
            sync[i] = 0;
        }

//    fmamt = (*in_fmamt)[i]; /* If FM is being used, apply it! */
//    fmpos = (int)(position + (((*in_fm)[i] / TH_MAX) * wavelength * fmamt)) % (int)wavelength;
        fmpos = position;

        pw = buf_pw[i];  /* Pulse Width */
        if(pw == 0) {
            pw = 0.5;
        }

        halfwave = wavelength * pw;
        if(fmpos < halfwave) {
            ratio = fmpos/(2*halfwave);
        } else {
            ratio = 0.5*((fmpos - halfwave) / (wavelength - halfwave)) + 0.5;
        }

        switch((int)buf_waveform[i]) {
            /* 0 = sine, 1 = sawtooth, 2 = square, 3 = tri, 4 = half-circle,
               5 = parabola */
            case 0:    /* SINE WAVE */
                out[i] = amp_max*sin(ratio*2*M_PI); /* This will fuck up if 
                                                       TH_MIX is not the 
                                                       negative of TH_MIN */
                break;
            case 1:    /* SAWTOOTH WAVE */
                out[i] = amp_range*ratio+amp_min;
                break;
            case 2:    /* SQUARE WAVE */
            {
                // out[i] = amp_max if ratio > 0.5
                // else out[i] = -amp_max
                float adjusted = ratio - 0.5;
                u_int32_t level = (*((u_int32_t *)&amp_max)) |
                                  ((*((u_int32_t *)&adjusted))&0x80000000);
                out[i] = *((float*)&level);
                break;
            }
            case 3:    /* TRIANGLE WAVE */
                ratio *= 2;
                if(ratio < 1) {
                    out[i] = amp_range*ratio+amp_min;
                } else {
                    out[i] = (-amp_range)*(ratio-1)+amp_max;
                }
                break;
            case 4:    /* HALF-CIRCLE WAVE */
                if(ratio < 0.5) {
                    out[i] = SQRT2_2*sqrt((wavelength-(2*position))*position/
                                          SQR(wavelength))*amp_max;
                } else {
                    out[i] = SQRT2_2*sqrt((wavelength-(2*(position-halfwave)))
                                          *(position-halfwave)/SQR(wavelength))
                        *amp_min;
                }
                break;
            case 5:    /* PARABOLA WAVE */
                if(ratio < 0.5) {
                    out[i] = amp_max*(1-SQR(ratio*4-1));
                } else {
                    out[i] = amp_max*(SQR((ratio-0.5)*4-1)-1);
                }
        }
    }
    
    out_last[0] = position;
/*    node->SetArg("out", out, windowlen);
    last[0] = position;
    node->SetArg("last", (float*)last, 1);
*/    
    return 0;
}
