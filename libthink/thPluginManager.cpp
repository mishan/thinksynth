/* $Id: thPluginManager.cpp,v 1.40 2003/06/03 04:09:29 aaronl Exp $ */

#include "think.h"
#include "config.h"

#include <stdio.h>
#include <unistd.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "thPlugin.h"
#include "thPluginManager.h"

thPluginManager::thPluginManager ()
{
}

thPluginManager::~thPluginManager ()
{
	UnloadPlugins();
}

const string thPluginManager::GetPath (const string &name)
{
	string path;
	struct stat dummy;

	/* Use the default path first */
	path = plugin_path + name + SHARED_SUFFIX;

	/* Check for existence in the expected place */
	if (stat (path.c_str(), &dummy) == -1) { /* File existeth not */
#ifdef USE_DEBUG
		fprintf (stderr, "thPluginManager: %s: %s\n", path.c_str(), strerror(errno));
#endif
		path = "plugins/" + name + SHARED_SUFFIX;
		if(stat(path.c_str(), &dummy) == -1) {
#ifdef USE_DEBUG
			fprintf(stderr, "thPluginManager: %s: %s\n", path.c_str(), strerror(errno));
#endif
			return string(); // XXX
		}
	}

	return path;
}

int thPluginManager::LoadPlugin (const string &name)
{
	thPlugin *plugin;
	const string path = GetPath(name);

	if (path.empty()) { /* Not found at all */
		fprintf (stderr, "Could not find the plugin anywhere!\n");
		return 1;
	}

	plugin = new thPlugin (path);

	if (plugin->GetState() == thNotLoaded) {	/* something messed up */
		delete plugin;
		return 1;
	}
	
	plugins[name] = plugin;

	return 0;
}


void thPluginManager::UnloadPlugin(const string &name)
{
	map<string, thPlugin*>::iterator i = plugins.find(name);

	if(i == plugins.end()) {
		fprintf(stderr, "thPluginManager::UnloadPlugin: No such plugin '%s'\n", name.c_str());
		return;
	}

	thPlugin *plugin = i->second;
	plugins.erase(i);
	delete plugin;
}

void thPluginManager::UnloadPlugins (void)
{
	thPlugin::DestroyMap(plugins);
}
