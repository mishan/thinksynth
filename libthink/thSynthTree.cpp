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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

thSynthTree::thSynthTree (const string &name, thSynth *synth)
{
    ionode_ = NULL;
    nodeindex_ = NULL;   /* the destructor delete[]s this */
    name_ = name;
    nodecount_ = 0;

    synth_ = synth;
}

thSynthTree::thSynthTree (const thSynthTree &oldtree)
{
    ionode_ = NULL;
    nodeindex_ = NULL;   /* the destructor delete[]s this */
    nodecount_ = oldtree.nodeCount();
    name_ = oldtree.name();
    desc_ = oldtree.desc();
    synth_ = oldtree.synth();

    thNode *oldionode = oldtree.IONode();

    /* A tree whose .dsp never declared an `io' node, or whose parse failed
       partway, has no ionode. Copying it yields an empty tree rather than a
       NULL dereference; callers check IONode() before using the result. */
    if (oldionode == NULL) {
        fprintf(stderr,
                "thSynthTree: cannot copy tree '%s': it has no io node\n",
                name_.c_str());
        return;
    }

    thNode *newnode = new thNode(*oldionode);

    newNode(newnode, false);
    ionode_ = newnode;

    copyHelper(oldionode);
}

thSynthTree::~thSynthTree ()
{
    delete [] nodeindex_;   /* delete[] on NULL is a no-op */
    nodeindex_ = NULL;

    DestroyMap(nodes_);

    /* chanargs_ was never freed -- every `@foo = ...' in every .dsp leaked a
       thArg, once per load. */
    DestroyMap(chanargs_);
}

/* Bounds-checked accessor for the node index. Ids come out of parsed .dsp
   files and out of arg pointers that may never have been resolved, so they
   cannot be trusted as raw array subscripts. */
thNode *thSynthTree::nodeAt (int id) const
{
    if (nodeindex_ == NULL || id < 0 || id >= nodecount_) {
        return NULL;
    }

    return nodeindex_[id];
}

void thSynthTree::copyHelper (thNode *parentnode)
{
    thNode *data, *newnode;
    const thNodeList &children = parentnode->children();

    if (children.empty() == false)
    {
        for (thNodeList::const_iterator i = children.begin();
             i != children.end(); i++)
        {
            data = *i;

            if (data == NULL)
            {
                continue;
            }

            if (findNode(data->name()) == NULL)
            {
                newnode = new thNode(*data);
                newNode(newnode, false);
                copyHelper(data);
            }
        }
    }
}

thArg *thSynthTree::getArg (const string &nodename, const string &argname)
{
    /* was nodes_.find(name_) -- looked up the tree's own name, so this overload
       always resolved to the wrong node (or to none at all). */
    NodeMap::const_iterator i = nodes_.find(nodename);

    if (i == nodes_.end())
    {
        printf("WARNING!!  Trying to get args from nonexistant node %s\n",
               nodename.c_str());
        return NULL;
    }

    return getArg(i->second, argname);
}

/* Follow pointers and return a thArg of a float string */
thArg *thSynthTree::getArg (thNode *node, const string &argname)
{
    thArg *args;
    string argpointname;

    if (node == NULL)
    {
        return NULL;
    }

    args = node->getArg(argname);

    /* If the arg doesnt exist, make it a 0 */
    if (args == NULL)
    {
        args = node->setArg(argname, 0);
    }

    while (args && (args->type() == thArg::ARG_POINTER) && node)
    {
        /* Recurse through the list of pointers until we get a real value. */
        thNode *next = nodeAt(args->nodePtrId());

        if (next == NULL)
        {
            printf("WARNING!!  Arg %s in node %s points at node id %d, which "
                   "does not exist\n", args->name().c_str(),
                   node->name().c_str(), args->nodePtrId());
            return NULL;
        }

        node = next;

        argpointname = args->argPtrName(); /* the arg this arg points to */
        args = node->getArg(args->argPtrId());
        /* If the arg doesnt exist, make it a 0 */
        if (args == NULL)
        {
            args = node->setArg(argpointname, 0);
        }
    }   /* Maybe also add some kind of infinite-loop checking thing? */

    if (args && args->type() == thArg::ARG_CHANNEL)
    {
        args = args->argPtr();
    }

    return args;
}

