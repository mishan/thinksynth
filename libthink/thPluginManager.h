/*
 * Copyright (C) 2004-2014 Metaphonic Labs
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

	int loadPlugin(const string &name);
	void unloadPlugin(const string &name);

	thPlugin *getPlugin (const string &name) { return plugins_[name]; };
private:
	typedef map<string, thPlugin*> PluginMap;
	PluginMap plugins_;
	string plugin_path_;

	void unloadPlugins (void);
	const string getPath (const string &name);
};

#endif /* TH_PLUGIN_MANAGER_H */
