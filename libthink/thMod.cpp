/* $Id: thMod.cpp,v 1.86 2004/07/29 06:24:35 ink Exp $ */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

thMod::thMod (const string &name, thSynth *argsynth)
{
	ionode = NULL;
	modname = name;
	nodecount = 0;

	synth = argsynth;
}

thMod::thMod (const thMod &oldmod)
{
	thNode *oldionode = oldmod.GetIONode();
	thNode *newnode = new thNode(oldionode->GetName(), oldionode->GetPlugin());

	ionode = NULL;
	nodecount = oldmod.GetNodeCount();
	modname = oldmod.GetName();

	newnode->SetArgCount(oldionode->GetArgCount());
	newnode->CopyArgs(oldionode->GetArgTree());

	NewNode(newnode, oldionode->GetID());
	ionode = newnode;

	CopyHelper(oldionode);

	synth = oldmod.synth;
}

thMod::~thMod ()
{
	if (nodeindex) {
		delete [] nodeindex;
	}
	DestroyMap(modnodes);
}

void thMod::CopyHelper (thNode *parentnode)
{
	thNode *data, *newnode;
	list<thNode*> children = parentnode->GetChildren();
	map<string, thArg*> argtree;

	if(!children.empty()) {
		for(list<thNode*>::const_iterator i = children.begin(); i != children.end(); i++) {
			data = *i;
//			printf("%s\n", data->GetName().c_str()); //%s %i\n", newnode->GetName(), newnode->GetID());
			if(!FindNode(data->GetName())) {
				newnode = new thNode(data->GetName(), data->GetPlugin());
				NewNode(newnode, data->GetID());
				CopyHelper(data);

				argtree = data->GetArgTree();

				newnode->SetArgCount(data->GetArgCount());

				if(!argtree.empty()) {
					newnode->CopyArgs(argtree);
				}
			}
		}
	}
}

thArg *thMod::GetArg (const string &nodename, const string &argname)
{
	map<string, thNode*>::const_iterator i;
	i = modnodes.find(modname);

	if (i == modnodes.end())
	{
		printf("WARNING!!  Trying to get args from nonexistant node %s\n", nodename.c_str());
		return NULL;
	}
	return GetArg(i->second, argname);
}

thArg *thMod::GetArg (thNode *node, const string &argname)
  /* Follow pointers and return a thArg of a float string */
{
	thArg *args;
	float *tmp;
	string argpointname;


	args = node->GetArg(argname);

	/* If the arg doesnt exist, make it a 0 */
	if(args == NULL) {
		tmp = new float[1];
		tmp[0] = 0;
		args = node->SetArg(argname, tmp, 1);
	}
	
	while (args && (args->argType == ARG_POINTER) && node) { 
		/* Recurse through the list of pointers until we get a real value. */
//		map <string, thNode*>::const_iterator i = modnodes.find(args->argPointNode);
//		if (i != modnodes.end()) {
//			node = i->second;
		
		node = nodeindex[args->argPointNodeID];
		//printf("Arg Point: %s (%i)\n", args->argPointName.c_str(), args->argPointArgID);
		//args = node->GetArg(args->argPointName);
		argpointname = args->argPointName; /* the arg this arg points to */
		args = node->GetArg(args->argPointArgID);
		/* If the arg doesnt exist, make it a 0 */
		if(args == NULL) {
			tmp = new float[1];
			tmp[0] = 0;
			args = node->SetArg(argpointname, tmp, 1);
			//args->SetIndex(node->AddArgToIndex(args));
		}
		/*}
		  else {
		  printf("WARNING!!  Pointer in %s to node (%s) that does not exist!\n", node->GetName().c_str(), args->argPointNode.c_str());
		  }*/
	}   /* Maybe also add some kind of infinite-loop checking thing? */

	if (args->argType == ARG_CHANNEL)
	{
		args = args->argPointArg;
	}

	return args;
}

thArg *thMod::GetArg (thNode *node, int argindex)
  /* Follow pointers and return a thArg of a float string */
{
	thArg *args;
	
	args = node->GetArg(argindex);

	if (args->argType == ARG_CHANNEL)
	{
		args = args->argPointArg;
	}

	while (args && (args->argType == ARG_POINTER) && node) { 
		/* Recurse through the list of pointers until we get a real value. */
//		map <string, thNode*>::const_iterator i = modnodes.find(args->argPointNode);
//		if (i != modnodes.end()) {
//			node = i->second;		
		node = nodeindex[args->argPointNodeID];
		args = node->GetArg(args->argPointArgID);
		
		if(args == NULL) {
			printf("ERROR!  INDEXED NODE POINTS TO NOWHERE!\n");
		}
	}   /* Maybe also add some kind of infinite-loop checking thing? */

	return args;
}

