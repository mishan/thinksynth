#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thList.h"
#include "thBSTree.h"
#include "thArg.h"
#include "thPlugin.h"
#include "thNode.h"
#include "thMod.h"
#include "thMidiNote.h"
#include "thArg.h"
#include "thMidiChan.h"

thMidiChan::thMidiChan (thMod *mod)
{
	modnode = mod;
}

thMidiChan::~thMidiChan ()
{
	/* delete mod; ? */
}

thMidiNote *thMidiChan::AddNote (float note, float velocity)
{
	thMidiNote *midinote = new thMidiNote(modnode, note, velocity);

	notes.Add(midinote);
	return midinote;
}

void thMidiChan::DelNote (thMidiNote *midinote)
{
	thListNode *node;

	for(node = notes.GetHead(); node; node = node->prev) {
		if(node->data == midinote) {
			notes.Remove(node);
			delete midinote;

			return;
		}
	}
}
