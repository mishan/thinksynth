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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

thNode::thNode (const string &name, thPlugin *thplug)
{
    plugin_ = thplug;
    nodeName_ = name;
    recalc_ = false;
    id_ = -1;  /* so we know newNode() has not assigned one yet */

    /* Grown on demand by growArgIndex(). A node that registers no args costs
       no allocation, and there is one place that can fail rather than four. */
    argindex_ = NULL;
    argCount_ = 0;
    argsize_ = 0;
    smoothLive_ = 0;

    initSmoothing();
}

thNode::thNode (const thNode &copyNode)
{
    recalc_ = false;
    argindex_ = NULL;
    argsize_ = 0;

    nodeName_ = copyNode.name();
    plugin_ = copyNode.plugin();
    id_ = copyNode.id();
    argCount_ = copyNode.argCount();
    smoothLive_ = 0;

    copyArgs(copyNode.args());
    initSmoothing();
}

thNode::~thNode (void)
{
    DestroyMap(args_);

    for (size_t k = 0; k < smooth_.size(); k++)
        delete smooth_[k].ramp;

    free(argindex_);
    argindex_ = NULL;
    argsize_ = 0;
    argCount_ = 0;
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

thArg *thNode::setTextArg (const string &name, const string &text)
{
    thArgMap::const_iterator i = args_.find(name);
    thArg *arg;

    if (i != args_.end() && i->second) {
        arg = i->second;
        arg->setText(text);
    }
    else {
        arg = new thArg(name, 0.0f);
        arg->setText(text);
        args_[name] = arg;
        arg->setIndex(addArgToIndex(arg));
    }

    return arg;
}

/* Makes `slots' a valid subscript count for argindex_.
 *
 * This was written out by hand in four places, and every one of them assigned
 * calloc()'s result and freed the old array before checking it. On a failed
 * allocation that freed the live index, stored NULL over it, and left the
 * caller to subscript NULL on the next line.
 *
 * ARGCHUNK at a time rather than doubling, because these stay small -- the
 * widest node in the corpus registers a couple of dozen args. */
bool thNode::growArgIndex (int slots)
{
    if (slots <= argsize_)
        return true;

    int newsize = argsize_;

    while (newsize < slots)
        newsize += ARGCHUNK;

    thArg **newindex = (thArg **)calloc(newsize, sizeof(thArg *));

    if (newindex == NULL)
        return false;

    /* argsize_, not argCount_: copyArgs() writes slots by index and can leave
       occupied ones above argCount_, so the whole old array travels. */
    if (argindex_ != NULL)
    {
        memcpy(newindex, argindex_, argsize_ * sizeof(thArg *));
        free(argindex_);
    }

    argindex_ = newindex;
    argsize_ = newsize;

    return true;
}

int thNode::addArgToIndex (thArg *arg)
{
    /* -1 is already what "this arg has no slot in the index" is spelled as --
       getArg(int) and copyArgs() both check for it -- so a failed growth has
       somewhere honest to go. */
    if (!growArgIndex(argCount_ + 1))
        return -1;

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

    for (thArgMap::const_iterator i = newargs.begin(); i != newargs.end(); i++)
    {
        data = i->second;

        /* thArgMap is often populated via operator[], which inserts NULLs for
           lookups that missed. Skip those rather than dereferencing them. */
        if (data == NULL)
            continue;

        if (data->type() == thArg::ARG_NOTE)
            continue;

        newarg = new thArg(data);

        args_[data->name()] = newarg;

        newarg->setIndex(data->index());

        /* An arg that was never indexed has index -1; it has no slot in the
           index array and writing it would clobber memory before argindex_. */
        if (newarg->index() < 0)
            continue;

        /* Valid slots are 0 .. argsize_-1, so index+1 has to fit. */
        if (!growArgIndex(newarg->index() + 1))
        {
            /* Out of memory. The arg is in args_ and will be freed with the
               node; it simply has no index, which is a state the rest of this
               file already knows how to read. */
            newarg->setIndex(-1);
            continue;
        }

        argindex_[newarg->index()] = newarg;

        if (newarg->index() >= argCount_)
            argCount_ = newarg->index() + 1;
    }
}

/* See the header. The copy has every arg copyArgs() gave it -- the
   prototype's, less the NULLs and ARG_NOTEs -- so the two maps, both in
   name order, are walked side by side. */
bool thNode::restore (const thNode &proto)
{
    thArgMap::const_iterator mine = args_.begin();

    for (size_t k = 0; k < smooth_.size(); k++)
        smooth_[k].primed = false;

    if (argCount_ != proto.argCount_)
        return false;

    for (thArgMap::const_iterator i = proto.args_.begin();
         i != proto.args_.end(); i++)
    {
        const thArg *from = i->second;

        if (from == NULL || from->type() == thArg::ARG_NOTE)
            continue;

        if (mine == args_.end() || mine->first != i->first ||
            mine->second == NULL)
            return false;

        mine->second->restore(from);
        ++mine;
    }

    return mine == args_.end();
}

/* In the constructors, so a voice, which is a copy, has its ramps from
   where it is made; their buffers are sized on its first window. */
void thNode::initSmoothing (void)
{
    for (size_t k = 0; k < smooth_.size(); k++)
        delete smooth_[k].ramp;

    smooth_.clear();
    smoothLive_ = 0;

    if (plugin_ == NULL)
        return;

    const vector<int> &args = plugin_->smoothedArgs();

    for (size_t k = 0; k < args.size(); k++)
    {
        Smooth s = { args[k], plugin_->getArgSmooth(args[k]), 0, false, false,
                     new thArg(plugin_->getArgName(args[k]), 0.0f) };

        smooth_.push_back(s);
    }

    live_.assign(smooth_.size(), 0);
}

void thNode::process (void)
{
}
