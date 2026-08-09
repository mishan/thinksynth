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

    /* Where plugins are actually being loaded from. Not necessarily what was
       passed to the constructor -- see resolveRoot. */
    const string &pluginPath (void) const { return plugin_path_; }

    /* Picks a plugin directory that exists and has plugins in it.
     *
     * An uninstalled build has them in ./plugins, an installed one in
     * $(libdir)/thinksynth, and someone running src/thinksynth from a
     * different directory has them relative to the binary. Trying each in
     * turn is the difference between the editor working from a fresh
     * checkout and not.
     *
     * In order: $THINK_PLUGIN_PATH, `preferred', ./plugins, and the two
     * places relative to the running executable. `preferred' is returned
     * unchanged if none of them has anything, so an error still names the
     * place the build expected. */
    static string resolveRoot (const string &preferred);

private:
    typedef map<string, thPlugin*> PluginMap;
    PluginMap plugins_;
    string plugin_path_;

    void unloadPlugins (void);
    const string getPath (const string &name);
};

#endif /* TH_PLUGIN_MANAGER_H */