thArg *thSynthTree::getArg (thNode *node, int argindex)
  /* Follow pointers and return a thArg of a float string */
{
    thArg *args;

    if (node == NULL)
    {
        return NULL;
    }

    args = node->getArg(argindex);

    if (args && args->type() == thArg::ARG_CHANNEL)
    {
        args = args->argPtr();
    }

    while (args && (args->type() == thArg::ARG_POINTER) && node)
    {
        thNode *next = nodeAt(args->nodePtrId());

        if (next == NULL)
        {
            printf("ERROR!  INDEXED ARG POINTS TO NODE ID %d, WHICH DOES NOT "
                   "EXIST!\n", args->nodePtrId());
            return NULL;
        }

        node = next;
        args = node->getArg(args->argPtrId());

        if (args == NULL)
        {
            printf("ERROR!  INDEXED NODE POINTS TO NOWHERE!\n");
        }
    }   /* Maybe also add some kind of infinite-loop checking thing? */

    return args;
}

void thSynthTree::newNode (thNode *node, bool set_id)
{
    if (set_id)
        node->setId(nodecount_++);

    nodes_[node->name()] = node;
}

void thSynthTree::setIONode (const string &name)
{
    NodeMap::const_iterator i = nodes_.find (name);

    if (i == nodes_.end())
    {
        printf ("thSynthTree::setIONode: ionode is NULL\n");
        return;
    }

    ionode_ = i->second;
}

void thSynthTree::printIONode (void)
{
    if (ionode_ == NULL) {
        return;
    }

    ionode_->printArgs();
}

void thSynthTree::setChanArg (thArg *arg)
{
    if (arg == NULL) {
        return;
    }

    thArg *oldArg = chanargs_[arg->name()];

    /* Guard self-assignment: deleting oldArg and then storing it back would
       leave a dangling pointer in the map. */
    if (oldArg == arg)
    {
        return;
    }

    if (oldArg)
    {
        delete oldArg;
    }

    chanargs_[arg->name()] = arg;
}

void thSynthTree::process (unsigned int windowlen)
{
    thPlugin *plug = NULL;

    if (ionode_ == NULL) {
        return;
    }

    const thNodeList &children = ionode_->children();

    ionode_->setRecalc(false);

    for (thNodeList::const_iterator i = children.begin(); i != children.end();i++)
    {
        if (*i && (*i)->recalc() == true) {
            processHelper(windowlen, *i);
        }
    }

    if ((plug = ionode_->plugin())) {
        plug->fire(ionode_, this, windowlen, synth_->getSampleRate());
    }
}

void thSynthTree::processHelper (unsigned int windowlen, thNode *node)
{
    if (node == NULL) {
        return;
    }

    const thNodeList &children = node->children();

    node->setRecalc(false);

    for (thNodeList::const_iterator i = children.begin(); i != children.end();i++)
    {
        if (*i && (*i)->recalc() == true) {
            processHelper(windowlen, *i);
        }
    }

    /* FIRE! -- the grammar permits nodes with no plugin, so this can be NULL */
    thPlugin *plug = node->plugin();

    if (plug) {
        plug->fire(node, this, windowlen, synth_->getSampleRate());
    }
}

/* reset the recalc flag for nodes with active plugins */
void thSynthTree::setActiveNodes(void)
{
    for (thNodeList::const_iterator i = activelist_.begin();
         i != activelist_.end(); ++i)
    {
        thNode *data = *i;

        if (data && data->recalc() == false)
        {
            data->setRecalc(true);
            setActiveNodesHelper(data);
        }
    }
}

