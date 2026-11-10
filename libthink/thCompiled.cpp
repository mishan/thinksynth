/*
 * Copyright (C) 2004-2026 The thinksynth authors
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

#include <map>
#include <mutex>

#include "think.h"
#include "thCompiled.h"

static std::mutex registryLock;

static std::map<std::string, thCompiledMake> &
units (void)
{
    static std::map<std::string, thCompiledMake> u;

    return u;
}

void thCompiledAdd (const std::string &fingerprint, thCompiledMake make)
{
    std::lock_guard<std::mutex> hold(registryLock);

    units()[fingerprint] = make;
}

thCompiled *thCompiledFor (const std::string &fingerprint)
{
    std::lock_guard<std::mutex> hold(registryLock);
    const auto u = units().find(fingerprint);

    return u == units().end() ? NULL : u->second();
}

bool thCompiledWrites (thSynthTree &tree, const std::string &name)
{
    thNode *node = tree.IONode();
    thArg *a = node ? node->getArg(name) : NULL;

    while (a != NULL && a->type() == thArg::ARG_POINTER)
    {
        node = tree.nodeAt(a->nodePtrId());
        a = node ? node->getArg(a->argPtrId()) : NULL;
    }
    return a != NULL && a->type() == thArg::ARG_VALUE && node != tree.IONode() &&
           node->plugin() != NULL &&
           node->plugin()->getArgDir(a->index()) == thPlugin::ARG_OUT;
}
