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

	args = new thList;
	notes = new thList;
}

thMidiChan::~thMidiChan ()
{
	delete args;
	delete notes;

	/* delete mod; ? */
}

void thMidiChan::AddNote (float note, float velocity)
{
	thMidiNote *midinote = new thMidiNote(note, velocity);

	notes->Add(midinote);
}

void thMidiChan::DelNote (thMidiNote *midinote)
{
	thNode *node;

	for(node = notes->GetHead(); node; node = node->prev) {
		if(node->data == midinote) {
			notes->Remove(node);
			delete midinote;

			return;
		}
	}
}
