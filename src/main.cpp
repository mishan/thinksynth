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

#include "thMidiNote.h"
#include "thMidiChan.h"

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
  ((thMod *)Synth.FindMod("static"))->BuildSynthTree();
  /* ((thMod *)Synth->FindMod("test"))->Process(); */
  

  ((thMod *)Synth.FindMod("static"))->BuildSynthTree();
  Synth.AddChannel("chan1", "static");
  Synth.AddNote("chan1", 20, 100);

  newmod = ((thMod *)Synth.FindMod("static"))->Copy();
  newmod->BuildSynthTree();
  newmod->Process(newmod, 1024);

  /*  printf("  = %f\n", ((thArgValue *)((thMod *)Synth.FindMod("static"))->GetArg("static", "out"))->argValues[0]); */
  printf("  = %f\n", ((thArgValue *)newmod->GetArg("ionode", "out"))->argValues[0]);
}