void thMod::NewNode (thNode *node)
{
	node->SetID(nodecount);
	modnodes[node->GetName()] = node;
	nodecount++;
}

void thMod::NewNode (thNode *node, int id)
{
	node->SetID(id);
	modnodes[node->GetName()] = node;
}

void thMod::SetIONode (const string &name)
{
	map <string, thNode*>::const_iterator i = modnodes.find (name);

	if (i == modnodes.end()) {
		printf ("thMod::SetIONode: ionode is NULL\n");
		return;
	}
	ionode = i->second;
}

void thMod::PrintIONode (void)
{
	ionode->PrintArgs();
}

void thMod::SetChanArg (thArg *arg)
{
	thArg *oldArg = chanargs[arg->GetArgName()];

	if (oldArg)
	{
		delete oldArg;
	}

	chanargs[arg->GetArgName()] = arg;
}

void thMod::Process (unsigned int windowlen)
{
	thPlugin *plug = NULL;
	list<thNode*> children = ionode->GetChildren();

	ionode->SetRecalc(false);

	for(list<thNode*>::const_iterator i = children.begin(); i != children.end(); i++) {
		if((*i)->GetRecalc() == true) {
			ProcessHelper(windowlen, *i);
		}
	}

	if((plug = ionode->GetPlugin())) {
		plug->Fire(ionode, this, windowlen, synth->GetSamples());
	}
}

void thMod::ProcessHelper(unsigned int windowlen, thNode *node)
{
	list<thNode*> children = node->GetChildren();

	node->SetRecalc(false);
  
	for(list<thNode*>::const_iterator i = children.begin(); i != children.end(); i++) {
		if((*i)->GetRecalc() == true) {
			ProcessHelper(windowlen, *i);
		}
	}

	/* FIRE! */
	node->GetPlugin()->Fire(node, this, windowlen, synth->GetSamples());
}

/* reset the recalc flag for nodes with active plugins */
void thMod::SetActiveNodes(void)
{
	for (list<thNode*>::const_iterator i = activelist.begin();
		 i != activelist.end(); ++i)
	{
		thNode *data = *i;
		if(data->GetRecalc() == false)
		{
			data->SetRecalc(true);
			SetActiveNodesHelper(data);
		}
	}
}

void thMod::SetActiveNodesHelper(thNode *node)
{
	list<thNode*> parents = node->GetParents();
	thNode *data;

	for(list<thNode*>::const_iterator i = parents.begin(); i != parents.end(); i++) {
		data = *i;
		if(data->GetRecalc() == false) {
			data->SetRecalc(true);
			SetActiveNodesHelper(data);
		}
	}
}

void thMod::BuildArgMap (void)
{
	thNode *curnode;  /* current node and arg in the loops */
	thArg *curarg;
	thPlugin *plugin;

	float *tmp;
	int index;
	int registeredargs = 0;

	int k;

	map<string,thArg*> argiterator;
	
	/* for every node in the thMod */
	for (map<string, thNode*>::const_iterator i = modnodes.begin();
		 i != modnodes.end(); i++)
	{
		curnode = i->second;
		if(!curnode)
		{
			fprintf(stderr, "thMod::BuildArgMap: curnode points to NULL\n");
		}

/* XXXXXXXXXXXXXX: RIGHT NOW the parser indexes the nodes as it reads them, it
should not do this.  Until that is fixed (won't take long) we set the counter
to 0 here and set the index of each node to -1 when it is first created. */
		curnode->SetArgCount(0);


		/* first, set up the args that the plugin registered */
		plugin = curnode->GetPlugin(); /* get the node's plugin */
		if(plugin != NULL) /* don't do this for nodes with no plugin */
		{
			registeredargs = plugin->GetArgs();

			if(registeredargs == 0)
			{
				printf("WARNING: Node %s has registered 0 args.  It is probably doing things the old, slow way.  (%s)\n", curnode->GetName().c_str(), curnode->GetPlugin()->GetPath().c_str());
			}
			else
			{
				for (k = 0; k < registeredargs; k++)
				{
					curarg = curnode->GetArg(plugin->GetArgName(k));
					/* if the arg does not exist, set it to 0 */
					if(curarg == NULL)
					{
						tmp = new float[1];
						tmp[0] = 0;
						curarg = curnode->SetArg(plugin->GetArgName(k), tmp, 1);
					}
					else
					{
						index = curnode->AddArgToIndex(curarg);
						curarg->SetIndex(index);
					}

				}
			}
		}
	
		argiterator = curnode->GetArgTree();
		
		/* We don't need any of this because now the index is assigned via SetArg */
		/* for each thArg inside each thNode inside the thMod */
		for (map<string, thArg*>::const_iterator j = argiterator.begin();
			 j != argiterator.end(); j++)
		{
			curarg = j->second;
			if(!curarg)
			{
				fprintf(stderr, "thMod::BuildArgMap: curarg points to NULL\n");
			}
			else
			{
				if(curarg->GetIndex() < 0) /* has not been indexed yet */
				{
					/* add the node to the index */
					index = curnode->AddArgToIndex(curarg);
					curarg->SetIndex(index);	
				}
			}
		}
	}
}

