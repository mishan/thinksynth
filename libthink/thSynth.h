/* $Id: thSynth.h,v 1.19 2003/04/27 04:27:08 joshk Exp $ */

#ifndef TH_SYNTH_H
#define TH_SYNTH_H

class thMidiNote;

class thSynth {
public:
	thSynth();
	~thSynth();

	void LoadMod(const char *name);
	thMod *FindMod(const char *modname);
	void ListMods(void);
	void BuildSynthTree(const char *modname);
	thPluginManager *GetPluginManager (void) const;
	void AddChannel(char *channame, char *modname);
	thMidiNote *AddNote(char *channame, float note, float velocity);
	void Process(const char *modname);

private:
	int BuildSynthTreeHelper(thMod *mod, thNode *parent, char *nodename);

	thBSTree modlist;
	thPluginManager pluginmanager;
	thList chanlist;
	thBSTree channels;

	unsigned int windowlen;
};

#endif /* TH_SYNTH_H */