void thSynthTree::setActiveNodesHelper(thNode *node)
{
    const thNodeList &parents = node->parents();
    thNode *data;

    for (thNodeList::const_iterator i = parents.begin(); i != parents.end(); i++)
    {
        data = *i;

        if (data && data->recalc() == false)
        {
            data->setRecalc(true);
            setActiveNodesHelper(data);
        }
    }
}

void thSynthTree::buildArgMap (void)
{
    thNode *curnode;  /* current node and arg in the loops */
    thArg *curarg;
    thPlugin *plugin;

    int index;
    int registeredargs = 0;

    int k;

    /* for every node in the thSynthTree */
    for (NodeMap::const_iterator i = nodes_.begin(); i != nodes_.end(); i++)
    {
        curnode = i->second;

        if (!curnode)
        {
            fprintf(stderr, "thSynthTree::BuildArgMap: curnode points to NULL\n");
            continue;
        }

/* XXXXXXXXXXXXXX: RIGHT NOW the parser indexes the nodes as it reads them, it
should not do this.  Until that is fixed (won't take long) we set the counter
to 0 here and set the index of each node to -1 when it is first created. */
        curnode->setArgCount(0);

        /* first, set up the args that the plugin registered */
        plugin = curnode->plugin(); /* get the node's plugin */
        if (plugin != NULL) /* don't do this for nodes with no plugin */
        {
            registeredargs = plugin->argCount();

            if (registeredargs == 0)
            {
                printf("WARNING: Node %s has registered 0 args.  It is probably doing things the old, slow way.  (%s)\n",
                       curnode->name().c_str(),
                       curnode->plugin()->path().c_str());
            }
            else
            {
                for (k = 0; k < registeredargs; k++)
                {
                    curarg = curnode->getArg(plugin->getArgName(k));
                    /* if the arg does not exist, set it to 0 */
                    if (curarg == NULL)
                    {
                        curarg = curnode->setArg(plugin->getArgName(k), 0);
                    }
                    else
                    {
                        index = curnode->addArgToIndex(curarg);
                        curarg->setIndex(index);
                    }

                }
            }
        }

        const thArgMap &argiterator = curnode->args();

        /* We don't need any of this because now the index is assigned via SetArg */
        /* for each thArg inside each thNode inside the thSynthTree */
        for (thArgMap::const_iterator j = argiterator.begin();
             j != argiterator.end(); j++)
        {
            curarg = j->second;

            if (curarg == NULL)
            {
                fprintf(stderr, "thSynthTree::BuildArgMap: curarg points to NULL\n");
            }
            else
            {
                if (curarg->index() < 0) /* has not been indexed yet */
                {
                    /* add the node to the index */
                    index = curnode->addArgToIndex(curarg);
                    curarg->setIndex(index);
                }
            }
        }
    }
}

/* A control is a `@name' block; a type is something a plugin knows about its
 * own arg. Nothing joins the two but the wire between them -- `in1 = @blim' --
 * so the type has to travel along it.
 *
 * A pass over the tree rather than a lookup at the point of use, because the
 * answer depends on *every* consumer and not on whichever one asks first. 197
 * of the 206 controls in the corpus drive exactly one parameter, but nine drive
 * several, and a control feeding both a waveform selector and a mixer gain
 * cannot become a list of six names without losing the ability to say 0.35 to
 * the mixer.
 *
 * So a control is typed only by unanimous agreement, and a consumer that says
 * nothing disagrees with one that does. "No opinion" is an opinion -- that this
 * is an ordinary continuous number -- and it is the one held by every arg of
 * every plugin that has not been taught otherwise.
 *
 * Runs after buildArgMap(), which is what makes a node arg's index equal to the
 * plugin's registration index for it. That correspondence is already
 * load-bearing: it is how every plugin's own `mod->getArg(node, args[IN_FREQ])'
 * finds anything. This rides on existing structure rather than on a
 * coincidence.
 */