void thMod::SetPointers (void)
{
	thNode *node;     /* for referencing nodes that curnode points to */
	thNode *curnode;  /* current node and arg in the loops */
	thArg *arg;
	thArg *curarg;
	float *tmp;

	map<string,thArg*> argiterator;
	
	/* for every node in the thMod */
	for (map<string, thNode*>::const_iterator i = modnodes.begin();
		 i != modnodes.end(); i++)
	{
		curnode = i->second;
		if(!curnode)
		{
			fprintf(stderr, "thMod::SetPointers: curnode points to NULL\n");
		}

		argiterator = curnode->GetArgTree();

		/* for each thArg inside each thNode inside the thMod */
		for (map<string, thArg*>::const_iterator j = argiterator.begin();
			 j != argiterator.end(); j++)
		{
			curarg = j->second;
			if(!curarg)
			{
				fprintf(stderr, "thMod::SetPointers: curarg points to NULL\n");
			}

			/* if the thArg is a pointer, set argPointNodeID to the node's ID */
			if(curarg && curarg->argType == ARG_POINTER)
			{
				node = FindNode(curarg->argPointNode);
				
				if(!node)
				{
					printf("SetPointers: Node %s not found!!\n",
						   curarg->argPointNode.c_str());
				}
				else
				{
					
					arg = node->GetArg(curarg->argPointName);
					
					/* if the arg does not exist, set it to 0 */
					if(arg == NULL)
					{
						tmp = new float[1];
						tmp[0] = 0;
						arg = node->SetArg(curarg->argPointName, tmp, 1);

						node->GetArgTree()[arg->argName] = arg;
					} 
					curarg->argPointNodeID = node->GetID();
					curarg->argPointArgID = arg->GetIndex();
				}
			}
		}
	}
}

void thMod::BuildNodeIndex (void)
{
	thNode *curnode;

//	nodeindex = (thNode **)calloc(nodecount, sizeof(thNode*));
	nodeindex = new thNode*[nodecount];
	/* for every node in the thMod */
	for (map<string, thNode*>::const_iterator i = modnodes.begin();
		 i != modnodes.end(); i++)
	{
		curnode = i->second;
		if(!curnode)
		{
			fprintf(stderr, "thMod::SetPointers: curnode points to NULL\n");
		}

		/* set the index to point to the thNode */
		nodeindex[curnode->GetID()] = curnode;
	}
}

void thMod::BuildSynthTree (void)
{
	BuildNodeIndex();  /* set up the index of thNodes */

	/* We don't want to recalc the root if something points here */
	ionode->SetRecalc(true);

	BuildSynthTreeHelper2(ionode->GetArgTree(), ionode);
}

int thMod::BuildSynthTreeHelper(thNode *parent, int nodeid)
{
	thNode *currentnode = nodeindex[nodeid];

	if(currentnode->GetRecalc() == true) {
		return(1);  /* This node has already been processed */
	}

	/* This node has now been marked as processed */
	currentnode->SetRecalc(true); 

	if(currentnode->GetPlugin()->GetState() == thActive) {
	  activelist.push_back(currentnode);
	}

	BuildSynthTreeHelper2(currentnode->GetArgTree(), currentnode);

	return 0;
}

void thMod::BuildSynthTreeHelper2(const map<string, thArg*> &argtree, thNode *currentnode)
{
	const thArg *data;
	thNode *node;

	for (map<string, thArg*>::const_iterator i = argtree.begin();
		 i != argtree.end(); i++)
	{
		data = i->second;
		if(!data)
		{
			fprintf(stderr, "thMod::BuildSynthTreeHelper2: data points to NULL\n");
		}

		if(data && data->argType == ARG_POINTER)
		{
			node = nodeindex[data->argPointNodeID];

			if(!node)
			{
				printf("CRITICAL: Node %s not found!!\n",
					   data->argPointNode.c_str());
			}

			currentnode->AddChild(node);
			node->AddParent(currentnode);

			/* Don't do the same node over and over */
			if(node->GetRecalc() == false)
			{
				BuildSynthTreeHelper(currentnode, data->argPointNodeID);
			}
		}
	}
}

void thMod::ListNodes(void)
{
	for(map<string,thNode*>::const_iterator i = modnodes.begin();
		i != modnodes.end(); i++)
	{
		printf("%s:  %s\n", modname.c_str(), i->second->GetName().c_str());
	}
}
