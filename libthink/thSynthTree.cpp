/* $Id: thSynthTree.cpp 1594 2005-02-01 03:38:45Z misha $ */
/*
 * Copyright (C) 2004 Metaphonic Labs
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
	thNode *newnode = new thNode(oldionode->GetName(), oldionode->GetPlugin());

	ionode_ = NULL;
	nodecount_ = oldtree.nodeCount();
	name_ = oldtree.name();

	newnode->SetArgCount(oldionode->GetArgCount());
	newnode->CopyArgs(oldionode->GetArgTree());

	newNode(newnode, oldionode->GetID());
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
	NodeList children = parentnode->GetChildren();
	thArgMap argtree;

	if(children.empty() == false)
	{
		for(NodeList::const_iterator i = children.begin();
			i != children.end(); i++)
		{
			data = *i;

			if(findNode(data->GetName()) == NULL)
			{
				newnode = new thNode(data->GetName(), data->GetPlugin());
				newNode(newnode, data->GetID());
				copyHelper(data);

				argtree = data->GetArgTree();

				newnode->SetArgCount(data->GetArgCount());

				if(argtree.empty() == false)
					newnode->CopyArgs(argtree);
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

	args = node->GetArg(argname);

	/* If the arg doesnt exist, make it a 0 */
	if(args == NULL)
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
		args = node->GetArg(args->argPtrId());
		/* If the arg doesnt exist, make it a 0 */
		if(args == NULL)
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
	
	args = node->GetArg(argindex);

	if (args->type() == thArg::ARG_CHANNEL)
	{
		args = args->argPtr();
	}

	while (args && (args->type() == thArg::ARG_POINTER) && node)
	{ 
		node = nodeindex_[args->nodePtrId()];
		args = node->GetArg(args->argPtrId());
		
		if(args == NULL)
		{
			printf("ERROR!  INDEXED NODE POINTS TO NOWHERE!\n");
		}
	}   /* Maybe also add some kind of infinite-loop checking thing? */

	return args;
}

void thSynthTree::newNode (thNode *node)
{
	node->SetID(nodecount_);
	nodes_[node->GetName()] = node;
	nodecount_++;
}

void thSynthTree::newNode (thNode *node, int id)
{
	node->SetID(id);
	nodes_[node->GetName()] = node;
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
	ionode_->PrintArgs();
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
	NodeList children = ionode_->GetChildren();

	ionode_->SetRecalc(false);

	for(NodeList::const_iterator i = children.begin(); i != children.end();i++)
	{
		if((*i)->GetRecalc() == true) {
			processHelper(windowlen, *i);
		}
	}

	if((plug = ionode_->GetPlugin())) {
		plug->fire(ionode_, this, windowlen, synth_->getSampleRate());
	}
}

void thSynthTree::processHelper(unsigned int windowlen, thNode *node)
{
	NodeList children = node->GetChildren();

	node->SetRecalc(false);
  
	for(NodeList::const_iterator i = children.begin(); i != children.end();i++)
	{
		if((*i)->GetRecalc() == true) {
			processHelper(windowlen, *i);
		}
	}

	/* FIRE! */
	node->GetPlugin()->fire(node, this, windowlen, synth_->getSampleRate());
}

/* reset the recalc flag for nodes with active plugins */
void thSynthTree::setActiveNodes(void)
{
	for (NodeList::const_iterator i = activelist_.begin();
		 i != activelist_.end(); ++i)
	{
		thNode *data = *i;

		if(data->GetRecalc() == false)
		{
			data->SetRecalc(true);
			setActiveNodesHelper(data);
		}
	}
}