void thSynthTree::typeChanArgs (void)
{
    struct Typing {
        float step;
        vector<string> names;
        bool conflict;

        Typing (void) : step(0), conflict(false) {}
    };

    map<string, Typing> typing;

    for (NodeMap::const_iterator i = nodes_.begin(); i != nodes_.end(); i++)
    {
        thNode *curnode = i->second;

        if (curnode == NULL)
            continue;

        thPlugin *plugin = curnode->plugin();

        const thArgMap &nodeargs = curnode->args();

        for (thArgMap::const_iterator j = nodeargs.begin();
             j != nodeargs.end(); j++)
        {
            thArg *curarg = j->second;

            if (curarg == NULL || curarg->type() != thArg::ARG_CHANNEL)
                continue;

            /* What this consumer has to say. The io node has no plugin, and an
               arg no plugin registered has no index into one; both are
               consumers with no opinion, which is an answer here rather than a
               reason to skip them. */
            float step = 0;
            vector<string> names;

            if (plugin)
            {
                const int idx = curarg->index();

                if (idx >= 0 && idx < plugin->argCount())
                {
                    step = plugin->getArgStep(idx);
                    names = plugin->getArgValues(idx);
                }
            }

            const string &control = curarg->argPtrName();

            map<string, Typing>::iterator seen = typing.find(control);

            if (seen == typing.end())
            {
                Typing t;

                t.step = step;
                t.names = names;

                typing[control] = t;
            }
            else if (seen->second.step != step || seen->second.names != names)
            {
                seen->second.conflict = true;
            }
        }
    }

    for (map<string, Typing>::const_iterator t = typing.begin();
         t != typing.end(); t++)
    {
        if (t->second.conflict)
            continue;

        if (t->second.step == 0 && t->second.names.empty())
            continue;               /* nothing anyone had an opinion about */

        thArg *chanarg = getChanArg(t->first);

        if (chanarg == NULL)
            continue;

        /* The file has the last word. `@x.step' and `@x.values' are the
           author's override, and an author who has said what a control is does
           not want it worked out again from the other end. */
        if (chanarg->typedByFile())
            continue;

        chanarg->setStep(t->second.step);

        if (!t->second.names.empty())
            chanarg->setValueNames(t->second.names);
    }
}

void thSynthTree::setPointers (void)
{
    thNode *node;     /* for referencing nodes that curnode points to */
    thNode *curnode;  /* current node and arg in the loops */
    thArg *arg;
    thArg *curarg;

    /* for every node in the thSynthTree */
    for (NodeMap::const_iterator i = nodes_.begin(); i != nodes_.end(); i++)
    {
        curnode = i->second;
        if (!curnode)
        {
            fprintf(stderr,
                    "thSynthTree::setPointers: curnode points to NULL\n");
            continue;
        }

        const thArgMap &argiterator = curnode->args();

        /* for each thArg inside each thNode inside the thSynthTree */
        for (thArgMap::const_iterator j = argiterator.begin();
             j != argiterator.end(); j++)
        {
            curarg = j->second;

            if (curarg == NULL)
            {
                fprintf(stderr, "thSynthTree::setPointers: curarg points to NULL\n");
            }

            /* if the thArg is a pointer, set argPointNodeID to the node's ID */
            if (curarg && curarg->type() == thArg::ARG_POINTER)
            {
                node = findNode(curarg->nodePtrName());

                if (node == NULL)
                {
                    printf("setPointers: Node %s not found!!\n",
                           curarg->nodePtrName().c_str());
                }
                else
                {
                    string argPtrName = curarg->argPtrName();

                    arg = node->getArg(argPtrName);

                    /* if the arg does not exist, set it to 0 */
                    if (arg == NULL)
                    {
                        /* setArg() already inserts into the node's arg map and
                           assigns an index. The old `node->args()[...] = arg'
                           here wrote into a by-value copy and was a no-op. */
                        arg = node->setArg(argPtrName, 0);
                    }

                    curarg->setNodePtrId(node->id());
                    curarg->setArgPtrId(arg->index());
                }
            }
        }
    }
}

