/* $Id: thSynth.cpp,v 1.33 2003/04/27 04:27:08 joshk Exp $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "thArg.h"
#include "thList.h"
#include "thBSTree.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"
#include "thMidiNote.h"
#include "thMidiChan.h"
#include "thSynth.h"

#include "parser.h"
/*
thNode *parsenode;
thMod *parsemod;
*/

thSynth::thSynth()
{
	windowlen = 1024;
}

thSynth::~thSynth()
{
}

void thSynth::LoadMod(const char *filename)
{
	if ((yyin = fopen(filename, "r")) == NULL) { /* 404 or smth */
		fprintf (stderr, "couldn't open %s: %s\n", filename, strerror(errno));
		exit(1);
	}

	parsemod = new thMod("newmod");     /* these are used by the parser */
	parsenode = new thNode("newnode", NULL);
	
	yyparse();
	
	delete parsenode;
	
	modlist.Insert(parsemod->GetName(), parsemod);
}

thMod *thSynth::FindMod(const char *modname)
{
	return (thMod *)modlist.GetData(modname);
}

/* Make these voids return something and add error checking everywhere! */

void thSynth::ListMods(void)
{
	modlist.PrintTree();
}

const thPluginManager *thSynth::GetPluginManager(void) const
{
	return &pluginmanager;
}

void thSynth::AddChannel(char *channame, char *modname)
{
	thMidiChan *newchan = new thMidiChan(FindMod(modname));
	channels.Insert(channame, newchan);
}

thMidiNote *thSynth::AddNote(char *channame, float note, float velocity)
{
	thMidiNote *newnote = (thMidiNote *)((thMidiChan *)channels.GetData(channame))->AddNote(note, velocity);
	return newnote;
}

void thSynth::Process(const char *modname)
{
	thMod *mod = FindMod(modname);
	mod->Process(mod, windowlen);
}
