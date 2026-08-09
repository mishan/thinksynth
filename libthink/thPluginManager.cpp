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

#include "config.h"

#include <stdio.h>
#include <string.h>   /* strerror */
#include <unistd.h>

/* macOS has had a real dlopen(3) since 10.3; the NSModule shim that used
   to sit behind USING_DARWIN here has been dead code for twenty years. */
#ifdef HAVE_DLFCN_H
# include <dlfcn.h>
#else
# error Need a dl implementation!
#endif

#include <errno.h>

#include <stdlib.h>     /* getenv */

#include <filesystem>   /* the plugin-root search, and every path join */
#include <system_error>

#include "think.h"

namespace fs = std::filesystem;

thPluginManager::thPluginManager (const string &path)
{
    plugin_path_ = resolveRoot(path);
}

thPluginManager::~thPluginManager ()
{
    unloadPlugins();
}

/* Does this directory hold plugins -- <root>/<category>/<something>.so?
 *
 * Was a nest of opendir/readdir/stat. directory_iterator does the same walk
 * without the manual "." and ".." skipping, without a second stat() per
 * entry, and without dirent.h, which Windows does not have.
 *
 * The non-throwing overloads are used deliberately: a plugin root that does
 * not exist is the normal case here -- resolveRoot() calls this on every
 * candidate in turn -- and it must read as "no" rather than as an exception.
 */
static bool hasPlugins (const fs::path &root)
{
    std::error_code ec;

    if (!fs::is_directory(root, ec))
        return false;

    for (const auto &cat : fs::directory_iterator(root, ec))
    {
        if (ec)
            return false;

        if (!cat.is_directory(ec))
            continue;

        for (const auto &f : fs::directory_iterator(cat.path(), ec))
        {
            if (ec)
                break;

            if (f.path().extension() == PLUGIN_SUFFIX)
                return true;
        }
    }

    return false;
}

/* getPath() builds "<root><category>/<name><suffix>" by plain concatenation,
   so the contract is that a root always ends in a separator. */
static string withTrailingSlash (const string &path)
{
    if (path.empty() || path[path.size() - 1] == '/')
        return path;

    return path + '/';
}

string thPluginManager::resolveRoot (const string &preferred)
{
    vector<fs::path> tries;

    const char *env = getenv("THINK_PLUGIN_PATH");

    if (env && *env)
        tries.push_back(env);

    tries.push_back(preferred);
    tries.push_back("plugins");

    const fs::path exe = thUtil::exeDir();

    if (!exe.empty())
    {
        /* Build tree: src/thinksynth run from anywhere. */
        tries.push_back(exe.parent_path() / "plugins");

        /* Unix install: <prefix>/bin/thinksynth against
           <prefix>/lib/thinksynth/plugins. This is what makes an installed
           tree relocatable rather than tied to the ${libdir} that was
           compiled in. */
        tries.push_back(exe.parent_path() / "lib" / PACKAGE_NAME / "plugins");

        /* macOS bundle: Contents/MacOS/thinksynth against
           Contents/Resources/plugins. */
        tries.push_back(exe.parent_path() / "Resources" / "plugins");

        /* Windows install, and the build tree on any platform. */
        tries.push_back(exe / "plugins");
    }

    for (size_t i = 0; i < tries.size(); i++)
    {
        if (tries[i].empty())
            continue;

        if (hasPlugins(tries[i]))
            return withTrailingSlash(tries[i].string());
    }

    return withTrailingSlash(preferred);
}

const string thPluginManager::getPath (const string &name)
{
    std::error_code ec;

    /* Use the default path first */
    string path = plugin_path_ + name + PLUGIN_SUFFIX;

    /* Check for existence in the expected place */
    if (!fs::exists(path, ec)) { /* File existeth not */
#ifdef USE_DEBUG
        fprintf (stderr, "thPluginManager: %s: not found\n", path.c_str());
#endif
        path = "plugins/" + name + PLUGIN_SUFFIX;
        if (!fs::exists(path, ec)) {
#ifdef USE_DEBUG
            fprintf(stderr, "thPluginManager: %s: not found\n", path.c_str());
#endif
            return ""; /* Empty string */
        }
    }

    return path;
}

int thPluginManager::loadPlugin (const string &name)
{
    thPlugin *plugin;
    const string path = getPath(name);

    if (path.empty()) { /* Not found at all */
        fprintf (stderr, "Could not find the plugin anywhere!\n");
        return 1;
    }

    plugin = new thPlugin (path);

    if (plugin->state() == thPlugin::NOTLOADED) { /* something messed up */
        delete plugin;
        return 1;
    }
    
    plugins_[name] = plugin;

    return 0;
}


void thPluginManager::unloadPlugin(const string &name)
{
    PluginMap::iterator i = plugins_.find(name);

    if (i == plugins_.end()) {
        fprintf(stderr, "thPluginManager::UnloadPlugin: No such plugin '%s'\n",
                name.c_str());
        return;
    }

    thPlugin *plugin = i->second;

    plugins_.erase(i);

    delete plugin;
}

void thPluginManager::unloadPlugins (void)
{
    DestroyMap(plugins_);
}
