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
#include <stdlib.h>
#include <string.h>

#include "think.h"

thNode::thNode (const string &name, thPlugin *thplug)
{
    plugin_ = thplug;
    nodeName_ = name;
    recalc_ = false;

    argindex_ = (thArg **)calloc(ARGCHUNK, sizeof(thArg *));
    argCount_ = 0;
    argsize_ = ARGCHUNK;
}

thNode::thNode (const thNode &copyNode)
{
    recalc_ = false;
    argsize_ = ARGCHUNK;
    argindex_ = (thArg **)calloc(ARGCHUNK, sizeof(thArg *));

    nodeName_ = copyNode.name();
    plugin_ = copyNode.plugin();
    id_ = copyNode.id();
    argCount_ = copyNode.argCount();

    copyArgs(copyNode.args());
}

thNode::~thNode (void)
{
    DestroyMap(args_);
}

thArg *thNode::setArg (const string &name, float value)
{
    thArgMap::const_iterator i = args_.find(name);
    thArg *arg;

    if (i == args_.end() || !i->second /* XXX shouldnt be necessary */) {
        arg = new thArg(name, value);
        args_[name] = arg;
        arg->setIndex(addArgToIndex(arg));
    }
    else {
        arg = i->second;
        arg->setArg(name, value);
    }

    return arg;
}

thArg *thNode::setArg (const string &name, const float *value, int len)
{
    thArgMap::const_iterator i = args_.find(name);
    thArg *arg;

    if (i == args_.end() || !i->second /* XXX shouldnt be necessary */) {
        arg = new thArg(name, value, len);
        args_[name] = arg;
        arg->setIndex(addArgToIndex(arg));
    }
    else {
        arg = i->second;
        arg->setArg(name, value, len);
    }

    return arg;
}

thArg *thNode::setArg (const string &name, const string &node,
                       const string &value)
{
    thArgMap::const_iterator i = args_.find(name);
    thArg *arg;

    if (i != args_.end() && i->second/* XXX we should not have to do this */) {
        arg = i->second;
        arg->setArg(name, node, value);
    }
    else {
        arg = new thArg(name, node, value);
        args_[name] = arg;
        arg->setIndex(addArgToIndex(arg));
    }

    return arg;
}

thArg *thNode::setArg (const string &name, const string &chanarg)
{
    thArgMap::const_iterator i = args_.find(name);
    thArg *arg;

    if (i != args_.end() && i->second/* XXX we should not have to do this */) {
        arg = i->second;
        arg->setArg(name, chanarg);
    }
    else {
        arg = new thArg(name, chanarg);
        args_[name] = arg;
        arg->setIndex(addArgToIndex(arg));
    }

    return arg;
}

int thNode::addArgToIndex (thArg *arg)
{
    thArg **newindex;

    if (argCount_ >= argsize_)
    {
        newindex = (thArg **)calloc(argsize_ + ARGCHUNK, sizeof(thArg *));

        memcpy(newindex, argindex_, argCount_ * sizeof(thArg*));
        free(argindex_);
        argindex_ = newindex;
        argsize_ += ARGCHUNK;
    }

    argindex_[argCount_++] = arg;

    return (argCount_ - 1);
}

void thNode::printArgs (void)
{
    for (thArgMap::const_iterator i = args_.begin(); i != args_.end(); i++)
        printf("%s\n", i->first.c_str());
}

void thNode::copyArgs (const thArgMap &newargs)
{
    thArg *newarg;
    thArg *data;
    thArg **newindex;

    for (thArgMap::const_iterator i = newargs.begin(); i != newargs.end(); i++)
    {
        data = i->second;

        if (data->type() == thArg::ARG_NOTE)
            continue;

        newarg = new thArg(data);

        args_[data->name()] = newarg;

        newarg->setIndex(data->index());
        while (newarg->index() > argsize_)
        {
            newindex = (thArg **)calloc(argsize_ + ARGCHUNK, sizeof(thArg *));

            memcpy(newindex, argindex_, argsize_ * sizeof(thArg*));
            free(argindex_);
            argindex_ = newindex;
            argsize_ += ARGCHUNK;
        }
        argindex_[newarg->index()] = newarg;
    }
}

void thNode::process (void)
{
}
