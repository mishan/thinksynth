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

#ifdef HAVE_DLFCN_H
# include <dlfcn.h>
#else
# ifdef USING_DARWIN
#  include "nsmodule_dl.h"
# else
#  error Need a dl implementation!
# endif
#endif

#include <fcntl.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>     /* getenv */
#include <dirent.h>     /* the plugin-root search */

#include "think.h"

thPluginManager::thPluginManager (const string &path)
{
    plugin_path_ = resolveRoot(path);
}

thPluginManager::~thPluginManager ()
{
    unloadPlugins();
}

/* Does this directory hold plugins -- <root>/<category>/<something>.so? */
static bool hasPlugins (const string &root)
{
    DIR *top = opendir(root.c_str());

    if (top == NULL)
        return false;

    bool found = false;
    struct dirent *de;

    while (!found && (de = readdir(top)) != NULL)
    {
        const string cat = de->d_name;

        if (cat == "." || cat == "..")
            continue;

        const string sub = root + cat;

        struct stat st;

        if (stat(sub.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
            continue;

        DIR *d = opendir(sub.c_str());

        if (d == NULL)
            continue;

        struct dirent *pe;
        const string suffix = PLUGIN_SUFFIX;

        while ((pe = readdir(d)) != NULL)
        {
            const string f = pe->d_name;

            if (f.size() > suffix.size() &&
                f.compare(f.size() - suffix.size(), suffix.size(),
                          suffix) == 0)
            { found = true; break; }
        }

        closedir(d);
    }

    closedir(top);

    return found;
}

/* The directory the running executable is in, with a trailing slash, or "". */
static string exeDir (void)
{
    char buf[4096];

    /* Linux only; everywhere else this candidate is simply skipped, which
       costs nothing because the cwd-relative one usually covers it. */
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);

    if (n <= 0)
        return "";

    buf[n] = 0;

    string path = buf;

    const string::size_type slash = path.rfind('/');

    return (slash == string::npos) ? "" : path.substr(0, slash + 1);
}

string thPluginManager::resolveRoot (const string &preferred)
{
    vector<string> tries;

    const char *env = getenv("THINK_PLUGIN_PATH");

    if (env && *env)
        tries.push_back(env);

    tries.push_back(preferred);
    tries.push_back("plugins");

    const string exe = exeDir();

    if (!exe.empty())
    {
        /* src/thinksynth run from anywhere: ../plugins from the binary. */
        tries.push_back(exe + "../plugins");
        tries.push_back(exe + "plugins");
    }

    for (size_t i = 0; i < tries.size(); i++)
    {
        string root = tries[i];

        if (root.empty())
            continue;

        if (root[root.size() - 1] != '/')
            root += '/';

        if (hasPlugins(root))
            return root;
    }

    string fallback = preferred;

    if (!fallback.empty() && fallback[fallback.size() - 1] != '/')
        fallback += '/';

    return fallback;
}

const string thPluginManager::getPath (const string &name)
{
    string path;
    struct stat dummy;

    /* Use the default path first */
    path = plugin_path_ + name + PLUGIN_SUFFIX;

    /* Check for existence in the expected place */
    if (stat (path.c_str(), &dummy) == -1) { /* File existeth not */
#ifdef USE_DEBUG
        fprintf (stderr, "thPluginManager: %s: %s\n", path.c_str(), strerror(errno));
#endif
        path = "plugins/" + name + PLUGIN_SUFFIX;
        if (stat(path.c_str(), &dummy) == -1) {
#ifdef USE_DEBUG
            fprintf(stderr, "thPluginManager: %s: %s\n", path.c_str(), strerror(errno));
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
