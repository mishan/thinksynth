/* $Id: thSynth.cpp,v 1.41 2003/04/28 22:32:48 ink Exp $ */

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

	modlist = new thBSTree(StringCompare);
	channels = new thBSTree(StringCompare);
}

thSynth::~thSynth()
{
	delete modlist;
	delete channels;
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
	
	modlist->Insert((void *)strdup(parsemod->GetName()), (void *)parsemod);
}

thMod *thSynth::FindMod(const char *modname)
{
	return (thMod *)modlist->GetData((void *)modname);
}

/* Make these voids return something and add error checking everywhere! */

void thSynth::ListMods(void)
{
	ListMods(modlist);
}

void thSynth::ListMods(thBSTree *node)
{
	if(!node) {
		return;
	}

	ListMods(node->GetLeft());
	printf("%s\n", (char *)node->GetId());
	ListMods(node->GetRight());
}

thPluginManager *thSynth::GetPluginManager(void)
{
	return &pluginmanager;
}

void thSynth::AddChannel(char *channame, char *modname, float amp)
{
	thMidiChan *newchan = new thMidiChan(FindMod(modname), amp, windowlen);
	channels->Insert((void *)channame, (void *)newchan);
}

thMidiNote *thSynth::AddNote(char *channame, float note, float velocity)
{
	thMidiNote *newnote = (thMidiNote *)((thMidiChan *)channels->GetData((void *)channame))->AddNote(note, velocity);
	return newnote;
}

void thSynth::Process(const char *modname)
{
	thMod *mod = FindMod(modname);
	mod->Process(windowlen);
}
