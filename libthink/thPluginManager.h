/*
 * Copyright (C) 2004-2026 Metaphonic Labs
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

#include <map>
#include <mutex>
#include <string>

#include "thExport.h"

class thPlugin;

/* The dlopen'd plugins, by "category/name", and the one thing in libthink a
 * parse can reach that is shared between parses.
 *
 * That matters since thinklang went pure: thSynth::parseTree runs without
 * the synth mutex, on the reasoning that a scanner and a context per call
 * make two simultaneous parses independent. They are -- of each other. But
 * both of them resolve their nodes' plugins through here, and this map was
 * neither locked nor even const on lookup: getPlugin was `plugins_[name]',
 * and map::operator[] inserts a NULL entry on every miss. So the *reader*
 * mutated the map, and two parses of a file naming a plugin neither had
 * loaded raced on the insert. A found plugin worked, which is why this was
 * quiet: the corpus sweeps parse serially and everything is loaded by the
 * time anything is concurrent.
 *
 * So: one mutex over the map, find() rather than operator[], and loadPlugin
 * is idempotent. getOrLoadPlugin exists because the callers all wanted
 * "resolve this name" and spelled it as get-then-load, which is a
 * check-then-act with a window in it however well the map is locked -- two
 * threads could both miss, both load, and one dlopen handle would be
 * overwritten and leaked. One call, one lock, one answer.
 *
 * The lock is held across dlopen. It is slow and it is correct; the dynamic
 * loader serializes itself anyway, and a plugin is loaded once per process
 * per name.
 */
/* std::string and std::map spelled out, and the includes above are this
 * header's own.
 *
 * The rest of libthink's headers do neither: they name `string' bare and
 * are only compilable because think.h included <string> and said `using
 * namespace std;' before including them. That works exactly as long as
 * every consumer goes through think.h, and this is the header most likely
 * to have a consumer that does not -- anything out of tree that wants to
 * ask what plugins exist. A header that only compiles in one include
 * order is a trap laid for whoever finds it first.
 *
 * Fixed here rather than everywhere at once because the family cannot be
 * changed piecemeal *and* checked: scripts/includecheck compiles each
 * self-contained header on its own, and that list can only grow. This is
 * the direction, not an inconsistency for its own sake.
 */
class THINK_API thPluginManager {
public:
    thPluginManager(const std::string &path);
    ~thPluginManager();

    /* Loads `name' if it is not loaded already. 0 on success, 1 if it could
       not be found or would not load. */
    int loadPlugin(const std::string &name);

    void unloadPlugin(const std::string &name);

    /* The loaded plugin, or NULL. Does not load, and -- unlike the
       operator[] this used to be -- does not modify anything. */
    thPlugin *getPlugin (const std::string &name);

    /* Resolve a name: the plugin, loading it if this is the first ask, or
       NULL if it cannot be had. What every caller actually wanted. */
    thPlugin *getOrLoadPlugin (const std::string &name);

    /* Where plugins are actually being loaded from. Not necessarily what was
       passed to the constructor -- see resolveRoot. */
    const std::string &pluginPath (void) const { return plugin_path_; }

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
    static std::string resolveRoot (const std::string &preferred);

private:
    typedef std::map<std::string, thPlugin*> PluginMap;
    PluginMap plugins_;

    /* Set once by the constructor and never written again, so it is read
       without the lock. */
    std::string plugin_path_;

    /* Guards plugins_ and nothing else. */
    std::mutex mutex_;

    /* Both assume mutex_ is held. */
    thPlugin *findLocked (const std::string &name) const;
    int loadLocked (const std::string &name);

    void unloadPlugins (void);
    const std::string getPath (const std::string &name);
};

#endif /* TH_PLUGIN_MANAGER_H */
