/* $Id: gthPrefs.h,v 1.4 2004/08/16 09:34:48 misha Exp $ */
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

#ifndef GTH_PREFS_H
#define GTH_PREFS_H

#define PREFS_FILE ".thinkrc"

class gthPrefs
{
public:
	gthPrefs (thSynth *argsynth);
	gthPrefs (thSynth *argsynth, const string &path);
	~gthPrefs (void);

	void Set (const string &key, string **vals);
	string **Get (const string &key);

	void Load (void);
	void Save (void);
private:
	thSynth *synth;
	map <string, string**> prefs;
	string prefsPath;
};

#endif /* GTH_PREFS_H */
