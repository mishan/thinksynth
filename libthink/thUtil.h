/* $Id: thNode.h 1240 2004-08-16 09:34:48Z misha $ */
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

#ifndef TH_UTIL_H
#define TH_UTIL_H

using namespace std;

#include <string>

class thUtil {
public:
	thUtil (void) { }
	~thUtil (void) { }

	static int getNumLength (int num);
	static string basename (const char* path);
	static string dirname (const char* path);
};

#endif /* TH_UTIL_H */
