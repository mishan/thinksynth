/* $Id: gthPatchfile.h,v 1.5 2004/11/16 23:22:02 misha Exp $ */
/*
 * Copyright (C) 2004 Metaphonic Labs
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

#ifndef GTH_PATCHFILE_H
#define GTH_PATCHFILE_H

#define NUM_PATCHES 16

typedef SigC::Signal0<void> type_signal_patches_changed;

class thArg;
class thMidiChan;

class gthPatchManager
{
public:
	gthPatchManager (int numPatches=NUM_PATCHES);
	~gthPatchManager (void);

	static gthPatchManager *instance (void);

	bool newPatch (const string &dspName, int chan);
	bool loadPatch (const string &filename, int chan);
	bool savePatch (const string &filename, int chan);
	bool unloadPatch (int chan);
	bool isLoaded (int chan);

	int numPatches (void) {
		return numPatches_;
	}

	thMidiChan::ArgMap getChannelArgs (int chan);

	typedef map<string, float> PatchFileArgs;
	struct PatchFile {
		PatchFileArgs args;
		string dspFile;
		string filename;
	};

	PatchFile *getPatch (int chan)
	{
		if ((chan < 0) || (chan >= NUM_PATCHES))
			return NULL;

		return patches_[chan];
	}

	type_signal_patches_changed signal_patches_changed (void) {
		return m_signal_patches_changed;
	}
private:
	bool parse (const string &filename, int chan);

	int numPatches_;
	PatchFile **patches_;
	static gthPatchManager *instance_;
	type_signal_patches_changed m_signal_patches_changed;
};

#endif /* GTH_PATCHFILE_H */
