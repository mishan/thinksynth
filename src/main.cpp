#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "think.h"

#include "thArg.h"
#include "thList.h"
#include "thBSTree.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"

#include "thException.h"
#include "thAudio.h"
#include "thAudioBuffer.h"
#include "thWav.h"
#include "thOSSAudio.h"

#include "thSynth.h"

#include "parser.h"

int main (int argc, char *argv[])
{
  thMod *newmod;

  if(argc != 2) {
    printf("Usage: %s <filename>\n", argv[0]);
    exit(1);
  }
  
  Synth.LoadMod(argv[1]);
  Synth.ListMods();
  //((thMod *)Synth->FindMod("test"))->PrintIONode();
  //printf("  = %f\n", *((thArgValue *)((thMod *)Synth->FindMod("test"))->GetArg("test1", "point"))->argValues);
  Synth.BuildSynthTree("static");
  //	((thMod *)Synth->FindMod("test"))->Process();
  
  Synth.Process("static");

  newmod = ((thMod *)Synth.FindMod("static"))->Copy();
  newmod->Process(newmod, 1024);

  //  printf("  = %f\n", ((thArgValue *)((thMod *)Synth.FindMod("static"))->GetArg("static", "out"))->argValues[0]);
  printf("  = %f\n", ((thArgValue *)newmod->GetArg("static", "out"))->argValues[0]);
}