void thSynthTree::setActiveNodesHelper(thNode *node)
{
	NodeList parents = node->GetParents();
	thNode *data;

	for(NodeList::const_iterator i = parents.begin(); i != parents.end(); i++)
	{
		data = *i;
	
		if(data->GetRecalc() == false)
		{
			data->SetRecalc(true);
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

		if(!curnode)
		{
			fprintf(stderr, "thSynthTree::BuildArgMap: curnode points to NULL\n");
		}

/* XXXXXXXXXXXXXX: RIGHT NOW the parser indexes the nodes as it reads them, it
should not do this.  Until that is fixed (won't take long) we set the counter
to 0 here and set the index of each node to -1 when it is first created. */
		curnode->SetArgCount(0);

		/* first, set up the args that the plugin registered */
		plugin = curnode->GetPlugin(); /* get the node's plugin */
		if(plugin != NULL) /* don't do this for nodes with no plugin */
		{
			registeredargs = plugin->getArgs();

			if(registeredargs == 0)
			{
				printf("WARNING: Node %s has registered 0 args.  It is probably doing things the old, slow way.  (%s)\n",
					   curnode->GetName().c_str(),
					   curnode->GetPlugin()->getPath().c_str());
			}
			else
			{
				for (k = 0; k < registeredargs; k++)
				{
					curarg = curnode->GetArg(plugin->getArgName(k));
					/* if the arg does not exist, set it to 0 */
					if(curarg == NULL)
					{
						curarg = curnode->setArg(plugin->getArgName(k), 0);
					}
					else
					{
						index = curnode->AddArgToIndex(curarg);
						curarg->setIndex(index);
					}

				}
			}
		}
	
		argiterator = curnode->GetArgTree();
		
		/* We don't need any of this because now the index is assigned via SetArg */
		/* for each thArg inside each thNode inside the thSynthTree */
		for (thArgMap::const_iterator j = argiterator.begin();
			 j != argiterator.end(); j++)
		{
			curarg = j->second;

			if(curarg == NULL)
			{
				fprintf(stderr, "thSynthTree::BuildArgMap: curarg points to NULL\n");
			}
			else
			{
				if(curarg->index() < 0) /* has not been indexed yet */
				{
					/* add the node to the index */
					index = curnode->AddArgToIndex(curarg);
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
		if(!curnode)
		{
			fprintf(stderr,
					"thSynthTree::setPointers: curnode points to NULL\n");
		}

		argiterator = curnode->GetArgTree();

		/* for each thArg inside each thNode inside the thSynthTree */
		for (thArgMap::const_iterator j = argiterator.begin();
			 j != argiterator.end(); j++)
		{
			curarg = j->second;

			if(curarg == NULL)
			{
				fprintf(stderr, "thSynthTree::setPointers: curarg points to NULL\n");
			}

			/* if the thArg is a pointer, set argPointNodeID to the node's ID */
			if(curarg && curarg->type() == thArg::ARG_POINTER)
			{
				node = findNode(curarg->nodePtrName());
				
				if(node == NULL)
				{
					printf("setPointers: Node %s not found!!\n",
						   curarg->nodePtrName().c_str());
				}
				else
				{
					string argPtrName = curarg->argPtrName();

					arg = node->GetArg(argPtrName);
					
					/* if the arg does not exist, set it to 0 */
					if(arg == NULL)
					{
						arg = node->setArg(argPtrName, 0);

						node->GetArgTree()[arg->name()] = arg;
					} 

					curarg->setNodePtrId(node->GetID());
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

		if(curnode == NULL)
		{
			fprintf(stderr,
					"thSynthTree::setPointers: curnode points to NULL\n");
		}

		/* set the index to point to the thNode */
		nodeindex_[curnode->GetID()] = curnode;
	}
}

void thSynthTree::buildSynthTree (void)
{
	buildNodeIndex();  /* set up the index of thNodes */

	/* We don't want to recalc the root if something points here */
	ionode_->SetRecalc(true);

	buildSynthTreeHelper2(ionode_->GetArgTree(), ionode_);
}

int thSynthTree::buildSynthTreeHelper(thNode *parent, int nodeid)
{
	thNode *currentnode = nodeindex_[nodeid];

	if(currentnode->GetRecalc() == true)
		return(1);  /* This node has already been processed */

	/* This node has now been marked as processed */
	currentnode->SetRecalc(true); 

	if(currentnode->GetPlugin()->getState() == thPlugin::ACTIVE)
		activelist_.push_back(currentnode);

	buildSynthTreeHelper2(currentnode->GetArgTree(), currentnode);

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

		if(data == NULL)
		{
			fprintf(stderr, "thSynthTree::buildSynthTreeHelper2: data points to NULL\n");
		}

		if(data && data->type() == thArg::ARG_POINTER)
		{
			node = nodeindex_[data->nodePtrId()];

			if(node == NULL)
			{
				printf("CRITICAL: Node %s not found!!\n",
					   data->nodePtrName().c_str());
			}

			currentnode->AddChild(node);
			node->AddParent(currentnode);

			/* Don't do the same node over and over */
			if(node->GetRecalc() == false)
			{
				buildSynthTreeHelper(currentnode, data->nodePtrId());
			}
		}
	}
}

void thSynthTree::listNodes(void)
{
	for(NodeMap::const_iterator i = nodes_.begin();
		i != nodes_.end(); i++)
	{
		printf("%s:  %s\n", name_.c_str(), i->second->GetName().c_str());
	}
}
