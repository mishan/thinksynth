#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thList.h"
#include "thPlugin.h"
#include "thNode.h"
#include "thMod.h"
#include "thMidiNote.h"
#include "thMidiChan.h"

thMidiChan::thMidiChan (thMod *mod)
{
	modnode = mod;
}

thMidiChan::~thMidiChan ()
{
}

void thMidiChan::AddNote (float note, float velocity)
{
}

void thMidiChan::DelNote (thMidiNote *midinote)
{
}