void thSynthTree::buildNodeIndex (void)
{
    thNode *curnode;

    /* buildSynthTree() can run more than once over a tree's lifetime. */
    delete [] nodeindex_;
    nodeindex_ = NULL;

    if (nodecount_ <= 0)
    {
        return;
    }

    /* Value-initialised: not every slot gets filled. The copy constructor only
       walks nodes reachable from the ionode, so unreachable ids leave holes,
       and those holes get dereferenced from the audio thread. */
    nodeindex_ = new thNode*[nodecount_]();

    /* for every node in the thSynthTree */
    for (NodeMap::const_iterator i = nodes_.begin(); i != nodes_.end(); i++)
    {
        curnode = i->second;

        if (curnode == NULL)
        {
            fprintf(stderr,
                    "thSynthTree::buildNodeIndex: curnode points to NULL\n");
            continue;
        }

        if (curnode->id() < 0 || curnode->id() >= nodecount_)
        {
            fprintf(stderr,
                    "thSynthTree::buildNodeIndex: node '%s' has id %d, outside "
                    "the index of %d nodes\n", curnode->name().c_str(),
                    curnode->id(), nodecount_);
            continue;
        }

        /* set the index to point to the thNode */
        nodeindex_[curnode->id()] = curnode;
    }
}

void thSynthTree::buildSynthTree (void)
{
    buildNodeIndex();  /* set up the index of thNodes */

    if (ionode_ == NULL)
    {
        fprintf(stderr,
                "thSynthTree::buildSynthTree: tree '%s' has no io node\n",
                name_.c_str());
        return;
    }

    /* We don't want to recalc the root if something points here */
    ionode_->setRecalc(true);

    buildSynthTreeHelper2(ionode_->args(), ionode_);
}

int thSynthTree::buildSynthTreeHelper(thNode *parent, int nodeid)
{
    thNode *currentnode = nodeAt(nodeid);

    if (currentnode == NULL)
        return 1;

    if (currentnode->recalc() == true)
        return(1);  /* This node has already been processed */

    /* This node has now been marked as processed */
    currentnode->setRecalc(true);

    /* The grammar permits nodes with no plugin, so this can be NULL. */
    thPlugin *plug = currentnode->plugin();

    if (plug && plug->state() == thPlugin::ACTIVE)
        activelist_.push_back(currentnode);

    buildSynthTreeHelper2(currentnode->args(), currentnode);

    return 0;
}

void thSynthTree::buildSynthTreeHelper2(const thArgMap &argtree,
                                        thNode *currentnode)
{
    const thArg *data;
    thNode *node;

    for (thArgMap::const_iterator i = argtree.begin(); i != argtree.end(); i++)
    {
        data = i->second;

        if (data == NULL)
        {
            fprintf(stderr, "thSynthTree::buildSynthTreeHelper2: data points to NULL\n");
        }

        if (data && data->type() == thArg::ARG_POINTER)
        {
            node = nodeAt(data->nodePtrId());

            if (node == NULL)
            {
                /* This used to print and then dereference anyway. An arg can
                   point at a node that setPointers() failed to resolve, so the
                   edge simply gets dropped. */
                printf("CRITICAL: Node %s not found -- dropping the edge from "
                       "%s->%s\n", data->nodePtrName().c_str(),
                       currentnode->name().c_str(), data->name().c_str());
                continue;
            }

            currentnode->addChild(node);
            node->addParent(currentnode);

            /* Don't do the same node over and over */
            if (node->recalc() == false)
            {
                buildSynthTreeHelper(currentnode, data->nodePtrId());
            }
        }
    }
}

void thSynthTree::listNodes(void)
{
    for (NodeMap::const_iterator i = nodes_.begin();
        i != nodes_.end(); i++)
    {
        if (i->second == NULL) {
            continue;
        }

        printf("%s:  %s\n", name_.c_str(), i->second->name().c_str());
    }
}
