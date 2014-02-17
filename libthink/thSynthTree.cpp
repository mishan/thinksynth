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

thSynthTree::thSynthTree (const string &name, thSynth *synth)
{
	ionode_ = NULL;
	name_ = name;
	nodecount_ = 0;

	synth_ = synth;
}

thSynthTree::thSynthTree (const thSynthTree &oldtree)
{
	thNode *oldionode = oldtree.IONode();
	thNode *newnode = new thNode(*oldionode);

	ionode_ = NULL;
	nodecount_ = oldtree.nodeCount();
	name_ = oldtree.name();

	newNode(newnode, false);
	ionode_ = newnode;

	copyHelper(oldionode);

	synth_ = oldtree.synth();
}

thSynthTree::~thSynthTree ()
{
	if (nodeindex_) {
		delete [] nodeindex_;
	}

	DestroyMap(nodes_);
}

void thSynthTree::copyHelper (thNode *parentnode)
{
	thNode *data, *newnode;
	thNodeList children = parentnode->children();

	if (children.empty() == false)
	{
		for (thNodeList::const_iterator i = children.begin();
			 i != children.end(); i++)
		{
			data = *i;

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
	NodeMap::const_iterator i = nodes_.find(name_);

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

	args = node->getArg(argname);

	/* If the arg doesnt exist, make it a 0 */
	if (args == NULL)
	{
		args = node->setArg(argname, 0);
	}

	while (args && (args->type() == thArg::ARG_POINTER) && node)
	{
		/* Recurse through the list of pointers until we get a real value. */
//		map <string, thNode*>::const_iterator i = modnodes.find(args->argPointNode);
//		if (i != modnodes.end()) {
//			node = i->second;

		node = nodeindex_[args->nodePtrId()];
		//printf("Arg Point: %s (%i)\n", args->argPointName.c_str(), args->argPointArgID);
		//args = node->GetArg(args->argPointName);
		argpointname = args->argPtrName(); /* the arg this arg points to */
		args = node->getArg(args->argPtrId());
		/* If the arg doesnt exist, make it a 0 */
		if (args == NULL)
		{
			args = node->setArg(argpointname, 0);
			//args->SetIndex(node->AddArgToIndex(args));
		}
		/*}
		  else {
		  printf("WARNING!!  Pointer in %s to node (%s) that does not exist!\n", node->GetName().c_str(), args->argPointNode.c_str());
		  }*/
	}   /* Maybe also add some kind of infinite-loop checking thing? */

	if (args->type() == thArg::ARG_CHANNEL)
	{
		args = args->argPtr();
	}

	return args;
}

thArg *thSynthTree::getArg (thNode *node, int argindex)
  /* Follow pointers and return a thArg of a float string */
{
	thArg *args;

	args = node->getArg(argindex);

	if (args->type() == thArg::ARG_CHANNEL)
	{
		args = args->argPtr();
	}

	while (args && (args->type() == thArg::ARG_POINTER) && node)
	{
		node = nodeindex_[args->nodePtrId()];
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
	ionode_->printArgs();
}

void thSynthTree::setChanArg (thArg *arg)
{
	thArg *oldArg = chanargs_[arg->name()];

	if (oldArg)
	{
		delete oldArg;
	}

	chanargs_[arg->name()] = arg;
}

void thSynthTree::process (unsigned int windowlen)
{
	thPlugin *plug = NULL;
	thNodeList children = ionode_->children();

	ionode_->setRecalc(false);

	for (thNodeList::const_iterator i = children.begin(); i != children.end();i++)
	{
		if ((*i)->recalc() == true) {
			processHelper(windowlen, *i);
		}
	}

	if ((plug = ionode_->plugin())) {
		plug->fire(ionode_, this, windowlen, synth_->getSampleRate());
	}
}

void thSynthTree::processHelper (unsigned int windowlen, thNode *node)
{
	thNodeList children = node->children();

	node->setRecalc(false);

	for (thNodeList::const_iterator i = children.begin(); i != children.end();i++)
	{
		if ((*i)->recalc() == true) {
			processHelper(windowlen, *i);
		}
	}

	/* FIRE! */
	node->plugin()->fire(node, this, windowlen, synth_->getSampleRate());
}

/* reset the recalc flag for nodes with active plugins */
void thSynthTree::setActiveNodes(void)
{
	for (thNodeList::const_iterator i = activelist_.begin();
		 i != activelist_.end(); ++i)
	{
		thNode *data = *i;

		if (data->recalc() == false)
		{
			data->setRecalc(true);
			setActiveNodesHelper(data);
		}
	}
}

void thSynthTree::setActiveNodesHelper(thNode *node)
{
	thNodeList parents = node->parents();
	thNode *data;

	for (thNodeList::const_iterator i = parents.begin(); i != parents.end(); i++)
	{
		data = *i;

		if (data->recalc() == false)
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

	thArgMap argiterator;

	/* for every node in the thSynthTree */
	for (NodeMap::const_iterator i = nodes_.begin(); i != nodes_.end(); i++)
	{
		curnode = i->second;

		if (!curnode)
		{
			fprintf(stderr, "thSynthTree::BuildArgMap: curnode points to NULL\n");
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

		argiterator = curnode->args();

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

void thSynthTree::setPointers (void)
{
	thNode *node;     /* for referencing nodes that curnode points to */
	thNode *curnode;  /* current node and arg in the loops */
	thArg *arg;
	thArg *curarg;

	thArgMap argiterator;

	/* for every node in the thSynthTree */
	for (NodeMap::const_iterator i = nodes_.begin(); i != nodes_.end(); i++)
	{
		curnode = i->second;
		if (!curnode)
		{
			fprintf(stderr,
					"thSynthTree::setPointers: curnode points to NULL\n");
		}

		argiterator = curnode->args();

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
						arg = node->setArg(argPtrName, 0);

						node->args()[arg->name()] = arg;
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

//	nodeindex = (thNode **)calloc(nodecount, sizeof(thNode*));
	nodeindex_ = new thNode*[nodecount_];

	/* for every node in the thSynthTree */
	for (NodeMap::const_iterator i = nodes_.begin(); i != nodes_.end(); i++)
	{
		curnode = i->second;

		if (curnode == NULL)
		{
			fprintf(stderr,
					"thSynthTree::setPointers: curnode points to NULL\n");
		}

		/* set the index to point to the thNode */
		nodeindex_[curnode->id()] = curnode;
	}
}

void thSynthTree::buildSynthTree (void)
{
	buildNodeIndex();  /* set up the index of thNodes */

	/* We don't want to recalc the root if something points here */
	ionode_->setRecalc(true);

	buildSynthTreeHelper2(ionode_->args(), ionode_);
}

int thSynthTree::buildSynthTreeHelper(thNode *parent, int nodeid)
{
	thNode *currentnode = nodeindex_[nodeid];

	if (currentnode->recalc() == true)
		return(1);  /* This node has already been processed */

	/* This node has now been marked as processed */
	currentnode->setRecalc(true);

	if (currentnode->plugin()->state() == thPlugin::ACTIVE)
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
			node = nodeindex_[data->nodePtrId()];

			if (node == NULL)
			{
				printf("CRITICAL: Node %s not found!!\n",
					   data->nodePtrName().c_str());
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
		printf("%s:  %s\n", name_.c_str(), i->second->name().c_str());
	}
}
