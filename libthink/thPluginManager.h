/* $Id: thPluginManager.h,v 1.12 2004/08/16 09:34:48 misha Exp $ */
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

#ifndef TH_PLUGIN_MANAGER_H
#define TH_PLUGIN_MANAGER_H 1

class thPluginManager {
public:
	thPluginManager(const string &path);
	~thPluginManager();

	int LoadPlugin(const string &name);
	void UnloadPlugin(const string &name);

	thPlugin *GetPlugin (const string &name) { return plugins[name]; };
private:
	map <string, thPlugin*> plugins;

	void UnloadPlugins (void);

	const string GetPath (const string &name);

	string plugin_path;
};

#endif /* TH_PLUGIN_MANAGER_H */
