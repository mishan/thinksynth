/* $Id$ */
/*
 * Copyright (C) 2004-2005 Metaphonic Labs
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

#ifndef TH_NODE_H
#define TH_NODE_H 1

class thNode {
public:
	thNode (const string &name, thPlugin *thplug);
	~thNode (void);

	void SetName (const string &name) { nodename = name; };

	string GetName (void) const { return nodename; }
	
	thArg *setArg (const string &name, float value);
	thArg *setArg (const string &name, const float *value, int len);
	thArg *setArg (const string &name, const string &node, const string &value);
	thArg *setArg (const string &name, const string &chanarg);

	int AddArgToIndex (thArg *arg);

	void SetArgCount (int argcnt) { argcounter = argcnt; };

	int GetArgCount (void) { return argcounter; };

	map<string,thArg*> GetArgTree (void) const { return args; }
	
	thArg *GetArg (const string &name) { return args[name]; };

	thArg *GetArg (int index) { return argindex[index]; };

	void PrintArgs (void);

	void SetID (int newid) { id = newid; }

	int GetID (void) const { return id; }

	bool GetRecalc(void) const { return recalc; }

	void SetRecalc(bool state) { recalc = state; }

	void AddChild(thNode *node) { children.push_back(node); }

	void AddParent(thNode *node) { parents.push_back(node); }

	list<thNode*> GetChildren (void) const { return children; }

	list<thNode*> GetParents(void) const { return parents; }

	void SetPlugin (thPlugin *plug) { plugin = plug; }

	thPlugin *GetPlugin() const { return plugin; }

	void CopyArgs (const map<string,thArg*> &args);

	void Process (void);

private:
	map<string, thArg*> args;

	thArg **argindex;
	int argcounter;
	int argsize;

	list<thNode*> parents, children;
	thPlugin *plugin;
	
	string nodename;
	bool recalc;

	int id;  /* id used as an index for thArg pointers */
};

#endif /* TH_NODE_H */
