/* $Id: adsr.cpp,v 1.1 2003/05/02 00:29:15 ink Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thArg.h"
#include "thList.h"
#include "thBSTree.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"
#include "thSynth.h"

char		*desc = "ADSR Envelope Generator";
thPluginState	mystate = thActive;

extern "C" int	module_init (thPlugin *plugin);
extern "C" int	module_callback (thNode *node, thMod *mod, unsigned int windowlen);
extern "C" void module_cleanup (struct module *mod);

void module_cleanup (struct module *mod)
{
	printf("ADSR module unloading\n");
}

int module_init (thPlugin *plugin)
{
	printf("ADSR plugin loaded\n");
	
	plugin->SetDesc (desc);
	plugin->SetState (mystate);
	
	return 0;
}

int module_callback (thNode *node, thMod *mod, unsigned int windowlen)
{
  thArgValue *in_a, *in_d, *in_s, *in_r;  /* User args */
  thArgValue *in_position;  /* [0] = position in stage, [1] = current stage */
  float *out = new float[windowlen];
  float out_pos[2];
  float temp, temp2;  /* each (*in_a)[] thing uses a modulus and all that */
  int position, phase;
  int i;

  in_a = (thArgValue *)mod->GetArg(node->GetName(), "a");
  in_d = (thArgValue *)mod->GetArg(node->GetName(), "d");
  in_s = (thArgValue *)mod->GetArg(node->GetName(), "s");
  in_r = (thArgValue *)mod->GetArg(node->GetName(), "r");

  in_position = (thArgValue *)mod->GetArg(node->GetName(), "position");
  position = (int)(*in_position)[0];
  phase = (int)(*in_position)[1];

  for(i=0;i<windowlen;i++) {
	switch (phase) {  /* Which phase of the ADSR are we in? */
	case 0:   /* Attack */
	  temp = (*in_a)[i];

	  play[i] = 1;  /* Dont kill this note yet! */

	  out[i] = ((position++)/temp)*TH_MAX;
	  if(position >= temp) {
		phase = 1;   /* A ended, go to D */
		position = 0;  /* ...and go back to the beginnning of the phase */
	  }
	  break;
	case 1:
	  temp = (*in_d)[i];
	  temp2 = (*in_s)[i];

	  play[i] = 1;

	  out[i] = ((temp-(position++))/temp)*(TH_MAX-temp2);
	  if(position >= temp) {
		phase = 2;   /* D ended, go to S */
		position = 0;
	  }
	  break;
	case 2:
	  temp = (*in_s)[i];

	  if(temp == 0) {  /* If there is no D section, make it end */
		play[i] = 0;
	  } else {
		play[i] = 1;
	  }

	  out[i] = temp;

	  if(trigger[i] == 0) {
		phase = 3;   /* D ended, time to fade out the note */
		/* position should already be 0 from the D section */
	  }
	}
	break;
  case 3:
	temp = (*in_r)[i];
	temp2 = (*in_s)[i];

	out = ((temp-(position++))/temp)*temp2;

	if(position >= temp) {
	  play[i] = 0;
	  phase = 4;
	}
	break;
  case 4:   /* The note has ended and we are padding with 0 */
	play[i] = 0;
	out[i] = 0;
	break;
  }

  out_pos[0] = position;
  out_pos[1] = phase;
  node->SetArg("position", out_pos, 2);
  node->SetArg("out", out, windowlen);

  return 0;
}

